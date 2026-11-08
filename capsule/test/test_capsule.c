#include "capsule.h"

#include <stdio.h>
#include <string.h>

static int checks;
static int failures;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

static void settle(capsule_t *box, int32_t x, int32_t y, int32_t z, uint32_t *now, uint32_t hold_ms)
{
    uint32_t end = *now + hold_ms;
    while (*now < end) {
        capsule_on_accel(box, x, y, z, *now);
        *now += 100;
    }
    capsule_on_accel(box, x, y, z, end);
    *now = end;
}

static void fill_wave(int16_t *pcm, size_t n)
{
    for (size_t i = 0; i < n; ++i) pcm[i] = (i & 1) ? -3000 : 3000;
}

static void standard_refs(capsule_t *box)
{
    capsule_set_ref(box, CAPSULE_POSE_STAND, 0, 0, 1000);
    capsule_set_ref(box, CAPSULE_POSE_FACE_DOWN, 0, 0, -1000);
    capsule_set_ref(box, CAPSULE_POSE_SIDE_LEFT, 1000, 0, 0);
    capsule_set_ref(box, CAPSULE_POSE_SIDE_RIGHT, -1000, 0, 0);
    capsule_set_ref(box, CAPSULE_POSE_FLAT, 0, 1000, 0);
}

static void test_pose_match(void)
{
    capsule_pose_map_t map;
    capsule_pose_map_clear(&map);
    CHECK(capsule_pose_match(&map, 0, 0, 1000) == CAPSULE_POSE_UNKNOWN);
    CHECK(capsule_pose_match(&map, 0, 0, 100) == CAPSULE_POSE_UNKNOWN);
    CHECK(capsule_pose_match(&map, 0, 0, 3000) == CAPSULE_POSE_UNKNOWN);

    capsule_pose_map_set(&map, CAPSULE_POSE_STAND, 0, 500, 866);
    CHECK(capsule_pose_match(&map, 0, 600, 800) == CAPSULE_POSE_STAND);
    CHECK(capsule_pose_match(&map, 1000, 0, 0) == CAPSULE_POSE_UNKNOWN);

    capsule_pose_map_set(&map, CAPSULE_POSE_FLAT, 0, 500, 866);
    CHECK(capsule_pose_match(&map, 0, 500, 866) == CAPSULE_POSE_UNKNOWN);

    CHECK(capsule_accel_millig(16384) == 999);
    CHECK(capsule_accel_millig(-16384) == -999);
    CHECK(capsule_accel_millig(9) == 1);
    CHECK(capsule_accel_millig(-9) == -1);
    CHECK(capsule_accel_millig(0) == 0);
    CHECK(capsule_accel_millig(8) == 0);
    CHECK(capsule_accel_millig(-8) == 0);
    CHECK(capsule_accel_millig(32767) == 1999);
    CHECK(capsule_accel_millig(-32768) == -1999);

    capsule_pose_map_clear(&map);
    CHECK(capsule_pose_map_set(&map, CAPSULE_POSE_STAND, 0, 0, 1000) == 0);
    CHECK(capsule_pose_map_set(&map, CAPSULE_POSE_STAND, 0, 0, 3000) != 0);
    CHECK(capsule_pose_match(&map, 0, 0, 1000) == CAPSULE_POSE_STAND);
    /* 第二名和直立差 20/1000，小于间隔 80，不能当成直立。 */
    CHECK(capsule_pose_map_set(&map, CAPSULE_POSE_FLAT, 0, 200, 980) == 0);
    CHECK(capsule_pose_match(&map, 0, 0, 1000) == CAPSULE_POSE_UNKNOWN);
}

static void test_pose_map_bytes(void)
{
    capsule_pose_map_t map;
    capsule_pose_map_t back;
    uint8_t blob[CAPSULE_POSE_MAP_BYTES];
    uint8_t short_blob[4] = {1, 2, 3, 4};
    capsule_pose_map_clear(&map);
    capsule_pose_map_set(&map, CAPSULE_POSE_STAND, 0, 0, 1000);
    capsule_pose_map_set(&map, CAPSULE_POSE_SIDE_RIGHT, -1000, 0, 0);
    CHECK(capsule_pose_map_export(&map, blob, sizeof blob) == CAPSULE_POSE_MAP_BYTES);
    CHECK(capsule_pose_map_export(&map, blob, 4) == 0);
    CHECK(capsule_pose_map_set(&back, CAPSULE_POSE_FLAT, 0, 1000, 0) == 0);
    CHECK(capsule_pose_map_import(&back, short_blob, sizeof short_blob) != 0);
    CHECK(back.ref[CAPSULE_POSE_FLAT - 1].set);
    {
        uint8_t bad[CAPSULE_POSE_MAP_BYTES];
        memset(bad, 0, sizeof bad);
        bad[0] = 'X';
        CHECK(capsule_pose_map_import(&back, bad, sizeof bad) != 0);
        CHECK(back.ref[CAPSULE_POSE_FLAT - 1].set);
    }
    {
        uint8_t dirty[CAPSULE_POSE_MAP_BYTES];
        memcpy(dirty, blob, sizeof dirty);
        memset(dirty + 4, 0, 12);
        dirty[4] = 1;
        dirty[8] = 2;
        dirty[12] = 3;
        CHECK(capsule_pose_map_import(&back, dirty, sizeof dirty) != 0);
        CHECK(back.ref[CAPSULE_POSE_FLAT - 1].set);
    }
    CHECK(capsule_pose_map_import(&back, blob, sizeof blob) == 0);
    CHECK(capsule_pose_match(&back, 0, 0, 1000) == CAPSULE_POSE_STAND);
    CHECK(capsule_pose_match(&back, -1000, 0, 0) == CAPSULE_POSE_SIDE_RIGHT);
    CHECK(!back.ref[CAPSULE_POSE_FLAT - 1].set);
}

