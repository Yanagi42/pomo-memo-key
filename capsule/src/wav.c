#include "capsule.h"

#include <string.h>

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    put_u16(p, (uint16_t)v);
    put_u16(p + 2, (uint16_t)(v >> 16));
}

static unsigned get_u16(const uint8_t *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)get_u16(p) | ((uint32_t)get_u16(p + 2) << 16);
}

int capsule_wav_write(uint8_t *dst, size_t cap, const int16_t *pcm, size_t samples, size_t *out_len)
{
    size_t data_bytes;
    size_t total;
    if (out_len) *out_len = 0;
    if (!dst || !out_len || !pcm || samples == 0) return -1;
    if (((uintptr_t)dst & 1u) != 0) return -1;
    if (samples > (SIZE_MAX - 44) / 2) return -1;
    data_bytes = samples * 2;
    if (data_bytes > (size_t)UINT32_MAX - 36u) return -1;
    total = 44 + data_bytes;
    if (cap < total) return -1;
    memcpy(dst, "RIFF", 4);
    put_u32(dst + 4, (uint32_t)(36 + data_bytes));
    memcpy(dst + 8, "WAVE", 4);
    memcpy(dst + 12, "fmt ", 4);
    put_u32(dst + 16, 16);
    put_u16(dst + 20, 1);
    put_u16(dst + 22, 1);
    put_u32(dst + 24, CAPSULE_WAV_RATE);
    put_u32(dst + 28, CAPSULE_WAV_RATE * 2);
    put_u16(dst + 32, 2);
    put_u16(dst + 34, 16);
    memcpy(dst + 36, "data", 4);
    put_u32(dst + 40, (uint32_t)data_bytes);
    if (samples) memcpy(dst + 44, pcm, data_bytes);
    *out_len = total;
    return 0;
}

int capsule_wav_read(const uint8_t *src, size_t len, const int16_t **pcm, size_t *samples)
{
    uint32_t riff_size;
    size_t end;
    size_t offset;
    bool format = false;
    const uint8_t *data = NULL;
    size_t count = 0;
    if (pcm) *pcm = NULL;
    if (samples) *samples = 0;
    if (!src || !pcm || !samples || len < 12) return -1;
    if (memcmp(src, "RIFF", 4) || memcmp(src + 8, "WAVE", 4)) return -1;
    riff_size = get_u32(src + 4);
    if (riff_size < 4 || (size_t)riff_size > len - 8) return -1;
    end = (size_t)riff_size + 8;
    offset = 12;
    while (offset < end) {
        const uint8_t *chunk;
        uint32_t length;
        if (end - offset < 8) return -1;
        chunk = src + offset;
        length = get_u32(chunk + 4);
        offset += 8;
        if (length > end - offset) return -1;
        if (!memcmp(chunk, "fmt ", 4)) {
            const uint8_t *fmt = src + offset;
            if (format || length < 16) return -1;
            if (get_u16(fmt) != 1 || get_u16(fmt + 2) != 1) return -1;
            if (get_u32(fmt + 4) != CAPSULE_WAV_RATE || get_u32(fmt + 8) != CAPSULE_WAV_RATE * 2) return -1;
            if (get_u16(fmt + 12) != 2 || get_u16(fmt + 14) != 16) return -1;
            format = true;
        } else if (!memcmp(chunk, "data", 4)) {
            if (data || length == 0 || (length & 1)) return -1;
            /* 奇数偏移上的 int16 在板上是未对齐访问。 */
            if (((uintptr_t)(src + offset) & 1u) != 0) return -1;
            data = src + offset;
            count = length / 2;
        }
        offset += length;
        if (length & 1) {
            if (offset >= end) return -1;
            offset++;
        }
    }
    if (!format || !data) return -1;
    *pcm = (const int16_t *)data;
    *samples = count;
    return 0;
}
