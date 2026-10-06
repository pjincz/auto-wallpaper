# AutoWallpaper

Windows 10/11 x64 原生 C++ 托盘壁纸程序。无主窗口、无广告、无图片介绍弹窗，无 Electron、Qt 或 .NET 依赖。

![AutoWallpaper icon](assets/app.png)

程序菜单、状态和提示为英语。图片原始标题和作者名保留图源内容；Bing 使用 en-US 英文介绍。项目图标同时用于 EXE、窗口类及托盘，ICO 内含 16/20/24/32/48/64/128/256 像素版本。可编辑源文件为 `assets/app.svg`，`tools/build-icon.ps1` 是对应的 Windows 图标生成脚本。

## 使用

运行 `dist\AutoWallpaper.exe`。第一次运行会下载并设置一张壁纸，默认 **Wallhaven、每 6 小时**。图标可能在 Windows 托盘的折叠菜单里。

右键托盘：

- 查看当前图片标题／编号、来源、实际分辨率和版权提示；点击原图页面查看完整介绍。
- 立即换一张（暂停期间也可手动换）。
- 暂停／恢复自动更换，暂停状态会保存。
- 选择 3、6、12、24 小时间隔。
- 在 Image source 菜单切换 Wallhaven、Bing、NASA 或 Wikimedia Commons。
- 退出；退出保留桌面当前壁纸。

没有自动添加开机启动。需要时可自行在 `Win+R` → `shell:startup` 中放置 EXE 的快捷方式。

设置与有限缓存位于 `%LOCALAPPDATA%\AutoWallpaper`。只保留两个轮换 BMP 和临时下载文件，不无限积累图片。重启程序或恢复自动更换后，重新计算一个完整间隔。电脑休眠期间不会唤醒电脑；到期任务在恢复后执行，定时检查精度约 30 秒。

暂停或切换图源会作废旧的自动下载结果；网络失败保留原壁纸，自动模式下 15 分钟后重试。错误只显示在菜单中。退出立即移除托盘，若正在等待网络，进程可能需要等待本次网络操作超时后才完全结束。

## 图源

| 来源 | 特点 | 当前支持 |
| --- | --- | --- |
| [Wallhaven](https://wallhaven.cc/help/api) | 专门的壁纸 API，随机选取普通分类、SFW、至少 1920×1080 的横屏图片；无需 API key 获取公开 SFW 图片。作者／版权信息以图片页面为准，SFW 依赖网站标签。 | 已接入，默认 |
| Bing | 摄影、风景和文字介绍丰富；使用非正式公开图片归档接口，接口可能变化。最近 8 天依次轮换。 | 已接入 |
| [Wikimedia Commons](https://www.mediawiki.org/wiki/API:Imageinfo) | 随机抽取精选图片搜索结果，选择至少 1920 像素宽的横向 JPEG/PNG；请求 1920 像素预览，并保留作者、许可和原页面。精选图片不等同于 SFW 分级。 | 已接入，无需 key |
| [NASA Image Library](https://images.nasa.gov/docs/images.nasa.gov_api_docs.pdf) | 随机搜索星云、星系、地球和哈勃主题，使用官方资源清单中的 large/medium JPEG，保留标题、署名和原页面。部分图片可能为方形／竖图或含第三方版权。 | 已接入，无需 key |

不推荐将 Unsplash 或 Pexels API 用作本程序图源：其官方规则限制独立壁纸应用。[Unsplash 规则](https://help.unsplash.com/en/articles/2511245-unsplash-api-guidelines)、[Pexels 规则](https://help.pexels.com/hc/en-us/articles/4405588861721-Can-I-use-the-API-as-a-wallpaper-app)。

图片可用于个人桌面不代表可以重新分发或商用。网络可达性取决于所在网络；不会自动切换到用户没有选择的来源。

## 编译

安装 Visual Studio 的“使用 C++ 的桌面开发”和 Windows 10/11 SDK，在项目目录执行：

```bat
build.cmd
```

也提供 CMake 工程（MSVC）。输出为 `dist\AutoWallpaper.exe`，静态链接 C++ 运行库，无需下载第三方库。

界面与壁纸设置使用 Win32；联网使用 WinHTTP；解码及 BMP 转换使用 WIC；JSON 使用 Windows 自带的 Windows.Data.Json（C++/WinRT 只是系统组件的 C++ 头文件封装，不依赖托管运行时）。下载线程仅在需要时创建，空闲时由消息循环等待。所有显示器使用 Windows 当前壁纸布局，不提供分别设置多屏壁纸功能。

## 验证

以下模式只下载、解析并解码图片到当前工作目录的 `check-output`，不会设置桌面或显示托盘：

```powershell
& .\dist\AutoWallpaper.exe --check bing | Out-Null
& .\dist\AutoWallpaper.exe --check wallhaven | Out-Null
& .\dist\AutoWallpaper.exe --check nasa | Out-Null
& .\dist\AutoWallpaper.exe --check commons | Out-Null
```

成功生成 `check-output\result.txt` 和 `pending.bmp`，失败写入 `check-error.txt` 并返回非零退出码。测试前应退出已运行的程序（程序为单实例）。

本机已通过 MSVC Release 编译，并实测四个来源的下载、JSON 解析和 WIC 转换。Wikimedia 曾返回 429，改用 1920 像素预览后通过；远程服务仍可能限流，失败时沿用程序的延迟重试。图标 PNG 已视觉检查，ICO 已嵌入 EXE。托盘交互和实际更换桌面仍需交互验收。
