/*
 * 闪念胶囊板端 P0 闭环：固定五姿态参照、番茄钟和 KEY2 录音。
 * 姿态参照来自用户于 2026-11-05 在本板采集的数据，开机不再提示重标定。
 * 板载 MEMS 麦克风经 audcodec/audprc 接入 audio_server。
 */
#include "rtthread.h"
#include "bf0_hal.h"
    #include <drivers/rtc.h>
#include "rtdevice.h"
#include "sensor.h"
#include "st_lsm6dsl_sensor_v1.h"
#include "audio_server.h"
#include "button.h"
#include "mem_section.h"
#include "capsule.h"
#include "ble_hid.h"
#include "ble_file.h"
#include "drv_touch.h"
#include <time.h>
#ifdef PKG_USING_LITTLEVGL2RTT
#include "lvgl.h"
#include "lv_ex_data.h"
#endif

void tf_library_init(uint8_t *wav_storage, size_t wav_bytes);
bool tf_library_ready(void);
bool tf_library_copy_selected(char *dst, size_t capacity);
int tf_library_save(const uint8_t *data, size_t len);
int tf_library_next(void);
int tf_library_delete(void);
int tf_library_play(void);
int tf_library_play_ram(const uint8_t *data, size_t len);
uint8_t tf_library_playback_state(void);
uint8_t tf_library_playback_progress(void);
void tf_library_playback_prepare(void);
void tf_library_playback_stop(void);
uint32_t tf_library_selected_duration_ms(void);
#define APP_NAME              "course_starter"
#define APP_VERSION           "0.4.2"
#define IMU_DRIVER_NAME       "lsm6dsl"
#define IMU_DEVICE_NAME       "acce_lsm6dsl"
#define IMU_BUS_NAME          "i2c1"
#define IMU_SAMPLE_MS         100
#define KEY2_PIN              43
#define SENSOR_POWER_PIN      30
#define CALIBRATED_STAND_X    (-39)
#define CALIBRATED_STAND_Y    (-999)
#define CALIBRATED_STAND_Z    (-52)
#define CALIBRATED_FACE_X     (25)
#define CALIBRATED_FACE_Y     (91)
#define CALIBRATED_FACE_Z     (1005)
#define CALIBRATED_LEFT_X     (970)
#define CALIBRATED_LEFT_Y     (-86)
#define CALIBRATED_LEFT_Z     (19)
#define CALIBRATED_RIGHT_X    (-1005)
#define CALIBRATED_RIGHT_Y    (-82)
#define CALIBRATED_RIGHT_Z    (55)
#define CALIBRATED_FLAT_X     (-55)
#define CALIBRATED_FLAT_Y     (-32)
#define CALIBRATED_FLAT_Z     (-980)
#define RECORD_MAX_SECONDS    30
#define RECORD_MAX_SAMPLES    (CAPSULE_WAV_RATE * RECORD_MAX_SECONDS)
#define AUDIO_CACHE_SIZE      2048

static rt_device_t imu_device;
static struct rt_i2c_bus_device *imu_bus;
static capsule_t capsule_box;
L2_NON_RET_BSS_SECT(memo, static int16_t memo_storage[RECORD_MAX_SAMPLES]);
L2_NON_RET_BSS_SECT(capture, static uint8_t capture_storage[RECORD_MAX_SAMPLES * sizeof(int16_t)]);
L2_NON_RET_BSS_SECT(wav, static uint8_t wav_storage[RECORD_MAX_SAMPLES * sizeof(int16_t) + 44]);
static audio_client_t audio_client;
static volatile uint32_t capture_bytes;
static volatile bool audio_overflow;
static volatile uint32_t recording_duration_ms;
static volatile uint32_t recording_started_ms;
static volatile bool recording_persisted;
static volatile bool recording_truncated;
static volatile bool beep_worker_active;
static bool beep_pending;

enum ui_command_type {
    UI_CMD_MODE_POMO, UI_CMD_MODE_TIME, UI_CMD_MODE_STOPWATCH, UI_CMD_MINUS, UI_CMD_PLUS, UI_CMD_BEEP,
    UI_CMD_TIMER_TOGGLE, UI_CMD_TIMER_RESET, UI_CMD_CLOCK_FORMAT, UI_CMD_CLOCK_SECONDS,
    UI_CMD_THEME, UI_CMD_STOPWATCH_TOGGLE, UI_CMD_STOPWATCH_RESET, UI_CMD_STOPWATCH_LAP,
    UI_CMD_RECORD, UI_CMD_VOLUME_UP, UI_CMD_VOLUME_DOWN, UI_CMD_MUTE,
    UI_CMD_PREVIOUS, UI_CMD_PLAY_PAUSE, UI_CMD_NEXT,
    UI_CMD_TF_NEXT, UI_CMD_TF_PLAY, UI_CMD_TF_DELETE
};
struct ui_command { enum ui_command_type type; uint32_t step; };
static rt_mq_t ui_command_queue;

#ifdef PKG_USING_LITTLEVGL2RTT
static lv_obj_t *ui_page;
static lv_obj_t *ui_timer_panel;
static lv_obj_t *ui_remote_left_panel;
static lv_obj_t *ui_remote_right_panel;
static lv_obj_t *ui_record_panel;
static int32_t ui_page_width;
static int32_t ui_page_height;
static int32_t ui_anim_from_x;
static int32_t ui_anim_from_y;
static int32_t ui_anim_to_x;
static int32_t ui_anim_to_y;
static capsule_pose_t ui_display_pose = CAPSULE_POSE_UNKNOWN;
static capsule_pose_t ui_pose_before_hidden = CAPSULE_POSE_STAND;
static bool ui_page_hidden;
static lv_obj_t *ui_stand_track;
static lv_obj_t *ui_stand_tabs[CAPSULE_STAND_MODE_COUNT];
static lv_obj_t *ui_stand_cards[CAPSULE_STAND_MODE_COUNT];
static lv_obj_t *ui_clock_label, *ui_clock_seconds_label, *ui_clock_ampm_label, *ui_clock_date_label;
static lv_obj_t *ui_clock_format_button, *ui_clock_seconds_button, *ui_night_button;
static lv_obj_t *ui_stopwatch_label, *ui_stopwatch_toggle_button, *ui_lap_labels[3];
static lv_obj_t *ui_timer_toggle_button, *ui_pomo_dot;
static volatile capsule_stand_mode_t ui_stand_snapshot = CAPSULE_STAND_TIME;
static volatile capsule_pomo_state_t ui_timer_state_snapshot = CAPSULE_POMO_IDLE;
static volatile bool ui_timer_reset_snapshot;
static volatile bool clock_24h = true, clock_seconds;
static volatile bool ui_night_mode;
static volatile bool ui_night_toggle_requested;
static bool stopwatch_running;
static uint32_t stopwatch_since_ms;
static uint64_t stopwatch_elapsed_ms;
static uint32_t stopwatch_laps[3], stopwatch_lap_count;
static volatile bool ui_clock_24h_snapshot = true, ui_clock_seconds_snapshot, ui_night_mode_snapshot;
static volatile bool ui_stopwatch_running_snapshot;
static volatile uint32_t ui_stopwatch_tenths_snapshot, ui_laps_snapshot[3], ui_lap_count_snapshot;
static bool ui_swipe_consumed;
static lv_point_t ui_swipe_start;
static rt_device_t ui_lcd_device;
static bool ui_display_blanked;
static lv_obj_t *ui_time_label;
static lv_obj_t *ui_mode_label;
static lv_obj_t *ui_timer_beep_button;
static volatile bool ui_beep_snapshot = true;
static volatile capsule_pose_t ui_pose_snapshot = CAPSULE_POSE_UNKNOWN;
static volatile uint32_t ui_seconds_snapshot = CAPSULE_POMO_MS / 1000u;
static volatile bool ui_recording_snapshot;
static volatile bool ui_recording_persisted_snapshot;
/* 各页面常驻录音指示（docs/07 §5）：左上角呼吸红点，不随页面滑动。 */
static lv_obj_t *ui_rec_badge;
enum { UI_MEDIA_CONTROL_COUNT = 6 };
static lv_obj_t *ui_remote_buttons[2][UI_MEDIA_CONTROL_COUNT];
static lv_obj_t *ui_remote_play_labels[2];
static lv_obj_t *ui_remote_desk_labels[2];
static lv_obj_t *ui_remote_bt_badges[2];
static lv_obj_t *ui_remote_bt_labels[2];
static lv_obj_t *ui_remote_feedback_labels[2];
static volatile bool ui_ble_connected_snapshot;
static volatile char ui_media_feedback_snapshot[32];
static volatile uint32_t ui_media_feedback_expiry;
static volatile bool ui_media_muted;
static volatile bool ui_media_playing = true;

static lv_obj_t *ui_memo_card;
static lv_obj_t *ui_memo_filename_label;
static lv_obj_t *ui_memo_wave_container;
static lv_obj_t *ui_memo_wave_bars[7];
static lv_obj_t *ui_memo_time_label;
static lv_obj_t *ui_memo_limit_label;
static lv_obj_t *ui_memo_progress_track;
static lv_obj_t *ui_memo_progress_bar;
static lv_obj_t *ui_memo_progress_knob;
static lv_obj_t *ui_memo_folder_button;
static lv_obj_t *ui_memo_play_button;
static lv_obj_t *ui_memo_play_icon;
static lv_obj_t *ui_memo_rec_button;
static lv_obj_t *ui_memo_rec_ring;
static lv_obj_t *ui_memo_rec_core;
static lv_obj_t *ui_memo_delete_button;
static lv_obj_t *ui_memo_delete_label;

static volatile bool ui_tf_ready_snapshot;
static char ui_tf_name_snapshot[32];
static bool tf_delete_pending;
enum tf_ui_feedback { TF_UI_READY, TF_UI_NO_CARD, TF_UI_NO_RECORDING, TF_UI_TRUNCATED, TF_UI_RECORDING_FAILED, TF_UI_PLAYBACK_FAILED };
static volatile enum tf_ui_feedback ui_tf_feedback;
enum { UI_PLAYBACK_IDLE, UI_PLAYBACK_PLAYING, UI_PLAYBACK_COMPLETE, UI_PLAYBACK_FAILED };
static volatile uint8_t ui_playback_state_snapshot;
static volatile uint8_t ui_playback_progress_snapshot;
static volatile bool playback_worker_active;
static volatile uint8_t playback_worker_state;
static volatile bool ui_tf_delete_confirm_snapshot;
static volatile uint32_t ui_selected_duration_snapshot;
enum memo_source {
    MEMO_SOURCE_TF = 0,
    MEMO_SOURCE_RAM
};
static volatile enum memo_source current_memo_source = MEMO_SOURCE_TF;
static volatile bool ram_memo_valid;

/* Shared styles keep every pose panel on the same RAM-selected palette. */
struct ui_theme_tokens {
    uint32_t screen, recessed, recessed_border, button, button_pressed, button_border;
    uint32_t primary, primary_pressed, danger, active, active_border;
    uint32_t strong, body, muted, accent, warm;
};
static const struct ui_theme_tokens ui_themes[2] = {
    {0xf7fafc, 0xedf2f7, 0xcbd5e1, 0xffffff, 0xe8eef5, 0xcbd5e1,
     0x0284c7, 0x0369a1, 0xe11d48, 0xe0f2fe, 0x38bdf8,
     0x0f172a, 0x334155, 0x566476, 0x0284c7, 0xfde68a},
    {0x0b111e, 0x090e18, 0x1e293b, 0x172234, 0x111a29, 0x334155,
     0x0284c7, 0x0284c7, 0xe11d48, 0x0c4a6e, 0x0284c7,
     0xf1f5f9, 0xcbd5e1, 0xa5b4c6, 0x38bdf8, 0xfde68a}
};
static lv_style_t ui_surface_style, ui_recessed_style, ui_button_style;
static lv_style_t ui_button_pressed_style, ui_primary_style, ui_primary_pressed_style, ui_danger_style;
static lv_style_t ui_active_style, ui_chip_style;
static lv_style_t ui_strong_style, ui_muted_style, ui_accent_style, ui_warm_style;

