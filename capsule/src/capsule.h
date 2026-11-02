/*
 * 闪念胶囊的纯逻辑。不调用传感器、屏幕和蓝牙。
 * 同一时刻只在一个线程里调用，不要放进中断。
 * 加速度单位是毫 g，换算与课程示例一致：millig = round(raw * 61 / 1000)，量程 ±2 g。
 * 相邻两拍间隔超过 CAPSULE_POSE_STABLE_MS 时，稳定窗口重头计。建议 100 ms。
 */
#ifndef CAPSULE_H
#define CAPSULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CAPSULE_WAV_RATE 16000
#define CAPSULE_POMO_MS (25u * 60u * 1000u)
#define CAPSULE_POSE_STABLE_MS 400u
#define CAPSULE_POSE_MATCH_MIN_COS_MILLI 850
#define CAPSULE_POSE_MATCH_MARGIN_MILLI 80
#define CAPSULE_DESK_LEAVE_MS 3000u
#define CAPSULE_KEY_LONG_MS 600u
#define CAPSULE_VAD_MAX_FRAMES 2048u

typedef enum {
    CAPSULE_POSE_UNKNOWN = 0,
    CAPSULE_POSE_STAND,
    CAPSULE_POSE_FACE_DOWN,
    CAPSULE_POSE_SIDE_LEFT,
    CAPSULE_POSE_SIDE_RIGHT,
    CAPSULE_POSE_FLAT
} capsule_pose_t;

#define CAPSULE_POSE_REF_COUNT 5

typedef struct {
    int32_t ax_mg;
    int32_t ay_mg;
    int32_t az_mg;
    bool set;
} capsule_pose_ref_t;

typedef struct {
    capsule_pose_ref_t ref[CAPSULE_POSE_REF_COUNT];
} capsule_pose_map_t;

/* "CPM1" 加五个小端 int32 三元组。全零表示未标定。其它值若不是静止重力，导入失败。 */
#define CAPSULE_POSE_MAP_BYTES (4u + CAPSULE_POSE_REF_COUNT * 12u)

typedef enum {
    CAPSULE_POMO_IDLE = 0,
    CAPSULE_POMO_RUNNING,
    CAPSULE_POMO_PAUSED,
    CAPSULE_POMO_DONE
} capsule_pomo_state_t;

typedef enum {
    CAPSULE_MACRO_NONE = 0,
    CAPSULE_MACRO_VOLUME_UP,
    CAPSULE_MACRO_NEXT,
    CAPSULE_MACRO_MIC_MUTE
} capsule_macro_t;

typedef enum {
    CAPSULE_MEMO_IDLE = 0,
    CAPSULE_MEMO_RECORDING,
    CAPSULE_MEMO_SAVED,
    CAPSULE_MEMO_SILENCE,
    CAPSULE_MEMO_NOSPACE,
    CAPSULE_MEMO_BAD
} capsule_memo_status_t;

typedef struct {
    int32_t rms_threshold;
    uint32_t frame_samples;
    uint32_t preroll_frames;
    uint32_t hangover_frames;
} capsule_vad_config_t;

typedef struct capsule capsule_t;

const char *capsule_pose_name(capsule_pose_t pose);

void capsule_pose_map_clear(capsule_pose_map_t *map);
/* 姿态无效，或合加速度不在约 0.4 g 到 2.5 g 之间时返回 -1，不改原参照。 */
int capsule_pose_map_set(capsule_pose_map_t *map, capsule_pose_t pose, int32_t ax_mg, int32_t ay_mg, int32_t az_mg);
/* 样本合加速度不在约 0.4 g 到 2.5 g 之间时返回未知。两个参照太接近时也返回未知。 */
capsule_pose_t capsule_pose_match(const capsule_pose_map_t *map, int32_t ax_mg, int32_t ay_mg, int32_t az_mg);
/* 与课程示例相同：±2 g，round(raw * 61 / 1000)，单位毫 g。 */
int32_t capsule_accel_millig(int16_t raw);
/* 空间不够时返回 0，不写入。导入失败时不改原表。 */
size_t capsule_pose_map_export(const capsule_pose_map_t *map, uint8_t *dst, size_t cap);
int capsule_pose_map_import(capsule_pose_map_t *map, const uint8_t *src, size_t len);