static void test_capture(void)
{
    static int16_t memo[8];
    static uint8_t wav[64];
    static uint8_t raw[80];
    capsule_t box;
    CHECK(capsule_init(&box, memo, 0, wav, sizeof wav) != 0);
    CHECK(capsule_init(&box, memo, 8, wav, 43) != 0);
    CHECK(capsule_init(&box, memo, 8, raw + 1, 64) != 0);
    CHECK(capsule_init(&box, (int16_t *)(raw + 1), 8, wav, sizeof wav) != 0);
    CHECK(capsule_init(&box, memo, 8, wav, sizeof wav) == 0);
    CHECK(capsule_capture(&box, CAPSULE_POSE_STAND) != 0);
    CHECK(!box.map.ref[CAPSULE_POSE_STAND - 1].set);
    capsule_on_accel(&box, 0, 0, 100, 0);
    CHECK(capsule_capture(&box, CAPSULE_POSE_STAND) != 0);
    CHECK(!box.map.ref[CAPSULE_POSE_STAND - 1].set);
    capsule_on_accel(&box, 0, 0, 1000, 100);
    CHECK(capsule_capture(&box, CAPSULE_POSE_UNKNOWN) != 0);
    CHECK(capsule_capture(&box, CAPSULE_POSE_STAND) == 0);
    CHECK(capsule_pose_match(&box.map, 0, 0, 1000) == CAPSULE_POSE_STAND);
    capsule_on_accel(&box, 0, 0, 3000, 200);
    CHECK(capsule_capture(&box, CAPSULE_POSE_STAND) != 0);
    CHECK(capsule_pose_match(&box.map, 0, 0, 1000) == CAPSULE_POSE_STAND);
}

static void test_wav(void)
{
    int16_t pcm[4] = {1, -2, 3, -4};
    uint8_t file[128];
    size_t len = 0;
    const int16_t *out = NULL;
    size_t samples = 0;
    CHECK(capsule_wav_write(file, sizeof file, pcm, 0, &len) != 0);
    CHECK(capsule_wav_write(file, sizeof file, NULL, 4, &len) != 0);
    CHECK(capsule_wav_write(file, sizeof file, pcm, 4, &len) == 0);
    CHECK(len == 52);
    CHECK(capsule_wav_read(file, len, &out, &samples) == 0);
    CHECK(samples == 4);
    CHECK(out[0] == 1 && out[3] == -4);

    file[22] = 2;
    CHECK(capsule_wav_read(file, len, &out, &samples) != 0);
    CHECK(capsule_wav_write(file, sizeof file, pcm, 4, &len) == 0);
    file[24] = 0x40;
    file[25] = 0x1f; /* 16000 改成 8000 */
    CHECK(capsule_wav_read(file, len, &out, &samples) != 0);
    CHECK(capsule_wav_read(file, 10, &out, &samples) != 0);

    {
        uint8_t storage[80];
        CHECK(capsule_wav_write(storage, sizeof storage, pcm, 4, &len) == 0);
        memmove(storage + 1, storage, len);
        CHECK(capsule_wav_read(storage + 1, len, &out, &samples) != 0);
        CHECK(capsule_wav_write(storage + 1, sizeof storage - 1, pcm, 4, &len) != 0);
    }
}


static void test_timer_and_macro(void)
{
    static int16_t memo[64];
    static uint8_t wav[256];
    capsule_t box;
    uint32_t now = 0;
    CHECK(capsule_init(&box, memo, 64, wav, sizeof wav) == 0);
    CHECK(capsule_init(NULL, memo, 64, wav, sizeof wav) != 0);
    standard_refs(&box);
    /* 姿态自动运行只属于 POMO 子模式；TIME 仍是产品默认，不能自动启动计时。 */
    CHECK(capsule_set_stand_mode(&box, CAPSULE_STAND_POMO, 0) == 0);

    settle(&box, 0, 0, 1000, &now, 100);
    CHECK(capsule_pose(&box) == CAPSULE_POSE_UNKNOWN);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_IDLE);

    settle(&box, 0, 0, 1000, &now, 400);
    CHECK(capsule_pose(&box) == CAPSULE_POSE_STAND);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_RUNNING);
    CHECK(capsule_show_seconds(&box) == 25u * 60u);
    CHECK(capsule_desk(&box));

    capsule_on_time(&box, now + 1000);
    CHECK(capsule_show_seconds(&box) == 25u * 60u - 1u);

    /* 不到 400 ms 的晃动不改变直立，计时继续。 */
    capsule_on_accel(&box, 1000, 0, 0, now + 1100);
    capsule_on_accel(&box, 1000, 0, 0, now + 1200);
    capsule_on_accel(&box, 0, 0, 1000, now + 1300);
    CHECK(capsule_pose(&box) == CAPSULE_POSE_STAND);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_RUNNING);

    now += 1300;
    settle(&box, 0, 0, -1000, &now, 400);
    CHECK(capsule_pose(&box) == CAPSULE_POSE_FACE_DOWN);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_PAUSED);
    CHECK(capsule_muted(&box));
    CHECK(capsule_show_seconds(&box) < 25u * 60u);
    settle(&box, 0, 1000, 0, &now, 400);
    CHECK(capsule_pose(&box) == CAPSULE_POSE_FLAT);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_PAUSED);
    CHECK(!capsule_muted(&box));

    {
        uint32_t paused = capsule_show_seconds(&box);
        settle(&box, 0, 0, 1000, &now, 400);
        CHECK(capsule_timer_state(&box) == CAPSULE_POMO_RUNNING);
        CHECK(!capsule_muted(&box));
        capsule_on_time(&box, now + 1000);
        now += 1000;
        CHECK(capsule_show_seconds(&box) == paused - 1u);
    }

    {
        uint32_t started = box.running_since_ms;
        capsule_on_time(&box, 0);
        CHECK(capsule_timer_state(&box) == CAPSULE_POMO_RUNNING);
        capsule_on_time(&box, started + CAPSULE_POMO_MS);
        CHECK(capsule_timer_state(&box) == CAPSULE_POMO_DONE);
        CHECK(capsule_show_seconds(&box) == 0);
        CHECK(capsule_take_beep(&box));
        capsule_on_time(&box, started + CAPSULE_POMO_MS + 5000);
        CHECK(capsule_timer_state(&box) == CAPSULE_POMO_DONE);
    }

    settle(&box, 0, 1000, 0, &now, 400);
    CHECK(capsule_pose(&box) == CAPSULE_POSE_FLAT);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_DONE);
    settle(&box, 0, 0, 1000, &now, 400);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_RUNNING);
    CHECK(capsule_show_seconds(&box) == 25u * 60u);
}

