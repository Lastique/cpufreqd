# cpufreqd

This project implements a Linux daemon that monitors CPU usage and automatically adjusts cpufreq parameters to optimize power consumption and performance.

Currently, the daemon only supports Intel CPUs with EPP and switches `energy_performance_preference` hint between `balance_power` (which is used while the CPU is idle) and `balance_performance` (during the increased CPU utilization).

The daemon is incompatible with other daemons that modify cpufreq settings, such as [TLP](https://github.com/linrunner/TLP) or [power-profiles-daemon](https://github.com/Rongronggg9/power-profiles-daemon). The other daemons need to be disabled to avoid conflicts.

The daemon supports [gamemode](https://github.com/feralinteractive/gamemode) and will suspend its operation while gamemode is active. This way, gamemode can temporarily set cpufreq parameters for increased performance (e.g. during a gaming session) and cpufreqd won't interfere. Once gamemode deactivates, cpufreqd takes over the control again. In order for this integration to work, `libgamemode.so.0` must be installed on the system, which is typically installed along with gamemode itself.
