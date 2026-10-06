#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <winhttp.h>
#include <wincodec.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <atomic>
#include <memory>
#include <vector>
#include <string>
#include <chrono>
#include <stdexcept>
#include <random>

using namespace winrt::Windows::Data::Json;
namespace fs = std::filesystem;
constexpr UINT TrayMessage = WM_APP + 1, DoneMessage = WM_APP + 2;
constexpr UINT Next = 100, Pause = 101, Open = 102, Exit = 103, Wallhaven = 110, Bing = 111;
constexpr UINT HoursBase = 200;
const wchar_t* sourceNames[] = {L"Wallhaven", L"Bing", L"NASA", L"Commons"};
const wchar_t* sourceLabels[] = {L"Wallhaven - SFW landscapes", L"Bing - daily photography", L"NASA - space imagery", L"Wikimedia Commons - featured photos"};
constexpr int SourceCount = 4;
const int intervals[] = {3, 6, 12, 24};
struct Photo { std::wstring title, credit, page, resolution, source; fs::path file; };
struct Result { Photo photo; std::wstring error; unsigned generation; };
HWND windowHandle{};
UINT taskbarCreated{};
NOTIFYICONDATAW tray{};
fs::path dataDir, config;
Photo current;
std::wstring status = L"Waiting for wallpaper";
bool paused = false, busy = false;
int hours = 6, provider = 0;
unsigned generation = 0;
std::atomic_bool stopping{false};
std::thread worker;
ULONGLONG due = 0;

