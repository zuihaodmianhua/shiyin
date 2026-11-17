# 第三方代码说明

本项目**只包含两个第三方单头文件库**，且都是宽松许可；其余全部源码
（`src/*.c`、`src/*.h`）均为本项目原创实现。

| 组件 | 文件 | 许可 | 用途 | 是否修改 |
| --- | --- | --- | --- | --- |
| minimp3 | `third_party/minimp3.h` | **CC0 1.0**（作者放弃著作权，等同公有领域） | MP3 解码 | 未修改源码；仅通过编译宏关闭浮点输出与 SSE |
| stb_image | `third_party/stb_image.h` | **公有领域 / MIT 双许可**（本项目按公有领域使用） | JPEG / PNG / BMP 封面解码 | 未修改；仅定义 `STBI_THREAD_LOCAL` 为空以适配无 CRT 环境 |

## 许可兼容性

- CC0 与公有领域：无署名义务、无传染性，可与 MIT 项目自由组合；
- 项目整体以 **MPL-2.0** 发布（见 `LICENSE`）；MPL 与 CC0/公有领域代码可以在同一作品中组合，第三方文件保留各自原有声明即可；
- 不包含 GPL / LGPL / AGPL / MPL 代码，也不包含来源不明的代码片段；
- 构建时链接 MinGW-w64 的 `libgcc`（GCC Runtime Library Exception），
  允许在专有/开源程序中使用，不改变本项目的 MIT 许可。

## 非代码参考

以下**规范文档**用于实现解码与标签解析，规范文本本身不进入源码，也不构成
代码复用：

- ISO/IEC 11172-3（MPEG-1 Audio Layer III）、ISO/IEC 13818-3（MPEG-2）
- FLAC 格式规范（RFC 9639 及 xiph.org 文档）
- ID3v2.2 / 2.3 / 2.4 规范、Ogg Vorbis 注释规范
- RIFF/WAVE、AIFF、Sun AU 容器格式说明

## 自查方法

```bash
grep -rniE "copyright|gpl|bsd|mit|cc0" src/     # 仅命中普通英文单词
ls third_party/                                  # 只应有两个头文件
```

如后续引入新依赖，请在此文件补一行说明其许可。

---

# Third-Party Code (English)

This project contains exactly two third-party single-header libraries, both permissively
licensed. Everything under `src/` is original code written for this project.

| Component | File | License | Used for | Modified |
| --- | --- | --- | --- | --- |
| minimp3 | `third_party/minimp3.h` | **CC0 1.0** | MP3 decoding | no; float output and SSE are disabled with macros |
| stb_image | `third_party/stb_image.h` | **public domain / MIT** (used as public domain) | JPEG / PNG / BMP cover decoding | no; `STBI_THREAD_LOCAL` is defined empty for the no-CRT build |

## Compatibility

- CC0 and public domain impose no attribution and no copyleft, so they combine freely
  with an MPL-2.0 project
- The project is released under **MPL-2.0** (see `LICENSE`); third-party files keep their
  own notices
- No GPL, LGPL, AGPL or MPL code is bundled, and no code of unknown origin is present
- The build links MinGW-w64's `libgcc` (GCC Runtime Library Exception), which does not
  change the project license

## Specifications, not code

The decoders and tag parsers were written from format documentation. Specification text
is not part of the source and is not code reuse:

- ISO/IEC 11172-3 and ISO/IEC 13818-3 (MPEG-1/2 Audio Layer III)
- The FLAC format specification (RFC 9639 and the xiph.org documents)
- ID3v2.2 / 2.3 / 2.4, Ogg Vorbis comment specification
- RIFF/WAVE, AIFF and Sun AU container documentation

## Checking

```bash
grep -rniE "copyright|gpl|bsd|mit|cc0" src/   # only ordinary English words
ls third_party/                              # should be exactly two headers
```
