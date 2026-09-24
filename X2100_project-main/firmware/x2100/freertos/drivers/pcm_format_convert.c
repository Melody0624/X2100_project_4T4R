#define as_u8(ptr) (*(uint8_t*)(ptr))
#define as_u16(ptr) (*(uint16_t*)(ptr))
#define as_u32(ptr) (*(uint32_t*)(ptr))
#define as_u64(ptr) (*(uint64_t*)(ptr))

#define as_u8c(ptr) (*(const uint8_t*)(ptr))
#define as_u16c(ptr) (*(const uint16_t*)(ptr))
#define as_u32c(ptr) (*(const uint32_t*)(ptr))
#define as_u64c(ptr) (*(const uint64_t*)(ptr))

#define fmt(src, dst)   ((src << 4) | (dst))
#define convert(src_fmt, dst_fmt, opt) \
    case ((src_fmt << 4) | (dst_fmt)) : opt; break

static inline uint32_t sx24(uint32_t x)
{
    if(x&0x00800000)
        return x|0xFF000000;
    return x&0x00FFFFFF;
}

int pcm_format_convert(void *dst_, void *src_, int samples, pcm_data_fmt src_fmt, pcm_data_fmt dst_fmt)
{
    char *dst = dst_;
    const char *src = src_;
    int src_step = pcm_data_sample_size(src_fmt);
    int dst_step = pcm_data_sample_size(dst_fmt);

    while(samples --) {
        switch (fmt(src_fmt, dst_fmt)) {

            /* some format to pcm_fmt_S8 */
            convert(pcm_fmt_U8,    pcm_fmt_S8, as_u8(dst) = as_u8c(src) ^ 0x80);
            convert(pcm_fmt_S16LE, pcm_fmt_S8, as_u8(dst) = as_u16c(src) >> 8);
            convert(pcm_fmt_U16LE, pcm_fmt_S8, as_u8(dst) = (as_u16c(src) >> 8) ^ 0x80);
            convert(pcm_fmt_S24LE, pcm_fmt_S8, as_u8(dst) = as_u32c(src) >> 16);
            convert(pcm_fmt_U24LE, pcm_fmt_S8, as_u8(dst) = (as_u32c(src) >> 16) ^ 0x80);
            convert(pcm_fmt_S32LE, pcm_fmt_S8, as_u8(dst) = as_u32c(src) >> 24);
            convert(pcm_fmt_U32LE, pcm_fmt_S8, as_u8(dst) = (as_u32c(src) >> 24) ^ 0x80);

            /* some format to pcm_fmt_U8 */
            convert(pcm_fmt_S8,    pcm_fmt_U8, as_u8(dst) = as_u8c(src) ^ 0x80);
            convert(pcm_fmt_S16LE, pcm_fmt_U8, as_u8(dst) = (as_u16c(src) >> 8) ^ 0x80);
            convert(pcm_fmt_U16LE, pcm_fmt_U8, as_u8(dst) = as_u16c(src) >> 8);
            convert(pcm_fmt_S24LE, pcm_fmt_U8, as_u8(dst) = (as_u32c(src) >> 16) ^ 0x80);
            convert(pcm_fmt_U24LE, pcm_fmt_U8, as_u8(dst) = as_u32c(src) >> 16);
            convert(pcm_fmt_S32LE, pcm_fmt_U8, as_u8(dst) = (as_u32c(src) >> 24) ^ 0x80);
            convert(pcm_fmt_U32LE, pcm_fmt_U8, as_u8(dst) = as_u32c(src) >> 24);

            /* some format to pcm_fmt_S16LE */
            convert(pcm_fmt_S8,    pcm_fmt_S16LE, as_u16(dst) = (as_u8c(src) ^ 0x80) << 8);
            convert(pcm_fmt_U8,    pcm_fmt_S16LE, as_u16(dst) = as_u8c(src) << 8);
            convert(pcm_fmt_U16LE, pcm_fmt_S16LE, as_u16(dst) = as_u16c(src) ^ 0x8000);
            convert(pcm_fmt_S24LE, pcm_fmt_S16LE, as_u16(dst) = as_u32c(src) >> 8);
            convert(pcm_fmt_U24LE, pcm_fmt_S16LE, as_u16(dst) = (as_u32c(src) >> 8) ^ 0x8000);
            convert(pcm_fmt_S32LE, pcm_fmt_S16LE, as_u16(dst) = as_u32c(src) >> 16);
            convert(pcm_fmt_U32LE, pcm_fmt_S16LE, as_u16(dst) = (as_u32c(src) >> 16) ^ 0x8000);

            /* some format to pcm_fmt_U16LE */
            convert(pcm_fmt_S8,    pcm_fmt_U16LE, as_u16(dst) = as_u8c(src) << 8);
            convert(pcm_fmt_U8,    pcm_fmt_U16LE, as_u16(dst) = (as_u8c(src) << 8) ^ 0x8000);
            convert(pcm_fmt_S16LE, pcm_fmt_U16LE, as_u16(dst) = as_u16c(src) ^ 0x8000);
            convert(pcm_fmt_S24LE, pcm_fmt_U16LE, as_u16(dst) = (as_u32c(src) >> 8) ^ 0x8000);
            convert(pcm_fmt_U24LE, pcm_fmt_U16LE, as_u16(dst) = as_u32c(src) >> 8);
            convert(pcm_fmt_S32LE, pcm_fmt_U16LE, as_u16(dst) = (as_u32c(src) >> 16) ^ 0x8000);
            convert(pcm_fmt_U32LE, pcm_fmt_U16LE, as_u16(dst) = as_u32c(src) >> 16);

            /* some format to pcm_fmt_S24LE */
            convert(pcm_fmt_S8,    pcm_fmt_S24LE, as_u32(dst) = sx24((uint32_t)as_u8c(src) << 16));
            convert(pcm_fmt_U8,    pcm_fmt_S24LE, as_u32(dst) = sx24((uint32_t)(as_u8c(src) ^ 0x80) << 16));
            convert(pcm_fmt_S16LE, pcm_fmt_S24LE, as_u32(dst) = sx24((uint32_t)as_u16c(src) << 8));
            convert(pcm_fmt_U16LE, pcm_fmt_S24LE, as_u32(dst) = sx24((uint32_t)(as_u16c(src) ^ 0x8000) << 8));
            convert(pcm_fmt_U24LE, pcm_fmt_S24LE, as_u32(dst) = sx24(as_u32c(src) ^ 0x800000));
            convert(pcm_fmt_S32LE, pcm_fmt_S24LE, as_u32(dst) = sx24(as_u32c(src) >> 8));
            convert(pcm_fmt_U32LE, pcm_fmt_S24LE, as_u32(dst) = sx24((as_u32c(src) >> 8) ^ 0x800000));

            /* some format to pcm_fmt_U24LE */
            convert(pcm_fmt_S8,    pcm_fmt_U24LE, as_u32(dst) = sx24((uint32_t)(as_u8c(src) ^ 0x80) << 16));
            convert(pcm_fmt_U8,    pcm_fmt_U24LE, as_u32(dst) = sx24((uint32_t)as_u8c(src) << 16));
            convert(pcm_fmt_S16LE, pcm_fmt_U24LE, as_u32(dst) = sx24((uint32_t)(as_u16c(src) ^ 0x8000) << 8));
            convert(pcm_fmt_U16LE, pcm_fmt_U24LE, as_u32(dst) = sx24((uint32_t)as_u16c(src) << 8));
            convert(pcm_fmt_S24LE, pcm_fmt_U24LE, as_u32(dst) = sx24(as_u32c(src) ^ 0x800000));
            convert(pcm_fmt_S32LE, pcm_fmt_U24LE, as_u32(dst) = sx24((as_u32c(src) >> 8) ^ 0x800000));
            convert(pcm_fmt_U32LE, pcm_fmt_U24LE, as_u32(dst) = sx24(as_u32c(src) >> 8));

            /* some format to pcm_fmt_S32LE */
            convert(pcm_fmt_S8,    pcm_fmt_S32LE, as_u32(dst) = (uint32_t)as_u8c(src) << 24);
            convert(pcm_fmt_U8,    pcm_fmt_S32LE, as_u32(dst) = ((uint32_t)(as_u8c(src) ^ 0x80) << 24));
            convert(pcm_fmt_S16LE, pcm_fmt_S32LE, as_u32(dst) = (uint32_t)as_u16c(src) << 16);
            convert(pcm_fmt_U16LE, pcm_fmt_S32LE, as_u32(dst) = ((uint32_t)(as_u16c(src) ^ 0x8000) << 16));
            convert(pcm_fmt_S24LE, pcm_fmt_S32LE, as_u32(dst) = as_u32c(src) << 8);
            convert(pcm_fmt_U24LE, pcm_fmt_S32LE, as_u32(dst) = ((as_u32c(src) ^ 0x800000) << 8));
            convert(pcm_fmt_U32LE, pcm_fmt_S32LE, as_u32(dst) = (as_u32c(src) ^ 0x80000000));

            /* some format to pcm_fmt_U32LE */
            convert(pcm_fmt_S8,    pcm_fmt_U32LE, as_u32(dst) = ((uint32_t)(as_u8c(src) ^ 0x80) << 24));
            convert(pcm_fmt_U8,    pcm_fmt_U32LE, as_u32(dst) = ((uint32_t)as_u8c(src) << 24));
            convert(pcm_fmt_S16LE, pcm_fmt_U32LE, as_u32(dst) = ((uint32_t)(as_u16c(src) ^ 0x8000) << 16));
            convert(pcm_fmt_U16LE, pcm_fmt_U32LE, as_u32(dst) = (uint32_t)as_u16c(src) << 16);
            convert(pcm_fmt_S24LE, pcm_fmt_U32LE, as_u32(dst) = ((as_u32c(src) ^ 0x800000) << 8));
            convert(pcm_fmt_U24LE, pcm_fmt_U32LE, as_u32(dst) = (as_u32c(src) << 8));
            convert(pcm_fmt_S32LE, pcm_fmt_U32LE, as_u32(dst) = (as_u32c(src) ^ 0x80000000));

            default: printf("%s: src_fmt:%d to dst_fmt:%d is not supported\n", __FUNCTION__, src_fmt, dst_fmt);
            return -1;
        }

        src += src_step;
        dst += dst_step;
    }
    return 0;
}