static void test_timer_minutes_presets(void)
{
    static int16_t memo[16];
    static uint8_t wav[64];
    capsule_t box;
    CHECK(capsule_init(&box, memo, 16, wav, sizeof wav) == 0);
    CHECK(capsule_timer_minutes(&box) == CAPSULE_POMO_MS / 60000u);
    CHECK(capsule_show_seconds(&box) == CAPSULE_POMO_MS / 1000u);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_IDLE);

    CHECK(capsule_set_timer_minutes(&box, 24, 0) == 0);
    CHECK(capsule_timer_minutes(&box) == 24);
    CHECK(capsule_show_seconds(&box) == 24u * 60u);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_IDLE);
    CHECK(capsule_set_timer_minutes(&box, 26, 0) == 0);
    CHECK(capsule_timer_minutes(&box) == 26);
    CHECK(capsule_show_seconds(&box) == 26u * 60u);

    /* 范围之外的值一律拒绝，并且不改动已选分钟数。 */
    CHECK(capsule_set_timer_minutes(&box, 0, 0) != 0);
    CHECK(capsule_set_timer_minutes(&box, 121, 0) != 0);
    CHECK(capsule_timer_minutes(&box) == 26);
    CHECK(capsule_show_seconds(&box) == 26u * 60u);

    CHECK(capsule_set_timer_minutes(&box, CAPSULE_TIMER_MIN_MINUTES, 0) == 0);
    CHECK(capsule_timer_minutes(&box) == 1);
    CHECK(capsule_show_seconds(&box) == 60u);
    CHECK(capsule_set_timer_minutes(&box, CAPSULE_TIMER_MAX_MINUTES, 0) == 0);
    CHECK(capsule_timer_minutes(&box) == 120);
    CHECK(capsule_show_seconds(&box) == 120u * 60u);
    CHECK(capsule_set_timer_minutes(NULL, 25, 0) != 0);
}

static void test_timer_minutes_independent_modes(void)
{
    static int16_t memo[16];
    static uint8_t wav[64];
    capsule_t box;
    uint32_t now = 0;
    CHECK(capsule_init(&box, memo, 16, wav, sizeof wav) == 0);
    standard_refs(&box);
    /* 姿态自动运行只属于 POMO 子模式；TIME 仍是产品默认，不能自动启动计时。 */
    CHECK(capsule_set_stand_mode(&box, CAPSULE_STAND_POMO, 0) == 0);

    CHECK(capsule_set_timer_minutes(&box, 24, 0) == 0);
    CHECK(capsule_set_timer_mode(&box, CAPSULE_TIMER_COUNTDOWN, 0) == 0);
    CHECK(capsule_timer_mode(&box) == CAPSULE_TIMER_COUNTDOWN);
    CHECK(capsule_timer_minutes(&box) == 25);
    CHECK(capsule_show_seconds(&box) == 25u * 60u);
    CHECK(capsule_set_timer_minutes(&box, 26, 0) == 0);
    CHECK(capsule_timer_minutes(&box) == 26);

    /* 两个模式的预设各自保存。 */
    CHECK(capsule_set_timer_mode(&box, CAPSULE_TIMER_POMODORO, 0) == 0);
    CHECK(capsule_timer_minutes(&box) == 24);
    CHECK(capsule_show_seconds(&box) == 24u * 60u);
    CHECK(capsule_set_timer_mode(&box, CAPSULE_TIMER_COUNTDOWN, 0) == 0);
    CHECK(capsule_timer_minutes(&box) == 26);
    CHECK(capsule_show_seconds(&box) == 26u * 60u);

    /* 在一个模式里跑掉的时间不影响另一个模式的剩余值。 */
    CHECK(capsule_set_timer_mode(&box, CAPSULE_TIMER_POMODORO, now) == 0);
    settle(&box, 0, 0, 1000, &now, 400);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_RUNNING);
    capsule_on_time(&box, now + 60u * 1000u);
    now += 60u * 1000u;
    CHECK(capsule_show_seconds(&box) == 24u * 60u - 60u);
    CHECK(capsule_set_timer_mode(&box, CAPSULE_TIMER_COUNTDOWN, now) == 0);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_RUNNING);
    CHECK(capsule_show_seconds(&box) == 26u * 60u);
    CHECK(capsule_set_timer_mode(&box, CAPSULE_TIMER_POMODORO, now) == 0);
    CHECK(capsule_show_seconds(&box) == 24u * 60u - 60u);
}

