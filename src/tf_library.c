#include "capsule.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>

#include "rtthread.h"
#include "dfs_file.h"
#include "dfs_posix.h"
#include "audio_server.h"

#define TF_ROOT "/"
#define TF_NAME_MAX 32
#define TF_MAX_INDEX 10000u
#define TF_TAG "tf"
#define TF_LOCAL_MUSIC_VOLUME 15
#define TF_PLAY_CHUNK_SAMPLES 320
#define TF_PLAY_RETRY_MS 10
enum { TF_PLAYBACK_IDLE, TF_PLAYBACK_PLAYING, TF_PLAYBACK_COMPLETE, TF_PLAYBACK_FAILED };
static volatile uint8_t playback_state;
static volatile uint8_t playback_progress;
static volatile bool playback_stop_requested;

uint8_t tf_library_playback_state(void) { return playback_state; }
uint8_t tf_library_playback_progress(void) { return playback_progress; }

void tf_library_playback_prepare(void)
{
    playback_stop_requested = false;
    playback_state = TF_PLAYBACK_IDLE;
    playback_progress = 0;
}

void tf_library_playback_stop(void)
{
    playback_stop_requested = true;
}

static bool tf_ready;
static uint8_t *ram_wav;
static size_t ram_wav_cap;

static char selected_path[TF_NAME_MAX];
static volatile uint32_t selected_duration_ms;
static rt_mutex_t selected_lock;

static uint32_t parse_wav_header_duration_ms(const uint8_t *header, size_t len)
{
    if (!header || len < 44) return 0;
    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0 ||
        memcmp(header + 12, "fmt ", 4) != 0) {
        return 0;
    }
    uint32_t fmt_len = (uint32_t)header[16] | ((uint32_t)header[17] << 8) |
                       ((uint32_t)header[18] << 16) | ((uint32_t)header[19] << 24);
    if (fmt_len != 16) return 0;
    uint16_t format_tag = (uint16_t)header[20] | ((uint16_t)header[21] << 8);
    uint16_t channels = (uint16_t)header[22] | ((uint16_t)header[23] << 8);
    uint32_t sample_rate = (uint32_t)header[24] | ((uint32_t)header[25] << 8) |
                           ((uint32_t)header[26] << 16) | ((uint32_t)header[27] << 24);
    uint16_t bits = (uint16_t)header[34] | ((uint16_t)header[35] << 8);
    if (format_tag != 1 || channels != 1 || sample_rate != CAPSULE_WAV_RATE || bits != 16) {
        return 0;
    }
    if (memcmp(header + 36, "data", 4) != 0) return 0;
    uint32_t data_bytes = (uint32_t)header[40] |
                          ((uint32_t)header[41] << 8) |
                          ((uint32_t)header[42] << 16) |
                          ((uint32_t)header[43] << 24);
    uint32_t duration_ms = data_bytes / 32u;
    if (duration_ms > 30000u) duration_ms = 30000u;
    return duration_ms;
}

static uint32_t read_selected_duration_ms(const char *path)
{
    struct dfs_fd fd;
    uint8_t header[44];
    int r;
    if (!path || path[0] == 0) return 0;
    if (dfs_file_open(&fd, path, O_RDONLY) != 0) return 0;
    r = dfs_file_read(&fd, header, sizeof(header));
    dfs_file_close(&fd);
    if (r < 44) return 0;
    return parse_wav_header_duration_ms(header, (size_t)r);
}

static void set_selected_path(const char *path)
{
    uint32_t dur_ms = 0;
    if (!selected_lock) return;
    if (path && path[0] != 0 && tf_ready) {
        dur_ms = read_selected_duration_ms(path);
    }
    rt_mutex_take(selected_lock, RT_WAITING_FOREVER);
    rt_snprintf(selected_path, sizeof selected_path, "%s", path ? path : "");
    selected_duration_ms = dur_ms;
    rt_mutex_release(selected_lock);
}

uint32_t tf_library_selected_duration_ms(void)
{
    if (!tf_ready) return 0;
    return selected_duration_ms;
}

bool tf_library_copy_selected(char *dst, size_t capacity)
{
    bool has_file;
    if (!dst || !capacity || !selected_lock) return false;
    rt_mutex_take(selected_lock, RT_WAITING_FOREVER);
    rt_snprintf(dst, capacity, "%s", selected_path);
    has_file = selected_path[0] != 0;
    rt_mutex_release(selected_lock);
    return has_file;
}

static bool is_wav_name(const char *name)
{
    size_t n;
    if (!name || strncmp(name, "memo", 4) != 0) return false;
    n = strlen(name);
    return n == 12 && strcmp(name + 8, ".wav") == 0;
}

static void path_for_index(char *path, size_t cap, unsigned index)
{
    rt_snprintf(path, cap, "/memo%04u.wav", index);
}

static bool choose_path(char *path, size_t cap)
{
    struct dfs_fd fd;
    unsigned i;
    for (i = 0; i < TF_MAX_INDEX; ++i) {
        path_for_index(path, cap, i);
        if (dfs_file_open(&fd, path, O_RDONLY) != 0) return true;
        dfs_file_close(&fd);
    }
    return false;
}

