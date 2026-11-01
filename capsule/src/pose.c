#include "capsule.h"

const char *capsule_pose_name(capsule_pose_t pose)
{
    switch (pose) {
    case CAPSULE_POSE_STAND: return "直立";
    case CAPSULE_POSE_FACE_DOWN: return "扣下";
    case CAPSULE_POSE_SIDE_LEFT: return "左侧";
    case CAPSULE_POSE_SIDE_RIGHT: return "右侧";
    case CAPSULE_POSE_FLAT: return "平放";
    default: return "未知";
    }
}

enum { MAG_MIN_MG = 400, MAG_MAX_MG = 2500 };

static int ref_index(capsule_pose_t pose)
{
    if (pose < CAPSULE_POSE_STAND || pose > CAPSULE_POSE_FLAT) return -1;
    return (int)pose - 1;
}

static uint32_t isqrt_u64(uint64_t x)
{
    uint64_t op = x;
    uint64_t res = 0;
    uint64_t one = 1ULL << 62;
    while (one > op) one >>= 2;
    while (one) {
        if (op >= res + one) {
            op -= res + one;
            res = (res >> 1) + one;
        } else {
            res >>= 1;
        }
        one >>= 2;
    }
    return (uint32_t)res;
}

static uint32_t mag_mg(int32_t x, int32_t y, int32_t z)
{
    uint64_t sum = (uint64_t)((int64_t)x * x) + (uint64_t)((int64_t)y * y) + (uint64_t)((int64_t)z * z);
    return isqrt_u64(sum);
}

static bool still_gravity(int32_t x, int32_t y, int32_t z)
{
    uint32_t mag = mag_mg(x, y, z);
    return mag >= MAG_MIN_MG && mag <= MAG_MAX_MG;
}

void capsule_pose_map_clear(capsule_pose_map_t *map)
{
    if (!map) return;
    for (int i = 0; i < CAPSULE_POSE_REF_COUNT; ++i) {
        map->ref[i].ax_mg = 0;
        map->ref[i].ay_mg = 0;
        map->ref[i].az_mg = 0;
        map->ref[i].set = false;
    }
}

int capsule_pose_map_set(capsule_pose_map_t *map, capsule_pose_t pose, int32_t ax_mg, int32_t ay_mg, int32_t az_mg)
{
    int index = ref_index(pose);
    if (!map || index < 0 || !still_gravity(ax_mg, ay_mg, az_mg)) return -1;
    map->ref[index].ax_mg = ax_mg;
    map->ref[index].ay_mg = ay_mg;
    map->ref[index].az_mg = az_mg;
    map->ref[index].set = true;
    return 0;
}

int32_t capsule_accel_millig(int16_t raw)
{
    int32_t scaled = (int32_t)raw * 61;
    if (scaled < 0) scaled -= 500;
    else scaled += 500;
    return scaled / 1000;
}

static void put_i32(uint8_t *p, int32_t v)
{
    uint32_t u = (uint32_t)v;
    p[0] = (uint8_t)u;
    p[1] = (uint8_t)(u >> 8);
    p[2] = (uint8_t)(u >> 16);
    p[3] = (uint8_t)(u >> 24);
}

static int32_t get_i32(const uint8_t *p)
{
    uint32_t u = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return (int32_t)u;
}

size_t capsule_pose_map_export(const capsule_pose_map_t *map, uint8_t *dst, size_t cap)
{
    if (!map || !dst || cap < CAPSULE_POSE_MAP_BYTES) return 0;
    dst[0] = 'C';
    dst[1] = 'P';
    dst[2] = 'M';
    dst[3] = '1';
    for (int i = 0; i < CAPSULE_POSE_REF_COUNT; ++i) {
        uint8_t *slot = dst + 4 + i * 12;
        if (!map->ref[i].set) {
            put_i32(slot, 0);
            put_i32(slot + 4, 0);
            put_i32(slot + 8, 0);
        } else {
            put_i32(slot, map->ref[i].ax_mg);
            put_i32(slot + 4, map->ref[i].ay_mg);
            put_i32(slot + 8, map->ref[i].az_mg);
        }
    }
    return CAPSULE_POSE_MAP_BYTES;
}

int capsule_pose_map_import(capsule_pose_map_t *map, const uint8_t *src, size_t len)
{
    capsule_pose_map_t next;
    if (!map || !src || len < CAPSULE_POSE_MAP_BYTES) return -1;
    if (src[0] != 'C' || src[1] != 'P' || src[2] != 'M' || src[3] != '1') return -1;
    capsule_pose_map_clear(&next);
    for (int i = 0; i < CAPSULE_POSE_REF_COUNT; ++i) {
        const uint8_t *slot = src + 4 + i * 12;
        int32_t ax = get_i32(slot);
        int32_t ay = get_i32(slot + 4);
        int32_t az = get_i32(slot + 8);
        if (ax == 0 && ay == 0 && az == 0) continue;
        if (!still_gravity(ax, ay, az)) return -1;
        next.ref[i].ax_mg = ax;
        next.ref[i].ay_mg = ay;
        next.ref[i].az_mg = az;
        next.ref[i].set = true;
    }
    *map = next;
    return 0;
}

capsule_pose_t capsule_pose_match(const capsule_pose_map_t *map, int32_t ax_mg, int32_t ay_mg, int32_t az_mg)
{
    uint32_t sample_mag;
    int best = -1;
    int second = -1;
    int winner = -1;
    if (!map) return CAPSULE_POSE_UNKNOWN;
    sample_mag = mag_mg(ax_mg, ay_mg, az_mg);
    /* 静止重力大约 1 g。太小是噪声，太大是正在甩动。 */
    if (sample_mag < MAG_MIN_MG || sample_mag > MAG_MAX_MG) return CAPSULE_POSE_UNKNOWN;
    for (int i = 0; i < CAPSULE_POSE_REF_COUNT; ++i) {
        const capsule_pose_ref_t *ref = &map->ref[i];
        uint32_t ref_mag;
        int64_t dot;
        int cos_milli;
        if (!ref->set) continue;
        ref_mag = mag_mg(ref->ax_mg, ref->ay_mg, ref->az_mg);
        if (ref_mag < MAG_MIN_MG || ref_mag > MAG_MAX_MG) continue;
        dot = (int64_t)ax_mg * ref->ax_mg + (int64_t)ay_mg * ref->ay_mg + (int64_t)az_mg * ref->az_mg;
        if (dot <= 0) cos_milli = 0;
        else cos_milli = (int)((uint64_t)dot * 1000u / ((uint64_t)sample_mag * ref_mag));
        if (cos_milli > 1000) cos_milli = 1000;
        if (cos_milli > best) {
            second = best;
            best = cos_milli;
            winner = i;
        } else if (cos_milli == best) {
            second = cos_milli;
            winner = -1;
        } else if (cos_milli > second) {
            second = cos_milli;
        }
    }
    if (winner < 0 || best < CAPSULE_POSE_MATCH_MIN_COS_MILLI) return CAPSULE_POSE_UNKNOWN;
    if (second >= 0 && best - second < CAPSULE_POSE_MATCH_MARGIN_MILLI) return CAPSULE_POSE_UNKNOWN;
    return (capsule_pose_t)(winner + 1);
}
