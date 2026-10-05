#ifndef JFIN_TIMELINE_H
#define JFIN_TIMELINE_H
#include <stdint.h>
#include <stdbool.h>
#define TIMELINE_X 20
#define TIMELINE_WIDTH 280
#define TIMELINE_HIT_TOP 66
#define TIMELINE_HIT_BOTTOM 106
int64_t timeline_clamp(int64_t target, int64_t duration);
int64_t timeline_position(int x, int64_t duration);
bool timeline_hit(int x, int y, int64_t duration);
#endif
