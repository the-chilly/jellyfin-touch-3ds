#include "ui/timeline.h"

int64_t timeline_clamp(int64_t target, int64_t duration)
{
    if (duration <= 0 || target <= 0) return 0;
    /* Never request EOF. This also handles clips shorter than a second. */
    int64_t end = duration > 10000000LL ? duration - 10000000LL : 0;
    return target > end ? end : target;
}

int64_t timeline_position(int x, int64_t duration)
{
    if (duration <= 0) return 0;
    int offset = x - TIMELINE_X;
    if (offset < 0) offset = 0;
    if (offset > TIMELINE_WIDTH) offset = TIMELINE_WIDTH;
    /* Split multiplication to avoid overflow even with malformed durations. */
    int64_t target = (duration / TIMELINE_WIDTH) * offset +
                    (duration % TIMELINE_WIDTH) * offset / TIMELINE_WIDTH;
    return timeline_clamp(target, duration);
}

bool timeline_hit(int x, int y, int64_t duration)
{
    return duration > 0 && x >= TIMELINE_X &&
        x <= TIMELINE_X + TIMELINE_WIDTH &&
        y >= TIMELINE_HIT_TOP && y <= TIMELINE_HIT_BOTTOM;
}