static void test_timer_minutes_running_delta(void)
{
    static int16_t memo[16];
    static uint8_t wav[64];
    capsule_t box;
    uint32_t now = 0;
    CHECK(capsule_init(&box, memo, 16, wav, sizeof wav) == 0);
    standard_refs(&box);
    /* 姿态自动运行只属于 POMO 子模式；TIME 仍是产品默认，不能自动启动计时。 */
    CHECK(capsule_set_stand_mode(&box, CAPSULE_STAND_POMO, 0) == 0);
    CHECK(capsule_set_timer_minutes(&box, 24, 0) == 0);
    settle(&box, 0, 0, 1000, &now, 400);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_RUNNING);
    CHECK(capsule_timer_minutes(&box) == 24);
    CHECK(capsule_show_seconds(&box) == 24u * 60u);

    /* 运行中调高两分钟：剩余时间立刻多两分钟。 */
    CHECK(capsule_set_timer_minutes(&box, 26, now) == 0);
    CHECK(capsule_timer_minutes(&box) == 26);
    CHECK(capsule_show_seconds(&box) == 26u * 60u);
    capsule_on_time(&box, now + 60u * 1000u);
    now += 60u * 1000u;
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_RUNNING);
    CHECK(capsule_show_seconds(&box) == 25u * 60u);

    /* 运行中调低两分钟：先结算已走时间，再扣掉两分钟差值。 */
    CHECK(capsule_set_timer_minutes(&box, 24, now) == 0);
    CHECK(capsule_timer_minutes(&box) == 24);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_RUNNING);
    CHECK(capsule_show_seconds(&box) == 23u * 60u);
    capsule_on_time(&box, now + 60u * 1000u);
    now += 60u * 1000u;
    CHECK(capsule_show_seconds(&box) == 22u * 60u);

    /* 倒计时模式同样是按差值结算。 */
    CHECK(capsule_init(&box, memo, 16, wav, sizeof wav) == 0);
    standard_refs(&box);
    /* 姿态自动运行只属于 POMO 子模式；TIME 仍是产品默认，不能自动启动计时。 */
    CHECK(capsule_set_stand_mode(&box, CAPSULE_STAND_POMO, 0) == 0);
    now = 0;
    CHECK(capsule_set_timer_mode(&box, CAPSULE_TIMER_COUNTDOWN, 0) == 0);
    CHECK(capsule_set_timer_minutes(&box, 26, 0) == 0);
    settle(&box, 0, 0, 1000, &now, 400);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_RUNNING);
    CHECK(capsule_show_seconds(&box) == 26u * 60u);
    CHECK(capsule_set_timer_minutes(&box, 25, now) == 0);
    CHECK(capsule_timer_minutes(&box) == 25);
    CHECK(capsule_show_seconds(&box) == 25u * 60u);
    capsule_on_time(&box, now + 60u * 1000u);
    now += 60u * 1000u;
    CHECK(capsule_show_seconds(&box) == 24u * 60u);

    /* 调低到不足剩余时间时直接结束，并只补一声提示音。 */
    CHECK(capsule_init(&box, memo, 16, wav, sizeof wav) == 0);
    standard_refs(&box);
    /* 姿态自动运行只属于 POMO 子模式；TIME 仍是产品默认，不能自动启动计时。 */
    CHECK(capsule_set_stand_mode(&box, CAPSULE_STAND_POMO, 0) == 0);
    now = 0;
    CHECK(capsule_set_timer_mode(&box, CAPSULE_TIMER_COUNTDOWN, 0) == 0);
    CHECK(capsule_set_timer_minutes(&box, 5, 0) == 0);
    settle(&box, 0, 0, 1000, &now, 400);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_RUNNING);
    capsule_on_time(&box, now + 5u * 60u * 1000u - 30u * 1000u);
    now += 5u * 60u * 1000u - 30u * 1000u;
    CHECK(capsule_show_seconds(&box) == 30u);
    CHECK(capsule_set_timer_minutes(&box, 4, now) == 0);
    CHECK(capsule_timer_minutes(&box) == 4);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_DONE);
    CHECK(capsule_show_seconds(&box) == 0);
    CHECK(capsule_take_beep(&box));
    CHECK(!capsule_take_beep(&box));

    /* 结束后再设分钟只改预设，已结束的状态保持到下一次姿态切换。 */
    CHECK(capsule_set_timer_minutes(&box, 24, now) == 0);
    CHECK(capsule_timer_minutes(&box) == 24);
    CHECK(box.remaining_ms[CAPSULE_TIMER_COUNTDOWN] == 24u * 60u * 1000u);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_DONE);
    CHECK(capsule_show_seconds(&box) == 0);
}

static void test_timer_minutes_expiry_beep(void)
{
    static int16_t memo[32];
    static uint8_t wav[128];
    capsule_t box;
    uint32_t now;
    uint32_t end;
    uint32_t t;

    /* 番茄钟用新预设自然到点：一声。 */
    CHECK(capsule_init(&box, memo, 32, wav, sizeof wav) == 0);
    standard_refs(&box);
    /* 姿态自动运行只属于 POMO 子模式；TIME 仍是产品默认，不能自动启动计时。 */
    CHECK(capsule_set_stand_mode(&box, CAPSULE_STAND_POMO, 0) == 0);
    now = 0;
    CHECK(capsule_set_timer_minutes(&box, 24, 0) == 0);
    settle(&box, 0, 0, 1000, &now, 400);
    end = box.running_since_ms + 24u * 60u * 1000u;
    capsule_on_time(&box, end - 1u);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_RUNNING);
    CHECK(!capsule_take_beep(&box));
    capsule_on_time(&box, end);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_DONE);
    CHECK(capsule_show_seconds(&box) == 0);
    CHECK(capsule_take_beep(&box));
    CHECK(!capsule_take_beep(&box));

    /* 倒计时模式自然到点：一声。 */
    CHECK(capsule_init(&box, memo, 32, wav, sizeof wav) == 0);
    standard_refs(&box);
    /* 姿态自动运行只属于 POMO 子模式；TIME 仍是产品默认，不能自动启动计时。 */
    CHECK(capsule_set_stand_mode(&box, CAPSULE_STAND_POMO, 0) == 0);
    now = 0;
    CHECK(capsule_set_timer_mode(&box, CAPSULE_TIMER_COUNTDOWN, 0) == 0);
    CHECK(capsule_set_timer_minutes(&box, 5, 0) == 0);
    settle(&box, 0, 0, 1000, &now, 400);
    end = box.running_since_ms + 5u * 60u * 1000u;
    capsule_on_time(&box, end);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_DONE);
    CHECK(capsule_take_beep(&box));
    CHECK(!capsule_take_beep(&box));

    /* 关掉提示音时番茄钟到点静音。 */
    CHECK(capsule_init(&box, memo, 32, wav, sizeof wav) == 0);
    standard_refs(&box);
    /* 姿态自动运行只属于 POMO 子模式；TIME 仍是产品默认，不能自动启动计时。 */
    CHECK(capsule_set_stand_mode(&box, CAPSULE_STAND_POMO, 0) == 0);
    now = 0;
    capsule_set_beep_enabled(&box, false);
    CHECK(!capsule_beep_enabled(&box));
    settle(&box, 0, 0, 1000, &now, 400);
    capsule_on_time(&box, box.running_since_ms + CAPSULE_POMO_MS);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_DONE);
    CHECK(!capsule_take_beep(&box));

    /* 关掉提示音时倒计时到点静音，事后重新打开不会补响。 */
    CHECK(capsule_init(&box, memo, 32, wav, sizeof wav) == 0);
    standard_refs(&box);
    /* 姿态自动运行只属于 POMO 子模式；TIME 仍是产品默认，不能自动启动计时。 */
    CHECK(capsule_set_stand_mode(&box, CAPSULE_STAND_POMO, 0) == 0);
    now = 0;
    CHECK(capsule_set_timer_mode(&box, CAPSULE_TIMER_COUNTDOWN, 0) == 0);
    CHECK(capsule_set_timer_minutes(&box, 5, 0) == 0);
    capsule_set_beep_enabled(&box, false);
    settle(&box, 0, 0, 1000, &now, 400);
    end = box.running_since_ms + 5u * 60u * 1000u;
    capsule_on_time(&box, end);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_DONE);
    CHECK(!capsule_take_beep(&box));
    capsule_set_beep_enabled(&box, true);
    CHECK(capsule_beep_enabled(&box));
    CHECK(!capsule_take_beep(&box));

    /* 倒计时模式面朝下到点：提示音被吞掉，翻回来也不再响。 */
    CHECK(capsule_init(&box, memo, 32, wav, sizeof wav) == 0);
    standard_refs(&box);
    /* 姿态自动运行只属于 POMO 子模式；TIME 仍是产品默认，不能自动启动计时。 */
    CHECK(capsule_set_stand_mode(&box, CAPSULE_STAND_POMO, 0) == 0);
    now = 0;
    CHECK(capsule_set_timer_mode(&box, CAPSULE_TIMER_COUNTDOWN, 0) == 0);
    CHECK(capsule_set_timer_minutes(&box, 5, 0) == 0);
    settle(&box, 0, 0, 1000, &now, 400);
    end = box.running_since_ms + 5u * 60u * 1000u;
    for (t = end - CAPSULE_POSE_STABLE_MS; t < end; t += 100) capsule_on_accel(&box, 0, 0, -1000, t);
    CHECK(capsule_pose(&box) == CAPSULE_POSE_STAND);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_RUNNING);
    capsule_on_accel(&box, 0, 0, -1000, end);
    CHECK(capsule_pose(&box) == CAPSULE_POSE_FACE_DOWN);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_DONE);
    CHECK(capsule_muted(&box));
    CHECK(!capsule_take_beep(&box));
    now = end;
    settle(&box, 0, 1000, 0, &now, 400);
    CHECK(!capsule_muted(&box));
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_DONE);
    CHECK(!capsule_take_beep(&box));
}

