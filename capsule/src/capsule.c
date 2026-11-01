#include "capsule.h"

#include <stdio.h>
#include <string.h>

/* 24 天以内的回绕仍算向前。倒退的时间戳不能把倒计时一次走完。 */
static bool moved_forward(uint32_t earlier, uint32_t later)
{
    return (int32_t)(later - earlier) >= 0;
}

static void pomo_run(capsule_t *box, uint32_t now_ms, uint32_t remaining_ms)
{
    box->pomo = CAPSULE_POMO_RUNNING;
    box->remaining_ms = remaining_ms;
    box->remaining_at_run_ms = remaining_ms;
    box->running_since_ms = now_ms;
    box->mute = false;
}

static void pomo_apply(capsule_t *box, uint32_t now_ms, bool pause)
{
    uint32_t elapsed;
    if (box->pomo != CAPSULE_POMO_RUNNING) return;
    if (!moved_forward(box->running_since_ms, now_ms)) return;
    elapsed = now_ms - box->running_since_ms;
    if (elapsed >= box->remaining_at_run_ms) {
        box->remaining_ms = 0;
        box->pomo = CAPSULE_POMO_DONE;
        if (!box->mute) box->beep_pending = true;
        return;
    }
    box->remaining_ms = box->remaining_at_run_ms - elapsed;
    if (pause) box->pomo = CAPSULE_POMO_PAUSED;
}

static void pomo_on_pose(capsule_t *box, capsule_pose_t pose, uint32_t now_ms)
{
    if (pose == CAPSULE_POSE_STAND) {
        if (box->pomo == CAPSULE_POMO_RUNNING) return;
        if (box->pomo == CAPSULE_POMO_PAUSED) pomo_run(box, now_ms, box->remaining_ms);
        else pomo_run(box, now_ms, CAPSULE_POMO_MS);
        return;
    }
    pomo_apply(box, now_ms, true);
    if (pose == CAPSULE_POSE_FACE_DOWN) {
        box->mute = true;
        box->beep_pending = false;
    } else {
        box->mute = false;
    }
}

static void note_pose(capsule_t *box, capsule_pose_t pose, uint32_t now_ms)
{
    if (pose == CAPSULE_POSE_STAND || pose == CAPSULE_POSE_FLAT || pose == CAPSULE_POSE_FACE_DOWN) {
        box->desk = true;
        box->away = false;
        return;
    }
    if (pose == CAPSULE_POSE_SIDE_LEFT || pose == CAPSULE_POSE_SIDE_RIGHT) {
        box->away = false;
        return;
    }
    if (box->desk && !box->away) {
        box->away = true;
        box->away_since_ms = now_ms;
    }
}

static void poll_away(capsule_t *box, uint32_t now_ms)
{
    if (!box->desk || !box->away || !moved_forward(box->away_since_ms, now_ms)) return;
    if (now_ms - box->away_since_ms >= CAPSULE_DESK_LEAVE_MS) {
        box->desk = false;
        box->away = false;
    }
}

static bool macro_mode(const capsule_t *box)
{
    if (!box->desk) return false;
    return box->stable == CAPSULE_POSE_SIDE_LEFT || box->stable == CAPSULE_POSE_SIDE_RIGHT;
}

static void tracker_reset(capsule_t *box)
{
    box->stable = CAPSULE_POSE_UNKNOWN;
    box->candidate = CAPSULE_POSE_UNKNOWN;
    box->have_candidate = false;
    box->have_sample_time = false;
}

static void tracker_push(capsule_t *box, capsule_pose_t pose, uint32_t now_ms)
{
    if (box->have_sample_time) {
        int32_t gap;
        if (!moved_forward(box->last_sample_ms, now_ms)) return;
        gap = (int32_t)(now_ms - box->last_sample_ms);
        if (gap > (int32_t)CAPSULE_POSE_STABLE_MS) box->have_candidate = false;
    }
    box->last_sample_ms = now_ms;
    box->have_sample_time = true;
    if (!box->have_candidate || pose != box->candidate) {
        box->candidate = pose;
        box->candidate_since_ms = now_ms;
        box->have_candidate = true;
        return;
    }
    if (now_ms - box->candidate_since_ms < CAPSULE_POSE_STABLE_MS) return;
    if (pose == box->stable) return;
    box->stable = pose;
    note_pose(box, pose, now_ms);
    pomo_on_pose(box, pose, now_ms);
}

