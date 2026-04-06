cmake_minimum_required(VERSION 3.20)
project(cpufreqd)

set(CMAKE_C_STANDARD 11)

add_subdirectory(src)

install(FILES conf/org.cpufreqd.conf DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/dbus-1/system.d)
install(PROGRAMS cpufreqd-ctl TYPE BIN)