static void test_beep_swallowed_when_face_down(void)
{
    static int16_t memo[32];
    static uint8_t wav[128];
    capsule_t box;
    uint32_t now = 0;
    CHECK(capsule_init(&box, memo, 32, wav, sizeof wav) == 0);
    standard_refs(&box);
    /* 姿态自动运行只属于 POMO 子模式；TIME 仍是产品默认，不能自动启动计时。 */
    CHECK(capsule_set_stand_mode(&box, CAPSULE_STAND_POMO, 0) == 0);
    settle(&box, 0, 0, 1000, &now, 400);
    capsule_on_time(&box, box.running_since_ms + CAPSULE_POMO_MS);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_DONE);
    settle(&box, 0, 0, -1000, &now, 400);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_DONE);
    CHECK(capsule_muted(&box));
    CHECK(!capsule_take_beep(&box));

    settle(&box, 700, 700, 0, &now, 400);
    CHECK(capsule_pose(&box) == CAPSULE_POSE_UNKNOWN);
    CHECK(!capsule_muted(&box));
}

static void test_macro_and_pocket(void)
{
    static int16_t memo[800];
    static uint8_t wav[2048];
    capsule_t box;
    uint32_t now = 0;
    CHECK(capsule_init(&box, memo, 800, wav, sizeof wav) == 0);
    standard_refs(&box);
    /* 姿态自动运行只属于 POMO 子模式；TIME 仍是产品默认，不能自动启动计时。 */
    CHECK(capsule_set_stand_mode(&box, CAPSULE_STAND_POMO, 0) == 0);
    settle(&box, 0, 0, 1000, &now, 400);
    settle(&box, 1000, 0, 0, &now, 400);
    CHECK(capsule_pose(&box) == CAPSULE_POSE_SIDE_LEFT);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_PAUSED);
    CHECK(!capsule_muted(&box));
    CHECK(capsule_desk(&box));

    capsule_on_key_down(&box, now);
    capsule_on_key_down(&box, now + 10);
    capsule_on_key_up(&box, now + 100);
    CHECK(capsule_last_macro(&box) == CAPSULE_MACRO_VOLUME_UP);
    CHECK(capsule_macro_seq(&box) == 1);
    CHECK(!capsule_recording(&box));

    capsule_on_key_down(&box, now + 200);
    capsule_on_key_up(&box, now + 200 + CAPSULE_KEY_LONG_MS);
    CHECK(capsule_last_macro(&box) == CAPSULE_MACRO_SYSTEM_MUTE);
    CHECK(capsule_macro_seq(&box) == 2);

    settle(&box, -1000, 0, 0, &now, 400);
    capsule_on_key_down(&box, now);
    capsule_on_key_up(&box, now + CAPSULE_KEY_LONG_MS - 1);
    CHECK(capsule_last_macro(&box) == CAPSULE_MACRO_NEXT);

    /* 离开桌面超过 3 秒后再侧放，按键回到录音。 */
    {
        uint32_t at = now;
        settle(&box, 700, 700, 0, &now, 400);
        CHECK(capsule_pose(&box) == CAPSULE_POSE_UNKNOWN);
        capsule_on_time(&box, at + CAPSULE_DESK_LEAVE_MS + 500);
        CHECK(!capsule_desk(&box));
        now = at + CAPSULE_DESK_LEAVE_MS + 500;
    }
    settle(&box, 1000, 0, 0, &now, 400);
    CHECK(capsule_pose(&box) == CAPSULE_POSE_SIDE_LEFT);
    CHECK(!capsule_desk(&box));
    capsule_on_key_down(&box, now);
    CHECK(capsule_recording(&box));
    capsule_on_key_up(&box, now + 50);
    CHECK(capsule_recording(&box));
    capsule_on_key_down(&box, now + 80);
    CHECK(capsule_memo_status(&box) == CAPSULE_MEMO_EMPTY);
}