static int memo_stop(capsule_t *box)
{
    size_t kept = 0;
    int err;
    box->recording = false;
    if (!box->memo_used) {
        box->memo_status = CAPSULE_MEMO_SILENCE;
        box->wav_len = 0;
        return 0;
    }
    err = capsule_vad_trim(&box->vad, box->memo, box->memo_used, &kept);
    if (err) {
        box->memo_status = CAPSULE_MEMO_BAD;
        box->wav_len = 0;
        return -1;
    }
    if (!kept) {
        box->memo_status = CAPSULE_MEMO_SILENCE;
        box->wav_len = 0;
        return 0;
    }
    if (!box->wav || capsule_wav_write(box->wav, box->wav_cap, box->memo, kept, &box->wav_len)) {
        box->memo_status = CAPSULE_MEMO_NOSPACE;
        box->wav_len = 0;
        return -1;
    }
    box->memo_status = CAPSULE_MEMO_SAVED;
    return 0;
}

int capsule_init(capsule_t *box, int16_t *memo_storage, size_t memo_samples, uint8_t *wav_storage, size_t wav_bytes)
{
    if (!box || !memo_storage || !memo_samples || !wav_storage || wav_bytes < 44) return -1;
    if (((uintptr_t)memo_storage | (uintptr_t)wav_storage) & 1u) return -1;
    memset(box, 0, sizeof(*box));
    box->memo = memo_storage;
    box->memo_cap = memo_samples;
    box->wav = wav_storage;
    box->wav_cap = wav_bytes;
    box->vad = capsule_vad_default();
    box->pomo = CAPSULE_POMO_IDLE;
    box->remaining_ms = CAPSULE_POMO_MS;
    tracker_reset(box);
    return 0;
}

int capsule_set_ref(capsule_t *box, capsule_pose_t pose, int32_t ax_mg, int32_t ay_mg, int32_t az_mg)
{
    if (!box) return -1;
    return capsule_pose_map_set(&box->map, pose, ax_mg, ay_mg, az_mg);
}

int capsule_capture(capsule_t *box, capsule_pose_t pose)
{
    if (!box || !box->have_sample_time) return -1;
    return capsule_set_ref(box, pose, box->ax_mg, box->ay_mg, box->az_mg);
}

void capsule_on_accel(capsule_t *box, int32_t ax_mg, int32_t ay_mg, int32_t az_mg, uint32_t now_ms)
{
    capsule_pose_t pose;
    if (!box) return;
    if (box->have_sample_time && !moved_forward(box->last_sample_ms, now_ms)) return;
    box->ax_mg = ax_mg;
    box->ay_mg = ay_mg;
    box->az_mg = az_mg;
    pose = capsule_pose_match(&box->map, ax_mg, ay_mg, az_mg);
    tracker_push(box, pose, now_ms);
    poll_away(box, now_ms);
}

void capsule_on_time(capsule_t *box, uint32_t now_ms)
{
    if (!box) return;
    pomo_apply(box, now_ms, false);
    poll_away(box, now_ms);
}

void capsule_on_key_down(capsule_t *box, uint32_t now_ms)
{
    if (!box || box->key_held) return;
    box->key_held = true;
    box->key_down_ms = now_ms;
    if (box->recording) {
        box->key_macro = false;
        memo_stop(box);
        return;
    }
    if (macro_mode(box)) {
        box->key_macro = true;
        box->key_pose = box->stable;
        return;
    }
    box->key_macro = false;
    box->memo_used = 0;
    box->truncated = false;
    box->recording = true;
    box->wav_len = 0;
    box->memo_status = CAPSULE_MEMO_RECORDING;
}

