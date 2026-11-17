# 拾音 · Shiyin

一个本地音乐播放器，能在 Windows 95 上运行，也能在现在的 Windows 上用。

它只做本地播放这一件事：打开你硬盘里的音频文件，显示封面和标签，按你选的方式播放。
没有在线音乐、没有账号、没有推荐，也没有安装程序——一个 exe，双击就能用。

项目用 C 写成，不链接 C 运行库，只调用 Windows 95 自带的 6 个系统 DLL，所以不需要
VC++ 运行库、.NET 或任何第三方组件。编译出来的 exe 约 320 KB。

---

## 功能

**播放**

- 支持 MP3、FLAC、WAV、AIFF/AIFC、AU/SND
- 封面与标签：ID3v1/v2（含 APIC 封面）、FLAC 的 Vorbis 注释与 PICTURE、
  WAV 的 LIST/INFO、AIFF 文本块；没有内嵌封面时会在歌曲目录找 `cover.jpg` 等
- 播放模式：顺序、列表循环、单曲循环、随机，用一个按钮循环切换
- 无缝衔接：上一首放完直接把下一首接上去，中间不留空；两首采样率不同时自动退回
  普通切换，可以在设置里关掉
- ReplayGain：读标签里的增益值并应用，不同专辑之间音量不会忽大忽小
- 淡入淡出：开始播放时 250ms 淡入，暂停前 120ms 淡出，可以关掉
- 拖动进度条时只预览位置，松手才真正跳转，拖动过程中声音不断

**曲库**

- 添加单个文件、整个文件夹，或把文件拖进窗口
- 导入/导出 M3U、M3U8、PLS 播放列表，相对路径按列表所在目录解析
- 多选（Ctrl 点选、Shift 范围选择）、批量删除
- 点列头按标题/专辑/时长排序，也可以直接按住某一行上下拖动调整顺序
- 每首歌单独记住播放位置（最近 32 首），下次打开同一首从上次的位置继续
- 元数据缓存：第一次启动解析标签后写入 `shiyin.meta`，之后启动只读缓存，
  歌单大的时候启动快很多

**界面**

- 深色、浅色两套配色，也可以选"跟随时间"（7:00–19:00 用浅色，其余用深色，
  每分钟检查一次）
- 歌词：读取歌曲旁边的同名 `.lrc`，支持 UTF-8 和 GBK，当前行高亮并跟着播放滚动
- 迷你播放器：最小化时变成一个小窗口，显示封面、歌词和控制按钮，双击回到主界面
- 系统托盘：显示/隐藏窗口、播放控制、退出；可以设置关闭窗口时收进托盘
- 设置项都存在 `shiyin.ini` 里，改动约 1 秒后写入，退出时也会保存

## 支持的格式

| 格式 | 扩展名 | 说明 |
| --- | --- | --- |
| MP3 | `.mp3` | MPEG-1/2/2.5 Layer III，CBR/VBR |
| FLAC | `.flac` | 4–32 bit、1–8 声道，自写解码器 |
| WAV | `.wav` | PCM 8/16/24/32、float32、µ-law、A-law、IMA ADPCM |
| AIFF | `.aiff` `.aif` `.aifc` | PCM（大端/小端）、µ-law、A-law |
| AU | `.au` `.snd` | PCM、float32、µ-law、A-law |

输出统一成 16 bit 立体声，通过 `waveOut` 送到声卡。文件采样率和声卡不一致时
会做线性插值重采样。

不支持：AAC/M4A、OGG Vorbis、WMA、APE、Opus、DSD。

## 在 Windows 95 上

这个项目一开始就按 Windows 95 的目标写，具体做法是：

- 用 `-nostdlib` 编译，入口是自己的 `sy_entry`，内存分配、字符串、浮点全部自己实现，
  不依赖 `msvcrt.dll`
- 导入表只有 6 个 DLL：`kernel32`、`user32`、`gdi32`、`winmm`、`shell32`、`comdlg32`
- PE 头写的是 `Subsystem 4.0`、`OS version 4.0`，编译参数 `-march=i486`，不使用 SSE
- 界面全部用 GDI 手绘，不使用 `comctl32`；GDI 对象按颜色缓存复用，因为 95 的
  GDI 堆很小
- 需要新系统才有的能力（深色标题栏、DPI 感知、系统设置页面）用 `LoadLibrary` 探测，
  取不到就跳过
