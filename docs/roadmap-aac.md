> **状态：未实现。** 本文是 AAC 支持的设计方案（路线图），对应版本 1.0 并不包含该功能；
> 当前播放器遇到 AAC/M4A 会提示「暂不支持该格式」。
>
> **Status: not implemented.** This is a design document for future AAC support. Version 1.0
> does not include it; the player reports AAC/M4A files as unsupported.

# AAC 解码支持设计

本文说明如何为 Shiyin 增加 AAC 支持：支持哪些变体、数据从哪里来、解码流程如何组织、
和在现有播放引擎里怎么接。当前版本尚未实现该功能。

## 目标与非目标

**目标**

- AAC-LC（MPEG-4 Audio Object Type 2）
- 两种容器：裸 ADTS（`.aac`）与 MP4/M4A（`ftyp` + `moov`）
- 采样率 8–96 kHz，单声道与双声道
- 输出 16 bit 交织立体声，与 `dec_read()` 现有接口一致

**非目标**

- HE-AAC（SBR）、HE-AAC v2（PS）、AAC-LD/ELD、xHE-AAC
- 多声道（>2）下混、AAC 编码
- 遇到上述变体时必须明确提示"不支持"，而不是解出噪声

## 数据表

AAC-LC 需要四类常量。这些是规范中的公开数值，不是程序逻辑，因此单独生成到
`src/aac_tables.h`，由 `tools/gen_aac_tables.py` 产出，便于复核与更新。

| 数据 | 规模 | 用途 | 规范出处 |
| --- | --- | --- | --- |
| Huffman 码本 | 11 个码本 | 频谱系数与 scalefactor 编码 | ISO/IEC 14496-3 表 4.A.2 – 4.A.12 |
| scalefactor band 偏移 | 每采样率一组，最多 49 个 | 频带划分 | 表 4.129（`swb_offset`） |
| 窗系数 | 2048 / 256 点正弦窗 | IMDCT 前加窗 | 4.6.2 |
| TNS 最大频带数 | 每采样率一组 | 时域噪声整形 | 表 4.136 |

数值可以对照 FFmpeg 的 `aactab.c`（LGPL）核对，但只取数值，不复制其代码结构；
生成脚本把数值写成平铺数组，与参考实现的打包结构无关。

## 模块划分

```
src/aac.c
  aac_open / aac_read / aac_seek / aac_close
  位读取、ADTS 帧头、raw_data_block（SCE/CPE）、ICS 解析、
  scalefactor 解码、Huffman 频谱解码、反量化、TNS、M/S 与 intensity、
  IMDCT、窗序列切换、50% 重叠相加

src/aac_files.c
  MP4 box 遍历：ftyp / moov / trak / mdia / minf / stbl / stsd / mp4a / esds
  从 esds 取 AudioSpecificConfig，从 stts/stsc/stsz/stco 计算每帧的文件偏移

src/decode.c
  新增 K_AAC 分支；探测时检查 ADTS syncword（0xFFF）或 MP4 的 ftyp 品牌
```

## 解码流程

1. **帧头**：ADTS 读 syncword、profile、sampling_frequency_index、
   channel_configuration、frame_length、buffer_fullness；MP4 则直接由 stbl 给出每帧偏移。
2. **raw_data_block**：按元素标签循环，处理 SCE（单声道）与 CPE（双声道）。
3. **ICS**：window_sequence（长/长起/八短/长止）、max_sfb、predictor 标志（LC 不用）。
4. **scalefactors**：Huffman 差分解码后累加，得到每频带的量化步长。
5. **频谱**：按 section data 选码本，Huffman 解码，escape 序列展开，符号位还原。
6. **反量化**：`sign * |x|^(4/3) * 2^((sf - 100)/4)`。
7. **立体声处理**：M/S（中侧）与 intensity（强度）在频域完成。
8. **TNS**：按频带做 LPC 滤波，仅在标志置位时执行。
9. **PNS**：噪声替换频带用伪随机噪声填充。
10. **IMDCT + 加窗 + 重叠相加**：长窗 2048、短窗 256×8，输出 1024 个样本。

## 与播放引擎的集成

- 解码器每帧输出 1024 个立体声样本，填满现有 `dec_read()` 的缓冲区
- 重叠相加需要保留上一帧的时域尾段，状态随解码器实例走
- seek 采用"跳到最近帧边界 + 丢弃到目标位置"的策略，不重建全文件索引
- 内存预算：单实例常驻不超过 200 KB，适配 Windows 95 机器 32 MB 级别的内存

## 性能预算

AAC-LC 的运算量明显高于 MP3：每帧一次 2048 点 IMDCT 加一次 1024 点反变换。
在 Pentium 100 级别机器上，44.1 kHz 立体声每帧的处理预算约 10 ms。为此：

- IMDCT 用定点实现（32 bit 中间值），窗系数预处理成表
- 反量化与 Huffman 解码避免浮点
- 不在解码线程里做内存分配

486 级别机器无法实时解码 AAC，这与现有 MP3 的能力边界一致。

## 测试方案

- 用 ffmpeg 生成 16 bit、44.1/48 kHz、单声道/立体声的样本，覆盖 ADTS 与 M4A 两种容器
- 与 libavcodec 的解码结果比对：最大误差不超过 1 LSB，SNR 不低于 90 dB
- 边界用例：首帧前有 ID3、帧长异常、TNS 开关、短窗序列、损坏的 moov
- 回归：现有 MP3 / FLAC / WAV / AIFF / AU 的测试必须继续通过

## 已知风险

