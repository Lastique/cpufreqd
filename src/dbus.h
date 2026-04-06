/*
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * https://www.boost.org/LICENSE_1_0.txt)
 *
 * Copyright (c) 2026 Andrey Semashev
 */

#ifndef CPUFREQD_DBUS_H_
#define CPUFREQD_DBUS_H_

#include <stdbool.h>
#include <stdint.h>

//! Initializes D-Bus
int init_dbus();
//! Deinitializes D-Bus
void deinit_dbus();

//! Checks if cpufreq control is suspended via D-Bus
bool is_suspended();

//! Processes D-Bus requests until the given absolute timeout
int process_dbus_until(int64_t timeout);

#endif // CPUFREQD_DBUS_H_
