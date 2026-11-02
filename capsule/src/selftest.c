#include "capsule.h"

#include <string.h>

int capsule_selftest(void)
{
    int failed = 0;
    capsule_pose_map_t map;
    int16_t pcm[4] = {1, -2, 3, -4};
    uint8_t file[64];
    size_t len = 0;
    const int16_t *out = NULL;
    size_t samples = 0;
    int16_t silence[8] = {0};
    size_t kept = 1;
    capsule_vad_config_t cfg;
    static int16_t memo[8];
    static uint8_t wav[64];
    capsule_t box;

    if (capsule_accel_millig(16384) != 999) failed++;
    capsule_pose_map_clear(&map);
    if (capsule_pose_match(&map, 0, 0, 1000) != CAPSULE_POSE_UNKNOWN) failed++;
    if (capsule_pose_map_set(&map, CAPSULE_POSE_STAND, 0, 0, 1000) != 0) failed++;
    if (capsule_pose_match(&map, 0, 0, 1000) != CAPSULE_POSE_STAND) failed++;
    if (capsule_wav_write(file, sizeof file, pcm, 4, &len) != 0) failed++;
    if (capsule_wav_read(file, len, &out, &samples) != 0 || samples != 4 || !out || out[0] != 1) failed++;
    cfg = capsule_vad_default();
    cfg.frame_samples = 4;
    if (capsule_vad_trim(&cfg, silence, 8, &kept) != 0 || kept != 0) failed++;
    if (capsule_init(&box, memo, 8, wav, 43) == 0) failed++;
    if (capsule_init(&box, memo, 8, wav, sizeof wav) != 0) failed++;
    return failed;
}
