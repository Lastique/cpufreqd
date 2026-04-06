find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSYSTEMD REQUIRED IMPORTED_TARGET libsystemd)

add_executable(cpufreqd
    cpufreqd.c
    dbus.c
)

target_link_libraries(cpufreqd PRIVATE PkgConfig::LIBSYSTEMD)

install(TARGETS cpufreqd)