static void test_record(void)
{
    enum { frames = 15, frame = 320, total = frames * frame };
    static int16_t memo[total];
    static int16_t pcm[total];
    static uint8_t wav[44 + total * 2];
    capsule_t box;
    uint32_t now = 0;
    const int16_t *decoded = NULL;
    size_t samples = 0;
    CHECK(capsule_init(&box, memo, total, wav, sizeof wav) == 0);
    standard_refs(&box);
    /* 姿态自动运行只属于 POMO 子模式；TIME 仍是产品默认，不能自动启动计时。 */
    CHECK(capsule_set_stand_mode(&box, CAPSULE_STAND_POMO, 0) == 0);
    settle(&box, 0, 1000, 0, &now, 400);
    CHECK(capsule_pose(&box) == CAPSULE_POSE_FLAT);

    memset(pcm, 0, sizeof pcm);
    fill_wave(pcm + 5 * frame, 5 * frame);
    capsule_on_key_down(&box, now);
    CHECK(capsule_recording(&box));
    CHECK(capsule_on_pcm(&box, pcm, total) == 0);
    capsule_on_key_up(&box, now + 100);
    CHECK(capsule_recording(&box));
    capsule_on_key_down(&box, now + 200);
    capsule_on_key_up(&box, now + 250);
    CHECK(capsule_memo_status(&box) == CAPSULE_MEMO_SAVED);
    CHECK(!capsule_memo_truncated(&box));
    CHECK(capsule_wav_read(capsule_wav_data(&box), capsule_wav_length(&box), &decoded, &samples) == 0);
    CHECK(samples == total);
    CHECK(decoded[0] == 0);
    CHECK(decoded[5 * frame] == 3000);

    CHECK(capsule_wav_length(&box) > 0);
    memset(pcm, 0, sizeof pcm);
    capsule_on_key_down(&box, now + 300);
    CHECK(capsule_wav_length(&box) == 0);
    CHECK(capsule_recording(&box));
    CHECK(capsule_on_pcm(&box, pcm, total) == 0);
    capsule_on_key_up(&box, now + 350);
    capsule_on_key_down(&box, now + 400);
    CHECK(capsule_memo_status(&box) == CAPSULE_MEMO_SAVED);
    CHECK(capsule_wav_read(capsule_wav_data(&box), capsule_wav_length(&box), &decoded, &samples) == 0);
    CHECK(samples == total);
}

static void test_memo_clear(void)
{
    static int16_t memo[320];
    static int16_t pcm[320];
    static uint8_t wav[44 + 320 * 2];
    capsule_t box;
    const int16_t *decoded = NULL;
    size_t samples = 0;

    CHECK(capsule_memo_clear(NULL) == -1);

    CHECK(capsule_init(&box, memo, 320, wav, sizeof wav) == 0);
    fill_wave(pcm, 320);

    /* 录音进行中禁止清空 */
    CHECK(capsule_record_start(&box) == 0);
    CHECK(capsule_recording(&box));
    CHECK(capsule_memo_clear(&box) == -1);
    CHECK(capsule_recording(&box));

    /* 录音完成且保存后，清空成功，WAV 和状态重置 */
    CHECK(capsule_on_pcm(&box, pcm, 320) == 0);
    CHECK(capsule_record_stop(&box) == 0);
    CHECK(capsule_memo_status(&box) == CAPSULE_MEMO_SAVED);
    CHECK(capsule_wav_length(&box) == 44 + 320 * 2);
    CHECK(capsule_wav_read(capsule_wav_data(&box), capsule_wav_length(&box), &decoded, &samples) == 0);

    CHECK(capsule_memo_clear(&box) == 0);
    CHECK(capsule_wav_length(&box) == 0);
    CHECK(capsule_memo_status(&box) == CAPSULE_MEMO_IDLE);
    CHECK(!capsule_memo_truncated(&box));
    CHECK(!capsule_recording(&box));
    CHECK(capsule_wav_read(capsule_wav_data(&box), capsule_wav_length(&box), &decoded, &samples) != 0);

    /* 截断录音后清空，截断标志与状态亦完全重置 */
    CHECK(capsule_record_start(&box) == 0);
    CHECK(capsule_on_pcm(&box, pcm, 320) == 0);
    CHECK(capsule_on_pcm(&box, pcm, 10) == 0);
    CHECK(capsule_memo_truncated(&box));
    CHECK(capsule_memo_status(&box) == CAPSULE_MEMO_NOSPACE);
    CHECK(capsule_memo_clear(&box) == 0);
    CHECK(capsule_wav_length(&box) == 0);
    CHECK(capsule_memo_status(&box) == CAPSULE_MEMO_IDLE);
    CHECK(!capsule_memo_truncated(&box));

    /* 清空后可重新开始正常录音 */
    CHECK(capsule_record_start(&box) == 0);
    CHECK(capsule_on_pcm(&box, pcm, 160) == 0);
    CHECK(capsule_record_stop(&box) == 0);
    CHECK(capsule_memo_status(&box) == CAPSULE_MEMO_SAVED);
    CHECK(capsule_wav_length(&box) == 44 + 160 * 2);
}

static void test_truncate_and_nospace(void)
{
    static int16_t memo[64];
    static int16_t pcm[200];
    static uint8_t wav[4096];
    static uint8_t tiny[44];
    capsule_t box;
    fill_wave(pcm, sizeof pcm / sizeof pcm[0]);
    CHECK(capsule_init(&box, memo, 64, wav, sizeof wav) == 0);
    capsule_on_key_down(&box, 0);
    CHECK(capsule_on_pcm(&box, pcm, 200) == 0);
    CHECK(capsule_memo_truncated(&box));
    CHECK(!capsule_recording(&box));
    CHECK(capsule_memo_status(&box) == CAPSULE_MEMO_NOSPACE);

    CHECK(capsule_init(&box, memo, 64, tiny, sizeof tiny) == 0);
    capsule_on_key_down(&box, 0);
    CHECK(capsule_on_pcm(&box, pcm, 64) == 0);
    capsule_on_key_up(&box, 5);
    capsule_on_key_down(&box, 10);
    CHECK(capsule_memo_status(&box) == CAPSULE_MEMO_NOSPACE);

}