void capsule_on_key_up(capsule_t *box, uint32_t now_ms)
{
    capsule_macro_t macro;
    int32_t held;
    if (!box || !box->key_held) return;
    box->key_held = false;
    if (!box->key_macro) return;
    box->key_macro = false;
    held = (int32_t)(now_ms - box->key_down_ms);
    if (held < 0) held = 0;
    if ((uint32_t)held >= CAPSULE_KEY_LONG_MS) macro = CAPSULE_MACRO_MIC_MUTE;
    else if (box->key_pose == CAPSULE_POSE_SIDE_LEFT) macro = CAPSULE_MACRO_VOLUME_UP;
    else if (box->key_pose == CAPSULE_POSE_SIDE_RIGHT) macro = CAPSULE_MACRO_NEXT;
    else return;
    box->last_macro = macro;
    box->macro_seq++;
}

int capsule_on_pcm(capsule_t *box, const int16_t *pcm, size_t n)
{
    size_t room;
    if (!box || (!pcm && n)) return -1;
    if (!box->recording || !n) return 0;
    room = box->memo_cap - box->memo_used;
    if (n > room) {
        n = room;
        box->truncated = true;
        box->recording = false;
    }
    if (n) {
        memcpy(box->memo + box->memo_used, pcm, n * sizeof(int16_t));
        box->memo_used += n;
    }
    if (box->truncated) memo_stop(box);
    return 0;
}

capsule_pose_t capsule_pose(const capsule_t *box) { return box ? box->stable : CAPSULE_POSE_UNKNOWN; }
capsule_pomo_state_t capsule_timer_state(const capsule_t *box) { return box ? box->pomo : CAPSULE_POMO_IDLE; }
bool capsule_muted(const capsule_t *box) { return box && box->mute; }
bool capsule_desk(const capsule_t *box) { return box && box->desk; }
bool capsule_recording(const capsule_t *box) { return box && box->recording; }
capsule_macro_t capsule_last_macro(const capsule_t *box) { return box ? box->last_macro : CAPSULE_MACRO_NONE; }
uint32_t capsule_macro_seq(const capsule_t *box) { return box ? box->macro_seq : 0; }
capsule_memo_status_t capsule_memo_status(const capsule_t *box) { return box ? box->memo_status : CAPSULE_MEMO_BAD; }
bool capsule_memo_truncated(const capsule_t *box) { return box && box->truncated; }
size_t capsule_wav_length(const capsule_t *box) { return box ? box->wav_len : 0; }
const uint8_t *capsule_wav_data(const capsule_t *box) { return box ? box->wav : NULL; }

uint32_t capsule_show_seconds(const capsule_t *box)
{
    if (!box || box->pomo == CAPSULE_POMO_DONE) return 0;
    if (box->pomo == CAPSULE_POMO_IDLE) return CAPSULE_POMO_MS / 1000u;
    return (box->remaining_ms + 999u) / 1000u;
}

bool capsule_take_beep(capsule_t *box)
{
    bool pending;
    if (!box) return false;
    pending = box->beep_pending;
    box->beep_pending = false;
    return pending;
}

static void format_mg(char *buf, size_t n, int32_t mg)
{
    uint32_t abs_mg = mg < 0 ? (uint32_t)(-(int64_t)mg) : (uint32_t)mg;
    snprintf(buf, n, "%s%lu.%03lu", mg < 0 ? "-" : "", (unsigned long)(abs_mg / 1000u), (unsigned long)(abs_mg % 1000u));
}

void capsule_format_status(const capsule_t *box, char *buf, size_t n)
{
    char x[16], y[16], z[16];
    uint32_t seconds;
    if (!buf || !n) return;
    if (!box) {
        buf[0] = 0;
        return;
    }
    seconds = capsule_show_seconds(box);
    format_mg(x, sizeof x, box->ax_mg);
    format_mg(y, sizeof y, box->ay_mg);
    format_mg(z, sizeof z, box->az_mg);
    snprintf(buf, n, "%s %02u:%02u x=%s y=%s z=%s",
             capsule_pose_name(box->stable),
             (unsigned)(seconds / 60u), (unsigned)(seconds % 60u), x, y, z);
}
