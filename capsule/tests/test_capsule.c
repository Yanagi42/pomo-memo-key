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

static void test_vad(void)
{
    capsule_vad_config_t cfg = capsule_vad_default();
    int16_t pcm[64];
    size_t kept = 99;
    cfg.frame_samples = 4;
    cfg.preroll_frames = 1;
    cfg.hangover_frames = 1;
    cfg.rms_threshold = 800;

    memset(pcm, 0, sizeof pcm);
    CHECK(capsule_vad_trim(&cfg, pcm, 40, &kept) == 0);
    CHECK(kept == 0);

    for (size_t i = 0; i < 40; ++i) pcm[i] = 4000;
    CHECK(capsule_vad_trim(&cfg, pcm, 40, &kept) == 0);
    CHECK(kept == 0);

    memset(pcm, 0, sizeof pcm);
    fill_wave(pcm + 16, 8); /* frames 4 and 5 */
    CHECK(capsule_vad_trim(&cfg, pcm, 40, &kept) == 0);
    CHECK(kept == 16);

    cfg.preroll_frames = 0;
    cfg.hangover_frames = 0;
    memset(pcm, 0, sizeof pcm);
    fill_wave(pcm, 4);
    fill_wave(pcm + 28, 4); /* frame 7 */
    CHECK(capsule_vad_trim(&cfg, pcm, 32, &kept) == 0);
    CHECK(kept == 8);
    CHECK(pcm[0] == 3000 && pcm[4] == 3000);

    cfg.hangover_frames = 1;
    memset(pcm, 0, sizeof pcm);
    fill_wave(pcm, 4);
    fill_wave(pcm + 8, 4);
    CHECK(capsule_vad_trim(&cfg, pcm, 12, &kept) == 0);
    CHECK(kept == 12);

    cfg.frame_samples = 4;
    cfg.preroll_frames = 0;
    cfg.hangover_frames = UINT32_MAX;
    memset(pcm, 0, sizeof pcm);
    fill_wave(pcm, 4);
    CHECK(capsule_vad_trim(&cfg, pcm, 12, &kept) == 0);
    CHECK(kept == 12);

    cfg.frame_samples = 1;
    CHECK(capsule_vad_trim(&cfg, pcm, CAPSULE_VAD_MAX_FRAMES + 1, &kept) != 0);
}

static void test_timer_and_macro(void)
{
    static int16_t memo[64];
    static uint8_t wav[256];
    capsule_t box;
    uint32_t now = 0;
    char status[96];
    CHECK(capsule_init(&box, memo, 64, wav, sizeof wav) == 0);
    CHECK(capsule_init(NULL, memo, 64, wav, sizeof wav) != 0);
    standard_refs(&box);

    settle(&box, 0, 0, 1000, &now, 100);
    CHECK(capsule_pose(&box) == CAPSULE_POSE_UNKNOWN);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_IDLE);

    settle(&box, 0, 0, 1000, &now, 400);
    CHECK(capsule_pose(&box) == CAPSULE_POSE_STAND);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_RUNNING);
    CHECK(capsule_show_seconds(&box) == 25u * 60u);
    CHECK(capsule_desk(&box));
    capsule_format_status(&box, status, sizeof status);
    CHECK(strstr(status, "直立 25:00") != NULL);

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