static void test_slow_and_backward(void)
{
    static int16_t memo[8];
    static uint8_t wav[64];
    capsule_t box;
    CHECK(capsule_init(&box, memo, 8, wav, sizeof wav) == 0);
    standard_refs(&box);
    /* 姿态自动运行只属于 POMO 子模式；TIME 仍是产品默认，不能自动启动计时。 */
    CHECK(capsule_set_stand_mode(&box, CAPSULE_STAND_POMO, 0) == 0);
    capsule_on_accel(&box, 0, 0, 1000, 0);
    capsule_on_accel(&box, 0, 0, 1000, 500);
    CHECK(capsule_pose(&box) == CAPSULE_POSE_UNKNOWN);
    capsule_on_accel(&box, 0, 0, 1000, 900);
    CHECK(capsule_pose(&box) == CAPSULE_POSE_STAND);
    capsule_on_accel(&box, 5, 6, -1000, 800);
    CHECK(box.ax_mg == 0);
    CHECK(box.az_mg == 1000);
    CHECK(capsule_pose(&box) == CAPSULE_POSE_STAND);
    capsule_on_accel(&box, 0, 0, 1000, 1000);
    CHECK(capsule_pose(&box) == CAPSULE_POSE_STAND);
}

static void test_beep_edges(void)
{
    static int16_t memo[32];
    static uint8_t wav[64];
    capsule_t box;
    uint32_t now = 0;
    uint32_t end;
    uint32_t t;
    CHECK(capsule_init(&box, memo, 32, wav, sizeof wav) == 0);
    standard_refs(&box);
    /* 姿态自动运行只属于 POMO 子模式；TIME 仍是产品默认，不能自动启动计时。 */
    CHECK(capsule_set_stand_mode(&box, CAPSULE_STAND_POMO, 0) == 0);
    settle(&box, 0, 0, 1000, &now, 400);
    end = box.running_since_ms + CAPSULE_POMO_MS;
    for (t = end - CAPSULE_POSE_STABLE_MS; t < end; t += 100) capsule_on_accel(&box, 0, 0, -1000, t);
    CHECK(capsule_pose(&box) == CAPSULE_POSE_STAND);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_RUNNING);
    capsule_on_accel(&box, 0, 0, -1000, end);
    CHECK(capsule_pose(&box) == CAPSULE_POSE_FACE_DOWN);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_DONE);
    CHECK(capsule_muted(&box));
    CHECK(!capsule_take_beep(&box));

    CHECK(capsule_init(&box, memo, 32, wav, sizeof wav) == 0);
    standard_refs(&box);
    /* 姿态自动运行只属于 POMO 子模式；TIME 仍是产品默认，不能自动启动计时。 */
    CHECK(capsule_set_stand_mode(&box, CAPSULE_STAND_POMO, 0) == 0);
    now = 0;
    settle(&box, 0, 0, 1000, &now, 400);
    capsule_on_time(&box, box.running_since_ms + CAPSULE_POMO_MS);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_DONE);
    settle(&box, 700, 700, 0, &now, 400);
    CHECK(capsule_pose(&box) == CAPSULE_POSE_UNKNOWN);
    CHECK(!capsule_muted(&box));
    CHECK(capsule_take_beep(&box));
}


/*
 * 直立三模式中心的状态机约定：默认 TIME 绝不自动启动计时；只有 POMO 子模式
 * 在 STAND 姿态下自动运行/恢复；手动启停、复位和到期归零都与卡片消费者一致。
 */
static void test_stand_hub_pose_gate(void)
{
    static int16_t memo[64];
    static uint8_t wav[256];
    capsule_t box;
    uint32_t now = 0;
    uint32_t paused;
    CHECK(capsule_init(&box, memo, 64, wav, sizeof wav) == 0);
    standard_refs(&box);

    /* 默认 TIME：直立也不自动启动，长按时长过去仍然 IDLE。 */
    CHECK(capsule_stand_mode(&box) == CAPSULE_STAND_TIME);
    settle(&box, 0, 0, 1000, &now, 400);
    CHECK(capsule_pose(&box) == CAPSULE_POSE_STAND);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_IDLE);
    capsule_on_time(&box, now + CAPSULE_POMO_MS);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_IDLE);
    CHECK(!capsule_take_beep(&box));
    CHECK(capsule_timer_toggle(&box, now) == -1);

    /* 只有 POMO 子模式的启停键能开始计时。 */
    CHECK(capsule_set_stand_mode(&box, CAPSULE_STAND_POMO, now) == 0);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_IDLE);
    CHECK(capsule_timer_toggle(&box, now) == 0);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_RUNNING);
    now += 1234;
    capsule_on_time(&box, now);
    CHECK(capsule_show_seconds(&box) == CAPSULE_POMO_MS / 1000 - 1);

    CHECK(capsule_timer_toggle(&box, now) == 0);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_PAUSED);
    paused = capsule_show_seconds(&box);
    now += 1000;
    capsule_on_time(&box, now);
    CHECK(capsule_show_seconds(&box) == paused);

    /* 离开直立总pause；回到直立且仍在 POMO 时自动恢复。 */
    CHECK(capsule_timer_toggle(&box, now) == 0);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_RUNNING);
    settle(&box, 1000, 0, 0, &now, 400);
    CHECK(capsule_pose(&box) == CAPSULE_POSE_SIDE_LEFT);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_PAUSED);
    CHECK(capsule_show_seconds(&box) == paused);
    settle(&box, 0, 0, 1000, &now, 400);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_RUNNING);

    /* 切到 STOPWATCH 会暂停，之后即使直立也不再自动运行。 */
    CHECK(capsule_set_stand_mode(&box, CAPSULE_STAND_STOPWATCH, now) == 0);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_PAUSED);
    settle(&box, 1000, 0, 0, &now, 400);
    settle(&box, 0, 0, 1000, &now, 400);
    CHECK(capsule_stand_mode(&box) == CAPSULE_STAND_STOPWATCH);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_PAUSED);

    CHECK(capsule_set_stand_mode(&box, CAPSULE_STAND_TIME, now) == 0);
    settle(&box, 0, 0, -1000, &now, 400);
    CHECK(capsule_muted(&box));
    settle(&box, 0, 0, 1000, &now, 400);
    CHECK(!capsule_muted(&box));
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_PAUSED);

    /* 切回 POMO 不是启动命令；只有启停键才继续计时。 */
    CHECK(capsule_set_stand_mode(&box, CAPSULE_STAND_POMO, now) == 0);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_PAUSED);
    CHECK(capsule_timer_toggle(&box, now) == 0);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_RUNNING);
    CHECK(capsule_set_stand_mode(&box, CAPSULE_STAND_TIME, now) == 0);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_PAUSED);

    CHECK(capsule_timer_reset(&box, now) == 0);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_IDLE);
    CHECK(capsule_show_seconds(&box) == CAPSULE_POMO_MS / 1000);
    CHECK(capsule_set_stand_mode(&box, CAPSULE_STAND_MODE_COUNT, now) == -1);
    CHECK(capsule_stand_mode(&box) == CAPSULE_STAND_TIME);
}