static void select_first_path(void)
{
    DIR *dir;
    struct dirent *entry;
    char best[TF_NAME_MAX] = {0};
    if (!tf_ready) return;
    dir = opendir(TF_ROOT);
    if (!dir) {
        set_selected_path("");
        return;
    }
    while ((entry = readdir(dir)) != NULL) {
        if (is_wav_name(entry->d_name) && (!best[0] || strcmp(entry->d_name, best) < 0))
            rt_snprintf(best, sizeof best, "%s", entry->d_name);
    }
    closedir(dir);
    if (best[0]) {
        char path[TF_NAME_MAX];
        rt_snprintf(path, sizeof path, "/%s", best);
        set_selected_path(path);
    } else {
        set_selected_path("");
    }
}

void tf_library_init(uint8_t *wav_storage, size_t wav_bytes)
{
    ram_wav = wav_storage;
    ram_wav_cap = wav_bytes;
    selected_lock = rt_mutex_create("tfsel", RT_IPC_FLAG_PRIO);
    if (!selected_lock) return;
    tf_ready = dfs_mount("sd0", TF_ROOT, "elm", 0, 0) == 0;
    if (tf_ready) select_first_path();
    else set_selected_path("");
}

bool tf_library_ready(void) { return tf_ready; }

int tf_library_append(const char *name, const char *text)
{
    struct dfs_fd fd;
    char path[TF_NAME_MAX];
    size_t len;
    size_t written = 0;
    if (!tf_ready || !name || !name[0] || !text) return -1;
    len = strlen(text);
    if (!len) return -1;
    rt_snprintf(path, sizeof path, "/%s", name);
    if (dfs_file_open(&fd, path, O_WRONLY | O_CREAT) != 0) return -1;
    if (dfs_file_lseek(&fd, fd.size) < 0) {
        dfs_file_close(&fd);
        return -1;
    }
    while (written < len) {
        int result = dfs_file_write(&fd, text + written, len - written);
        if (result <= 0) {
            dfs_file_close(&fd);
            return -1;
        }
        written += (size_t)result;
    }
    dfs_file_close(&fd);
    return 0;
}

int tf_library_save(const uint8_t *data, size_t len)
{
    struct dfs_fd fd;
    char path[TF_NAME_MAX];
    size_t written = 0;
    if (!tf_ready || !data || !len || !choose_path(path, sizeof path)) return -1;
    if (dfs_file_open(&fd, path, O_WRONLY | O_CREAT | O_TRUNC) != 0) return -1;
    while (written < len) {
        int result = dfs_file_write(&fd, data + written, len - written);
        if (result <= 0) {
            dfs_file_close(&fd);
            dfs_file_unlink(path);
            return -1;
        }
        written += (size_t)result;
    }
    dfs_file_close(&fd);
    set_selected_path(path);
    return 0;
}

int tf_library_next(void)
{
    DIR *dir;
    struct dirent *entry;
    char best[TF_NAME_MAX] = {0};
    char current_path[TF_NAME_MAX];
    const char *current;
    if (!tf_ready) return -1;
    if (!tf_library_copy_selected(current_path, sizeof current_path)) current_path[0] = 0;
    current = current_path[0] == '/' ? current_path + 1 : current_path;
    dir = opendir(TF_ROOT);
    if (!dir) return -1;
    while ((entry = readdir(dir)) != NULL) {
        if (!is_wav_name(entry->d_name)) continue;
        if ((!current[0] || strcmp(entry->d_name, current) > 0) &&
            (!best[0] || strcmp(entry->d_name, best) < 0))
            rt_snprintf(best, sizeof best, "%s", entry->d_name);
    }
    closedir(dir);
    if (!best[0]) {
        select_first_path();
    } else {
        char path[TF_NAME_MAX];
        rt_snprintf(path, sizeof path, "/%s", best);
        set_selected_path(path);
    }
    return best[0] || current_path[0] ? 0 : -1;
}

int tf_library_delete(void)
{
    char path[TF_NAME_MAX];
    if (!tf_ready || !tf_library_copy_selected(path, sizeof path)) return -1;
    if (dfs_file_unlink(path) != 0) return -1;
    set_selected_path("");
    select_first_path();
    return 0;
}

