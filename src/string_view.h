/*
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * https://www.boost.org/LICENSE_1_0.txt)
 *
 * Copyright (c) 2026 Andrey Semashev
 */

#ifndef CPUFREQD_STRING_VIEW_H_
#define CPUFREQD_STRING_VIEW_H_

#include <stddef.h>

//! A string with a length
typedef struct string_view
{
    const char* str;
    size_t size;
}
string_view;

#define STRING_VIEW_INIT(str) { str, (sizeof(str) - 1u) }

#endif // CPUFREQD_STRING_VIEW_H_
