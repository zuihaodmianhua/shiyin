# 拾音 · Shiyin 1.0.0 — 发布说明

## 中文

一个本地音乐播放器，能在 Windows 95 上运行，也能在现在的 Windows 上用。

### 主要功能
- MP3 / FLAC / WAV / AIFF / AU，自写解码器，不依赖系统编解码器
- 深色 / 浅色 / 跟随时间主题，歌词、迷你播放器、系统托盘
- 播放列表导入导出（M3U/PLS）、多选、排序、拖拽调序
- 每首独立播放位置，元数据缓存让大歌单二次启动很快
- 无缝衔接、ReplayGain、淡入淡出
- 界面中英文切换，立即生效

### 兼容性
- Windows 95 起可用：不链接 C 运行库，只导入 6 个系统 DLL，PE 头 4.0
- 同时支持 Windows 2000 / XP / Vista / 7 / 8 / 10 / 11

### 已知限制
- 不支持 AAC / OGG / WMA / APE

### 文件
| 文件 | 大小 | 说明 |
| --- | --- | --- |
| `Shiyin.exe` | 340822 字节 | 主程序，无需安装 |
| `Shiyin-1.0-src.zip` | 约 230 KB | 完整源码（MPL-2.0） |

SHA256（Shiyin.exe）：

```
CA8B6A8FC7608A10FC3243758795FFF2BCC93B09A9DFDEA069460FA53F00A3DD
```

---

## English

Shiyin 1.0 is a local music player for Windows 95 and later.

### Features
- MP3 / FLAC / WAV / AIFF / AU with its own decoders; no system codecs required
- Dark, light and follow-the-clock themes; lyrics; compact player; tray icon
- Playlist import/export (M3U, PLS), multi-selection, sorting, drag to reorder
- Per-track resume positions and a metadata cache for fast restarts
- Gapless playback, ReplayGain, fade in/out
- Interface language switch (Chinese / English), effective immediately

### Compatibility
- Windows 95 and later: no C runtime, six system DLLs, PE headers at 4.0
- Also runs on Windows 2000 / XP / Vista / 7 / 8 / 10 / 11

### Known limitations
- AAC, OGG, WMA and APE are not supported

### Files
| File | Size | Notes |
| --- | --- | --- |
| `Shiyin.exe` | 340822 bytes | the player, no installer |
| `Shiyin-1.0-src.zip` | ~230 KB | full source (MPL-2.0) |
