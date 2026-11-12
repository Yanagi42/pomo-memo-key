#include "capsule.h"


int capsule_selftest(void)
{
    int failed = 0;
    capsule_pose_map_t map;
    int16_t pcm[4] = {1, -2, 3, -4};
    _Alignas(2) uint8_t file[64];
    size_t len = 0;
    const int16_t *out = NULL;
    size_t samples = 0;
    static int16_t memo[8];
    _Alignas(2) static uint8_t wav[64];
    capsule_t box;

    if (capsule_accel_millig(16384) != 999) failed++;
    capsule_pose_map_clear(&map);
    if (capsule_pose_match(&map, 0, 0, 1000) != CAPSULE_POSE_UNKNOWN) failed++;
    if (capsule_pose_map_set(&map, CAPSULE_POSE_STAND, 0, 0, 1000) != 0) failed++;
    if (capsule_pose_match(&map, 0, 0, 1000) != CAPSULE_POSE_STAND) failed++;
    if (capsule_wav_write(file, sizeof file, pcm, 4, &len) != 0) failed++;
    if (capsule_wav_read(file, len, &out, &samples) != 0 || samples != 4 || !out || out[0] != 1) failed++;
    if (capsule_init(&box, memo, 8, wav, 43) == 0) failed++;
    if (capsule_init(&box, memo, 8, wav, sizeof wav) != 0) failed++;
    if (capsule_set_stand_mode(&box, CAPSULE_STAND_POMO, 0) != 0) failed++;
    if (capsule_timer_toggle(&box, 0) != 0 || capsule_timer_state(&box) != CAPSULE_POMO_RUNNING) failed++;
    if (capsule_timer_toggle(&box, 1000) != 0 || capsule_timer_state(&box) != CAPSULE_POMO_PAUSED) failed++;
    if (capsule_timer_toggle(&box, 2000) != 0 || capsule_timer_state(&box) != CAPSULE_POMO_RUNNING) failed++;
    return failed;
}