static void ui_neutral_container(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

static void ui_theme_face(lv_style_t *style, uint32_t top, uint32_t bottom,
                          uint32_t border, uint32_t text)
{
    lv_style_set_bg_opa(style, LV_OPA_COVER);
    lv_style_set_bg_color(style, lv_color_hex(top));
    lv_style_set_bg_grad_color(style, lv_color_hex(bottom));
    lv_style_set_bg_grad_dir(style, LV_GRAD_DIR_VER);
    lv_style_set_border_color(style, lv_color_hex(border));
    lv_style_set_text_color(style, lv_color_hex(text));
}

static void ui_theme_apply(bool dark)
{
    const struct ui_theme_tokens *t = &ui_themes[dark ? 1 : 0];
    /* 主题的唯一写者是 LVGL 线程：快照只从这份 RAM 状态取值，避免旧快照把夜模式改回去。 */
    ui_night_mode = dark;
    ui_theme_face(&ui_surface_style, t->screen, t->screen, t->screen, t->body);
    ui_theme_face(&ui_recessed_style, t->recessed, t->recessed, t->recessed_border, t->strong);
    ui_theme_face(&ui_button_style, t->button, t->button, t->button_border, t->body);
    ui_theme_face(&ui_button_pressed_style, t->button_pressed, t->button_pressed,
                  t->button_border, t->body);
    ui_theme_face(&ui_primary_style, t->primary, t->primary, t->primary, 0xffffff);
    ui_theme_face(&ui_primary_pressed_style, t->primary_pressed, t->primary_pressed,
                  t->primary_pressed, 0xffffff);
    ui_theme_face(&ui_danger_style, t->danger, t->danger, t->danger, 0xffffff);
    ui_theme_face(&ui_active_style, t->active, t->active, t->active_border,
                  dark ? 0x38bdf8 : 0x0369a1);
    ui_theme_face(&ui_chip_style, t->active, t->active, t->active_border, t->muted);
    lv_style_set_text_color(&ui_strong_style, lv_color_hex(t->strong));
    lv_style_set_text_color(&ui_muted_style, lv_color_hex(t->muted));
    lv_style_set_text_color(&ui_accent_style, lv_color_hex(t->accent));
    lv_style_set_text_color(&ui_warm_style, lv_color_hex(t->warm));
    if (ui_memo_card) {
        lv_obj_set_style_bg_color(ui_memo_card, lv_color_hex(t->recessed), 0);
        lv_obj_set_style_border_color(ui_memo_card, lv_color_hex(t->recessed_border), 0);
    }
    if (ui_memo_wave_container) {
        lv_obj_set_style_bg_color(ui_memo_wave_container, lv_color_hex(t->button), 0);
        lv_obj_set_style_border_color(ui_memo_wave_container, lv_color_hex(t->button_border), 0);
    }
    for (int b = 0; b < 7; ++b) {
        if (ui_memo_wave_bars[b]) {
            if (ui_recording_snapshot) {
                lv_obj_set_style_bg_color(ui_memo_wave_bars[b], lv_color_hex(0xF43F5E), 0);
            } else if (ui_playback_state_snapshot == UI_PLAYBACK_PLAYING) {
                lv_obj_set_style_bg_color(ui_memo_wave_bars[b], lv_color_hex(t->accent), 0);
            } else {
                lv_obj_set_style_bg_color(ui_memo_wave_bars[b], lv_color_hex(t->muted), 0);
            }
        }
    }
    if (ui_memo_progress_track) {
        lv_obj_set_style_bg_color(ui_memo_progress_track, lv_color_hex(t->button), 0);
        lv_obj_set_style_border_color(ui_memo_progress_track, lv_color_hex(t->button_border), 0);
    }
    for (int side = 0; side < 2; ++side) {
        if (ui_remote_bt_badges[side]) {
            if (ui_ble_connected_snapshot) {
                lv_obj_set_style_bg_color(ui_remote_bt_badges[side], lv_color_hex(t->active), 0);
                lv_obj_set_style_border_color(ui_remote_bt_badges[side], lv_color_hex(t->active_border), 0);
                lv_obj_set_style_text_color(ui_remote_bt_labels[side], lv_color_hex(dark ? 0x38bdf8 : 0x0369a1), 0);
            } else {
                lv_obj_set_style_bg_color(ui_remote_bt_badges[side], lv_color_hex(t->button), 0);
                lv_obj_set_style_border_color(ui_remote_bt_badges[side], lv_color_hex(t->button_border), 0);
                lv_obj_set_style_text_color(ui_remote_bt_labels[side], lv_color_hex(t->muted), 0);
            }
        }
    }
    lv_obj_report_style_change(NULL);
    if (ui_page) lv_obj_invalidate(lv_scr_act());
}

static void ui_theme_init(void)
{
    lv_style_t *styles[] = {&ui_surface_style, &ui_recessed_style, &ui_button_style,
        &ui_button_pressed_style, &ui_primary_style, &ui_primary_pressed_style, &ui_danger_style, &ui_active_style,
        &ui_chip_style, &ui_strong_style, &ui_muted_style, &ui_accent_style, &ui_warm_style};
    for (unsigned i = 0; i < sizeof styles / sizeof styles[0]; ++i) lv_style_init(styles[i]);
    lv_style_set_radius(&ui_recessed_style, 24);
    lv_style_set_border_width(&ui_recessed_style, 1);
    lv_style_set_radius(&ui_button_style, 16);
    lv_style_set_border_width(&ui_button_style, 1);
    lv_style_set_pad_all(&ui_button_style, 0);
    lv_style_set_bg_opa(&ui_button_pressed_style, LV_OPA_COVER);
    lv_style_set_bg_opa(&ui_active_style, LV_OPA_COVER);
    lv_style_set_shadow_width(&ui_button_style, 3);
    lv_style_set_shadow_opa(&ui_button_style, LV_OPA_10);
    lv_style_set_radius(&ui_chip_style, LV_RADIUS_CIRCLE);
    lv_style_set_border_width(&ui_chip_style, 1);
    ui_theme_apply(false);
}

static void ui_stand_gesture_event(lv_event_t *event)
{
    lv_indev_t *input = lv_indev_get_act();
    lv_point_t point;
    lv_event_code_t code = lv_event_get_code(event);
    if (!input) return;
    lv_indev_get_point(input, &point);
    if (code == LV_EVENT_PRESSED) {
        ui_swipe_start = point;
        ui_swipe_consumed = false;
    } else if (code == LV_EVENT_PRESSING || code == LV_EVENT_RELEASED) {
        int dx = point.x - ui_swipe_start.x;
        int dy = point.y - ui_swipe_start.y;
        if (!ui_swipe_consumed && (dx > 40 || dx < -40) &&
            (dx > 0 ? dx : -dx) > (dy > 0 ? dy : -dy)) {
            int mode = (int)ui_stand_snapshot + (dx < 0 ? 1 : -1);
            struct ui_command command = {0};
            ui_swipe_consumed = true;
            if (mode < 0 || mode >= CAPSULE_STAND_MODE_COUNT) return;
            command.type = mode == CAPSULE_STAND_POMO ? UI_CMD_MODE_POMO :
                           mode == CAPSULE_STAND_TIME ? UI_CMD_MODE_TIME : UI_CMD_MODE_STOPWATCH;
            if (ui_command_queue) rt_mq_send(ui_command_queue, &command, sizeof command);
        }
    }
}

static void ui_stand_bubble(lv_obj_t *obj)
{
    lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(obj); ++i)
        ui_stand_bubble(lv_obj_get_child(obj, i));
}

static void (*ui_panel_flush_cb)(lv_disp_drv_t *disp_drv, const lv_area_t *area,
                                 lv_color_t *color_p);

static void ui_rotated_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area,
                                lv_color_t *color_p)
{
    lv_area_t panel_area = *area;
    const lv_area_t *saved_clip_area = disp_drv->draw_ctx->clip_area;

    panel_area.x1 -= disp_drv->offset_x;
    panel_area.x2 -= disp_drv->offset_x;
    panel_area.y1 -= disp_drv->offset_y;
    panel_area.y2 -= disp_drv->offset_y;
    disp_drv->draw_ctx->clip_area = &panel_area;
    ui_panel_flush_cb(disp_drv, area, color_p);
    disp_drv->draw_ctx->clip_area = saved_clip_area;
}

static void ui_command_event(lv_event_t *event)
{
    struct ui_command command = {0};
    if (ui_display_pose == CAPSULE_POSE_STAND && ui_swipe_consumed) return;
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !ui_command_queue) return;
    command.type = (enum ui_command_type)(uintptr_t)lv_event_get_user_data(event);
    rt_kprintf("[%s] touch command queued=%d\n", APP_NAME, command.type);
    if (rt_mq_send(ui_command_queue, &command, sizeof(command)) != RT_EOK)
        rt_kprintf("[%s] touch command queue full; command=%d dropped\n", APP_NAME, command.type);
}

static void ui_timer_adjust_event(lv_event_t *event)
{
    static lv_obj_t *held_button;
    static uint32_t pressed_ms, repeated_ms;
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *button = lv_event_get_target(event);
    uint32_t now = lv_tick_get();
    struct ui_command command = {
        .type = (enum ui_command_type)(uintptr_t)lv_event_get_user_data(event),
        .step = CAPSULE_TIMER_STEP_MINUTES,
    };
    if (ui_swipe_consumed && code != LV_EVENT_PRESSED) return;
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (held_button == button) held_button = NULL;
        return;
    }
    if (code == LV_EVENT_PRESSED) {
        held_button = button;
        pressed_ms = repeated_ms = now;
        return;
    } else if (code == LV_EVENT_LONG_PRESSED && held_button == button) {
        repeated_ms = now;
    } else if (code == LV_EVENT_SHORT_CLICKED) {
        /* Only short releases emit a tap; CLICKED after a hold is ignored. */
    } else if (code == LV_EVENT_PRESSING && held_button == button) {
        uint32_t held_ms = now - pressed_ms;
        uint32_t interval = held_ms >= 4000 ? 60 : held_ms >= 2000 ? 120 : 250;
        if (held_ms < 500 || now - repeated_ms < interval) return;
        repeated_ms = now;
    } else {
        return;
    }
    if (ui_command_queue && rt_mq_send(ui_command_queue, &command, sizeof command) != RT_EOK)
        rt_kprintf("[%s] timer adjust queue full\n", APP_NAME);
}

static lv_obj_t *ui_command_button(lv_obj_t *screen, const char *text,
                                   int width, int height, lv_align_t align,
                                   int x, int y, enum ui_command_type command_type)
{
    lv_obj_t *button = lv_btn_create(screen);
    lv_obj_t *label = lv_label_create(button);
    lv_obj_add_style(button, &ui_button_style, 0);
    lv_obj_set_style_text_font(button, &lv_font_montserrat_16, 0);
    lv_obj_add_style(button, &ui_button_pressed_style, LV_STATE_PRESSED);
    for (lv_obj_t *ancestor = screen; ancestor; ancestor = lv_obj_get_parent(ancestor)) {
        if (ancestor == ui_timer_panel) {
            lv_obj_add_event_cb(button, ui_stand_gesture_event, LV_EVENT_ALL, NULL);
            break;
        }
    }
    lv_obj_set_size(button, width, height);
    lv_obj_align(button, align, x, y);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    if (command_type == UI_CMD_PLUS || command_type == UI_CMD_MINUS)
        lv_obj_add_event_cb(button, ui_timer_adjust_event, LV_EVENT_ALL,
                            (void *)(uintptr_t)command_type);
    else
        lv_obj_add_event_cb(button, ui_command_event, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)command_type);
    return button;
}
static void ui_anim_exec(void *var, int32_t progress)
{
    (void)var;
    lv_obj_set_pos(ui_page,
                   ui_anim_from_x + (ui_anim_to_x - ui_anim_from_x) * progress / 1000,
                   ui_anim_from_y + (ui_anim_to_y - ui_anim_from_y) * progress / 1000);
}

static void ui_start_transition(capsule_pose_t pose)
{
    lv_anim_t animation;
    if (!ui_page || pose == CAPSULE_POSE_UNKNOWN) return;
    if (pose == CAPSULE_POSE_FACE_DOWN) {
        if (!ui_page_hidden)
            ui_pose_before_hidden = ui_display_pose == CAPSULE_POSE_UNKNOWN ? CAPSULE_POSE_STAND : ui_display_pose;
        lv_anim_del(ui_page, ui_anim_exec);
        lv_obj_add_flag(ui_page, LV_OBJ_FLAG_HIDDEN);
        ui_page_hidden = true;
        ui_display_pose = pose;
        return;
    }
    if (ui_page_hidden) {
        lv_obj_clear_flag(ui_page, LV_OBJ_FLAG_HIDDEN);
        ui_page_hidden = false;
    }
    lv_anim_del(ui_page, ui_anim_exec);
    ui_anim_from_x = lv_obj_get_x(ui_page);
    ui_anim_from_y = lv_obj_get_y(ui_page);
    switch (pose) {
    case CAPSULE_POSE_STAND:
        ui_anim_to_x = -ui_page_width;
        ui_anim_to_y = -ui_page_height;
        break;
    case CAPSULE_POSE_SIDE_LEFT:
        ui_anim_to_x = 0;
        ui_anim_to_y = -ui_page_height;
        break;
    case CAPSULE_POSE_SIDE_RIGHT:
        ui_anim_to_x = -2 * ui_page_width;
        ui_anim_to_y = -ui_page_height;
        break;
    case CAPSULE_POSE_FLAT:
        ui_anim_to_x = -ui_page_width;
        ui_anim_to_y = -2 * ui_page_height;
        break;
    default:
        return;
    }
    ui_display_pose = pose;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, ui_page);
    lv_anim_set_exec_cb(&animation, ui_anim_exec);
    lv_anim_set_values(&animation, 0, 1000);
    lv_anim_set_time(&animation, 260);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
}





static void ui_bind_active_style(lv_obj_t *obj, lv_style_t *style);
static void ui_set_active(lv_obj_t *obj, lv_style_t *style, bool active);
static void ui_rec_pulse_exec(void *var, int32_t opa)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)opa, 0);
}

