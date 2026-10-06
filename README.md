# AutoWallpaper

[中文文档](README.zh.md)

A native C++ wallpaper tray app for Windows 10/11 x64. No main window, ads, or image-description pop-ups. No Electron, Qt, or .NET dependencies.

![AutoWallpaper icon](assets/app.png)

Menus, status messages, and prompts are in English. Image titles and author names retain their original source text; Bing uses English descriptions from the en-US feed. The application, window class, and tray share a custom icon with 16/20/24/32/48/64/128/256-pixel variants. The editable vector asset is `assets/app.svg`; `tools/build-icon.ps1` generates the corresponding Windows icon assets.

## Usage

Run `dist\AutoWallpaper.exe`. On first launch, the app downloads and applies a wallpaper. The default is **Wallhaven, every 6 hours**. The icon may appear in the Windows tray overflow menu.

Right-click the tray icon to:

- View the current image title or ID, source, actual resolution, and copyright information. Open the source page for full details.
- Select **Next wallpaper**, including while automatic changes are paused.
- Pause or resume automatic changes. The pause setting is saved.
- Choose an interval of 3, 6, 12, or 24 hours.
- Switch between Wallhaven, Bing, NASA, and Wikimedia Commons under **Image source**.
- Exit the app, leaving the current wallpaper in place.

The app does not add itself to startup. To launch it at sign-in, press `Win+R`, enter `shell:startup`, and place a shortcut to the EXE in that folder.

Settings and a bounded image cache are stored in `%LOCALAPPDATA%\AutoWallpaper`. The cache contains two alternating BMP files and temporary download files; it does not accumulate an unlimited image history. Restarting the app or resuming automatic changes starts a new full interval. The app does not wake a sleeping computer. Due updates run after the computer resumes, with timer checks approximately every 30 seconds.

Pausing or switching sources invalidates an in-progress download result. If a network request fails, the app keeps the current wallpaper and retries after 15 minutes while automatic changes are enabled. Errors appear only in the menu. Exiting removes the tray icon immediately, but an active network operation may need to time out before the process finishes.

## Image sources

| Source | Details | Support |
| --- | --- | --- |
| [Wallhaven](https://wallhaven.cc/help/api) | A dedicated wallpaper API. Random images from the general category, labeled SFW, at least 1920×1080, with landscape aspect ratios. Public SFW requests do not need an API key. Author and copyright details are available on the source page; SFW classification relies on the site's tags. | Included; default |
| Bing | Photography, scenery, and descriptions from an unofficial public image archive endpoint, which may change. Cycles through the most recent eight days. | Included |
| [Wikimedia Commons](https://www.mediawiki.org/wiki/API:Imageinfo) | Randomly samples featured-image search results, selecting landscape JPEG/PNG images at least 1920 pixels wide. Requests a 1920-pixel preview and retains author, license, and source-page information. Featured status is not an SFW rating. | Included; no API key |
| [NASA Image Library](https://images.nasa.gov/docs/images.nasa.gov_api_docs.pdf) | Random searches for nebulae, galaxies, Earth, and Hubble imagery. Uses large/medium JPEG renditions from the official asset manifest and retains titles, credits, and source pages. Some images may be square, portrait-oriented, or subject to third-party copyright. | Included; no API key |

Unsplash and Pexels APIs are not recommended for this app because their official rules restrict standalone wallpaper applications. See the [Unsplash guidelines](https://help.unsplash.com/en/articles/2511245-unsplash-api-guidelines) and [Pexels guidelines](https://help.pexels.com/hc/en-us/articles/4405588861721-Can-I-use-the-API-as-a-wallpaper-app).

Availability for personal desktop use does not imply permission to redistribute an image or use it commercially. Source availability depends on your network. The app does not automatically switch to a source you have not selected.

## Build

Install Visual Studio with the **Desktop development with C++** workload and a Windows 10/11 SDK, then run this command from the project directory:

```bat
build.cmd
```

The script produces `dist\AutoWallpaper.exe` with a statically linked C++ runtime. No third-party libraries need to be downloaded. A CMake project for MSVC is also included.

The interface and wallpaper settings use Win32. Networking uses WinHTTP, image decoding and BMP conversion use WIC, and JSON parsing uses Windows.Data.Json. C++/WinRT provides C++ headers for that built-in Windows component; it does not require a managed runtime. A download thread is created only when needed, and the message loop waits while idle. The app uses the current Windows wallpaper layout across displays; independent per-monitor wallpapers are not supported.

## Verification

These modes download, parse, and decode an image into `check-output` in the current working directory. They do not change the desktop wallpaper or show a tray icon:

```powershell
& .\dist\AutoWallpaper.exe --check bing | Out-Null
& .\dist\AutoWallpaper.exe --check wallhaven | Out-Null
& .\dist\AutoWallpaper.exe --check nasa | Out-Null
& .\dist\AutoWallpaper.exe --check commons | Out-Null
```

A successful check produces `check-output\result.txt` and `check-output\pending.bmp`. A failed check writes `check-error.txt` and returns a nonzero exit code. Exit any running instance before testing; the app allows only one instance at a time.

The MSVC Release build and image downloads, JSON parsing, and WIC conversion have been verified locally for all four sources. Wikimedia initially returned HTTP 429; a subsequent check using a 1920-pixel preview succeeded. Remote services may still rate-limit requests, in which case the app uses its delayed retry behavior. The icon PNG has been visually checked, and the ICO is embedded in the EXE. Tray interaction and actual desktop wallpaper changes still require interactive acceptance testing.
