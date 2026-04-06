/*
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * https://www.boost.org/LICENSE_1_0.txt)
 *
 * Copyright (c) 2026 Andrey Semashev
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#if (defined(__x86_64__) || defined(__i386__)) && defined(__SSE2__)
#include <immintrin.h>
#endif
#include <systemd/sd-bus.h>

#include "time_utils.h"
#include "dbus.h"

//! A set of bits indicating suspend requests. Position of each bit corresponds to a cookie returned for a given suspend request.
typedef struct suspend_request_bitset
{
    //! Pointer to the bits in the bitset
    uint64_t* data;
    //! Number of set bits in the bitset
    uint32_t set_count;
    //! Available storage in the bitset, in the number of bits
    uint32_t capacity;
}
suspend_request_bitset;

//! The amount of bits to grow suspend request bitset by
enum { suspend_request_bitset_grow_count = 128u };
_Static_assert((suspend_request_bitset_grow_count % 64u) == 0u, "bitset grow size must be a whole number of words used to represent the bitset");

//! A set of suspend requests
static suspend_request_bitset g_suspend_requests = {};

//! Returns the number of suspend requests registered via D-Bus
static int dbus_get_suspend_request_count(sd_bus_message* msg, void* userdata, sd_bus_error* error)
{
    return sd_bus_reply_method_return(msg, "u", g_suspend_requests.set_count);
}

//! Requests suspending cpufreq control by the daemon
static int dbus_suspend(sd_bus_message* msg, void* userdata, sd_bus_error* error)
{
    uint32_t cookie = 0u;
    const uint32_t capacity = g_suspend_requests.capacity;
    if (g_suspend_requests.set_count < capacity)
    {
        // Find a free bit in the set
#if (defined(__x86_64__) || defined(__i386__)) && defined(__SSE4_1__)
        __m128i mm_all_ones = _mm_undefined_si128();
        mm_all_ones = _mm_cmpeq_epi32(mm_all_ones, mm_all_ones);
        for (uint32_t i = 0u, n = capacity / 64u; i < n; i += 2u)
        {
            __m128i mm = _mm_loadu_si128((const __m128i*)(g_suspend_requests.data + i));
            if (!_mm_testc_si128(mm, mm_all_ones))
            {
                if (g_suspend_requests.data[i] == UINT64_C(0xFFFFFFFFFFFFFFFF))
                    ++i;

                cookie = __builtin_ctzll(~g_suspend_requests.data[i]);
                break;
            }
        }
#else
        for (uint32_t i = 0u, n = capacity / 64u; i < n; ++i)
        {
            if (g_suspend_requests.data[i] != UINT64_C(0xFFFFFFFFFFFFFFFF))
            {
                cookie = __builtin_ctzll(~g_suspend_requests.data[i]);
                break;
            }
        }
#endif
    }
    else
    {
        // Grow bitset
        if (capacity > (0xFFFFFFFFu - suspend_request_bitset_grow_count))
        {
            return sd_bus_error_set_const(error, SD_BUS_ERROR_LIMITS_EXCEEDED, "The list of suspend requests is too large");
        }

        const uint32_t new_capacity = capacity + suspend_request_bitset_grow_count;
        uint64_t* new_data = (uint64_t*)malloc((size_t)new_capacity / 8u);
        if (!new_data)
        {
            return sd_bus_error_set_const(error, SD_BUS_ERROR_NO_MEMORY, "No memory to grow the list of suspend requests");
        }

        // We know all bits in the previous bitset are set, so we can just fill the new buffer instead of copying
        __builtin_memset(new_data, 0xFF, capacity / 8u);
        __builtin_memset(new_data + capacity / 64u, 0, suspend_request_bitset_grow_count / 8u);

        cookie = capacity;

        free(g_suspend_requests.data);
        g_suspend_requests.data = new_data;
        g_suspend_requests.capacity = new_capacity;
    }

    g_suspend_requests.data[cookie / 64u] |= UINT64_C(1) << (cookie & 63u);
    ++g_suspend_requests.set_count;

    fprintf(stderr, "Received suspend request, cookie: %u\n", (unsigned int)cookie);

    return sd_bus_reply_method_return(msg, "u", cookie);
}

//! Requests resuming cpufreq control by the daemon
static int dbus_resume(sd_bus_message* msg, void* userdata, sd_bus_error* error)
{
    uint32_t cookie = 0u;
    int res = sd_bus_message_read(msg, "u", &cookie);
    if (res < 0)
    {
        return sd_bus_error_set_const(error, SD_BUS_ERROR_FAILED, "Cookie not specified");
    }

    const uint64_t cookie_bit = UINT64_C(1) << (cookie & 63u);
    if (cookie >= g_suspend_requests.capacity || (g_suspend_requests.data[cookie / 64u] & cookie_bit) == 0u)
    {
        return sd_bus_error_set_const(error, SD_BUS_ERROR_FAILED, "Suspend request not found");
    }

    fprintf(stderr, "Received resume request, cookie: %u\n", (unsigned int)cookie);

    g_suspend_requests.data[cookie / 64u] &= ~cookie_bit;
    --g_suspend_requests.set_count;

    return sd_bus_reply_method_return(msg, NULL);
}

static const sd_bus_vtable g_ctl_vtable[] =
{
    SD_BUS_VTABLE_START(0),

    SD_BUS_METHOD_WITH_ARGS("GetSuspendRequestCount", SD_BUS_NO_ARGS, SD_BUS_RESULT("u", count), dbus_get_suspend_request_count, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD_WITH_ARGS("Suspend", SD_BUS_NO_ARGS, SD_BUS_RESULT("u", cookie), dbus_suspend, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD_WITH_ARGS("Resume", SD_BUS_ARGS("u", cookie), SD_BUS_NO_RESULT, dbus_resume, SD_BUS_VTABLE_UNPRIVILEGED),

    SD_BUS_VTABLE_END
};

//! D-Bus to use
static sd_bus* g_dbus = NULL;
//! D-Bus slot for the control object
static sd_bus_slot* g_ctl_slot = NULL;

//! Initializes D-Bus
int init_dbus()
{
    int res = geteuid() == 0 ? sd_bus_open_system(&g_dbus) : sd_bus_open_user(&g_dbus);
    if (res < 0)
    {
        fprintf(stderr, "Failed to open D-Bus: error %d, %s\n", -res, strerror(-res));
        return res;
    }

    res = sd_bus_add_object_vtable(g_dbus, &g_ctl_slot, "/org/cpufreqd/Control", "org.cpufreqd.Control", g_ctl_vtable, NULL);
    if (res < 0)
    {
        sd_bus_close_unref(g_dbus);
        g_dbus = NULL;
        fprintf(stderr, "Failed to add D-Bus control object: error %d, %s\n", -res, strerror(-res));
        return res;
    }

    res = sd_bus_request_name(g_dbus, "org.cpufreqd", 0);
    if (res < 0)
    {
        sd_bus_slot_unref(g_ctl_slot);
        sd_bus_close_unref(g_dbus);
        g_dbus = NULL;
        fprintf(stderr, "Failed to register org.cpufreqd name on D-Bus: error %d, %s\n", -res, strerror(-res));
        return res;
    }

    return 0;
}

//! Deinitializes D-Bus
void deinit_dbus()
{
    if (g_ctl_slot)
    {
        sd_bus_slot_unref(g_ctl_slot);
        g_ctl_slot = NULL;
    }

    if (g_dbus)
    {
        sd_bus_close_unref(g_dbus);
        g_dbus = NULL;
    }
}

//! Checks if cpufreq control is suspended via D-Bus
bool is_suspended()
{
    return g_suspend_requests.set_count > 0u;
}

//! Processes D-Bus requests until the given absolute timeout
int process_dbus_until(int64_t timeout)
{
    while (true)
    {
        int res = sd_bus_process(g_dbus, NULL);
        if (res > 0)
        {
            continue;
        }
        else if (res < 0)
        {
            fprintf(stderr, "Failed to process D-Bus events: error %d, %s\n", -res, strerror(-res));
            return res;
        }

        int64_t wait_time = timeout - clock_monotonic_now_us();
        if (wait_time <= 0)
            break;

        res = sd_bus_wait(g_dbus, wait_time);
        if (res < 0)
        {
            fprintf(stderr, "Failed to wait for D-Bus events: error %d, %s\n", -res, strerror(-res));
            return res;
        }
    }

    return 0;
}
