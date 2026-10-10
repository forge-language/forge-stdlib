#ifndef FORGE_TIME_H
#define FORGE_TIME_H

#include <stdint.h>

int64_t fr_time_now_ms(void);
int64_t fr_time_monotonic_ms(void);
void fr_sleep_ms(int64_t ms);

#endif
