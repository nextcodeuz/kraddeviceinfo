# Cross-compile toolchain: Linux host -> Windows x64 (MinGW-w64) with
# Windows Qt 5.15.2 (win64_mingw81) libraries fetched via aqtinstall.
# Host tools (moc/rcc) come from the Linux Qt5 dev package (output is
# platform-independent).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)

find_program(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}-gcc-posix)
find_program(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++-posix)
find_program(CMAKE_RC_COMPILER  ${TOOLCHAIN_PREFIX}-windres)

set(CMAKE_FIND_ROOT_PATH
    /usr/${TOOLCHAIN_PREFIX}
    /opt/qt-win/5.15.2/mingw81_64
)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Qt package path
set(QT_MINGW_DIR /opt/qt-win/5.15.2/mingw81_64 CACHE PATH "")
set(CMAKE_PREFIX_PATH ${QT_MINGW_DIR})

# Use Linux-host moc/rcc/lrelease (byte-identical output, exe won't run here)
set(QT_MOC_EXECUTABLE /usr/lib/qt5/bin/moc     CACHE FILEPATH "")
set(QT_RCC_EXECUTABLE /usr/lib/qt5/bin/rcc     CACHE FILEPATH "")
set(QT_LRELEASE_EXECUTABLE /usr/lib/qt5/bin/lrelease CACHE FILEPATH "")
set(QT_LUPDATE_EXECUTABLE /usr/lib/qt5/bin/lupdate   CACHE FILEPATH "")

# Qt5Config extras try to locate mkspecs via qmake.exe - point manually
set(QT_MKSPECS_DIR ${QT_MINGW_DIR}/mkspecs CACHE PATH "")
