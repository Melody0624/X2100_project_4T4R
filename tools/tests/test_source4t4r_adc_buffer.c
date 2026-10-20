/* Exercise the real startup and TLV packers with the existing host FFT stubs. */
#define main unused_pipeline_main
#include "test_vendor2t4r_source_pipeline.c"
#undef main

static int fail_next_malloc;
void *__real_malloc(size_t size);
void *__wrap_malloc(size_t size) {
    if (fail_next_malloc) { fail_next_malloc = 0; return NULL; }
    return __real_malloc(size);
}

int main(void) {
    GlbCtx *ctx = set_parameters(false, false, 0, 0);
    MemoryPool *pool = &g_memoryPool[0];
    assert(ctx && pool->initialized);
    unsigned int point_size = pool->shared_pkt_buf_size;
    char *point_buffer = pool->shared_pkt_buf;
    fft_cpx_f32 *dsp_buffer = pool->adcData_winDC;
    char *raw_buffer = pool->saver_pkt_buf_raw;
    unsigned int raw_bytes = ctx->waveParams.numChirps[0] *
        (sizeof(CheetahChirpHeadInfo) + sizeof(uint16_t) *
         ctx->basicParams.numRxs[0] * ctx->waveParams.adcLen[0] + CHIRP_END_BYTES);
    unsigned int required = sizeof(Mmw_output_message_header) + sizeof(Mmw_output_message_tl) + raw_bytes;
    assert(point_size < required);

    Mmw_pkt_info info = {0};
    info.detInfo.header.numDets = MAX_DETECTIONS;
    info.trkInfo.header.numTrks = MAX_TRACKS;
    int point_ret = mmw_packetize_results(&info, 1, point_buffer, NULL, 0, 0);
    assert(point_ret > 0 && (unsigned int)point_ret <= point_size);
    char *expected_points = malloc(point_ret);
    assert(expected_points);
    memcpy(expected_points, point_buffer, point_ret);
    assert(init_memory_pool(ctx, 0, 0, 0) == 0 && pool->shared_pkt_buf == point_buffer);

    /* A failed expansion must leave the original allocation and DSP intact. */
    fail_next_malloc = 1;
    assert(init_memory_pool(ctx, 0, 1, 0) == -1);
    assert(!fail_next_malloc && pool->shared_pkt_buf == point_buffer);
    assert(pool->shared_pkt_buf_size == point_size && pool->initialized);
    assert(pool->adcData_winDC == dsp_buffer && pool->saver_pkt_buf_raw == raw_buffer);
    assert(memcmp(point_buffer, expected_points, point_ret) == 0);

    assert(init_memory_pool(ctx, 0, 1, 0) == 0);
    assert(pool->shared_pkt_buf_size == required);
    assert(pool->adcData_winDC == dsp_buffer && pool->saver_pkt_buf_raw == raw_buffer);
    for (unsigned int i = 0; i < raw_bytes; ++i) raw_buffer[i] = (char)(i * 17 + 3);
    for (unsigned int frame = 2; frame < 102; ++frame) {
        int ret = adc_packetize_results(raw_buffer, raw_bytes, frame,
            pool->shared_pkt_buf, pool->shared_pkt_buf_size, NULL, 0, 0);
        assert(ret == (int)required);
        Mmw_output_message_header header;
        Mmw_output_message_tl tl;
        memcpy(&header, pool->shared_pkt_buf, sizeof(header));
        memcpy(&tl, pool->shared_pkt_buf + sizeof(header), sizeof(tl));
        assert(header.frameNumber == frame && header.numTLVs == 1);
        assert(header.totalPacketLen == required && header.magicWord[0] == 0x0102);
        assert(tl.type == MMW_OUTPUT_MSG_ADC_FRAME && tl.length == raw_bytes);
        assert(memcmp(pool->shared_pkt_buf + sizeof(header) + sizeof(tl), raw_buffer, raw_bytes) == 0);
    }
    char *adc_buffer = pool->shared_pkt_buf;
    assert(init_memory_pool(ctx, 0, 1, 0) == 0 && pool->shared_pkt_buf == adc_buffer);
    assert(init_memory_pool(ctx, 0, 0, 0) == 0 && pool->shared_pkt_buf == adc_buffer);
    assert(mmw_packetize_results(&info, 1, adc_buffer, NULL, 0, 0) == point_ret);
    assert(memcmp(adc_buffer, expected_points, point_ret) == 0);
    free(expected_points);
    free_memory_pool(0);
    free(ctx);

    /* Also cover an ADC-sized allocation on the first initialization. */
    ctx = set_parameters(false, false, 0, 0);
    assert(ctx);
    free_memory_pool(0);
    assert(init_memory_pool(ctx, 0, 1, 0) == 0);
    assert(pool->shared_pkt_buf_size >= required);
    memset(pool->saver_pkt_buf_raw, 0x5a, raw_bytes);
    assert(adc_packetize_results(pool->saver_pkt_buf_raw, raw_bytes, 1,
        pool->shared_pkt_buf, pool->shared_pkt_buf_size, NULL, 0, 0) == (int)required);
    free_memory_pool(0);
    free(ctx);
    printf("PASS: points=%u bytes ADC=%u bytes, 100 full ADC frames; repeated init, allocation failure and DSP pointer preservation\n",
           point_size, required);
    return 0;
}