static void ui_init(void)
{
    extern rt_err_t littlevgl2rtt_init(const char *name);
    rt_err_t result = littlevgl2rtt_init("lcd");
    if (result != RT_EOK)
    {
        rt_kprintf("[%s] UI init failed: %d\n", APP_NAME, result);
        return;
    }
    lv_disp_t *display = lv_disp_get_default();
    ui_panel_flush_cb = display->driver->flush_cb;
    display->driver->flush_cb = ui_rotated_flush_cb;
    display->driver->sw_rotate = 1;
    lv_disp_set_rotation(display, LV_DISP_ROT_270);
    lv_ex_data_pool_init();
    lv_obj_t *screen = lv_scr_act();
    ui_theme_init();
    ui_neutral_container(screen);
    lv_obj_add_style(screen, &ui_surface_style, 0);
    ui_lcd_device = rt_device_find("lcd");
    if (!ui_lcd_device)
        rt_kprintf("[%s] LCD device unavailable for brightness control\n", APP_NAME);
    ui_page = lv_obj_create(screen);
    ui_page_width = lv_disp_get_hor_res(display);
    ui_page_height = lv_disp_get_ver_res(display);
    lv_obj_set_size(ui_page, 3 * ui_page_width, 3 * ui_page_height);
    lv_obj_set_pos(ui_page, -ui_page_width, -ui_page_height);
    ui_neutral_container(ui_page);
    lv_obj_add_style(ui_page, &ui_surface_style, 0);
    ui_timer_panel = lv_obj_create(ui_page);
    ui_remote_left_panel = lv_obj_create(ui_page);
    ui_remote_right_panel = lv_obj_create(ui_page);
    ui_record_panel = lv_obj_create(ui_page);
    lv_obj_set_size(ui_timer_panel, ui_page_width, ui_page_height);
    lv_obj_set_pos(ui_timer_panel, ui_page_width, ui_page_height);
    lv_obj_set_size(ui_remote_left_panel, ui_page_width, ui_page_height);
    lv_obj_set_pos(ui_remote_left_panel, 0, ui_page_height);
    lv_obj_set_size(ui_remote_right_panel, ui_page_width, ui_page_height);
    lv_obj_set_pos(ui_remote_right_panel, 2 * ui_page_width, ui_page_height);
    lv_obj_set_size(ui_record_panel, ui_page_width, ui_page_height);
    lv_obj_set_pos(ui_record_panel, ui_page_width, 2 * ui_page_height);
    {
        lv_obj_t *panels[] = {ui_timer_panel, ui_remote_left_panel, ui_remote_right_panel, ui_record_panel};
        for (unsigned i = 0; i < sizeof panels / sizeof panels[0]; ++i) {
            ui_neutral_container(panels[i]);
            lv_obj_add_style(panels[i], &ui_surface_style, 0);
        }
    }
    {
        static const char *names[] = {"POMO", "TIME", "STOPWATCH"};
        static const enum ui_command_type commands[] = {UI_CMD_MODE_POMO, UI_CMD_MODE_TIME, UI_CMD_MODE_STOPWATCH};
        lv_obj_t *viewport = lv_obj_create(ui_timer_panel);
        ui_neutral_container(viewport);
        lv_obj_add_style(viewport, &ui_surface_style, 0);
        lv_obj_set_size(viewport, ui_page_width, ui_page_height - 68);
        lv_obj_set_pos(viewport, 0, 68);
        ui_stand_track = lv_obj_create(viewport);
        ui_neutral_container(ui_stand_track);
        lv_obj_add_style(ui_stand_track, &ui_surface_style, 0);
        lv_obj_set_size(ui_stand_track, 3 * ui_page_width, ui_page_height - 68);
        lv_obj_set_pos(ui_stand_track, -ui_page_width, 0);
        for (int i = 0; i < CAPSULE_STAND_MODE_COUNT; ++i) {
            ui_stand_tabs[i] = ui_command_button(ui_timer_panel, names[i], 112, 36,
                LV_ALIGN_TOP_MID, (i - 1) * 118, 20, commands[i]);
            ui_bind_active_style(ui_stand_tabs[i], &ui_active_style);
            lv_obj_set_style_radius(ui_stand_tabs[i], 14, 0);
            lv_obj_t *pane = lv_obj_create(ui_stand_track);
            ui_neutral_container(pane);
            lv_obj_add_style(pane, &ui_surface_style, 0);
            lv_obj_set_size(pane, ui_page_width, ui_page_height - 68);
            lv_obj_set_pos(pane, i * ui_page_width, 0);
            ui_stand_cards[i] = lv_obj_create(pane);
            lv_obj_clear_flag(ui_stand_cards[i], LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_scrollbar_mode(ui_stand_cards[i], LV_SCROLLBAR_MODE_OFF);
            lv_obj_set_style_pad_all(ui_stand_cards[i], 0, 0);
            lv_obj_add_style(ui_stand_cards[i], &ui_recessed_style, 0);
            lv_obj_set_size(ui_stand_cards[i], ui_page_width - 40, 208);
            lv_obj_align(ui_stand_cards[i], LV_ALIGN_TOP_MID, 0, 0);
            if (i == CAPSULE_STAND_POMO) {
                ui_time_label = lv_label_create(ui_stand_cards[i]);
                lv_obj_set_style_text_font(ui_time_label, &lv_font_montserrat_48, 0);
                ui_bind_active_style(ui_time_label, &ui_warm_style);
                lv_obj_align(ui_time_label, LV_ALIGN_CENTER, 0, -14);
                ui_timer_beep_button = ui_command_button(ui_stand_cards[i], LV_SYMBOL_BELL,
                    44, 44, LV_ALIGN_RIGHT_MID, -12, -14, UI_CMD_BEEP);
                ui_bind_active_style(ui_timer_beep_button, &ui_primary_style);
                ui_mode_label = lv_label_create(ui_stand_cards[i]);
                lv_obj_set_style_text_font(ui_mode_label, &lv_font_montserrat_16, 0);
                lv_obj_add_style(ui_mode_label, &ui_muted_style, 0);
                lv_obj_align(ui_mode_label, LV_ALIGN_CENTER, 7, 40);
                ui_pomo_dot = lv_obj_create(ui_stand_cards[i]);
                ui_neutral_container(ui_pomo_dot);
                lv_obj_set_size(ui_pomo_dot, 6, 6);
                lv_obj_set_style_radius(ui_pomo_dot, LV_RADIUS_CIRCLE, 0);
                lv_obj_align_to(ui_pomo_dot, ui_mode_label, LV_ALIGN_OUT_LEFT_MID, -8, 0);
                ui_command_button(pane, "-", 95, 56, LV_ALIGN_BOTTOM_LEFT, 20, -20, UI_CMD_MINUS);
                ui_command_button(pane, LV_SYMBOL_REFRESH, 95, 56, LV_ALIGN_BOTTOM_LEFT, 125, -20, UI_CMD_TIMER_RESET);
                ui_timer_toggle_button = ui_command_button(pane, LV_SYMBOL_PLAY, 95, 56,
                    LV_ALIGN_BOTTOM_LEFT, 230, -20, UI_CMD_TIMER_TOGGLE);
                lv_obj_add_style(ui_timer_toggle_button, &ui_primary_style, 0);
                lv_obj_add_style(ui_timer_toggle_button, &ui_primary_pressed_style, LV_STATE_PRESSED);
                ui_command_button(pane, "+", 95, 56, LV_ALIGN_BOTTOM_LEFT, 335, -20, UI_CMD_PLUS);
            } else if (i == CAPSULE_STAND_TIME) {
                ui_clock_label = lv_label_create(ui_stand_cards[i]);
                lv_obj_set_style_text_font(ui_clock_label, &lv_font_montserrat_48, 0);
                ui_bind_active_style(ui_clock_label, &ui_warm_style);
                ui_clock_seconds_label = lv_label_create(ui_stand_cards[i]);
                lv_obj_set_style_text_font(ui_clock_seconds_label, &lv_font_montserrat_24, 0);
                lv_obj_add_style(ui_clock_seconds_label, &ui_accent_style, 0);
                ui_clock_ampm_label = lv_label_create(ui_stand_cards[i]);
                lv_obj_set_style_text_font(ui_clock_ampm_label, &lv_font_montserrat_14, 0);
                lv_obj_add_style(ui_clock_ampm_label, &ui_accent_style, 0);
                ui_clock_date_label = lv_label_create(ui_stand_cards[i]);
                lv_obj_set_style_text_font(ui_clock_date_label, &lv_font_montserrat_16, 0);
                lv_obj_add_style(ui_clock_date_label, &ui_muted_style, 0);
                lv_obj_align(ui_clock_date_label, LV_ALIGN_CENTER, 0, 44);
                ui_clock_format_button = ui_command_button(pane, "24H", 128, 56, LV_ALIGN_BOTTOM_LEFT, 20, -20, UI_CMD_CLOCK_FORMAT);
                ui_clock_seconds_button = ui_command_button(pane, "SECONDS", 128, 56, LV_ALIGN_BOTTOM_MID, 0, -20, UI_CMD_CLOCK_SECONDS);
                ui_night_button = ui_command_button(pane, "NIGHT", 128, 56, LV_ALIGN_BOTTOM_RIGHT, -20, -20, UI_CMD_THEME);
                ui_bind_active_style(ui_clock_seconds_button, &ui_primary_style);
                ui_bind_active_style(ui_night_button, &ui_primary_style);
            } else {
                ui_stopwatch_label = lv_label_create(ui_stand_cards[i]);
                lv_obj_set_style_text_font(ui_stopwatch_label, &lv_font_montserrat_48, 0);
                ui_bind_active_style(ui_stopwatch_label, &ui_warm_style);
                lv_obj_align(ui_stopwatch_label, LV_ALIGN_CENTER, 0, -24);
                for (int lap = 0; lap < 3; ++lap) {
                    ui_lap_labels[lap] = lv_label_create(ui_stand_cards[i]);
                    lv_obj_set_style_text_font(ui_lap_labels[lap], &lv_font_montserrat_16, 0);
                    lv_obj_add_style(ui_lap_labels[lap], &ui_chip_style, 0);
                    lv_obj_set_style_pad_hor(ui_lap_labels[lap], 8, 0);
                    lv_obj_set_style_pad_ver(ui_lap_labels[lap], 3, 0);
                    lv_obj_align(ui_lap_labels[lap], LV_ALIGN_CENTER, 0, 20 + lap * 26);
                }
                ui_command_button(pane, LV_SYMBOL_REFRESH, 128, 56, LV_ALIGN_BOTTOM_LEFT, 20, -20, UI_CMD_STOPWATCH_RESET);
                ui_stopwatch_toggle_button = ui_command_button(pane, LV_SYMBOL_PLAY, 128, 56,
                    LV_ALIGN_BOTTOM_MID, 0, -20, UI_CMD_STOPWATCH_TOGGLE);
                lv_obj_add_style(ui_stopwatch_toggle_button, &ui_primary_style, 0);
                lv_obj_add_style(ui_stopwatch_toggle_button, &ui_primary_pressed_style, LV_STATE_PRESSED);
                ui_bind_active_style(ui_stopwatch_toggle_button, &ui_primary_style);
                ui_command_button(pane, "LAP", 128, 56, LV_ALIGN_BOTTOM_RIGHT, -20, -20, UI_CMD_STOPWATCH_LAP);
            }
        }
        ui_stand_bubble(ui_timer_panel);
        lv_obj_add_event_cb(ui_timer_panel, ui_stand_gesture_event, LV_EVENT_ALL, NULL);
    }
    {
        static const char *media_labels[UI_MEDIA_CONTROL_COUNT] = {
            LV_SYMBOL_VOLUME_MID " -", LV_SYMBOL_VOLUME_MAX " +",
            LV_SYMBOL_PREV, LV_SYMBOL_NEXT,
            LV_SYMBOL_MUTE, ""
        };
        static const enum ui_command_type media_commands[UI_MEDIA_CONTROL_COUNT] = {
            UI_CMD_VOLUME_DOWN, UI_CMD_VOLUME_UP,
            UI_CMD_PREVIOUS, UI_CMD_NEXT,
            UI_CMD_MUTE, UI_CMD_PLAY_PAUSE
        };
        static const int16_t btn_x[2] = { 20, 234 };
        static const int16_t btn_y[3] = { 65, 151, 237 };
        lv_obj_t *panels[2] = { ui_remote_left_panel, ui_remote_right_panel };
        for (int side = 0; side < 2; ++side) {
            ui_remote_desk_labels[side] = lv_label_create(panels[side]);
            lv_obj_clear_flag(ui_remote_desk_labels[side], LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_text_font(ui_remote_desk_labels[side], &lv_font_montserrat_14, 0);
            lv_obj_add_style(ui_remote_desk_labels[side], &ui_muted_style, 0);
            lv_label_set_text(ui_remote_desk_labels[side], "DESK " LV_SYMBOL_BULLET " 02");
            lv_obj_set_pos(ui_remote_desk_labels[side], 24, 20);

            ui_remote_bt_badges[side] = lv_obj_create(panels[side]);
            ui_neutral_container(ui_remote_bt_badges[side]);
            lv_obj_clear_flag(ui_remote_bt_badges[side], LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_size(ui_remote_bt_badges[side], 140, 28);
            lv_obj_set_style_radius(ui_remote_bt_badges[side], LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(ui_remote_bt_badges[side], 1, 0);
            lv_obj_set_style_border_color(ui_remote_bt_badges[side], lv_color_hex(ui_themes[0].button_border), 0);
            lv_obj_set_style_bg_color(ui_remote_bt_badges[side], lv_color_hex(ui_themes[0].button), 0);
            lv_obj_set_pos(ui_remote_bt_badges[side], 450 - 20 - 140, 16);

            ui_remote_bt_labels[side] = lv_label_create(ui_remote_bt_badges[side]);
            lv_obj_clear_flag(ui_remote_bt_labels[side], LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_text_font(ui_remote_bt_labels[side], &lv_font_montserrat_14, 0);
            lv_label_set_text(ui_remote_bt_labels[side], LV_SYMBOL_BLUETOOTH " OFFLINE");
            lv_obj_center(ui_remote_bt_labels[side]);

            for (int index = 0; index < UI_MEDIA_CONTROL_COUNT; ++index) {
                int col = index % 2;
                int row = index / 2;
                ui_remote_buttons[side][index] = ui_command_button(
                    panels[side], media_labels[index], 196, 74, LV_ALIGN_TOP_LEFT,
                    btn_x[col], btn_y[row], media_commands[index]);
                lv_obj_set_style_radius(ui_remote_buttons[side][index], 16, 0);
                if (index == 4) {
                    ui_bind_active_style(ui_remote_buttons[side][index], &ui_primary_style);
                } else if (index == 5) {
                    lv_obj_add_style(ui_remote_buttons[side][index], &ui_primary_style, 0);
                    ui_remote_play_labels[side] = lv_label_create(ui_remote_buttons[side][index]);
                    lv_obj_clear_flag(ui_remote_play_labels[side], LV_OBJ_FLAG_CLICKABLE);
                    lv_label_set_text(ui_remote_play_labels[side], ui_media_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
                    lv_obj_center(ui_remote_play_labels[side]);
                }
            }

            ui_remote_feedback_labels[side] = lv_label_create(panels[side]);
            lv_obj_clear_flag(ui_remote_feedback_labels[side], LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_text_font(ui_remote_feedback_labels[side], &lv_font_montserrat_14, 0);
            lv_obj_add_style(ui_remote_feedback_labels[side], &ui_accent_style, 0);
            lv_label_set_text(ui_remote_feedback_labels[side], "BLUETOOTH DISCONNECTED");
            lv_obj_align(ui_remote_feedback_labels[side], LV_ALIGN_BOTTOM_MID, 0, -18);
        }
    }
    screen = ui_record_panel;
    ui_memo_card = lv_obj_create(screen);
    ui_neutral_container(ui_memo_card);
    lv_obj_set_size(ui_memo_card, 410, 210);
    lv_obj_set_style_radius(ui_memo_card, 24, 0);
    lv_obj_set_style_border_width(ui_memo_card, 1, 0);
    lv_obj_add_style(ui_memo_card, &ui_recessed_style, 0);
    lv_obj_set_pos(ui_memo_card, 20, 20);

    ui_memo_filename_label = lv_label_create(ui_memo_card);
    lv_obj_set_width(ui_memo_filename_label, 270);
    lv_label_set_long_mode(ui_memo_filename_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(ui_memo_filename_label, &lv_font_montserrat_16, 0);
    lv_obj_add_style(ui_memo_filename_label, &ui_strong_style, 0);
    lv_label_set_text(ui_memo_filename_label, "NO TF CARD " LV_SYMBOL_BULLET " RAM");
    lv_obj_set_pos(ui_memo_filename_label, 18, 16);

    ui_memo_wave_container = lv_obj_create(ui_memo_card);
    ui_neutral_container(ui_memo_wave_container);
    lv_obj_set_size(ui_memo_wave_container, 76, 26);
    lv_obj_set_style_radius(ui_memo_wave_container, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ui_memo_wave_container, 1, 0);
    lv_obj_set_style_bg_color(ui_memo_wave_container, lv_color_hex(ui_themes[0].button), 0);
    lv_obj_set_style_border_color(ui_memo_wave_container, lv_color_hex(ui_themes[0].button_border), 0);
    lv_obj_set_pos(ui_memo_wave_container, 410 - 18 - 76, 12);
    for (int b = 0; b < 7; ++b) {
        ui_memo_wave_bars[b] = lv_obj_create(ui_memo_wave_container);
        ui_neutral_container(ui_memo_wave_bars[b]);
        lv_obj_clear_flag(ui_memo_wave_bars[b], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(ui_memo_wave_bars[b], 2, 0);
        lv_obj_set_size(ui_memo_wave_bars[b], 4, 6);
        lv_obj_set_pos(ui_memo_wave_bars[b], 8 + b * 9, 10);
    }

    ui_memo_time_label = lv_label_create(ui_memo_card);
    lv_obj_set_style_text_font(ui_memo_time_label, &lv_font_montserrat_48, 0);
    lv_obj_add_style(ui_memo_time_label, &ui_strong_style, 0);
    lv_label_set_text(ui_memo_time_label, "00:00");
    lv_obj_set_pos(ui_memo_time_label, 18, 70);

    ui_memo_limit_label = lv_label_create(ui_memo_card);
    lv_obj_set_style_text_font(ui_memo_limit_label, &lv_font_montserrat_16, 0);
    lv_obj_add_style(ui_memo_limit_label, &ui_muted_style, 0);
    lv_label_set_text(ui_memo_limit_label, "00:30");
    lv_obj_set_pos(ui_memo_limit_label, 410 - 18 - 50, 94);

    ui_memo_progress_track = lv_obj_create(ui_memo_card);
    ui_neutral_container(ui_memo_progress_track);
    lv_obj_clear_flag(ui_memo_progress_track, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(ui_memo_progress_track, 374, 22);
    lv_obj_set_style_radius(ui_memo_progress_track, 11, 0);
    lv_obj_set_style_border_width(ui_memo_progress_track, 1, 0);
    lv_obj_set_style_bg_color(ui_memo_progress_track, lv_color_hex(ui_themes[0].button), 0);
    lv_obj_set_style_border_color(ui_memo_progress_track, lv_color_hex(ui_themes[0].button_border), 0);
    lv_obj_set_pos(ui_memo_progress_track, 18, 168);

    ui_memo_progress_bar = lv_obj_create(ui_memo_progress_track);
    ui_neutral_container(ui_memo_progress_bar);
    lv_obj_clear_flag(ui_memo_progress_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_style(ui_memo_progress_bar, &ui_primary_style, 0);
    lv_obj_set_size(ui_memo_progress_bar, 0, 22);
    lv_obj_set_style_radius(ui_memo_progress_bar, 11, 0);
    lv_obj_set_style_border_width(ui_memo_progress_bar, 0, 0);
    lv_obj_set_pos(ui_memo_progress_bar, 0, 0);

    ui_memo_progress_knob = lv_obj_create(ui_memo_progress_track);
    ui_neutral_container(ui_memo_progress_knob);
    lv_obj_clear_flag(ui_memo_progress_knob, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(ui_memo_progress_knob, 12, 12);
    lv_obj_set_style_radius(ui_memo_progress_knob, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ui_memo_progress_knob, lv_color_hex(0xffffff), 0);
    lv_obj_set_pos(ui_memo_progress_knob, 0, 5);

    ui_memo_folder_button = ui_command_button(screen, LV_SYMBOL_DIRECTORY, 95, 64, LV_ALIGN_TOP_LEFT, 20, 306, UI_CMD_TF_NEXT);
    lv_obj_set_style_radius(ui_memo_folder_button, 16, 0);
    ui_bind_active_style(ui_memo_folder_button, &ui_primary_style);

    ui_memo_play_button = ui_command_button(screen, "", 95, 64, LV_ALIGN_TOP_LEFT, 125, 306, UI_CMD_TF_PLAY);
    lv_obj_set_style_radius(ui_memo_play_button, 16, 0);
    ui_memo_play_icon = lv_label_create(ui_memo_play_button);
    lv_obj_clear_flag(ui_memo_play_icon, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(ui_memo_play_icon, LV_SYMBOL_PLAY);
    lv_obj_center(ui_memo_play_icon);

    ui_memo_rec_button = ui_command_button(screen, "", 95, 64, LV_ALIGN_TOP_LEFT, 230, 306, UI_CMD_RECORD);
    lv_obj_set_style_radius(ui_memo_rec_button, 16, 0);
    lv_obj_add_style(ui_memo_rec_button, &ui_primary_style, 0);
    ui_bind_active_style(ui_memo_rec_button, &ui_danger_style);
    ui_memo_rec_ring = lv_obj_create(ui_memo_rec_button);
    ui_neutral_container(ui_memo_rec_ring);
    lv_obj_clear_flag(ui_memo_rec_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(ui_memo_rec_ring, 22, 22);
    lv_obj_set_style_radius(ui_memo_rec_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ui_memo_rec_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui_memo_rec_ring, 2, 0);
    lv_obj_set_style_border_color(ui_memo_rec_ring, lv_color_hex(0xffffff), 0);
    lv_obj_center(ui_memo_rec_ring);

    ui_memo_rec_core = lv_obj_create(ui_memo_rec_ring);
    ui_neutral_container(ui_memo_rec_core);
    lv_obj_clear_flag(ui_memo_rec_core, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(ui_memo_rec_core, 8, 8);
    lv_obj_set_style_radius(ui_memo_rec_core, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ui_memo_rec_core, lv_color_hex(0xffffff), 0);
    lv_obj_center(ui_memo_rec_core);

    ui_memo_delete_button = ui_command_button(screen, "", 95, 64, LV_ALIGN_TOP_LEFT, 335, 306, UI_CMD_TF_DELETE);
    lv_obj_set_style_radius(ui_memo_delete_button, 16, 0);
    ui_bind_active_style(ui_memo_delete_button, &ui_danger_style);
    ui_memo_delete_label = lv_label_create(ui_memo_delete_button);
    lv_obj_clear_flag(ui_memo_delete_label, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(ui_memo_delete_label, LV_SYMBOL_TRASH);
    lv_obj_center(ui_memo_delete_label);

    /* 各页面常驻录音指示：左上角呼吸红点（不占布局、不拦触摸），扣下熄屏时隐藏。 */
    ui_rec_badge = lv_obj_create(lv_scr_act());
    ui_neutral_container(ui_rec_badge);
    lv_obj_set_size(ui_rec_badge, 14, 14);
    lv_obj_set_style_radius(ui_rec_badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ui_rec_badge, lv_color_hex(0xF43F5E), 0);
    lv_obj_set_style_bg_opa(ui_rec_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui_rec_badge, 2, 0);
    lv_obj_set_style_border_color(ui_rec_badge, lv_color_hex(0xffffff), 0);
    lv_obj_set_pos(ui_rec_badge, 6, 6);
    lv_obj_add_flag(ui_rec_badge, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_rec_badge, LV_OBJ_FLAG_CLICKABLE);

    {
        lv_anim_t pulse;
        lv_anim_init(&pulse);
        lv_anim_set_var(&pulse, ui_rec_badge);
        lv_anim_set_exec_cb(&pulse, ui_rec_pulse_exec);
        lv_anim_set_values(&pulse, LV_OPA_30, LV_OPA_COVER);
        lv_anim_set_time(&pulse, 700);
        lv_anim_set_playback_time(&pulse, 700);
        lv_anim_set_repeat_count(&pulse, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&pulse);
    }
    {
        time_t now = time(NULL);
        struct tm *rtc_time = localtime(&now);
        if (!rtc_time || rtc_time->tm_year + 1900 < 2026) {
            if (set_date(2026, 11, 8) != RT_EOK || set_time(10, 42, 0) != RT_EOK)
                rt_kprintf("[%s] RTC default initialization failed\n", APP_NAME);
            else
                rt_kprintf("[%s] RTC initialized to 2026-11-08 10:42:00\n", APP_NAME);
        }
    }
    rt_kprintf("[%s] UI ready: interactive timer, touch enabled by SDK input driver\n", APP_NAME);
}

static void ui_stand_track_exec(void *obj, int32_t x)
{
    lv_obj_set_x(obj, x);
}

static void ui_bind_active_style(lv_obj_t *obj, lv_style_t *style)
{
    lv_state_t state = style == &ui_danger_style ? LV_STATE_USER_1 : LV_STATE_CHECKED;
    lv_obj_add_style(obj, style, state);
}

static void ui_set_active(lv_obj_t *obj, lv_style_t *style, bool active)
{
    lv_state_t state = style == &ui_danger_style ? LV_STATE_USER_1 : LV_STATE_CHECKED;
    if (!obj || lv_obj_has_state(obj, state) == active) return;
    if (active) lv_obj_add_state(obj, state);
    else lv_obj_clear_state(obj, state);
}

static bool shown_dark;
static void ui_stand_update(void)
{
    static int shown_mode = CAPSULE_STAND_TIME;
    char text[48];
    uint32_t seconds = ui_seconds_snapshot;
    /* 只有 LVGL 线程改主题：命令线程只置请求位，避免旧快照把夜模式改回去。 */
    if (ui_night_toggle_requested) {
        ui_night_toggle_requested = false;
        ui_theme_apply(!ui_night_mode);
        ui_night_mode_snapshot = ui_night_mode;
    }
    if (shown_dark != ui_night_mode_snapshot) {
        shown_dark = ui_night_mode_snapshot;
        ui_theme_apply(shown_dark);
        ui_set_active(ui_time_label, &ui_warm_style, shown_dark);
        ui_set_active(ui_clock_label, &ui_warm_style, shown_dark);
        ui_set_active(ui_stopwatch_label, &ui_warm_style, shown_dark);
    }
    if (shown_mode != ui_stand_snapshot) {
        lv_anim_t animation;
        shown_mode = ui_stand_snapshot;
        lv_anim_del(ui_stand_track, ui_stand_track_exec);
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, ui_stand_track);
        lv_anim_set_exec_cb(&animation, ui_stand_track_exec);
        lv_anim_set_values(&animation, lv_obj_get_x(ui_stand_track), -shown_mode * ui_page_width);
        lv_anim_set_time(&animation, 220);
        lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
        lv_anim_start(&animation);
    }
    for (int i = 0; i < CAPSULE_STAND_MODE_COUNT; ++i)
        ui_set_active(ui_stand_tabs[i], &ui_active_style, i == shown_mode);
    lv_snprintf(text, sizeof text, "%02u:%02u", (unsigned)(seconds / 60), (unsigned)(seconds % 60));
    lv_label_set_text(ui_time_label, text);
    lv_obj_align(ui_time_label, LV_ALIGN_CENTER, 0, -14);
    lv_label_set_text(ui_mode_label, ui_timer_state_snapshot == CAPSULE_POMO_RUNNING ? "RUNNING" :
        ui_timer_state_snapshot == CAPSULE_POMO_PAUSED ? "PAUSED" :
        ui_timer_state_snapshot == CAPSULE_POMO_DONE ? "COMPLETED" : ui_timer_reset_snapshot ? "RESET" : "READY");
    lv_obj_align(ui_mode_label, LV_ALIGN_CENTER, 7, 40);
    lv_obj_align_to(ui_pomo_dot, ui_mode_label, LV_ALIGN_OUT_LEFT_MID, -8, 0);
    lv_obj_set_style_bg_color(ui_pomo_dot, lv_color_hex(ui_timer_state_snapshot == CAPSULE_POMO_RUNNING ?
        0x0284c7 : ui_themes[shown_dark].muted), 0);
    lv_label_set_text(lv_obj_get_child(ui_timer_toggle_button, 0),
        ui_timer_state_snapshot == CAPSULE_POMO_RUNNING ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    ui_set_active(ui_timer_beep_button, &ui_primary_style, ui_beep_snapshot);
    {
        time_t now = time(NULL);
        struct tm *clock = localtime(&now);
        if (clock) {
        unsigned hour = clock->tm_hour;
        if (!ui_clock_24h_snapshot) hour = hour % 12 ? hour % 12 : 12;
        lv_snprintf(text, sizeof text, "%02u:%02u", hour, (unsigned)clock->tm_min);
        lv_label_set_text(ui_clock_label, text);
        lv_obj_align(ui_clock_label, LV_ALIGN_CENTER, 0, -14);
        lv_obj_update_layout(ui_clock_label);
        lv_snprintf(text, sizeof text, ":%02u", (unsigned)clock->tm_sec);
        lv_label_set_text(ui_clock_seconds_label, text);
        /* 24 号字基线比 48 号字高 4 px，负偏移让 :SS 底部与 HH:MM 基线对齐。 */
        lv_obj_align_to(ui_clock_seconds_label, ui_clock_label, LV_ALIGN_OUT_RIGHT_BOTTOM,
            4, lv_font_montserrat_24.base_line - lv_font_montserrat_48.base_line);
        lv_obj_update_layout(ui_clock_seconds_label);
        if (ui_clock_seconds_snapshot) lv_obj_clear_flag(ui_clock_seconds_label, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(ui_clock_seconds_label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(ui_clock_ampm_label, clock->tm_hour >= 12 ? "PM" : "AM");
        lv_obj_align_to(ui_clock_ampm_label, ui_clock_label, LV_ALIGN_OUT_RIGHT_TOP,
            ui_clock_seconds_snapshot ? lv_obj_get_width(ui_clock_seconds_label) + 8 : 4, -4);
        if (ui_clock_24h_snapshot) lv_obj_add_flag(ui_clock_ampm_label, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(ui_clock_ampm_label, LV_OBJ_FLAG_HIDDEN);
        static const char *days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
        static const char *months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
        lv_snprintf(text, sizeof text, "%s " LV_SYMBOL_BULLET " %s %02u", days[clock->tm_wday],
            months[clock->tm_mon], (unsigned)clock->tm_mday);
        lv_label_set_text(ui_clock_date_label, text);
    }
    }
    lv_label_set_text(lv_obj_get_child(ui_clock_format_button, 0), ui_clock_24h_snapshot ? "24H" : "12H");
    ui_set_active(ui_clock_seconds_button, &ui_primary_style, ui_clock_seconds_snapshot);
    ui_set_active(ui_night_button, &ui_primary_style, shown_dark);
    ui_set_active(ui_stopwatch_toggle_button, &ui_primary_style, ui_stopwatch_running_snapshot);
    uint32_t tenths = ui_stopwatch_tenths_snapshot;
    lv_snprintf(text, sizeof text, "%02u:%02u.%u", (unsigned)(tenths / 600),
        (unsigned)(tenths / 10 % 60), (unsigned)(tenths % 10));
    lv_label_set_text(ui_stopwatch_label, text);
    lv_obj_align(ui_stopwatch_label, LV_ALIGN_CENTER, 0, -24);
    lv_label_set_text(lv_obj_get_child(ui_stopwatch_toggle_button, 0),
        ui_stopwatch_running_snapshot ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    for (int i = 0; i < 3; ++i) {
        if (i == 0 && !ui_lap_count_snapshot) lv_label_set_text(ui_lap_labels[i], "NO LAP");
        else if ((unsigned)i < ui_lap_count_snapshot) {
            tenths = ui_laps_snapshot[i];
            lv_snprintf(text, sizeof text, "LAP %u " LV_SYMBOL_BULLET " %02u:%02u.%u",
                (unsigned)(ui_lap_count_snapshot - i), (unsigned)(tenths / 600),
                (unsigned)(tenths / 10 % 60), (unsigned)(tenths % 10));
            lv_label_set_text(ui_lap_labels[i], text);
        }
        if (i == 0 || (unsigned)i < ui_lap_count_snapshot) lv_obj_clear_flag(ui_lap_labels[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(ui_lap_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void ui_update(void)
{
    if (!ui_page) return;
    if (ui_page_hidden && ui_pose_snapshot != CAPSULE_POSE_FACE_DOWN) {
        lv_obj_clear_flag(ui_page, LV_OBJ_FLAG_HIDDEN);
        ui_page_hidden = false;
        ui_display_pose = ui_pose_before_hidden;
    }
    if (ui_pose_snapshot != CAPSULE_POSE_UNKNOWN && ui_pose_snapshot != ui_display_pose)
        ui_start_transition(ui_pose_snapshot);
    {
        if (ui_pose_snapshot == CAPSULE_POSE_FACE_DOWN && !ui_display_blanked) {
            uint8_t brightness = 0;
            if (ui_lcd_device && rt_device_control(ui_lcd_device, RTGRAPHIC_CTRL_SET_BRIGHTNESS, &brightness) == RT_EOK)
                ui_display_blanked = true;
        } else if (ui_pose_snapshot != CAPSULE_POSE_FACE_DOWN &&
                   ui_pose_snapshot != CAPSULE_POSE_UNKNOWN && ui_display_blanked) {
            uint8_t brightness = 50;
            if (ui_lcd_device && rt_device_control(ui_lcd_device, RTGRAPHIC_CTRL_SET_BRIGHTNESS, &brightness) == RT_EOK)
                ui_display_blanked = false;
        }
    }
    ui_stand_update();
    if (ui_rec_badge) {
        if (ui_recording_snapshot && !ui_page_hidden)
            lv_obj_clear_flag(ui_rec_badge, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(ui_rec_badge, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui_display_pose == CAPSULE_POSE_FLAT) {
        uint32_t sec = 0;
        if (ui_recording_snapshot) {
            sec = (rt_tick_get_millisecond() - recording_started_ms) / 1000u;
        } else if (current_memo_source == MEMO_SOURCE_TF && ui_tf_ready_snapshot && strcmp(ui_tf_name_snapshot, "No TF file") != 0) {
            sec = ui_selected_duration_snapshot / 1000u;
        } else if (current_memo_source == MEMO_SOURCE_RAM && ram_memo_valid) {
            sec = recording_duration_ms / 1000u;
        } else {
            sec = 0;
        }
        if (sec > 30u) sec = 30u;
        char dur_text[16];
        lv_snprintf(dur_text, sizeof dur_text, "%02u:%02u", (unsigned)(sec / 60u), (unsigned)(sec % 60u));
        lv_label_set_text(ui_memo_time_label, dur_text);

        if (ui_recording_snapshot) {
            lv_label_set_text(ui_memo_filename_label, "RECORDING...");
        } else if (ui_tf_feedback == TF_UI_PLAYBACK_FAILED) {
            lv_label_set_text(ui_memo_filename_label, "PLAYBACK FAILED");
        } else if (ui_tf_feedback == TF_UI_RECORDING_FAILED) {
            lv_label_set_text(ui_memo_filename_label, "MIC UNAVAILABLE");
        } else if (ui_tf_feedback == TF_UI_TRUNCATED) {
            lv_label_set_text(ui_memo_filename_label, "RECORDING TRUNCATED");
        } else if (current_memo_source == MEMO_SOURCE_RAM) {
            if (ram_memo_valid) {
                lv_label_set_text(ui_memo_filename_label, "MEMO_RAM.WAV");
            } else {
                lv_label_set_text(ui_memo_filename_label, ui_tf_ready_snapshot ? "NO TF RECORDINGS" : "NO TF CARD " LV_SYMBOL_BULLET " RAM");
            }
        } else if (!ui_tf_ready_snapshot) {
            lv_label_set_text(ui_memo_filename_label, "NO TF CARD " LV_SYMBOL_BULLET " RAM");
        } else if (ui_tf_feedback == TF_UI_NO_RECORDING || strcmp(ui_tf_name_snapshot, "No TF file") == 0) {
            lv_label_set_text(ui_memo_filename_label, "NO TF RECORDINGS");
        } else {
            char upper_name[32];
            size_t n = 0;
            while (ui_tf_name_snapshot[n] && n < sizeof(upper_name) - 1) {
                char c = ui_tf_name_snapshot[n];
                if (c >= 'a' && c <= 'z') c -= 32;
                upper_name[n] = c;
                n++;
            }
            upper_name[n] = '\0';
            const char *display_name = upper_name[0] == '/' ? upper_name + 1 : upper_name;
            lv_label_set_text(ui_memo_filename_label, display_name);
        }

        bool wave_active = ui_recording_snapshot || (ui_playback_state_snapshot == UI_PLAYBACK_PLAYING);
        if (wave_active) {
            uint32_t tick = rt_tick_get_millisecond() / 70u;
            for (int b = 0; b < 7; ++b) {
                uint8_t h = (uint8_t)(4 + ((tick * (b + 3) + b * 5) % 17));
                lv_obj_set_height(ui_memo_wave_bars[b], h);
                lv_obj_set_y(ui_memo_wave_bars[b], 20 - h);
                lv_obj_set_style_bg_color(ui_memo_wave_bars[b],
                    ui_recording_snapshot ? lv_color_hex(0xF43F5E) : lv_color_hex(ui_themes[shown_dark].accent), 0);
            }
        } else {
            static const uint8_t idle_h[7] = { 4, 6, 4, 7, 5, 6, 4 };
            for (int b = 0; b < 7; ++b) {
                lv_obj_set_height(ui_memo_wave_bars[b], idle_h[b]);
                lv_obj_set_y(ui_memo_wave_bars[b], 20 - idle_h[b]);
                lv_obj_set_style_bg_color(ui_memo_wave_bars[b], lv_color_hex(ui_themes[shown_dark].muted), 0);
            }
        }

        uint32_t pct = 0;
        if (ui_recording_snapshot) {
            pct = (sec * 100u) / 30u;
        } else if (ui_playback_state_snapshot == UI_PLAYBACK_PLAYING) {
            pct = ui_playback_progress_snapshot;
        } else if (sec > 0) {
            pct = (sec * 100u) / 30u;
        }
        if (pct > 100u) pct = 100u;
        int32_t fill_w = (374 * (int32_t)pct) / 100;
        lv_obj_set_width(ui_memo_progress_bar, fill_w);
        int32_t knob_x = fill_w > 12 ? fill_w - 6 : 0;
        if (knob_x > 374 - 12) knob_x = 374 - 12;
        lv_obj_set_pos(ui_memo_progress_knob, knob_x, 5);

        ui_set_active(ui_memo_folder_button, &ui_primary_style, ui_tf_ready_snapshot);
        lv_label_set_text(ui_memo_play_icon, playback_worker_active ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY);
        ui_set_active(ui_memo_rec_button, &ui_danger_style, ui_recording_snapshot);
        if (ui_tf_delete_confirm_snapshot) {
            lv_label_set_text(ui_memo_delete_label, "CONFIRM?");
            ui_set_active(ui_memo_delete_button, &ui_danger_style, true);
        } else {
            lv_label_set_text(ui_memo_delete_label, LV_SYMBOL_TRASH);
            ui_set_active(ui_memo_delete_button, &ui_danger_style, false);
        }
    }

    for (int side = 0; side < 2; ++side) {
        if (ui_ble_connected_snapshot) {
            lv_obj_set_style_bg_color(ui_remote_bt_badges[side], lv_color_hex(ui_themes[shown_dark].active), 0);
            lv_obj_set_style_border_color(ui_remote_bt_badges[side], lv_color_hex(ui_themes[shown_dark].active_border), 0);
            lv_obj_set_style_text_color(ui_remote_bt_labels[side], lv_color_hex(shown_dark ? 0x38bdf8 : 0x0369a1), 0);
            lv_label_set_text(ui_remote_bt_labels[side], LV_SYMBOL_BLUETOOTH " CONNECTED");
        } else {
            lv_obj_set_style_bg_color(ui_remote_bt_badges[side], lv_color_hex(ui_themes[shown_dark].button), 0);
            lv_obj_set_style_border_color(ui_remote_bt_badges[side], lv_color_hex(ui_themes[shown_dark].button_border), 0);
            lv_obj_set_style_text_color(ui_remote_bt_labels[side], lv_color_hex(ui_themes[shown_dark].muted), 0);
            lv_label_set_text(ui_remote_bt_labels[side], LV_SYMBOL_BLUETOOTH " OFFLINE");
        }
        for (int i = 0; i < UI_MEDIA_CONTROL_COUNT; ++i) {
            if (!ui_ble_connected_snapshot) {
                lv_obj_add_state(ui_remote_buttons[side][i], LV_STATE_DISABLED);
                lv_obj_set_style_opa(ui_remote_buttons[side][i], LV_OPA_40, 0);
            } else {
                lv_obj_clear_state(ui_remote_buttons[side][i], LV_STATE_DISABLED);
                lv_obj_set_style_opa(ui_remote_buttons[side][i], LV_OPA_COVER, 0);
            }
        }
        ui_set_active(ui_remote_buttons[side][4], &ui_primary_style, ui_media_muted && ui_ble_connected_snapshot);
        lv_label_set_text(ui_remote_play_labels[side], ui_media_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);

        uint32_t now = rt_tick_get_millisecond();
        int32_t remaining = (int32_t)(ui_media_feedback_expiry - now);
        if (remaining > 0 && remaining <= 1500 && ui_media_feedback_snapshot[0]) {
            lv_label_set_text(ui_remote_feedback_labels[side], (const char *)ui_media_feedback_snapshot);
        } else {
            lv_label_set_text(ui_remote_feedback_labels[side],
                              ui_ble_connected_snapshot ? "MEDIA CONTROLLER" : "BLUETOOTH DISCONNECTED");
        }
    }
}

static void ui_pump(void)
{
    rt_uint32_t delay_ms;
    ui_update();
    delay_ms = lv_task_handler();
    rt_thread_mdelay(delay_ms > 5 ? delay_ms : 5);
}

static void ui_task_entry(void *parameter)
{
    (void)parameter;
    ui_init();
    while (1) ui_pump();
}

static void ui_start(void)
{
    rt_thread_t thread = rt_thread_create("pomo_ui", ui_task_entry, NULL,
                                          4096, 20, 10);
    if (!thread || rt_thread_startup(thread) != RT_EOK)
    {
        rt_kprintf("[%s] UI task start failed\n", APP_NAME);
        return;
    }
    rt_thread_mdelay(1100);
}
#endif

static int imu_read(int32_t *x, int32_t *y, int32_t *z)
{
    uint8_t bytes[6];
    int16_t raw_x, raw_y, raw_z;
    int64_t magnitude_sq;
    rt_size_t read_bytes;
    /* The SDK transfer takes this recursive mutex too; keep pinmux inside it. */
    if (rt_mutex_take(&imu_bus->lock, RT_WAITING_FOREVER) != RT_EOK) return -1;
    HAL_PIN_Set(PAD_PA40, I2C1_SCL, PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA39, I2C1_SDA, PIN_NOPULL, 1);
    read_bytes = rt_i2c_mem_read(imu_bus, LSM6DSL_ADDR_DEFAULT, 0x28, 8,
                                 bytes, sizeof bytes);
    HAL_PIN_Set(PAD_PA33, I2C1_SDA, PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA37, I2C1_SCL, PIN_NOPULL, 1);
    rt_mutex_release(&imu_bus->lock);
    if (read_bytes != sizeof bytes) return -1;
    raw_x = (int16_t)((uint16_t)bytes[1] << 8 | bytes[0]);
    raw_y = (int16_t)((uint16_t)bytes[3] << 8 | bytes[2]);
    raw_z = (int16_t)((uint16_t)bytes[5] << 8 | bytes[4]);
    *x = capsule_accel_millig(raw_x);
    *y = capsule_accel_millig(raw_y);
    *z = capsule_accel_millig(raw_z);
    magnitude_sq = (int64_t)*x * *x + (int64_t)*y * *y + (int64_t)*z * *z;
    return magnitude_sq >= 400 * 400 && magnitude_sq <= 2500 * 2500 ? 0 : -1;
}

static int imu_init(void)
{
    struct rt_sensor_config config = {0};
    rt_err_t result;

    HAL_PIN_Set(PAD_PA40, I2C1_SCL, PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA39, I2C1_SDA, PIN_NOPULL, 1);
    rt_thread_mdelay(5);
    imu_bus = rt_i2c_bus_device_find(IMU_BUS_NAME);
    if (!imu_bus) return -1;
    config.intf.dev_name = IMU_BUS_NAME;
    config.intf.user_data = (void *)(uintptr_t)LSM6DSL_ADDR_DEFAULT;
    config.irq_pin.pin = RT_PIN_NONE;
    result = rt_hw_lsm6dsl_init(IMU_DRIVER_NAME, &config);
    if (result != RT_EOK)
    {
        rt_kprintf("[%s] IMU init failed (%d), bus=%s addr=0x%02x\n",
                   APP_NAME, result, IMU_BUS_NAME, LSM6DSL_ADDR_DEFAULT);
        return -1;
    }
    imu_device = rt_device_find(IMU_DEVICE_NAME);
    if (!imu_device || rt_device_open(imu_device, RT_DEVICE_FLAG_RDWR) != RT_EOK)
    {
        rt_kprintf("[%s] IMU open failed\n", APP_NAME);
        return -1;
    }
    if (rt_device_control(imu_device, RT_SENSOR_CTRL_SET_RANGE, (void *)2000) != RT_EOK ||
        rt_device_control(imu_device, RT_SENSOR_CTRL_SET_ODR, (void *)100) != RT_EOK)
    {
        rt_kprintf("[%s] IMU configuration failed\n", APP_NAME);
        return -1;
    }
    rt_kprintf("[%s] IMU ready: bus=%s addr=0x%02x sample=%dms\n",
               APP_NAME, IMU_BUS_NAME, LSM6DSL_ADDR_DEFAULT, IMU_SAMPLE_MS);
    tf_library_init(wav_storage, sizeof wav_storage);
    char boot_sel[32];
    if (tf_library_ready() && tf_library_copy_selected(boot_sel, sizeof boot_sel) &&
        strcmp(boot_sel, "No TF file") != 0) {
        current_memo_source = MEMO_SOURCE_TF;
    } else {
        current_memo_source = MEMO_SOURCE_RAM;
    }
    ram_memo_valid = false;
    rt_kprintf("[%s] TF library %s (initial source=%s)\n", APP_NAME,
               tf_library_ready() ? "ready" : "RAM fallback",
               current_memo_source == MEMO_SOURCE_TF ? "TF" : "RAM");
    return 0;
}

static int capsule_init_with_calibration(void)
{
    if (capsule_init(&capsule_box, memo_storage, RECORD_MAX_SAMPLES,
                     wav_storage, sizeof wav_storage) != 0) return -1;
    if (capsule_set_ref(&capsule_box, CAPSULE_POSE_STAND,
                        CALIBRATED_STAND_X, CALIBRATED_STAND_Y, CALIBRATED_STAND_Z) != 0 ||
        capsule_set_ref(&capsule_box, CAPSULE_POSE_FACE_DOWN,
                        CALIBRATED_FACE_X, CALIBRATED_FACE_Y, CALIBRATED_FACE_Z) != 0 ||
        capsule_set_ref(&capsule_box, CAPSULE_POSE_SIDE_LEFT,
                        CALIBRATED_LEFT_X, CALIBRATED_LEFT_Y, CALIBRATED_LEFT_Z) != 0 ||
        capsule_set_ref(&capsule_box, CAPSULE_POSE_SIDE_RIGHT,
                        CALIBRATED_RIGHT_X, CALIBRATED_RIGHT_Y, CALIBRATED_RIGHT_Z) != 0 ||
        capsule_set_ref(&capsule_box, CAPSULE_POSE_FLAT,
                        CALIBRATED_FLAT_X, CALIBRATED_FLAT_Y, CALIBRATED_FLAT_Z) != 0) return -1;
    rt_kprintf("[%s] using saved five-pose calibration\n", APP_NAME);
    return 0;
}

static const int16_t beep_period[16] = {
    0, 11480, 21213, 27716, 30000, 27716, 21213, 11480,
    0, -11480, -21213, -27716, -30000, -27716, -21213, -11480,
};

static void play_beep(void)
{
    audio_parameter_t param = {0};
    int16_t chunk[160];
    audio_client_t client;
    uint32_t flush_ms = 0;
    bool success = true;

    audio_server_set_private_volume(AUDIO_TYPE_LOCAL_MUSIC, 15);
    param.write_bits_per_sample = 16;
    param.write_channnel_num = 1;
    param.write_samplerate = CAPSULE_WAV_RATE;
    param.write_cache_size = 4096;
    client = audio_open(AUDIO_TYPE_LOCAL_MUSIC, AUDIO_TX, &param, NULL, NULL);
    if (!client) {
        rt_kprintf("[%s] beep open failed\n", APP_NAME);
        return;
    }
    for (int i = 0; i < (int)(sizeof chunk / sizeof chunk[0]); ++i)
        chunk[i] = beep_period[i % (sizeof beep_period / sizeof beep_period[0])];

    /* 50 x 10 ms chunks: a 500 ms tone, long enough for the SPK amplifier to settle. */
    for (int i = 0; i < 50 && success; ++i) {
        size_t offset = 0;
        unsigned retries = 0;
        while (offset < sizeof chunk) {
            int written = audio_write(client, (uint8_t *)chunk + offset,
                                      sizeof chunk - offset);
            if (written > 0) {
                offset += (size_t)written;
                retries = 0;
            } else if (written == 0 && retries++ < 100) {
                rt_thread_mdelay(10);
            } else {
                success = false;
                rt_kprintf("[%s] beep write failed (%d), offset=%u\n", APP_NAME,
                           written, (unsigned)offset);
                break;
            }
        }
    }
    if (success && audio_ioctl(client, AUDIO_IOCTL_FLUSH_TIME_MS, &flush_ms) != 0) {
        success = false;
        rt_kprintf("[%s] beep flush query failed\n", APP_NAME);
    }
    if (success) rt_thread_mdelay(flush_ms + 50);
    audio_close(client);
    rt_kprintf("[%s] timer beep %s\n", APP_NAME, success ? "emitted" : "failed");
}

static void beep_worker_entry(void *parameter)
{
    (void)parameter;
    play_beep();
    beep_worker_active = false;
}

static bool start_beep_worker(void)
{
    rt_thread_t thread;
    if (beep_worker_active) return false;
    beep_worker_active = true;
    thread = rt_thread_create("timer_beep", beep_worker_entry, NULL, 3072, 20, 10);
    if (!thread || rt_thread_startup(thread) != RT_EOK) {
        beep_worker_active = false;
        rt_kprintf("[%s] beep thread start failed\n", APP_NAME);
        return false;
    }
    return true;
}

static int audio_callback_record(audio_server_callback_cmt_t cmd, void *userdata, uint32_t reserved)
{
    audio_server_coming_data_t *data;
    uint32_t length, available, copy_length;
    (void)userdata;
    if (cmd != as_callback_cmd_data_coming) return RT_EOK;

    data = (audio_server_coming_data_t *)reserved;
    length = data->data_len & ~1u;
    available = sizeof(capture_storage) - capture_bytes;
    copy_length = length < available ? length : (available & ~1u);
    if (copy_length)
    {
        memcpy(capture_storage + capture_bytes, data->data, copy_length);
        capture_bytes += copy_length;
    }
    if (copy_length != data->data_len) audio_overflow = true;
    return RT_EOK;
}

static int start_recording(void)
{
    tf_delete_pending = false;
    ui_tf_delete_confirm_snapshot = false;
    if (playback_worker_active || beep_worker_active) {
        rt_kprintf("[%s] recording start rejected while audio output is active\n", APP_NAME);
        return -1;
    }
    audio_parameter_t param = {0};
    param.read_bits_per_sample = 16;
    param.read_channnel_num = 1;
    param.read_samplerate = CAPSULE_WAV_RATE;
    param.read_which_mic = AUDIO_MIC0_ONLY;
    param.read_cache_size = AUDIO_CACHE_SIZE;
    param.disable_uplink_agc = 1;
    capture_bytes = 0;
    audio_overflow = false;
    audio_client = audio_open(AUDIO_TYPE_LOCAL_RECORD, AUDIO_RX, &param,
                              audio_callback_record, NULL);
    if (!audio_client)
    {
        ui_tf_feedback = TF_UI_RECORDING_FAILED;
        rt_kprintf("[%s] record open failed\n", APP_NAME);
        return -1;
    }
    if (capsule_record_start(&capsule_box) != 0)
    {
        audio_close(audio_client);
        audio_client = NULL;
        ui_tf_feedback = TF_UI_RECORDING_FAILED;
        rt_kprintf("[%s] record state start failed\n", APP_NAME);
        return -1;
    }
    recording_started_ms = rt_tick_get_millisecond();
    recording_duration_ms = 0;
    recording_persisted = false;
    recording_truncated = false;
    ui_tf_feedback = TF_UI_READY;
    tf_delete_pending = false;
    ui_tf_delete_confirm_snapshot = false;
    return 0;
}

static void stop_recording(void)
{
    size_t samples, offset = 0;
    if (audio_client)
    {
        audio_close(audio_client);
        audio_client = NULL;
    }
    recording_duration_ms = rt_tick_get_millisecond() - recording_started_ms;
    samples = capture_bytes / sizeof(int16_t);
    while (offset < samples)
    {
        size_t count = samples - offset;
        if (count > 320) count = 320;
        capsule_on_pcm(&capsule_box, (const int16_t *)capture_storage + offset, count);
        offset += count;
    }
    recording_truncated = audio_overflow;
    if (audio_overflow)
    {
        const int16_t overflow_sample = 0;
        capsule_on_pcm(&capsule_box, &overflow_sample, 1);
    }
    else
    {
        capsule_record_stop(&capsule_box);
    }
    recording_truncated = recording_truncated || capsule_memo_truncated(&capsule_box) ||
                          capsule_memo_status(&capsule_box) == CAPSULE_MEMO_NOSPACE;
    if ((capsule_memo_status(&capsule_box) == CAPSULE_MEMO_SAVED || recording_truncated) &&
        capsule_wav_length(&capsule_box) > 44 &&
        tf_library_save(capsule_wav_data(&capsule_box), capsule_wav_length(&capsule_box)) == 0) {
        char path[32];
        recording_persisted = true;
        current_memo_source = MEMO_SOURCE_TF;
        ram_memo_valid = false;
        ui_tf_feedback = recording_truncated ? TF_UI_TRUNCATED : TF_UI_READY;
        tf_delete_pending = false;
        ui_tf_delete_confirm_snapshot = false;
        if (tf_library_copy_selected(path, sizeof path))
            rt_kprintf("[%s] recording persisted on TF: %s\n", APP_NAME, path);
    } else {
        recording_persisted = false;
        if (capsule_wav_length(&capsule_box) > 44) {
            current_memo_source = MEMO_SOURCE_RAM;
            ram_memo_valid = true;
        } else {
            ram_memo_valid = false;
        }
        ui_tf_feedback = recording_truncated ? TF_UI_TRUNCATED : TF_UI_READY;
        tf_delete_pending = false;
        ui_tf_delete_confirm_snapshot = false;
        rt_kprintf("[%s] recording kept in RAM; persistent=%d truncated=%d\n",
                   APP_NAME, recording_persisted ? 1 : 0, recording_truncated ? 1 : 0);
    }
    rt_kprintf("[%s] recording stopped: status=%d wav=%lu bytes truncated=%d\n",
               APP_NAME, capsule_memo_status(&capsule_box),
               (unsigned long)capsule_wav_length(&capsule_box),
               capsule_memo_truncated(&capsule_box) ? 1 : 0);
    audio_overflow = false;
}

static void toggle_recording(void)
{
    if (capsule_recording(&capsule_box)) stop_recording();
    else start_recording();
}

struct key_event
{
    button_action_t action;
};

static rt_mq_t key_event_queue;

static void key2_handler(int32_t pin, button_action_t action)
{
    struct key_event event = { action };
    (void)pin;
    if (action == BUTTON_PRESSED || action == BUTTON_RELEASED)
        rt_mq_send(key_event_queue, &event, sizeof(event));
}

static int key2_init(void)
{
    button_cfg_t config = {
        .pin = KEY2_PIN,
        .active_state = BUTTON_ACTIVE_HIGH,
        .mode = PIN_MODE_INPUT,
        .button_handler = key2_handler,
        .debounce_time = 20,
        .name = "key2",
    };
    int32_t id;
    key_event_queue = rt_mq_create("key2", sizeof(struct key_event), 4, RT_IPC_FLAG_FIFO);
    if (!key_event_queue) return -1;
    id = button_init(&config);
    if (id < 0 || button_enable(id) != SF_EOK) return -1;
    return 0;
}

static const char *macro_name(capsule_macro_t macro)
{
    switch (macro)
    {
    case CAPSULE_MACRO_VOLUME_UP: return "VOLUME UP";
    case CAPSULE_MACRO_NEXT: return "NEXT TRACK";
    case CAPSULE_MACRO_SYSTEM_MUTE: return "SYSTEM MUTE";
    default: return "none";
    }
}
static int send_ble_command(ble_hid_command_t command)
{
    int result = ble_hid_send(command);
    if (result != 0)
        rt_kprintf("[%s] BLE control unavailable (%d); pair POMO_MEMO_HID in Bluetooth settings\n",
                   APP_NAME, result);
    return result;
}

static void send_media_command(ble_hid_command_t command, const char *feedback)
{
    uint32_t now = rt_tick_get_millisecond();
    if (!ui_ble_connected_snapshot) {
        rt_snprintf((char *)ui_media_feedback_snapshot, sizeof ui_media_feedback_snapshot, "%s", "BLUETOOTH DISCONNECTED");
        ui_media_feedback_expiry = now + 1500;
        return;
    }
    int res = send_ble_command(command);
    if (res != 0) {
        rt_snprintf((char *)ui_media_feedback_snapshot, sizeof ui_media_feedback_snapshot, "%s", "BLUETOOTH DISCONNECTED");
        ui_media_feedback_expiry = now + 1500;
        return;
    }
    if (command == BLE_HID_MUTE) {
        ui_media_muted = !ui_media_muted;
        feedback = ui_media_muted ? "MUTED" : "UNMUTED";
    } else if (command == BLE_HID_PLAY_PAUSE) {
        ui_media_playing = !ui_media_playing;
        feedback = ui_media_playing ? "PLAYING" : "PAUSED";
    }
    if (feedback) {
        rt_snprintf((char *)ui_media_feedback_snapshot, sizeof ui_media_feedback_snapshot, "%s", feedback);
        ui_media_feedback_expiry = now + 1500;
    }
}

static void send_ble_macro(capsule_macro_t macro)
{
    switch (macro) {
    case CAPSULE_MACRO_VOLUME_UP:
        send_media_command(BLE_HID_VOLUME_UP, "VOL + TRIGGERED");
        break;
    case CAPSULE_MACRO_NEXT:
        send_media_command(BLE_HID_NEXT, "NEXT TRIGGERED");
        break;
    case CAPSULE_MACRO_SYSTEM_MUTE:
        send_media_command(BLE_HID_MUTE, NULL);
        break;
    default: break;
    }
}

static void handle_key_event(const struct key_event *event)
{
    uint32_t now = rt_tick_get_millisecond();
    if (event->action == BUTTON_PRESSED)
    {
        if (capsule_recording(&capsule_box))
        {
            stop_recording();
        }
        else if (capsule_desk(&capsule_box) &&
                 (capsule_pose(&capsule_box) == CAPSULE_POSE_SIDE_LEFT ||
                  capsule_pose(&capsule_box) == CAPSULE_POSE_SIDE_RIGHT))
        {
            capsule_on_key_down(&capsule_box, now);
        }
        else
        {
            start_recording();
        }
    }
    else if (event->action == BUTTON_RELEASED)
    {
        uint32_t sequence = capsule_macro_seq(&capsule_box);
        capsule_on_key_up(&capsule_box, now);
        if (capsule_macro_seq(&capsule_box) != sequence) {
            capsule_macro_t macro = capsule_last_macro(&capsule_box);
            rt_kprintf("[%s] macro=%s seq=%lu\n", APP_NAME,
                       macro_name(macro), (unsigned long)capsule_macro_seq(&capsule_box));
            send_ble_macro(macro);
        }
    }
}

static void playback_thread_entry(void *parameter)
{
    int result;
    char sel_path[32];
    (void)parameter;
    if (current_memo_source == MEMO_SOURCE_RAM && ram_memo_valid && capsule_wav_length(&capsule_box) > 44) {
        rt_kprintf("[%s] playback started: latest RAM recording\n", APP_NAME);
        result = tf_library_play_ram(capsule_wav_data(&capsule_box), capsule_wav_length(&capsule_box));
    } else if (current_memo_source == MEMO_SOURCE_TF && tf_library_ready() &&
               tf_library_copy_selected(sel_path, sizeof sel_path) &&
               strcmp(sel_path, "No TF file") != 0) {
        rt_kprintf("[%s] playback started: selected TF recording (%s)\n", APP_NAME, sel_path);
        ram_memo_valid = false;
        result = tf_library_play();
    } else if (ram_memo_valid && capsule_wav_length(&capsule_box) > 44) {
        rt_kprintf("[%s] playback started: latest RAM recording\n", APP_NAME);
        result = tf_library_play_ram(capsule_wav_data(&capsule_box), capsule_wav_length(&capsule_box));
    } else {
        rt_kprintf("[%s] playback failed: no recording available\n", APP_NAME);
        result = -1;
    }
    if (result == 0 && tf_library_playback_state() == UI_PLAYBACK_IDLE) {
        playback_worker_state = UI_PLAYBACK_IDLE;
        ui_playback_state_snapshot = UI_PLAYBACK_IDLE;
        ui_playback_progress_snapshot = 0;
    } else if (result == 0) {
        playback_worker_state = UI_PLAYBACK_COMPLETE;
    } else {
        playback_worker_state = UI_PLAYBACK_FAILED;
        ui_tf_feedback = TF_UI_PLAYBACK_FAILED;
    }
    playback_worker_active = false;
    rt_kprintf("[%s] playback %s\n", APP_NAME,
               playback_worker_state == UI_PLAYBACK_IDLE ? "stopped" : (result == 0 ? "complete" : "failed"));
}

static void start_playback(void)
{
    rt_thread_t thread;
    if (playback_worker_active || beep_worker_active) {
        rt_kprintf("[%s] playback request ignored: audio output is active\n", APP_NAME);
        return;
    }
    if (capsule_recording(&capsule_box)) {
        playback_worker_state = UI_PLAYBACK_FAILED;
        ui_playback_progress_snapshot = 0;
        ui_tf_feedback = TF_UI_PLAYBACK_FAILED;
        rt_kprintf("[%s] playback unavailable while recording\n", APP_NAME);
        return;
    }
    if (ui_tf_feedback == TF_UI_PLAYBACK_FAILED) {
        ui_tf_feedback = TF_UI_READY;
    }
    tf_library_playback_prepare();
    playback_worker_active = true;
    playback_worker_state = UI_PLAYBACK_PLAYING;
    ui_playback_progress_snapshot = 0;
    thread = rt_thread_create("play_memo", playback_thread_entry, NULL, 4096, 20, 10);
    if (!thread || rt_thread_startup(thread) != RT_EOK) {
        playback_worker_active = false;
        playback_worker_state = UI_PLAYBACK_FAILED;
        ui_tf_feedback = TF_UI_PLAYBACK_FAILED;
        rt_kprintf("[%s] playback thread start failed\n", APP_NAME);
    }
}

static void handle_ui_commands(void)
{
    struct ui_command command;
    uint32_t now = rt_tick_get_millisecond();
    while (ui_command_queue && rt_mq_recv(ui_command_queue, &command, sizeof(command), 0) == RT_EOK)
    {
        rt_kprintf("[%s] touch command dispatch=%d pose=%d\n", APP_NAME,
                   command.type, capsule_pose(&capsule_box));
        uint32_t minutes;
        switch (command.type)
        {
        case UI_CMD_MODE_POMO:
        case UI_CMD_MODE_TIME:
        case UI_CMD_MODE_STOPWATCH:
            capsule_set_stand_mode(&capsule_box, command.type == UI_CMD_MODE_POMO ? CAPSULE_STAND_POMO :
                command.type == UI_CMD_MODE_TIME ? CAPSULE_STAND_TIME : CAPSULE_STAND_STOPWATCH, now);
            break;
        case UI_CMD_TIMER_TOGGLE:
            capsule_timer_toggle(&capsule_box, now);
            ui_timer_reset_snapshot = false;
            break;
        case UI_CMD_TIMER_RESET:
            capsule_timer_reset(&capsule_box, now);
            ui_timer_reset_snapshot = true;
            beep_pending = false;
            break;
        case UI_CMD_CLOCK_FORMAT: clock_24h = !clock_24h; break;
        case UI_CMD_CLOCK_SECONDS: clock_seconds = !clock_seconds; break;
        case UI_CMD_THEME: ui_night_toggle_requested = true; break;
        case UI_CMD_STOPWATCH_TOGGLE:
            if (stopwatch_running) stopwatch_elapsed_ms += (uint32_t)(now - stopwatch_since_ms);
            else stopwatch_since_ms = now;
            stopwatch_running = !stopwatch_running;
            break;
        case UI_CMD_STOPWATCH_RESET:
            stopwatch_running = false;
            stopwatch_elapsed_ms = 0;
            stopwatch_lap_count = 0;
            break;
        case UI_CMD_STOPWATCH_LAP:
            if (stopwatch_running) {
                stopwatch_laps[2] = stopwatch_laps[1];
                stopwatch_laps[1] = stopwatch_laps[0];
                stopwatch_laps[0] = (uint32_t)((stopwatch_elapsed_ms +
                    (uint32_t)(now - stopwatch_since_ms)) / 100u);
                ++stopwatch_lap_count;
            }
            break;
        case UI_CMD_MINUS:
            minutes = capsule_timer_minutes(&capsule_box);
            if (minutes > CAPSULE_TIMER_MIN_MINUTES) {
                uint32_t step = command.step ? command.step : 1u;
                uint32_t next = minutes - CAPSULE_TIMER_MIN_MINUTES > step ? minutes - step : CAPSULE_TIMER_MIN_MINUTES;
                capsule_set_timer_minutes(&capsule_box, next, now);
            }
            break;
        case UI_CMD_PLUS:
            minutes = capsule_timer_minutes(&capsule_box);
            if (minutes < CAPSULE_TIMER_MAX_MINUTES) {
                uint32_t step = command.step ? command.step : 1u;
                uint32_t room = CAPSULE_TIMER_MAX_MINUTES - minutes;
                uint32_t next = step < room ? minutes + step : CAPSULE_TIMER_MAX_MINUTES;
                capsule_set_timer_minutes(&capsule_box, next, now);
            }
            break;
        case UI_CMD_BEEP:
            capsule_set_beep_enabled(&capsule_box, !capsule_beep_enabled(&capsule_box));
            break;
        case UI_CMD_RECORD:
            tf_delete_pending = false;
            ui_tf_delete_confirm_snapshot = false;
            toggle_recording();
            break;
        case UI_CMD_VOLUME_UP:
            send_media_command(BLE_HID_VOLUME_UP, "VOL + TRIGGERED");
            break;
        case UI_CMD_VOLUME_DOWN:
            send_media_command(BLE_HID_VOLUME_DOWN, "VOL - TRIGGERED");
            break;
        case UI_CMD_MUTE:
            send_media_command(BLE_HID_MUTE, NULL);
            break;
        case UI_CMD_PREVIOUS:
            send_media_command(BLE_HID_PREVIOUS, "PREV TRIGGERED");
            break;
        case UI_CMD_PLAY_PAUSE:
            send_media_command(BLE_HID_PLAY_PAUSE, NULL);
            break;
        case UI_CMD_NEXT:
            send_media_command(BLE_HID_NEXT, "NEXT TRIGGERED");
            break;
        case UI_CMD_TF_NEXT:
            if (playback_worker_active || capsule_recording(&capsule_box)) {
                rt_kprintf("[%s] FILE change rejected while audio is active\n", APP_NAME);
                break;
            }
            tf_delete_pending = false;
            ui_tf_delete_confirm_snapshot = false;
            recording_truncated = false;
            if (tf_library_next() != 0) {
                ui_tf_feedback = tf_library_ready() ? TF_UI_NO_RECORDING : TF_UI_NO_CARD;
                rt_kprintf("[%s] FILE unavailable: insert TF card or save a recording first\n", APP_NAME);
            } else {
                current_memo_source = MEMO_SOURCE_TF;
                ui_tf_feedback = TF_UI_READY;
                rt_kprintf("[%s] FILE selected\n", APP_NAME);
            }
            break;
        case UI_CMD_TF_PLAY:
            tf_delete_pending = false;
            ui_tf_delete_confirm_snapshot = false;
            if (capsule_recording(&capsule_box)) break;
            if (playback_worker_active) {
                tf_library_playback_stop();
            } else {
                start_playback();
            }
            break;
        case UI_CMD_TF_DELETE:
            if (playback_worker_active || capsule_recording(&capsule_box)) {
                tf_delete_pending = false;
                ui_tf_delete_confirm_snapshot = false;
                rt_kprintf("[%s] DELETE rejected while audio is active\n", APP_NAME);
                break;
            }
            if (!tf_delete_pending) {
                tf_delete_pending = true;
                ui_tf_delete_confirm_snapshot = true;
            } else {
                tf_delete_pending = false;
                ui_tf_delete_confirm_snapshot = false;
                if (current_memo_source == MEMO_SOURCE_RAM) {
                    if (ram_memo_valid && capsule_wav_length(&capsule_box) > 44) {
                        capsule_memo_clear(&capsule_box);
                        ram_memo_valid = false;
                        recording_duration_ms = 0;
                        recording_truncated = false;
                        ui_tf_feedback = TF_UI_READY;
                        rt_kprintf("[%s] RAM recording deleted\n", APP_NAME);
                    } else {
                        ui_tf_feedback = tf_library_ready() ? TF_UI_NO_RECORDING : TF_UI_NO_CARD;
                        rt_kprintf("[%s] DELETE failed: select a RAM recording first\n", APP_NAME);
                    }
                } else if (!tf_library_ready() ||
                           strcmp(ui_tf_name_snapshot, "No TF file") == 0) {
                    ui_tf_feedback = tf_library_ready() ? TF_UI_NO_RECORDING : TF_UI_NO_CARD;
                    rt_kprintf("[%s] DELETE failed: select a TF recording first\n", APP_NAME);
                } else if (tf_library_delete() != 0) {
                    ui_tf_feedback = tf_library_ready() ? TF_UI_NO_RECORDING : TF_UI_NO_CARD;
                    rt_kprintf("[%s] DELETE failed: select a TF recording first\n", APP_NAME);
                } else {
                    ui_tf_feedback = TF_UI_READY;
                }
            }
            break;
        }
    }
}

static void report_status(void)
{
    static capsule_pose_t last_pose = CAPSULE_POSE_UNKNOWN;
    static capsule_pomo_state_t last_state = CAPSULE_POMO_IDLE;
    static uint32_t last_seconds = UINT32_MAX;
    capsule_pose_t pose = capsule_pose(&capsule_box);
    capsule_pomo_state_t state = capsule_timer_state(&capsule_box);
    uint32_t seconds = capsule_show_seconds(&capsule_box);
#ifdef PKG_USING_LITTLEVGL2RTT
    ui_pose_snapshot = pose;
    ui_seconds_snapshot = seconds;
    ui_stand_snapshot = capsule_stand_mode(&capsule_box);
    ui_timer_state_snapshot = state;
    if (state != CAPSULE_POMO_IDLE) ui_timer_reset_snapshot = false;
    ui_clock_24h_snapshot = clock_24h;
    ui_clock_seconds_snapshot = clock_seconds;
    ui_night_mode_snapshot = ui_night_mode;
    ui_stopwatch_running_snapshot = stopwatch_running;
    ui_stopwatch_tenths_snapshot = (uint32_t)((stopwatch_elapsed_ms +
        (stopwatch_running ? (uint32_t)(rt_tick_get_millisecond() - stopwatch_since_ms) : 0u)) / 100u);
    for (int i = 0; i < 3; ++i) ui_laps_snapshot[i] = stopwatch_laps[i];
    ui_lap_count_snapshot = stopwatch_lap_count;
    ui_tf_ready_snapshot = tf_library_ready();
    ui_selected_duration_snapshot = tf_library_selected_duration_ms();
    {
        static char last_tf_name[sizeof ui_tf_name_snapshot];
        if (!tf_library_copy_selected(ui_tf_name_snapshot, sizeof ui_tf_name_snapshot))
            rt_snprintf(ui_tf_name_snapshot, sizeof ui_tf_name_snapshot, "No TF file");
        if (strcmp(ui_tf_name_snapshot, last_tf_name) != 0) {
            if (ui_tf_feedback == TF_UI_READY || ui_tf_feedback == TF_UI_NO_RECORDING ||
                ui_tf_feedback == TF_UI_NO_CARD || ui_tf_feedback == TF_UI_TRUNCATED)
                ui_tf_feedback = (ui_tf_ready_snapshot && strcmp(ui_tf_name_snapshot, "No TF file") != 0 ?
                                   TF_UI_READY : (ui_tf_ready_snapshot ? TF_UI_NO_RECORDING : TF_UI_NO_CARD));
            rt_snprintf(last_tf_name, sizeof last_tf_name, "%s", ui_tf_name_snapshot);
        } else if (ui_tf_feedback == TF_UI_READY && strcmp(ui_tf_name_snapshot, "No TF file") == 0) {
            ui_tf_feedback = (ui_tf_ready_snapshot ? TF_UI_NO_RECORDING : TF_UI_NO_CARD);
        }
    }
    if (playback_worker_active) {
        ui_playback_state_snapshot = UI_PLAYBACK_PLAYING;
        if (tf_library_playback_state() == UI_PLAYBACK_PLAYING)
            ui_playback_progress_snapshot = tf_library_playback_progress();
    } else {
        ui_playback_state_snapshot = playback_worker_state;
        if (playback_worker_state == UI_PLAYBACK_COMPLETE)
            ui_playback_progress_snapshot = 100;
        else if (playback_worker_state == UI_PLAYBACK_IDLE)
            ui_playback_progress_snapshot = 0;
    }
    ui_beep_snapshot = capsule_beep_enabled(&capsule_box);
    ui_recording_snapshot = capsule_recording(&capsule_box);
    ui_ble_connected_snapshot = ble_hid_connected();
    ui_recording_persisted_snapshot = recording_persisted;
#endif
    if (pose != CAPSULE_POSE_FLAT && pose != CAPSULE_POSE_UNKNOWN) {
        tf_delete_pending = false;
        ui_tf_delete_confirm_snapshot = false;
    }
    if (pose != last_pose || state != last_state || seconds != last_seconds)
    {
        char status[96];
        capsule_format_status(&capsule_box, status, sizeof status);
        rt_kprintf("[%s] status=%s muted=%d recording=%d\n", APP_NAME, status,
                   capsule_muted(&capsule_box) ? 1 : 0,
                   capsule_recording(&capsule_box) ? 1 : 0);
        last_pose = pose;
        last_state = state;
        last_seconds = seconds;
    }
}
/* 录音/回放期间禁止 BLE 传文件读卡，避免与 SPI1 写入冲突（回 X 9）。 */
static bool memo_tf_busy(void)
{
    return capsule_recording(&capsule_box) || playback_worker_active;
}

int main(void)
{
    rt_kprintf("\n[%s] v%s boot ok\n", APP_NAME, APP_VERSION);
    {
        int failed = capsule_selftest();
        rt_kprintf("[%s] capsule selftest %s (%d)\n", APP_NAME,
                   failed ? "FAIL" : "PASS", failed);
    }
    ble_hid_init();
    ble_file_init(memo_tf_busy);
    if (imu_init() != 0 || capsule_init_with_calibration() != 0 || key2_init() != 0)
    {
        rt_kprintf("[%s] startup blocked\n", APP_NAME);
        while (1) rt_thread_mdelay(1000);
    }
    /* BLE is initialized before the longer sensor setup so BLE_POWER_ON_IND cannot be missed. */
#ifdef PKG_USING_LITTLEVGL2RTT
    ui_command_queue = rt_mq_create("uicmd", sizeof(struct ui_command), 8, RT_IPC_FLAG_FIFO);
    if (!ui_command_queue) rt_kprintf("[%s] UI command queue unavailable\n", APP_NAME);
    ui_start();
#endif
#ifdef POMO_TEST_10S
    {
        uint32_t test_now = rt_tick_get_millisecond();
        capsule_on_accel(&capsule_box, CALIBRATED_STAND_X, CALIBRATED_STAND_Y,
                         CALIBRATED_STAND_Z, test_now);
    }
#endif
    uint32_t last_sample_ms = 0;
    uint32_t last_status_ms = UINT32_MAX;
    while (1)
    {
        int32_t x, y, z;
        uint32_t now = rt_tick_get_millisecond();
        struct key_event event;
        while (rt_mq_recv(key_event_queue, &event, sizeof(event), 0) == RT_EOK)
            handle_key_event(&event);
        handle_ui_commands();
        if (audio_overflow && capsule_recording(&capsule_box))
            stop_recording();
        if ((int32_t)(now - last_sample_ms) >= (int32_t)IMU_SAMPLE_MS)
        {
            last_sample_ms = now;
            if (imu_read(&x, &y, &z) == 0)
                capsule_on_accel(&capsule_box, x, y, z, now);
            else
                rt_kprintf("[%s] IMU read failed; timer clock continues\n", APP_NAME);
        }
        capsule_on_time(&capsule_box, now);
        if (capsule_take_beep(&capsule_box)) beep_pending = true;
        if (capsule_muted(&capsule_box) || !capsule_beep_enabled(&capsule_box)) beep_pending = false;
        if (beep_pending && !beep_worker_active && !capsule_recording(&capsule_box)) {
#ifdef PKG_USING_LITTLEVGL2RTT
            if (!playback_worker_active && start_beep_worker()) beep_pending = false;
#else
            if (start_beep_worker()) beep_pending = false;
#endif
        }
        if (last_status_ms == UINT32_MAX || now - last_status_ms >= 100) {
            report_status();
            last_status_ms = now;
        }
        rt_thread_mdelay(10);
    }
}