- 音频输出用 `waveOut`（`CALLBACK_EVENT`），不使用 WASAPI——那是 Vista 以后才有的接口

这些限制也带来一些代价：音频线程只有一个，界面重绘和播放会抢 CPU；缓冲区要给到
4×100ms 才不会在老机器上断音，所以拖动进度条、暂停这些操作的响应不是"立刻"，
而是有一两百毫秒的余量。

## 使用

1. 把 `Shiyin.exe` 放在任意可写目录，配置和缓存会写在同一目录；
2. 双击运行，点「添加文件」「文件夹」或者把文件拖进窗口；
3. 双击列表里的歌曲开始播放。

常用快捷键：`空格` 播放/暂停，`←` `→` 快退/快进，`Ctrl+←` `Ctrl+→` 上一首/下一首，
`↑` `↓` 上下选歌，`Delete` 删除，`/` 或 `Ctrl+F` 搜索，`L` 切换歌词，`Esc` 返回。

右键菜单里有：播放、从列表移除、打开所在文件夹、复制路径、上移/下移、导入/导出
播放列表、隐藏到托盘、输出设备、睡眠定时。

文件关联在设置里：9x/XP 直接写注册表；Windows 8 以后系统不允许程序直接改默认程序，
所以只会把拾音登记为候选并打开系统设置，由你自己选。

## 构建

需要 i686 的 mingw-w64 工具链，放在 `tools/mingw32`：

```
cmd /c build.bat
```