std::wstring wide(const std::string& s) {
    return std::wstring(winrt::to_hstring(s));
}
std::wstring windowsError(const winrt::hresult_error& error) {
    wchar_t text[64]{};
    swprintf_s(text, L"Windows error 0x%08X", static_cast<unsigned>(error.code().value));
    return text;
}
void check(BOOL ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Internet {
    HINTERNET h{};
    explicit Internet(HINTERNET value) : h(value) { if (!h) throw std::runtime_error("WinHTTP handle failed"); }
    ~Internet() { WinHttpCloseHandle(h); }
    Internet(const Internet&) = delete;
};
// HTTPS only; WinHTTP retains its default certificate validation.
std::vector<char> fetch(const std::wstring& url, size_t limit) {
    URL_COMPONENTS c{sizeof(c)};
    c.dwSchemeLength = c.dwHostNameLength = c.dwUrlPathLength = c.dwExtraInfoLength = DWORD(-1);
    check(WinHttpCrackUrl(url.c_str(), 0, 0, &c), "Invalid URL");
    if (c.nScheme != INTERNET_SCHEME_HTTPS) throw std::runtime_error("HTTPS required");
    std::wstring host(c.lpszHostName, c.dwHostNameLength);
    std::wstring path(c.lpszUrlPath, c.dwUrlPathLength);
    if (c.dwExtraInfoLength) path.append(c.lpszExtraInfo, c.dwExtraInfoLength);
    Internet session(WinHttpOpen(L"AutoWallpaper/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    WinHttpSetTimeouts(session.h, 10000, 10000, 15000, 15000);
    Internet connection(WinHttpConnect(session.h, host.c_str(), c.nPort, 0));
    Internet request(WinHttpOpenRequest(connection.h, L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(request.h, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
    check(WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0), "Network send failed");
    check(WinHttpReceiveResponse(request.h, nullptr), "Network response failed");
    DWORD code = 0, length = sizeof(code);
    check(WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &code, &length, WINHTTP_NO_HEADER_INDEX), "HTTP status failed");
    if (code != 200) throw std::runtime_error("HTTP " + std::to_string(code) + " from " + winrt::to_string(host));
    std::vector<char> bytes;
    char buffer[65536];
    const auto deadline = GetTickCount64() + 120000;
    for (;;) {
        if (stopping || GetTickCount64() > deadline) throw std::runtime_error("Download cancelled or timed out");
        DWORD read = 0;
        check(WinHttpReadData(request.h, buffer, sizeof(buffer), &read), "Download failed");
        if (!read) break;
        if (bytes.size() + read > limit) throw std::runtime_error("Response too large");
        bytes.insert(bytes.end(), buffer, buffer + read);
    }
    return bytes;
}
JsonObject json(const std::wstring& url) {
    auto bytes = fetch(url, 2 * 1024 * 1024);
    return JsonObject::Parse(winrt::to_hstring(std::string(bytes.begin(), bytes.end())));
}
std::wstring field(const JsonObject& object, const wchar_t* key) {
    return std::wstring(object.GetNamedString(key, L""));
}
uint32_t randomIndex(uint32_t count) {
    if (!count) throw std::runtime_error("No images returned by source");
    static thread_local std::mt19937 engine(std::random_device{}());
    return std::uniform_int_distribution<uint32_t>(0, count - 1)(engine);
}
std::wstring urlEncode(const std::wstring& value) {
    const auto bytes = winrt::to_string(value);
    std::wstring out;
    const wchar_t* hex = L"0123456789ABCDEF";
    for (unsigned char c : bytes) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') out += wchar_t(c);
        else { out += L'%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}
std::wstring plainText(const std::wstring& html) {
    std::wstring out;
    bool tag = false;
    for (wchar_t c : html) {
        if (c == L'<') tag = true;
        else if (c == L'>') { tag = false; out += L' '; }
        else if (!tag) out += c;
    }
    for (auto pair : {std::pair{L"&amp;", L"&"}, {L"&quot;", L"\""}, {L"&#39;", L"'"}, {L"&nbsp;", L" "}, {L"&lt;", L"<"}, {L"&gt;", L">"}}) {
        size_t pos = 0;
        while ((pos = out.find(pair.first, pos)) != std::wstring::npos) {
            out.replace(pos, wcslen(pair.first), pair.second); pos += wcslen(pair.second);
        }
    }
    return out;
}
Photo download(int source, const std::wstring& previous) {
    Photo p;
    std::wstring image;
    if (source == 0) {
        auto root = json(L"https://wallhaven.cc/api/v1/search?categories=100&purity=100&sorting=random&atleast=1920x1080&ratios=16x9%2C16x10%2C21x9&ai_art_filter=0");
        auto entries = root.GetNamedArray(L"data");
        for (const auto& entry : entries) {
            auto item = entry.GetObject();
            if (field(item, L"purity") != L"sfw" || field(item, L"url") == previous) continue;
            image = field(item, L"path");
            p.title = L"Wallhaven #" + field(item, L"id");
            p.page = field(item, L"url");
            p.resolution = field(item, L"resolution");
            p.credit = L"See source page for author and copyright";
            p.source = L"Wallhaven";
            break;
        }
    } else if (source == 1) {
        auto root = json(L"https://www.bing.com/HPImageArchive.aspx?format=js&idx=0&n=8&mkt=en-US");
        auto entries = root.GetNamedArray(L"images");
        uint32_t start = 0;
        for (uint32_t i = 0; i < entries.Size(); ++i)
            if (field(entries.GetObjectAt(i), L"copyrightlink") == previous) start = (i + 1) % entries.Size();
        for (uint32_t offset = 0; offset < entries.Size(); ++offset) {
            auto item = entries.GetObjectAt((start + offset) % entries.Size());
            const auto page = field(item, L"copyrightlink");
            if (page == previous) continue;
            image = L"https://www.bing.com" + field(item, L"urlbase") + L"_1920x1080.jpg";
            p.title = field(item, L"title");
            p.credit = field(item, L"copyright");
            p.page = page;
            p.resolution = L"1920 × 1080";
            p.source = L"Bing";
            break;
        }
    } else if (source == 2) {
        const wchar_t* topics[] = {L"nebula", L"galaxy", L"earth from space", L"hubble"};
        auto entries = json(L"https://images-api.nasa.gov/search?media_type=image&page_size=100&q=" + urlEncode(topics[randomIndex(4)]))
            .GetNamedObject(L"collection").GetNamedArray(L"items");
        const auto start = randomIndex(entries.Size());
        for (uint32_t i = 0; i < entries.Size() && i < 8; ++i) {
            auto item = entries.GetObjectAt((start + i) % entries.Size());
            auto meta = item.GetNamedArray(L"data").GetObjectAt(0);
            auto id = field(meta, L"nasa_id");
            auto page = L"https://images.nasa.gov/details/" + urlEncode(id);
            if (page == previous) continue;
            auto assets = json(L"https://images-api.nasa.gov/asset/" + urlEncode(id)).GetNamedObject(L"collection").GetNamedArray(L"items");
            for (const auto& asset : assets) {
                auto href = field(asset.GetObject(), L"href");
                if (href.find(L"~large.jpg") != std::wstring::npos) { image = href; break; }
                if (href.find(L"~medium.jpg") != std::wstring::npos) image = href;
            }
            if (image.empty()) continue;
            if (image.rfind(L"http://", 0) == 0) image.replace(0, 7, L"https://");
            p.title = field(meta, L"title"); p.page = page; p.source = L"NASA Image Library";
            p.credit = field(meta, L"photographer");
            if (p.credit.empty()) p.credit = field(meta, L"secondary_creator");
            if (p.credit.empty()) p.credit = L"NASA / " + field(meta, L"center");
            p.credit += L" - see source page for image credits";
            break;
        }
    } else if (source == 3) {
        auto root = json(L"https://commons.wikimedia.org/w/api.php?action=query&format=json&formatversion=2&generator=search&gsrsearch=filetype:bitmap%20incategory:Featured_pictures_on_Wikimedia_Commons&gsrnamespace=6&gsrlimit=40&gsroffset=" + std::to_wstring(randomIndex(20) * 40) + L"&prop=imageinfo&iiprop=url%7Csize%7Cmime%7Cextmetadata&iiurlwidth=1920&iiextmetadatalanguage=en&iiextmetadatafilter=Artist%7CLicenseShortName");
        auto entries = root.GetNamedObject(L"query").GetNamedArray(L"pages");
        const auto start = randomIndex(entries.Size());
        for (uint32_t i = 0; i < entries.Size(); ++i) {
            auto item = entries.GetObjectAt((start + i) % entries.Size());
            if (!item.HasKey(L"imageinfo")) continue;
            auto info = item.GetNamedArray(L"imageinfo").GetObjectAt(0);
            auto mime = field(info, L"mime");
            if (mime != L"image/jpeg" && mime != L"image/png") continue;
            if (info.GetNamedNumber(L"width") < 1920 || info.GetNamedNumber(L"width") <= info.GetNamedNumber(L"height")) continue;
            p.page = field(info, L"descriptionurl");
            if (p.page == previous) continue;
            image = field(info, L"thumburl");
            if (image.empty()) image = field(info, L"url");
            p.title = field(item, L"title");
            if (p.title.rfind(L"File:", 0) == 0) p.title.erase(0, 5);
            auto meta = info.GetNamedObject(L"extmetadata", JsonObject());
            p.credit = plainText(field(meta.GetNamedObject(L"Artist", JsonObject()), L"value"));
            p.credit += L" | " + plainText(field(meta.GetNamedObject(L"LicenseShortName", JsonObject()), L"value"));
            p.source = L"Wikimedia Commons";
            break;
        }
    } else throw std::runtime_error("Unknown image source");
    if (image.empty()) throw std::runtime_error("No suitable image returned");
    auto bytes = fetch(image, 32 * 1024 * 1024);
    p.file = dataDir / L"pending.image";
    std::ofstream file(p.file, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
    if (!file) throw std::runtime_error("Cannot save image");
    // Decode before applying, rejecting HTML/error responses and huge images.
    winrt::com_ptr<IWICImagingFactory> factory;
    winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(factory.put())));
    winrt::com_ptr<IWICBitmapDecoder> decoder;
    winrt::check_hresult(factory->CreateDecoderFromFilename(p.file.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, decoder.put()));
    winrt::com_ptr<IWICBitmapFrameDecode> frame;
    winrt::check_hresult(decoder->GetFrame(0, frame.put()));
    UINT width = 0, height = 0;
    winrt::check_hresult(frame->GetSize(&width, &height));
    if (!width || !height || uint64_t(width) * height > 60000000) throw std::runtime_error("Invalid image dimensions");
    p.resolution = std::to_wstring(width) + L" × " + std::to_wstring(height);
    // Encode as BMP for reliable SPI_SETDESKWALLPAPER support, independent of source format.
    p.file = dataDir / L"pending.bmp";
    winrt::com_ptr<IWICStream> stream;
    winrt::check_hresult(factory->CreateStream(stream.put()));
    winrt::check_hresult(stream->InitializeFromFilename(p.file.c_str(), GENERIC_WRITE));
    winrt::com_ptr<IWICBitmapEncoder> encoder;
    winrt::check_hresult(factory->CreateEncoder(GUID_ContainerFormatBmp, nullptr, encoder.put()));
    winrt::check_hresult(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));
    winrt::com_ptr<IWICBitmapFrameEncode> output;
    winrt::check_hresult(encoder->CreateNewFrame(output.put(), nullptr));
    winrt::check_hresult(output->Initialize(nullptr));
    winrt::check_hresult(output->SetSize(width, height));
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    winrt::check_hresult(output->SetPixelFormat(&format));
    winrt::check_hresult(output->WriteSource(frame.get(), nullptr));
    winrt::check_hresult(output->Commit());
    winrt::check_hresult(encoder->Commit());
    return p;
}
void saveSettings() {
    WritePrivateProfileStringW(L"Settings", L"Hours", std::to_wstring(hours).c_str(), config.c_str());
    WritePrivateProfileStringW(L"Settings", L"Source", sourceNames[provider], config.c_str());
    WritePrivateProfileStringW(L"Settings", L"Paused", paused ? L"1" : L"0", config.c_str());
}
void persistPhoto() {
    auto path = dataDir / L"current.ini";
    if (!fs::exists(path)) { std::ofstream bom(path, std::ios::binary); bom.write("\xff\xfe", 2); }
    for (auto pair : {std::pair{L"Title", current.title}, {L"Credit", current.credit},
        {L"Page", current.page}, {L"Resolution", current.resolution}, {L"Source", current.source},
        {L"File", current.file.wstring()}})
        WritePrivateProfileStringW(L"Photo", pair.first, pair.second.c_str(), path.c_str());
}
std::wstring readIni(const fs::path& path, const wchar_t* section, const wchar_t* key) {
    wchar_t buffer[4096]{};
    GetPrivateProfileStringW(section, key, L"", buffer, 4096, path.c_str());
    return buffer;
}
void updateTray() {
    std::wstring tip = L"AutoWallpaper - " + std::wstring(paused ? L"Paused" : busy ? L"Downloading" : L"Running");
    wcsncpy_s(tray.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &tray);
}
void beginDownload() {
    if (busy || stopping) return;
    if (worker.joinable()) worker.join();
    busy = true;
    status = L"Downloading wallpaper...";
    updateTray();
    const int source = provider;
    const unsigned token = generation;
    const auto previous = current.page;
    worker = std::thread([source, token, previous] {
        auto result = std::make_unique<Result>();
        result->generation = token;
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            result->photo = download(source, previous);
        } catch (const winrt::hresult_error& e) { result->error = windowsError(e); }
          catch (const std::exception& e) { result->error = wide(e.what()); }
        if (!stopping && PostMessageW(windowHandle, DoneMessage, 0, reinterpret_cast<LPARAM>(result.get()))) result.release();
    });
}
std::wstring menuText(std::wstring s) {
    for (auto& c : s) if (c == L'&') c = L'＋'; else if (c < L' ') c = L' ';
    if (s.size() > 110) s = s.substr(0, 107) + L"…";
    return s;
}
void showMenu() {
    HMENU menu = CreatePopupMenu();
    auto info = [&](const std::wstring& s) { AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, menuText(s).c_str()); };
    info(current.title.empty() ? L"No wallpaper set yet" : current.title);
    if (!current.source.empty()) info(current.source + L" · " + current.resolution);
    if (!current.credit.empty()) info(current.credit);
    info(status);
    AppendMenuW(menu, MF_STRING | (current.page.empty() ? MF_GRAYED : 0), Open, L"Open source page / Image details");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (busy ? MF_GRAYED : 0), Next, L"Next wallpaper");
    AppendMenuW(menu, MF_STRING | (paused ? MF_CHECKED : 0), Pause, paused ? L"Resume automatic changes" : L"Pause automatic changes");
    HMENU intervalMenu = CreatePopupMenu();
    for (int i = 0; i < 4; ++i) AppendMenuW(intervalMenu, MF_STRING | (hours == intervals[i] ? MF_CHECKED : 0),
        HoursBase + i, (L"Every " + std::to_wstring(intervals[i]) + L" hours").c_str());
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(intervalMenu), L"Change interval");
    HMENU sources = CreatePopupMenu();
    for (int i = 0; i < SourceCount; ++i)
        AppendMenuW(sources, MF_STRING | (provider == i ? MF_CHECKED : 0), Wallhaven + i, sourceLabels[i]);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(sources), L"Image source");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, Exit, L"Exit");
    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(windowHandle);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, windowHandle, nullptr);
    DestroyMenu(menu);
    PostMessageW(windowHandle, WM_NULL, 0, 0);
    if (command) PostMessageW(windowHandle, WM_COMMAND, command, 0);
}
void addTray() {
    tray.cbSize = sizeof(tray); tray.hWnd = windowHandle; tray.uID = 1;
    tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    tray.uCallbackMessage = TrayMessage;
    tray.hIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    wcscpy_s(tray.szTip, L"AutoWallpaper");
    Shell_NotifyIconW(NIM_ADD, &tray);
    updateTray();
}
LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == taskbarCreated && taskbarCreated) { addTray(); return 0; }
    switch (message) {
    case TrayMessage:
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU || lParam == WM_LBUTTONUP) showMenu();
        return 0;
    case WM_TIMER:
        if (!paused && !busy && GetTickCount64() >= due) beginDownload();
        return 0;
    case DoneMessage: {
        std::unique_ptr<Result> result(reinterpret_cast<Result*>(lParam));
        if (worker.joinable()) worker.join();
        busy = false;
        if (result->generation != generation) {
            status = paused ? L"Paused - keeping current wallpaper" : L"Settings updated - waiting for next change";
            updateTray();
            return 0;
        }
        if (result->error.empty()) {
            // Alternate filenames: never overwrite the currently active wallpaper before success.
            auto target = dataDir / (current.file.filename() == L"wallpaper-a.bmp" ? L"wallpaper-b.bmp" : L"wallpaper-a.bmp");
            if (!MoveFileExW(result->photo.file.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING))
                result->error = L"Cannot save wallpaper file";
            else if (!SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, const_cast<wchar_t*>(target.c_str()), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE))
                result->error = L"Windows could not set wallpaper";
            else {
                current = std::move(result->photo); current.file = target; persistPhoto();
                status = L"Updated - every " + std::to_wstring(hours) + L" hours";
            }
        }
        if (!result->error.empty()) status = std::wstring(paused ? L"Failed (automatic changes paused): " : L"Failed (retry in 15 minutes): ") + result->error;
        due = GetTickCount64() + (result->error.empty() ? ULONGLONG(hours) * 3600000 : 900000);
        updateTray();
        return 0;
    }
    case WM_COMMAND: {
        const UINT id = LOWORD(wParam);
        if (id == Exit) { DestroyWindow(hwnd); return 0; }
        if (id == Next) beginDownload();
        if (id == Open && current.page.rfind(L"https://", 0) == 0)
            ShellExecuteW(hwnd, L"open", current.page.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (id == Pause) {
            paused = !paused; ++generation;
            due = GetTickCount64() + ULONGLONG(hours) * 3600000;
            status = paused ? L"Paused - keeping current wallpaper" : L"Automatic changes resumed";
            saveSettings(); updateTray();
        }
        if (id >= Wallhaven && id < Wallhaven + SourceCount) {
            provider = static_cast<int>(id - Wallhaven); ++generation;
            due = GetTickCount64(); saveSettings();
            status = paused ? L"Source changed - still paused" : L"Source changed - waiting for wallpaper";
            if (!paused && !busy) beginDownload();
        }
        if (id >= HoursBase && id < HoursBase + 4) {
            hours = intervals[id - HoursBase];
            due = GetTickCount64() + ULONGLONG(hours) * 3600000;
            status = L"Interval set to " + std::to_wstring(hours) + L" hours";
            saveSettings();
        }
        return 0;
    }
    case WM_DESTROY:
        stopping = true;
        KillTimer(hwnd, 1);
        Shell_NotifyIconW(NIM_DELETE, &tray);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\AutoWallpaper.Native.v1");
    if (!mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(mutex); return 0; }
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        PWSTR local = nullptr;
        winrt::check_hresult(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local));
        dataDir = fs::path(local) / L"AutoWallpaper";
        CoTaskMemFree(local);
        // A smoke-test mode downloads and decodes only; never changes desktop wallpaper.
        if (wcsstr(commandLine, L"--check")) {
            dataDir = fs::current_path() / L"check-output";
            fs::create_directories(dataDir);
            std::error_code ignored;
            fs::remove(fs::current_path() / L"check-error.txt", ignored);
            fs::remove(dataDir / L"result.txt", ignored);
            int checkSource = wcsstr(commandLine, L"bing") ? 1 : wcsstr(commandLine, L"nasa") ? 2 : wcsstr(commandLine, L"commons") ? 3 : 0;
            auto p = download(checkSource, L"");
            std::ofstream(dataDir / L"result.txt") << winrt::to_string(p.title + L"\n" + p.resolution + L"\n" + p.page);
            CloseHandle(mutex);
            return 0;
        }
        fs::create_directories(dataDir);
        config = dataDir / L"settings.ini";
        hours = GetPrivateProfileIntW(L"Settings", L"Hours", 6, config.c_str());
        if (hours != 3 && hours != 6 && hours != 12 && hours != 24) hours = 6;
        const auto savedSource = readIni(config, L"Settings", L"Source");
        for (int i = 0; i < SourceCount; ++i) if (savedSource == sourceNames[i]) provider = i;
        paused = GetPrivateProfileIntW(L"Settings", L"Paused", 0, config.c_str()) != 0;
        auto photoIni = dataDir / L"current.ini";
        current = {readIni(photoIni, L"Photo", L"Title"), readIni(photoIni, L"Photo", L"Credit"),
            readIni(photoIni, L"Photo", L"Page"), readIni(photoIni, L"Photo", L"Resolution"),
            readIni(photoIni, L"Photo", L"Source"), readIni(photoIni, L"Photo", L"File")};
        if (paused) status = L"Paused - keeping current wallpaper";
        else if (!current.file.empty() && fs::exists(current.file)) {
            due = GetTickCount64() + ULONGLONG(hours) * 3600000;
            status = L"Running - every " + std::to_wstring(hours) + L" hours";
        }
        taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
        WNDCLASSW wc{}; wc.lpfnWndProc = windowProc; wc.hInstance = instance; wc.lpszClassName = L"AutoWallpaper.Native";
        wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
        if (!RegisterClassW(&wc)) throw std::runtime_error("Cannot register window");
        windowHandle = CreateWindowExW(0, wc.lpszClassName, L"AutoWallpaper", WS_OVERLAPPED, 0, 0, 0, 0,
            nullptr, nullptr, instance, nullptr);
        if (!windowHandle) throw std::runtime_error("Cannot create window");
        addTray();
        SetTimer(windowHandle, 1, 30000, nullptr);
        if (!paused && due == 0) beginDownload();
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
        if (worker.joinable()) worker.join();
        while (PeekMessageW(&message, nullptr, DoneMessage, DoneMessage, PM_REMOVE)) delete reinterpret_cast<Result*>(message.lParam);
    } catch (const std::exception& e) {
        if (wcsstr(commandLine, L"--check")) std::ofstream("check-error.txt") << e.what();
        else MessageBoxW(nullptr, wide(e.what()).c_str(), L"AutoWallpaper startup failed", MB_OK | MB_ICONERROR);
        CloseHandle(mutex); return 1;
    } catch (const winrt::hresult_error& e) {
        if (wcsstr(commandLine, L"--check")) std::ofstream("check-error.txt") << winrt::to_string(e.message());
        else MessageBoxW(nullptr, windowsError(e).c_str(), L"AutoWallpaper startup failed", MB_OK | MB_ICONERROR);
        CloseHandle(mutex); return 1;
    }
    CloseHandle(mutex);
    return 0;
}
