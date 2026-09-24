#ifndef _WAV_UTILS_H_
#define _WAV_UTILS_H_

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define MK_LE_2(a0, a1) \
    (((a0)<<0) | ((a1)<<8))

#define MK_LE_4(a0, a1, a2, a3) \
    (((a0)<<0) | ((a1)<<8) | ((a2)<<16) | ((a3)<<24))

static inline unsigned short wav_get_half(uint8_t *d, int off)
{
    d += off;
    return MK_LE_2(d[0], d[1]);
}

static inline unsigned int wav_get_word(uint8_t *d, int off)
{
    d += off;
    return MK_LE_4(d[0], d[1], d[2], d[3]);
}

#define WAV_FORMAT_PCM 0x0001 // PCM
#define WAV_FORMAT_IEEE_FLOAT 0x0003 // IEEE float
#define WAV_FORMAT_ALAW 0x0006 // 8-bit ITU-T G.711 A-law
#define WAV_FORMAT_MULAW 0x0007 // 8-bit ITU-T G.711 µ-law
#define WAV_FORMAT_EXTENSIBLE 0xFFFE // Determined by SubFormat

#define wav_riff_ckid(d) wav_get_word(d, 0)             // "RIFF"
#define wav_riff_cksize(d) wav_get_word(d, 4)           // wav 文件大小 减去 8
#define wav_riff_wavid(d) wav_get_word(d, 8)            // "WAVE"

static inline uint32_t wave_find_ck(uint8_t *d, const char *ck)
{
    if (!(wav_riff_ckid(d) == MK_LE_4('R','I','F','F')) ||
        !(wav_riff_wavid(d) == MK_LE_4('W','A','V','E')))
        return -1;

    uint32_t N = wav_riff_cksize(d);
    int n = 4;

    d += 12;

    while (1) {
        if (d[0]==ck[0]&&d[1]==ck[1]&&d[2]==ck[2]&&d[3]==ck[3])
            return n+8;
        uint32_t sz = wav_get_word(d, 4);
        if ((N - n) <= sz)
            return -1;
        n += sz + 8;
        d += sz + 8;
    }

    return 0;
}

static inline uint32_t wav_ck_word(uint8_t *d, const char *name, int off)
{
    uint32_t n = wave_find_ck(d, name);
    printf("n: %d\n", n);
    if (n == -1)
        return 0;
    return wav_get_word(d, n+off);
}

static inline uint16_t wav_ck_half(uint8_t *d, const char *name, int off)
{
    uint32_t n = wave_find_ck(d, name);
    printf("n: %d\n", n);
    if (n == -1)
        return 0;
    return wav_get_half(d, n+off);
}

#define wav_fmt_cksize(d)           wav_ck_word(d, "fmt ", 4)     // 16 (标准的都是16)
#define wav_fmt_audio_format(d)     wav_ck_half(d, "fmt ", 8)     // 格式 看 WAV_FORMAT* 的宏
#define wav_fmt_num_channels(d)     wav_ck_half(d, "fmt ", 10)    // 通道数 1, 2, 3, ...
#define wav_fmt_sample_rate(d)      wav_ck_word(d, "fmt ", 12)    // 采样率 16000, 48000, 44100 ...
#define wav_fmt_byte_rate(d)        wav_ck_word(d, "fmt ", 16)
#define wav_fmt_block_align(d)      wav_ck_half(d, "fmt ", 20)    // 1 2 4, ...
#define wav_fmt_bits_per_sample(d)  wav_ck_half(d, "fmt ", 22)    // 8, 16, 24, 32

#define wav_data_cksize(d)          wav_ck_word(d, "data", 4)    // 音频数据有效大小, byte 为单位

#define wav_extra_data_size(d) (wav_riff_cksize(d) - wav_data_cksize(d) - 36)

#define wav_is_ok(d) \
  ((wav_riff_ckid(d) == MK_LE_4('R','I','F','F')) && \
   (wav_riff_wavid(d) == MK_LE_4('W','A','V','E')) && \
   (wav_fmt_cksize(d) == 16) && \
   (wav_data_cksize(d) != 0))

static inline void *wav_pcm_data(uint8_t *d)
{
    uint32_t n = wave_find_ck(d, "data");
    return n == -1 ? d : d+n+8;
}

static inline uint32_t wav_pcm_frames(uint8_t *d)
{
    return wav_data_cksize(d) / wav_fmt_block_align(d);
}

static inline void dump_wav(unsigned char *d)
{
    printf("riff_ckid: %x\n",  wav_riff_ckid(d));
    printf("riff_cksize: %d\n",  wav_riff_cksize(d));
    printf("riff_wavid: %x\n",  wav_riff_wavid(d));
    printf("fmt_cksize: %d\n",  wav_fmt_cksize(d));
    printf("fmt_audio_format: %x\n",  wav_fmt_audio_format(d));
    printf("fmt_num_channels: %d\n",  wav_fmt_num_channels(d));
    printf("fmt_sample_rate: %d\n",  wav_fmt_sample_rate(d));
    printf("fmt_byte_rate: %d\n",  wav_fmt_byte_rate(d));
    printf("fmt_block_align: %d\n",  wav_fmt_block_align(d));
    printf("fmt_bits_per_sample: %d\n",  wav_fmt_bits_per_sample(d));
    printf("data_cksize: %d\n",  wav_data_cksize(d));
    printf("extral data size: %d\n", wav_extra_data_size(d));

    printf("wav is ok: %d\n", wav_is_ok(d));
    printf("wav pcm frames: %d\n", wav_pcm_frames(d));
    printf("off: fmt:%d data:%d LIST:%d\n", wave_find_ck(d, "fmt "), 
     wave_find_ck(d, "data "),  wave_find_ck(d, "LIST"));
}

// #include "include/include_bin.h"

// INCBIN(data, "/home/jwu/下载/nfc_same.wav");

// int main(int argc, char *argv[])
// {
//     uint8_t *d = (void *) dataData;

//     dump_wav(d);

//     return 0;
// }

#endif /* _WAV_UTILS_H_ */
