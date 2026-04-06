/*
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * https://www.boost.org/LICENSE_1_0.txt)
 *
 * Copyright (c) 2026 Andrey Semashev
 */

#ifndef CPUFREQD_TIME_UTILS_H_
#define CPUFREQD_TIME_UTILS_H_

#include <stdint.h>
#include <time.h>

//! Returns current time, according to CLOCK_MONOTONIC, in microseconds since epoch
static inline int64_t clock_monotonic_now_us()
{
    struct timespec ts = {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * (int64_t)1000000 + ts.tv_nsec / 1000;
}

#endif // CPUFREQD_TIME_UTILS_H_