/*
 * 到期归零是卡片和固件共用的事实：remaining 归 0 才能画 00:00，再次启动要
 * 回到预设；切模式后已完成的模式仍保持 DONE。
 */
static void test_stand_hub_expiry_zero(void)
{
    static int16_t memo[64];
    static uint8_t wav[256];
    capsule_t box;
    uint32_t now = 0;
    CHECK(capsule_init(&box, memo, 64, wav, sizeof wav) == 0);
    standard_refs(&box);
    CHECK(capsule_set_stand_mode(&box, CAPSULE_STAND_POMO, 0) == 0);

    CHECK(capsule_set_timer_minutes(&box, 120, now) == 0);
    settle(&box, 0, 0, 1000, &now, 400);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_RUNNING);
    CHECK(capsule_show_seconds(&box) == 7200);
    CHECK(capsule_timer_reset(&box, now) == 0);
    CHECK(capsule_show_seconds(&box) == 7200);
    capsule_on_time(&box, now + 1000);
    now += 1000;
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_IDLE);

    CHECK(capsule_set_timer_minutes(&box, 1, now) == 0);
    CHECK(capsule_timer_toggle(&box, now) == 0);
    now += 60000;
    capsule_on_time(&box, now);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_DONE);
    CHECK(capsule_timer_reset(&box, now) == 0);
    CHECK(!capsule_take_beep(&box));
    CHECK(capsule_show_seconds(&box) == 60);
    CHECK(capsule_timer_toggle(&box, now) == 0);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_RUNNING);
    CHECK(capsule_show_seconds(&box) == 60);
    capsule_set_beep_enabled(&box, false);
    capsule_on_time(&box, now + 60000);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_DONE);
    CHECK(!capsule_take_beep(&box));
    CHECK(capsule_show_seconds(&box) == 0);

    CHECK(capsule_timer_toggle(&box, now) == 0);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_RUNNING);
    CHECK(capsule_show_seconds(&box) == 60);
    now += 61000;
    capsule_on_time(&box, now);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_DONE);
    CHECK(capsule_show_seconds(&box) == 0);

    /* 完成的模式切走再切回仍然是 DONE，不会被误当成新预设。 */
    CHECK(capsule_set_timer_mode(&box, CAPSULE_TIMER_COUNTDOWN, now) == 0);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_IDLE);
    CHECK(capsule_set_timer_mode(&box, CAPSULE_TIMER_POMODORO, now) == 0);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_DONE);
    CHECK(capsule_show_seconds(&box) == 0);

    /* 到期必须发布可消费的 0，再次启动重新载入预设。 */
    CHECK(capsule_init(&box, memo, 64, wav, sizeof wav) == 0);
    standard_refs(&box);
    CHECK(capsule_set_stand_mode(&box, CAPSULE_STAND_POMO, 0) == 0);
    CHECK(capsule_set_timer_minutes(&box, 1, 0) == 0);
    settle(&box, 0, 0, 1000, &now, 400);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_RUNNING);
    CHECK(capsule_show_seconds(&box) == 60);
    now += 60000;
    capsule_on_time(&box, now);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_DONE);
    CHECK(capsule_show_seconds(&box) == 0);
    CHECK(capsule_take_beep(&box));
    CHECK(capsule_timer_toggle(&box, now) == 0);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_RUNNING);
    CHECK(capsule_show_seconds(&box) == 60);
    now += 70000;
    capsule_on_time(&box, now);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_DONE);
    CHECK(capsule_show_seconds(&box) == 0);
    CHECK(capsule_take_beep(&box));
    CHECK(capsule_timer_reset(&box, now) == 0);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_IDLE);
    CHECK(capsule_show_seconds(&box) == 60);

    CHECK(capsule_timer_toggle(NULL, 0) == -1);
    CHECK(capsule_timer_reset(NULL, 0) == -1);
    CHECK(capsule_set_stand_mode(NULL, CAPSULE_STAND_TIME, 0) == -1);
}

int main(void)
{
    test_pose_match();
    test_pose_map_bytes();
    test_capture();
    test_wav();
    test_timer_and_macro();
    test_timer_minutes_presets();
    test_timer_minutes_independent_modes();
    test_timer_minutes_running_delta();
    test_timer_minutes_expiry_beep();
    test_beep_swallowed_when_face_down();
    test_beep_edges();
    test_slow_and_backward();
    test_macro_and_pocket();
    test_record();
    test_memo_clear();
    test_truncate_and_nospace();
    test_stand_hub_pose_gate();
    test_stand_hub_expiry_zero();
    CHECK(capsule_selftest() == 0);
    if (failures) {
        fprintf(stderr, "%d failed, %d checks\n", failures, checks);
        return 1;
    }
    printf("ok %d checks\n", checks);
    return 0;
}
