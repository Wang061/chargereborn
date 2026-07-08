#include "net_dash_core.h"
#include <string.h>

void dash_ring_init(dash_ring_t *ring)
{
    ring->head = 0;
    ring->count = 0;
}

void dash_ring_push(dash_ring_t *ring, const dash_log_entry_t *entry)
{
    ring->entries[ring->head] = *entry;
    ring->head = (uint8_t)((ring->head + 1) % DASH_LOG_RING_CAP);
    if (ring->count < DASH_LOG_RING_CAP) ring->count++;
}

int dash_ring_snapshot(const dash_ring_t *ring, dash_log_entry_t *out, int out_cap)
{
    int n = ring->count;
    if (n > out_cap) n = out_cap;
    for (int i = 0; i < n; i++) {
        int idx = (ring->head - 1 - i + DASH_LOG_RING_CAP) % DASH_LOG_RING_CAP;
        out[i] = ring->entries[idx];
    }
    return n;
}

void dash_class_sample_init(dash_class_sample_t *s)
{
    strncpy(s->cls_name, "?", DASH_CLS_NAME_MAX - 1);
    s->cls_name[DASH_CLS_NAME_MAX - 1] = '\0';
    s->score = 0.0f;
}

void dash_class_sample_update(dash_class_sample_t *s, const char *cls_name, float score)
{
    if (score < DASH_CLASS_SCORE_MIN) return;
    strncpy(s->cls_name, cls_name, DASH_CLS_NAME_MAX - 1);
    s->cls_name[DASH_CLS_NAME_MAX - 1] = '\0';
    s->score = score;
}
