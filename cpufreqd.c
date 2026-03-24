/*
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * https://www.boost.org/LICENSE_1_0.txt)
 *
 * Copyright (c) 2026 Andrey Semashev
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>
#include <time.h>
#include <errno.h>

//! Shows supported command line options
static void print_help()
{
    puts
    (
        "cpufreqd [options]\n"
        "\n"
        "Options:\n"
        "\n"
        "  --help - Show this help.\n"
        "  --enable-gamemode - Enable support for gamemode tracking. Requires libgamemode.so.0\n"
        "                      to be installed and D-Bus to be accessible.\n"
    );
}

//! A string with a length
typedef struct string_view
{
    const char* str;
    size_t size;
} string_view;

#define STRING_VIEW_INIT(str) { str, (sizeof(str) - 1u) }

//! EPP value to set when the CPU is mostly idle
static const string_view g_epp_low_load_value = STRING_VIEW_INIT("balance_power");
//! EPP value to set when the CPU is under load
static const string_view g_epp_high_load_value = STRING_VIEW_INIT("balance_performance");

//! Pointer to libgamemode library
static void* g_libgamemode = NULL;

//! Type of the gamemode_query_status function from libgamemode.so
typedef int (gamemode_query_status_t)(void);
static gamemode_query_status_t* g_gamemode_query_status = NULL;

//! Initializes function pointers for interacting with libgamemode
static void init_libgamemode()
{
    g_libgamemode = dlopen("libgamemode.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!g_libgamemode)
        g_libgamemode = dlopen("libgamemode.so", RTLD_NOW | RTLD_LOCAL);

    if (g_libgamemode)
    {
        g_gamemode_query_status = (gamemode_query_status_t*)dlsym(g_libgamemode, "real_gamemode_query_status");
        if (!g_gamemode_query_status)
        {
            dlclose(g_libgamemode);
            g_libgamemode = NULL;
        }
    }
    else
    {
        fputs("libgamemode.so[.0] library not found, gamemode support will be disabled", stderr);
    }
}

//! Checks id gamemode is active
static inline bool is_gamemode_active()
{
    return (g_gamemode_query_status != NULL) && g_gamemode_query_status() > 0;
}

//! Skips space characters
static inline const char* skip_spaces(const char* p)
{
    char c = *p;

    while (c == ' ' || c == '\t')
    {
        ++p;
        c = *p;
    }

    return p;
}

//! Parses an integer from string without using the current locale (i.e. assuming C locale). Only supports non-negative decimal numbers.
static inline uint64_t strtoui64_cloc(const char* str, const char** str_end)
{
    uint64_t x = 0u;

    char c = *str;
    while (c >= '0' && c <= '9')
    {
        x = x * 10u + (c - '0');
        ++str;
        c = *str;
    }

    *str_end = str;

    return x;
}

//! Formats an integer to string without using the current locale (i.e. assuming C locale). Only supports non-negative decimal numbers.
static inline char* ui32toa_cloc(uint32_t n, char* str)
{
    char buf[16u];
    char* p = buf;
    do
    {
        *p = (n % 10u) + '0';
        n /= 10u;
        ++p;
    }
    while (n > 0u);

    while (p > buf)
    {
        --p;
        *str = *p;
        ++str;
    }

    *str = '\0';

    return str;
}

static uint64_t g_idle_time = 0u;
static uint64_t g_total_time = 0u;
static bool g_high_cpu_load = false;

//! Size of the buffer to read /proc/stat into. Must fit at least the entire first line of the file.
#define PROC_STAT_BUF_SIZE 512

//! Reads CPU times from /proc/stat
static int update_cpu_times()
{
    char buf[PROC_STAT_BUF_SIZE];

    int fd = open("/proc/stat", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
    {
        perror("Failed to open /proc/stat");
        return -1;
    }

    size_t read_size = 0u;
    do
    {
        ssize_t sz = read(fd, buf + read_size, sizeof(buf) - 1u - read_size);
        if (sz < 0)
        {
            int err = errno;
            if (err == EINTR)
                continue;

            perror("Failed to read /proc/stat");
            close(fd);
            return -1;
        }
        else if (sz == 0)
        {
            break;
        }

        read_size += sz;
    }
    while (read_size < sizeof(buf) - 1u);
    buf[read_size] = '\0';

    close(fd);

    if (buf[0] == 'c' && buf[1] == 'p' && buf[2] == 'u' && (buf[3] == ' ' || buf[3] == '\t'))
    {
        const char* p = skip_spaces(buf + 4);
        uint64_t idle_time = 0u, total_time = 0u;
        uint32_t column = 0u;
        while (*p >= '0' && *p <= '9')
        {
            const char* p_end;
            uint64_t ticks = strtoui64_cloc(p, &p_end);

            total_time += ticks;

            if (column == 3u || column == 4u)
                idle_time += ticks;

            ++column;
            p = skip_spaces(p_end);
        }

        g_idle_time = idle_time;
        g_total_time = total_time;
    }
    else
    {
        fputs("Unexpected first line format in /proc/stat", stderr);
        return -1;
    }

    return 0;
}

static const string_view g_sysfs_epp_path_prefix = STRING_VIEW_INIT("/sys/devices/system/cpu/cpufreq/policy");
static const string_view g_sysfs_epp_path_suffix = STRING_VIEW_INIT("/energy_performance_preference");

//! Sets EPP mode to the given value for all CPUs
static int set_epp(string_view epp_value)
{
    //printf("Setting EPP value %s\n", epp_value.str);

    char path[256u];
    memcpy(path, g_sysfs_epp_path_prefix.str, g_sysfs_epp_path_prefix.size);
    char* p = path + g_sysfs_epp_path_prefix.size;
    for (uint32_t i = 0u; i < 65536u; ++i)
    {
        char* q = ui32toa_cloc(i, p);
        memcpy(q, g_sysfs_epp_path_suffix.str, g_sysfs_epp_path_suffix.size);
        q[g_sysfs_epp_path_suffix.size] = '\0';

        int fd = open(path, O_WRONLY | O_CLOEXEC);
        if (fd < 0)
        {
            int err = errno;
            if (err == ENOENT)
                break;

            fprintf(stderr, "Failed to open EPP file: %s: error %d, %s\n", path, err, strerror(err));
            return -1;
        }

        size_t written_size = 0u;
        do
        {
            ssize_t sz = write(fd, epp_value.str, epp_value.size - written_size);
            if (sz < 0)
            {
                int err = errno;
                if (err == EINTR)
                    continue;

                perror("Failed to write EPP value");
                close(fd);
                return -1;
            }

            written_size += sz;
        }
        while (written_size < epp_value.size);

        close(fd);
    }

    return 0;
}

static inline int main_loop()
{
    struct timespec sleep_time = {};
    sleep_time.tv_sec = 1;
    unsigned int low_cpu_load_times = 0u;
    bool update_cpufreq = true;
    int res = 0;

    while (true)
    {
        nanosleep(&sleep_time, NULL);

        const uint64_t last_idle_time = g_idle_time;
        const uint64_t last_total_time = g_total_time;

        res = update_cpu_times();
        if (res < 0)
            return res;

        uint64_t idle_time_delta = g_idle_time - last_idle_time;
        uint64_t total_time_delta = g_total_time - last_total_time;
        uint64_t busy_time_delta = total_time_delta - idle_time_delta;

        if (!g_high_cpu_load)
        {
            // Switch to high load mode at 10% CPU utilization
            uint64_t threshold = total_time_delta / 10u;
            if (busy_time_delta > threshold)
            {
                g_high_cpu_load = true;
                update_cpufreq = true;
            }
        }
        else
        {
            // Switch to low load mode at 6.25% CPU utilization
            uint64_t threshold = total_time_delta / 16u;
            if (busy_time_delta < threshold)
            {
                // Wait for a few sleep times before dropping to low CPU load mode
                if (low_cpu_load_times < 2u)
                {
                    ++low_cpu_load_times;
                }
                else
                {
                    low_cpu_load_times = 0u;
                    g_high_cpu_load = false;
                    update_cpufreq = true;
                }
            }
        }

        if (update_cpufreq && !is_gamemode_active())
        {
            res = set_epp(g_high_cpu_load ? g_epp_high_load_value : g_epp_low_load_value);
            if (res < 0)
                return res;

            update_cpufreq = false;
        }
    }
}

int main(int argc, char* argv[])
{
    {
        bool enable_gamemode = false;
        for (int i = 1; i < argc; ++i)
        {
            if (strcmp(argv[i], "--enable-gamemode") == 0)
            {
                enable_gamemode = true;
            }
            else if (strcmp(argv[i], "--help") == 0)
            {
                print_help();
                return 0;
            }
            else
            {
                fprintf(stderr, "Unsupported option: %s\nUse --help to display supported options.\n", argv[i]);
                return 1;
            }
        }

        if (enable_gamemode)
            init_libgamemode();
    }

    if (update_cpu_times() < 0)
        return 1;

    if (main_loop() < 0)
        return 1;

    return 0;
}