- Huffman 表录入错误难以定位 —— 用"每组码本前缀码长度完备"做自检
- MP4 box 解析遇到损坏文件必须安全退出，不能越界读
- 市面上 HE-AAC 文件很多，必须准确识别并给出明确提示

## 参考

- ISO/IEC 13818-7（MPEG-2 AAC）、ISO/IEC 14496-3（MPEG-4 Audio）
- FFmpeg `libavcodec/aactab.c`：仅用于核对常量数值

---

# AAC Support Design (English)

This document describes how Shiyin would gain AAC support: which variants are in scope,
where the tables come from, how decoding is organised, and how it plugs into the existing
playback engine. The feature is **not implemented** in the current release.

## Scope

**Goals**

- AAC-LC (MPEG-4 Audio Object Type 2)
- Two containers: raw ADTS (`.aac`) and MP4/M4A (`ftyp` + `moov`)
- 8–96 kHz, mono and stereo
- 16-bit interleaved output, matching the existing `dec_read()` interface

**Non-goals**

- HE-AAC (SBR), HE-AAC v2 (PS), AAC-LD/ELD, xHE-AAC
- Downmixing more than two channels, AAC encoding
- Files using these variants must be reported as unsupported instead of decoding to noise

## Tables

AAC-LC needs four groups of constants. They are public numbers from the specification,
not program logic, so they are generated into `src/aac_tables.h` by
`tools/gen_aac_tables.py` to keep the values reviewable.

| Data | Size | Purpose | Specification |
| --- | --- | --- | --- |
| Huffman codebooks | 11 codebooks | spectral and scalefactor coding | ISO/IEC 14496-3 tables 4.A.2–4.A.12 |
| Scalefactor band offsets | one set per sample rate, up to 49 bands | band layout | table 4.129 (`swb_offset`) |
| Window coefficients | 2048 / 256 point sine | windowing before IMDCT | 4.6.2 |
| TNS max bands | one set per sample rate | temporal noise shaping | table 4.136 |

The numbers can be cross-checked against FFmpeg's `aactab.c` (LGPL), but only the values
are used; the generated file is a flat array and does not mirror their packing.

## Modules

```
src/aac.c
  aac_open / aac_read / aac_seek / aac_close
  bit reader, ADTS header, raw_data_block (SCE/CPE), ICS parsing,
  scalefactor decoding, Huffman spectral decoding, dequantisation, TNS,
  M/S and intensity, IMDCT, window sequence switching, 50% overlap-add

src/aac_files.c
  MP4 box walk: ftyp / moov / trak / mdia / minf / stbl / stsd / mp4a / esds
  reads AudioSpecificConfig from esds and frame offsets from stts/stsc/stsz/stco

src/decode.c
  new K_AAC branch; probing checks the ADTS syncword (0xFFF) or the MP4 ftyp brand
```

## Decoding steps

1. **Header**: ADTS reads syncword, profile, sampling_frequency_index,
   channel_configuration, frame_length, buffer_fullness; for MP4 the `stbl` gives the
   offset of every frame directly.
2. **raw_data_block**: loop over element tags, handling SCE (mono) and CPE (stereo).
3. **ICS**: window_sequence (long / long start / eight short / long stop), max_sfb,
   predictor flag (unused for LC).
4. **Scalefactors**: Huffman differential decoding, accumulated into a quantisation step
   per band.
5. **Spectrum**: codebook per section, Huffman decoding, escape sequence expansion,
   sign restoration.
6. **Dequantisation**: `sign * |x|^(4/3) * 2^((sf - 100)/4)`.
7. **Stereo**: M/S (mid-side) and intensity are applied in the frequency domain.
8. **TNS**: per-band LPC filtering, only where the flag is set.
9. **PNS**: noise substitution fills bands that carry no coded energy.
10. **IMDCT + window + overlap-add**: 2048-point long or 256-point short windows,
    producing 1024 samples.

## Integration with the playback engine

- Each frame yields 1024 stereo samples, filling the existing `dec_read()` buffer
- Overlap-add keeps the tail of the previous frame; the state lives with the decoder
- Seeking jumps to the nearest frame boundary and discards up to the target position;
  no full-file index is built
- Memory: under 200 KB resident per instance, which suits a 32 MB Windows 95 machine

## Performance budget

AAC-LC costs noticeably more than MP3: one 2048-point IMDCT plus a 1024-point inverse
transform per frame. On a Pentium 100 at 44.1 kHz stereo the budget is about 10 ms per
frame, so:

- IMDCT is implemented in fixed point (32-bit intermediates) with precomputed window tables
- Dequantisation and Huffman decoding avoid floating point
- No allocation happens on the decode path

A 486 cannot decode AAC in real time; that matches the existing limits for MP3.

## Testing

- Samples generated with ffmpeg: 16-bit, 44.1/48 kHz, mono and stereo, in both ADTS and M4A
- Compared against libavcodec output: maximum error of 1 LSB, SNR of at least 90 dB
- Edge cases: ID3 before the first frame, unusual frame length, TNS on and off, short
  window sequences, a damaged `moov`
- Regression: the existing MP3 / FLAC / WAV / AIFF / AU tests must keep passing

## Risks

- A transcription error in a Huffman table is hard to find — self-check that each
  codebook's prefix-code lengths are complete
- MP4 box parsing must fail safely on damaged files and never read out of bounds
- HE-AAC files are common; they must be recognised and reported clearly

## References

- ISO/IEC 13818-7 (MPEG-2 AAC), ISO/IEC 14496-3 (MPEG-4 Audio)
- FFmpeg `libavcodec/aactab.c`: used only to verify constant values