static void test_beep_swallowed_when_face_down(void)
{
    static int16_t memo[32];
    static uint8_t wav[128];
    capsule_t box;
    uint32_t now = 0;
    CHECK(capsule_init(&box, memo, 32, wav, sizeof wav) == 0);
    standard_refs(&box);
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
    CHECK(capsule_last_macro(&box) == CAPSULE_MACRO_MIC_MUTE);
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
    CHECK(capsule_memo_status(&box) == CAPSULE_MEMO_SILENCE);
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
    box.vad.preroll_frames = 1;
    box.vad.hangover_frames = 1;
    standard_refs(&box);
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
    CHECK(samples == 7 * frame);
    CHECK(decoded[frame] == 3000);

    CHECK(capsule_wav_length(&box) > 0);
    memset(pcm, 0, sizeof pcm);
    capsule_on_key_down(&box, now + 300);
    CHECK(capsule_wav_length(&box) == 0);
    CHECK(capsule_recording(&box));
    CHECK(capsule_on_pcm(&box, pcm, total) == 0);
    capsule_on_key_up(&box, now + 350);
    capsule_on_key_down(&box, now + 400);
    CHECK(capsule_memo_status(&box) == CAPSULE_MEMO_SILENCE);
    CHECK(capsule_wav_length(&box) == 0);
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
    box.vad.frame_samples = 32;
    box.vad.preroll_frames = 0;
    box.vad.hangover_frames = 0;
    capsule_on_key_down(&box, 0);
    CHECK(capsule_on_pcm(&box, pcm, 200) == 0);
    CHECK(capsule_memo_truncated(&box));
    CHECK(!capsule_recording(&box));
    CHECK(capsule_memo_status(&box) == CAPSULE_MEMO_SAVED);

    CHECK(capsule_init(&box, memo, 64, tiny, sizeof tiny) == 0);
    box.vad.frame_samples = 32;
    box.vad.preroll_frames = 0;
    box.vad.hangover_frames = 0;
    capsule_on_key_down(&box, 0);
    CHECK(capsule_on_pcm(&box, pcm, 64) == 0);
    capsule_on_key_up(&box, 5);
    capsule_on_key_down(&box, 10);
    CHECK(capsule_memo_status(&box) == CAPSULE_MEMO_NOSPACE);

    {
        static int16_t many_memo[CAPSULE_VAD_MAX_FRAMES + 1];
        static int16_t many_pcm[CAPSULE_VAD_MAX_FRAMES + 1];
        static uint8_t many_wav[256];
        CHECK(capsule_init(&box, many_memo, CAPSULE_VAD_MAX_FRAMES + 1, many_wav, sizeof many_wav) == 0);
        box.vad.frame_samples = 1;
        capsule_on_key_down(&box, 0);
        CHECK(capsule_on_pcm(&box, many_pcm, CAPSULE_VAD_MAX_FRAMES + 1) == 0);
        capsule_on_key_up(&box, 5);
        capsule_on_key_down(&box, 10);
        CHECK(capsule_memo_status(&box) == CAPSULE_MEMO_BAD);
        CHECK(capsule_wav_length(&box) == 0);
    }
}

static void test_slow_and_backward(void)
{
    static int16_t memo[8];
    static uint8_t wav[64];
    capsule_t box;
    CHECK(capsule_init(&box, memo, 8, wav, sizeof wav) == 0);
    standard_refs(&box);
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
    now = 0;
    settle(&box, 0, 0, 1000, &now, 400);
    capsule_on_time(&box, box.running_since_ms + CAPSULE_POMO_MS);
    CHECK(capsule_timer_state(&box) == CAPSULE_POMO_DONE);
    settle(&box, 700, 700, 0, &now, 400);
    CHECK(capsule_pose(&box) == CAPSULE_POSE_UNKNOWN);
    CHECK(!capsule_muted(&box));
    CHECK(capsule_take_beep(&box));
}

int main(void)
{
    test_pose_match();
    test_pose_map_bytes();
    test_capture();
    test_wav();
    test_vad();
    test_timer_and_macro();
    test_beep_swallowed_when_face_down();
    test_beep_edges();
    test_slow_and_backward();
    test_macro_and_pocket();
    test_record();
    test_truncate_and_nospace();
    CHECK(capsule_selftest() == 0);
    if (failures) {
        fprintf(stderr, "%d failed, %d checks\n", failures, checks);
        return 1;
    }
    printf("ok %d checks\n", checks);
    return 0;
}