产物在 `build\Shiyin.exe`。辅助脚本在 `tools\`：

- `check_pe.py` — 检查导入表和 PE 头是否符合 Windows 95
- `test_flac.py` — FLAC 解码与参考解码器逐比特对比
- `test_modes.py` — 四种播放模式的行为测试
- `make_media.py` / `make_flac.py` — 生成测试用的音频文件

## 已知问题

- 不支持 AAC、OGG、WMA、APE，遇到这些文件只会提示不支持；AAC 的实现方案见 [docs/roadmap-aac.md](docs/roadmap-aac.md)（尚未实现）
- 无缝衔接只在两首采样率相同时生效，否则退回普通切换
- 元数据缓存只按"路径 + 文件大小"判断是否失效；如果只改了标签而没有改变文件大小，
  需要手动删掉 `shiyin.meta`
- 歌词只读同名 `.lrc`，不读内嵌歌词，也不联网获取
- Windows 10/11 上无法把程序设为默认播放器，只能登记到"打开方式"
- 托盘图标依赖资源管理器的通知区域；在某些受限会话里添加会失败，此时
  "最小化到托盘"和"关闭到托盘"会自动退回普通行为

## 第三方代码与许可

项目代码以 [Mozilla Public License 2.0](LICENSE) 发布。

第三方只有两个单头文件库，都是宽松许可，详见 [THIRD_PARTY.md](THIRD_PARTY.md)：

- `minimp3`（CC0）— MP3 解码
- `stb_image`（公有领域 / MIT）— 封面图解码

FLAC、WAV、AIFF、AU 的解码器，以及标签解析、播放引擎、界面绘制都是本项目
自己实现的。

---

# Shiyin (English)

A local music player that runs on Windows 95 and on current Windows releases.

It does one thing: it plays audio files from your disk, shows the cover and tags, and
lets you listen the way you want. No online catalogue, no account, no recommendations,
no installer — a single exe you can double-click.

The program is written in C, does not link against the C runtime, and imports only the
six system DLLs that ship with Windows 95, so it needs no VC++ redistributable, no .NET
and no third-party components. The resulting executable is about 320 KB.

## Features

**Playback**

- Formats: MP3, FLAC, WAV, AIFF/AIFC, AU/SND
- Tags and covers: ID3v1/v2 (including APIC), FLAC Vorbis comments and PICTURE,
  WAV LIST/INFO, AIFF text chunks; falls back to `cover.jpg` and friends in the
  same folder
- Playback modes: in order, repeat all, repeat one, shuffle — one button cycles
  through them
- Gapless playback: the next track is decoded into the same stream, so there is no
  silence between tracks; if the sample rates differ it falls back to a normal switch.
  Can be disabled
- ReplayGain: reads the gain value from the tags and applies it, so albums no longer
  jump in loudness
- Fade in (250 ms) at start and fade out (120 ms) before pausing; can be disabled
- Dragging the seek bar only previews the position; the real seek happens on release,
  so audio keeps playing while you drag

**Library**

- Add files, whole folders, or drag files onto the window
- Import and export M3U / M3U8 / PLS playlists, relative paths resolved against the
  playlist
- Multi-selection with Ctrl and Shift, batch removal
- Click a column header to sort by title, album or length; or drag a row up and down
- Per-track resume positions for the 32 most recent tracks, restored on the next launch
- Metadata cache in `shiyin.meta`: tags are parsed once, later launches read the cache,
  which matters for large libraries

**Interface**

- Dark and light palettes, plus a "follow the clock" mode (light between 07:00 and
  19:00, checked once a minute)
- Lyrics from a `.lrc` file next to the track (UTF-8 or GBK), current line highlighted
- Compact player when minimised: cover, lyrics and transport, double-click to restore
- System tray: show/hide, playback control, exit; optional "close to tray"
- Settings live in `shiyin.ini` and are written about a second after a change

## Format support

| Format | Extension | Notes |
| --- | --- | --- |
| MP3 | `.mp3` | MPEG-1/2/2.5 Layer III, CBR/VBR |
| FLAC | `.flac` | 4–32 bit, 1–8 channels, own decoder |
| WAV | `.wav` | PCM 8/16/24/32, float32, µ-law, A-law, IMA ADPCM |
| AIFF | `.aiff` `.aif` `.aifc` | PCM (big or little endian), µ-law, A-law |
| AU | `.au` `.snd` | PCM, float32, µ-law, A-law |

Output is always 16-bit stereo through `waveOut`; a linear resampler covers the case
where the file rate differs from the device rate.

Not supported: AAC/M4A, OGG Vorbis, WMA, APE, Opus, DSD.

## Windows 95 notes

- Built with `-nostdlib`; the entry point is `sy_entry` and the runtime (malloc, strings,
  floating point helpers) is part of the project, so `msvcrt.dll` is never loaded
- The import table contains only `kernel32`, `user32`, `gdi32`, `winmm`, `shell32`,
  `comdlg32`
- PE headers say `Subsystem 4.0` and `OS version 4.0`; compiled with `-march=i486`,
  no SSE
- The UI is drawn with plain GDI; GDI objects are cached and reused because the
  Windows 95 GDI heap is small
- Features that only exist on newer systems (dark title bar, DPI awareness, the Settings
  page for default apps) are resolved with `LoadLibrary` and skipped when unavailable
- Audio goes through `waveOut` with `CALLBACK_EVENT`; WASAPI is intentionally not used

The trade-off is real: a single audio thread shares one CPU with the UI, and the buffer
holds 4×100 ms so that old machines do not drop out, which is why seeking and pausing
have a few hundred milliseconds of latency rather than being instant.

## Building

An i686 mingw-w64 toolchain is expected in `tools/mingw32`:

```
cmd /c build.bat
```

The executable is written to `build\Shiyin.exe`. Helper scripts in `tools\`:

- `check_pe.py` — verifies the import table and PE headers are Windows 95 compatible
- `test_flac.py` — compares the FLAC decoder against a reference decoder, bit for bit
- `test_modes.py` — behavioural tests for the four playback modes
- `make_media.py` / `make_flac.py` — generate the test audio files

## Known issues

- AAC, OGG, WMA and APE are not supported; a design for AAC lives in [docs/roadmap-aac.md](docs/roadmap-aac.md) and is not implemented yet
- Gapless playback only works when both tracks share a sample rate
- The metadata cache is invalidated by path and file size only; re-tagging a file
  without changing its size requires deleting `shiyin.meta`
- Lyrics come from a sibling `.lrc` file; embedded lyrics and online lookup are not
  supported
- On Windows 10/11 the player cannot become the default handler by itself; it registers
  itself for "Open with" and opens the system settings page
- The tray icon needs the Explorer notification area; in restricted sessions adding it
  fails and "minimise to tray" / "close to tray" fall back to normal behaviour

## License

Source code is released under the [Mozilla Public License 2.0](LICENSE).

Two single-header libraries are bundled, both permissively licensed — see
[THIRD_PARTY.md](THIRD_PARTY.md):

- `minimp3` (CC0) — MP3 decoding
- `stb_image` (public domain / MIT) — cover image decoding

The FLAC, WAV, AIFF and AU decoders, the tag parsers, the playback engine and the UI
are implemented in this project.