/* 播放一段完整 WAV（16 kHz/16 bit/单声道）：PCM 指针由 capsule_wav_read 给出。 */
static int play_wav(const uint8_t *data, size_t len)
{
    const int16_t *pcm;
    size_t samples;
    audio_parameter_t param = {0};
    audio_client_t client;
    uint32_t flush_ms = 0;
    int result = -1;
    size_t total_samples;
    bool stopped = false;

    if (playback_stop_requested) {
        playback_state = TF_PLAYBACK_IDLE;
        playback_progress = 0;
        return 0;
    }
    if (!data || len < 44) {
        rt_kprintf("[%s] play: invalid WAV buffer\n", TF_TAG);
        playback_state = TF_PLAYBACK_FAILED;
        return -1;
    }
    if (capsule_wav_read(data, len, &pcm, &samples) != 0 || !pcm || !samples) {
        rt_kprintf("[%s] play: WAV decode failed (bytes=%u)\n", TF_TAG, (unsigned)len);
        playback_state = TF_PLAYBACK_FAILED;
        return -1;
    }
    if (playback_stop_requested) {
        playback_state = TF_PLAYBACK_IDLE;
        playback_progress = 0;
        return 0;
    }
    total_samples = samples;
    playback_progress = 0;
    playback_state = TF_PLAYBACK_PLAYING;
    param.write_bits_per_sample = 16;
    param.write_channnel_num = 1;
    param.write_samplerate = CAPSULE_WAV_RATE;
    param.write_cache_size = 4096;
    client = audio_open(AUDIO_TYPE_LOCAL_MUSIC, AUDIO_TX, &param, NULL, NULL);
    if (!client) {
        rt_kprintf("[%s] play: audio_open failed\n", TF_TAG);
        playback_state = TF_PLAYBACK_FAILED;
        return -1;
    }
    if (playback_stop_requested) {
        stopped = true;
        goto close_client;
    }
    audio_server_set_private_volume(AUDIO_TYPE_LOCAL_MUSIC, TF_LOCAL_MUSIC_VOLUME);
    while (samples) {
        if (playback_stop_requested) {
            stopped = true;
            goto close_client;
        }
        size_t chunk = samples > TF_PLAY_CHUNK_SAMPLES ? TF_PLAY_CHUNK_SAMPLES : samples;
        size_t chunk_bytes = chunk * sizeof(*pcm);
        int written = audio_write(client, (uint8_t *)pcm, chunk_bytes);
        if (written == (int)chunk_bytes) {
            pcm += chunk;
            samples -= chunk;
            playback_progress = (uint8_t)(((total_samples - samples) * 100u) / total_samples);
            if (playback_stop_requested) {
                stopped = true;
                goto close_client;
            }
        } else if (written == 0) {
            rt_thread_mdelay(TF_PLAY_RETRY_MS);
            if (playback_stop_requested) {
                stopped = true;
                goto close_client;
            }
        } else {
            rt_kprintf("[%s] play: audio_write failed (%d/%u)\n", TF_TAG, written,
                       (unsigned)chunk_bytes);
            goto close_client;
        }
    }
    if (playback_stop_requested) {
        stopped = true;
        goto close_client;
    }
    if (audio_ioctl(client, AUDIO_IOCTL_FLUSH_TIME_MS, &flush_ms) != 0) {
        rt_kprintf("[%s] play: flush query failed\n", TF_TAG);
        goto close_client;
    }
    while (flush_ms > 0) {
        if (playback_stop_requested) {
            stopped = true;
            goto close_client;
        }
        uint32_t step = flush_ms > TF_PLAY_RETRY_MS ? TF_PLAY_RETRY_MS : flush_ms;
        rt_thread_mdelay(step);
        flush_ms -= step;
    }
    if (playback_stop_requested) {
        stopped = true;
        goto close_client;
    }
    playback_progress = 100;
    result = 0;
close_client:
    audio_close(client);
    if (stopped) {
        playback_state = TF_PLAYBACK_IDLE;
        playback_progress = 0;
        result = 0;
    } else if (result == 0) {
        playback_state = TF_PLAYBACK_COMPLETE;
    } else {
        playback_state = TF_PLAYBACK_FAILED;
    }
    return result;
}

int tf_library_play_ram(const uint8_t *data, size_t len)
{
    if (playback_stop_requested) {
        playback_state = TF_PLAYBACK_IDLE;
        playback_progress = 0;
        return 0;
    }
    if (!data || len < 44) return -1;
    return play_wav(data, len);
}

int tf_library_play(void)
{
    struct dfs_fd fd;
    size_t length = 0;
    char path[TF_NAME_MAX];
    int result;

    if (playback_stop_requested) {
        playback_state = TF_PLAYBACK_IDLE;
        playback_progress = 0;
        return 0;
    }
    if (!tf_ready || !tf_library_copy_selected(path, sizeof path) ||
        !ram_wav || ram_wav_cap < 44) return -1;
    if (dfs_file_open(&fd, path, O_RDONLY) != 0) return -1;
    while (length < ram_wav_cap) {
        if (playback_stop_requested) {
            dfs_file_close(&fd);
            playback_state = TF_PLAYBACK_IDLE;
            playback_progress = 0;
            return 0;
        }
        int read = dfs_file_read(&fd, ram_wav + length, ram_wav_cap - length);
        if (read <= 0) break;
        length += (size_t)read;
    }
    dfs_file_close(&fd);
    if (playback_stop_requested) {
        playback_state = TF_PLAYBACK_IDLE;
        playback_progress = 0;
        return 0;
    }
    if (length < 44) return -1;
    result = play_wav(ram_wav, length);
    if (result != 0 && playback_state != TF_PLAYBACK_IDLE)
        rt_kprintf("[%s] play: %s failed\n", TF_TAG, path);
    return result;
}
