# cpufreqd

This project implements a Linux daemon that monitors CPU usage and automatically adjusts cpufreq parameters to optimize power consumption and performance.

Currently, the daemon only supports Intel CPUs with EPP and switches `energy_performance_preference` hint between `balance_power` (which is used while the CPU is idle) and `balance_performance` (during the increased CPU utilization).

The daemon is incompatible with other daemons that modify cpufreq settings, such as [TLP](https://github.com/linrunner/TLP) or [power-profiles-daemon](https://github.com/Rongronggg9/power-profiles-daemon). The other daemons need to be disabled to avoid conflicts.

# Command Line Interface

The daemon can be controlled from the command line using the `cpufreqd-ctl` utility. Run `cpufreqd-ctl help` for the list of supported commands.

In particular, `cpufreqd-ctl suspend` adds a suspend request to the running cpufreqd daemon. While there is at least one suspend request active, the daemon will not adjust cpufreq parameters. The command returns a cookie that can be used in the subsequent `cpufreqd-ctl resume <cookie>` command to remove the added suspend request.

In order for the `cpufreqd-ctl` utility to function, the daemon must be run with `--enable-dbus` parameter and with system D-Bus available.

# Gamemode Integration

The daemon can be used together with [gamemode](https://github.com/feralinteractive/gamemode). For this, gamemode should be configured to either not touch cpufreq parameters such as CPU governor and EPP, or to suspend and resume cpufreqd when a game is started and finished. For example, the following lines can be added in `/etc/gamemode.ini`:

```
[custom]
; Custom scripts (executed using the shell) when gamemode starts and ends
start=file="/tmp/gamemode_$PPID.cpufreqd_cookie"; [ -f "$file" ] || /usr/bin/cpufreqd-ctl suspend > "$file"
end=file="/tmp/gamemode_$PPID.cpufreqd_cookie"; if [ -f "$file" ]; then read -r cookie < "$file" && rm -f "$file" && /usr/bin/cpufreqd-ctl resume "$cookie"; fi
```