capsule_vad_config_t capsule_vad_default(void);
/* 就地压缩。返回 0 时 *kept 为留下的样本数。帧数超过 CAPSULE_VAD_MAX_FRAMES 返回 -1。 */
int capsule_vad_trim(const capsule_vad_config_t *cfg, int16_t *pcm, size_t n, size_t *kept);

/*
 * 16 kHz、16 bit、单声道 PCM。读出的指针指向 src 内部。
 * 样本数为 0、缓冲未按 2 字节对齐、或放不下时，写出失败。
 * 数据落在奇数地址上时，读出失败。
 */
int capsule_wav_write(uint8_t *dst, size_t cap, const int16_t *pcm, size_t samples, size_t *out_len);
int capsule_wav_read(const uint8_t *src, size_t len, const int16_t **pcm, size_t *samples);

/*
 * memo_storage 与 wav_storage 都要按 2 字节对齐。
 * memo_storage 的容量就是最长一条录音。wav_storage 至少 44 字节。
 * 全静音时不写 WAV。标定要在开始使用前做完。
 */
int capsule_init(capsule_t *box, int16_t *memo_storage, size_t memo_samples, uint8_t *wav_storage, size_t wav_bytes);
int capsule_set_ref(capsule_t *box, capsule_pose_t pose, int32_t ax_mg, int32_t ay_mg, int32_t az_mg);
/* 把最近一次加速度记成这个姿态的参照。还没有样本，或这一拍不是静止重力时返回 -1。 */
int capsule_capture(capsule_t *box, capsule_pose_t pose);
void capsule_on_accel(capsule_t *box, int32_t ax_mg, int32_t ay_mg, int32_t az_mg, uint32_t now_ms);
void capsule_on_time(capsule_t *box, uint32_t now_ms);
void capsule_on_key_down(capsule_t *box, uint32_t now_ms);
void capsule_on_key_up(capsule_t *box, uint32_t now_ms);
int capsule_on_pcm(capsule_t *box, const int16_t *pcm, size_t n);

capsule_pose_t capsule_pose(const capsule_t *box);
capsule_pomo_state_t capsule_timer_state(const capsule_t *box);
uint32_t capsule_show_seconds(const capsule_t *box);
bool capsule_muted(const capsule_t *box);
bool capsule_take_beep(capsule_t *box);
bool capsule_desk(const capsule_t *box);
bool capsule_recording(const capsule_t *box);
capsule_macro_t capsule_last_macro(const capsule_t *box);
uint32_t capsule_macro_seq(const capsule_t *box);
capsule_memo_status_t capsule_memo_status(const capsule_t *box);
bool capsule_memo_truncated(const capsule_t *box);
size_t capsule_wav_length(const capsule_t *box);
const uint8_t *capsule_wav_data(const capsule_t *box);
void capsule_format_status(const capsule_t *box, char *buf, size_t n);
/* 固件启动时调用的短自检。返回失败条数，0 为通过。 */
int capsule_selftest(void);

/* 由调用方放在静态存储里。除 vad 和 map 外，字段只由本模块改写。 */
struct capsule {
    capsule_pose_map_t map;
    capsule_pose_t stable;
    capsule_pose_t candidate;
    bool have_candidate;
    uint32_t candidate_since_ms;
    bool have_sample_time;
    uint32_t last_sample_ms;
    int32_t ax_mg, ay_mg, az_mg;
    capsule_pomo_state_t pomo;
    uint32_t remaining_ms;
    uint32_t remaining_at_run_ms;
    uint32_t running_since_ms;
    bool mute;
    bool beep_pending;
    bool desk;
    bool away;
    uint32_t away_since_ms;
    int16_t *memo;
    size_t memo_cap;
    size_t memo_used;
    bool recording;
    bool truncated;
    capsule_memo_status_t memo_status;
    capsule_vad_config_t vad;
    uint8_t *wav;
    size_t wav_cap;
    size_t wav_len;
    bool key_held;
    bool key_macro;
    capsule_pose_t key_pose;
    uint32_t key_down_ms;
    capsule_macro_t last_macro;
    uint32_t macro_seq;
};

#endif
