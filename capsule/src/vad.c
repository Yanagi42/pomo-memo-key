#include "capsule.h"

#include <string.h>

capsule_vad_config_t capsule_vad_default(void)
{
    capsule_vad_config_t cfg;
    cfg.rms_threshold = 800;
    cfg.frame_samples = 320; /* 20 ms */
    cfg.preroll_frames = 10; /* 200 ms */
    cfg.hangover_frames = 15; /* 300 ms */
    return cfg;
}

static int frame_is_speech(const int16_t *pcm, size_t count, int32_t threshold)
{
    int64_t sum = 0;
    int64_t energy = 0;
    int32_t mean;
    int64_t limit;
    if (!count || threshold <= 0) return 0;
    for (size_t i = 0; i < count; ++i) sum += pcm[i];
    mean = (int32_t)(sum / (int64_t)count);
    for (size_t i = 0; i < count; ++i) {
        int32_t delta = (int32_t)pcm[i] - mean;
        energy += (int64_t)delta * delta;
    }
    limit = (int64_t)threshold * threshold * (int64_t)count;
    return energy >= limit;
}

int capsule_vad_trim(const capsule_vad_config_t *cfg, int16_t *pcm, size_t n, size_t *kept)
{
    /* 单线程使用。放静态区，避免占满 RT-Thread 任务栈。 */
    static uint8_t speech[CAPSULE_VAD_MAX_FRAMES];
    static uint8_t keep[CAPSULE_VAD_MAX_FRAMES];
    size_t frames;
    size_t frame_n;
    size_t out = 0;
    if (kept) *kept = 0;
    if (!cfg || !kept || cfg->frame_samples == 0 || cfg->rms_threshold <= 0) return -1;
    if (n == 0) return 0;
    if (!pcm) return -1;
    frame_n = cfg->frame_samples;
    frames = (n + frame_n - 1) / frame_n;
    if (frames > CAPSULE_VAD_MAX_FRAMES) return -1;
    memset(speech, 0, frames);
    memset(keep, 0, frames);
    for (size_t f = 0; f < frames; ++f) {
        size_t offset = f * frame_n;
        size_t count = n - offset;
        if (count > frame_n) count = frame_n;
        speech[f] = (uint8_t)frame_is_speech(pcm + offset, count, cfg->rms_threshold);
    }
    for (size_t f = 0; f < frames; ++f) {
        size_t begin;
        size_t end;
        if (!speech[f]) continue;
        begin = f > cfg->preroll_frames ? f - cfg->preroll_frames : 0;
        end = f + (size_t)cfg->hangover_frames;
        if (end < f || end >= frames) end = frames - 1;
        for (size_t k = begin; k <= end; ++k) keep[k] = 1;
    }
    for (size_t f = 0; f < frames; ++f) {
        size_t offset;
        size_t count;
        if (!keep[f]) continue;
        offset = f * frame_n;
        count = n - offset;
        if (count > frame_n) count = frame_n;
        if (out != offset) memmove(pcm + out, pcm + offset, count * sizeof(int16_t));
        out += count;
    }
    *kept = out;
    return 0;
}
