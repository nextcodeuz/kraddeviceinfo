cmake_minimum_required(VERSION 3.16)

project(KradDeviceInfo
    VERSION 1.1.0
    DESCRIPTION "KradDeviceInfo - complete Windows hardware & software inventory"
    LANGUAGES CXX)

set(APP_ID      "krad.device.info")
set(APP_NAME    "KradDeviceInfo")

option(KRAD_BUILD_TESTS "Build smoke tests" ON)

# ---- toolchain defaults ---------------------------------------------------
if (WIN32 AND NOT DEFINED CMAKE_SYSTEM_VERSION)
    set(CMAKE_SYSTEM_VERSION 10.0 CACHE STRING "Windows SDK version")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---- core library (Qt-free, portable) --------------------------------------
add_library(kradcore STATIC
    src/core/util.cpp
    src/core/report.cpp
    src/core/bench.cpp
)

target_include_directories(kradcore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(kradcore PRIVATE UNICODE _UNICODE)

if (MSVC)
    target_compile_options(kradcore PRIVATE /W4 /utf-8 /permissive-)
else()
    target_compile_options(kradcore PRIVATE -Wall -Wextra)
endif()

# ---- platform backends ------------------------------------------------------
if (WIN32)
    set(KRAD_PLATFORM_SOURCES
        src/platform/win32/wincompat.cpp
        src/platform/win32/wmi_helper.cpp
        src/platform/win32/pdh.cpp
        src/platform/win32/devices_guids.cpp
        src/platform/win32/os_system.cpp
        src/platform/win32/smbios.cpp
        src/platform/win32/cpu.cpp
        src/platform/win32/memory.cpp
        src/platform/win32/gpu.cpp
        src/platform/win32/storage.cpp
        src/platform/win32/network.cpp
        src/platform/win32/monitors.cpp
        src/platform/win32/devices.cpp
        src/platform/win32/software.cpp
        src/platform/win32/perfmon.cpp
        src/platform/win32/full_report.cpp
    )
    # compile floor Win7; runtime degrades gracefully on older systems
    target_compile_definitions(kradcore PUBLIC
        _WIN32_WINNT=0x0601 WINVER=0x0601 NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    set(KRAD_PLATFORM_SOURCES src/platform/linux_backend.cpp)
endif()

target_sources(kradcore PRIVATE ${KRAD_PLATFORM_SOURCES})

if (WIN32)
    target_link_libraries(kradcore PUBLIC
        ole32 oleaut32 wbemuuid uuid pdh iphlpapi ws2_32
        setupapi cfgmgr32 psapi shlwapi shell32 powrprof winmm
    )
else()
    find_package(Threads REQUIRED)
    target_link_libraries(kradcore PUBLIC Threads::Threads)
endif()

# ---- Qt application ---------------------------------------------------------
set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" CACHE STRING "Qt path")

find_package(Qt5 REQUIRED COMPONENTS Widgets Concurrent Network)

# Cross-compiling: Qt package ships host tools as .exe (unrunnable) -
# swap in Linux-host moc/rcc (their output is platform-independent).
if (CMAKE_CROSSCOMPILING AND KRAD_HOST_MOC)
    set(QT_MOC_EXECUTABLE      ${KRAD_HOST_MOC})
    set(QT_RCC_EXECUTABLE      ${KRAD_HOST_RCC})
    set(QT_LRELEASE_EXECUTABLE ${KRAD_HOST_LRELEASE})
    if (TARGET Qt5::moc)
        set_target_properties(Qt5::moc PROPERTIES
            IMPORTED_LOCATION ${KRAD_HOST_MOC})
    endif()
    if (TARGET Qt5::rcc)
        set_target_properties(Qt5::rcc PROPERTIES
            IMPORTED_LOCATION ${KRAD_HOST_RCC})
    endif()
    if (TARGET Qt5::qmake)
        set_target_properties(Qt5::qmake PROPERTIES
            IMPORTED_LOCATION ${KRAD_HOST_LRELEASE})
    endif()
endif()

add_executable(${APP_NAME} WIN32 MACOSX_BUNDLE
    src/app/main.cpp
    src/app/cli.cpp
    src/app/export.cpp
    src/app/online.cpp
    src/gui/theme.cpp
    src/gui/mainwindow.cpp
    src/gui/widgets/gauge.cpp
    src/gui/widgets/chart.cpp
    src/gui/widgets/pageparts.cpp
    src/gui/widgets/icons.cpp
)

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.rc")
    if (MINGW)
        target_sources(${APP_NAME} PRIVATE resources/app.rc)
    elseif (MSVC)
        target_sources(${APP_NAME} PRIVATE resources/app.rc)
    endif()
endif()

target_link_libraries(${APP_NAME} PRIVATE kradcore Qt5::Widgets Qt5::Concurrent Qt5::Network)
target_compile_definitions(${APP_NAME} PRIVATE UNICODE _UNICODE)

if (MSVC)
    target_compile_options(${APP_NAME} PRIVATE /W4 /utf-8 /MP)
    target_link_options(${APP_NAME} PRIVATE
        $<$<NOT:$<CONFIG:Debug>>:/OPT:REF /OPT:ICF>)
else()
    target_compile_options(${APP_NAME} PRIVATE -Wall -Wextra)
endif()

set_target_properties(${APP_NAME} PROPERTIES
    OUTPUT_NAME kraddeviceinfo
)

# ---- toolchain sanity --------------------------------------------------------
if (WIN32 AND MINGW AND KRAD_BUILD_TESTS)
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_FLAGS "-std=c++17")
    check_cxx_source_compiles(
        "#include <thread>\nint main(){ std::thread t([]{}); t.join(); return 0; }"
        KRAD_HAVE_STD_THREAD)
    if (NOT KRAD_HAVE_STD_THREAD)
        message(FATAL_ERROR
            "MinGW build requires the POSIX-threads variant of GCC "
            "(x86_64-w64-mingw32-g++-posix / MinGW-w64 posix toolchain). "
            "The benchmark module uses std::thread. Reinstall MinGW with "
            "posix threads or build with MSVC.")
    endif()
endif()

# ---- tests -------------------------------------------------------------------
if (KRAD_BUILD_TESTS AND WIN32)
    add_executable(krad_smoke tests/win_smoke.cpp)
    target_link_libraries(krad_smoke PRIVATE kradcore)
    target_compile_definitions(krad_smoke PRIVATE UNICODE _UNICODE)
endif()

# online services test (network + dashboard) - works on Linux demo backend too
if (KRAD_BUILD_TESTS AND NOT WIN32)
    add_executable(online_test tests/online_test.cpp src/app/online.cpp)
    target_include_directories(online_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(online_test PRIVATE kradcore
        Qt5::Widgets Qt5::Concurrent Qt5::Network)
    set_target_properties(online_test PROPERTIES WIN32_EXECUTABLE FALSE)
endif()

# ---- install -----------------------------------------------------------------
install(TARGETS ${APP_NAME}
        RUNTIME DESTINATION bin
        BUNDLE  DESTINATION .)
