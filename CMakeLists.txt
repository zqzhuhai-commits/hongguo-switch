cmake_minimum_required(VERSION 3.15)
option(HONGGUO_PREVIEW "Build desktop screenshots from the native Switch UI" OFF)
if(HONGGUO_PREVIEW)
  include(tests/preview/CMakeLists.txt)
  return()
endif()
set(BOREALIS_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/nx-player/library/borealis/library)
include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)
option(USE_SHARED_LIB "" OFF)
cmake_dependent_option(USE_SYSTEM_FMT "" OFF "NOT USE_SHARED_LIB" ON)
cmake_dependent_option(USE_SYSTEM_TINYXML2 "" OFF "NOT USE_SHARED_LIB" ON)
cmake_dependent_option(USE_SYSTEM_TWEENY "" OFF "NOT USE_SHARED_LIB" ON)
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)
project(hongguo-switch LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(PROJECT_TITLEID "0100F00D53414600")
set(APP_TITLE "红果短剧")
add_subdirectory(${BOREALIS_LIBRARY} borealis)
find_package(PkgConfig REQUIRED)
set(PKG_CONFIG_USE_STATIC_LIBS ON)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
pkg_check_modules(POSTER_CODECS REQUIRED IMPORTED_TARGET libavcodec libswscale)
set(QJS ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/quickjs)
add_library(qjs STATIC ${QJS}/quickjs.c ${QJS}/dtoa.c ${QJS}/libregexp.c ${QJS}/libunicode.c ${QJS}/cutils.c)
target_compile_definitions(qjs PRIVATE _GNU_SOURCE __SWITCH__ CONFIG_VERSION="2026-06-04")
target_compile_options(qjs PRIVATE -fwrapv -O2 -g1)
target_include_directories(qjs PUBLIC ${QJS})
set(SRC native/main.cpp native/backend.cpp native/poster.cpp native/poster_decode.cpp native/workers.cpp ${BOREALIS_LIBRARY}/lib/platforms/switch/switch_wrapper.c)
program_target(${PROJECT_NAME} "${SRC}")
target_include_directories(${PROJECT_NAME} PRIVATE native ${BOREALIS_LIBRARY}/include/borealis/extern ${BOREALIS_LIBRARY}/lib/extern/glfw/include)
target_compile_options(${PROJECT_NAME} PRIVATE -g1 -fexceptions -ffunction-sections -fdata-sections)
target_link_options(${PROJECT_NAME} PRIVATE -Wl,--gc-sections)
target_link_libraries(${PROJECT_NAME} PRIVATE borealis qjs PkgConfig::MPV PkgConfig::CURL PkgConfig::POSTER_CODECS mbedtls mbedx509 mbedcrypto z ${APP_PLATFORM_LIB})
add_custom_target(package-input DEPENDS ${PROJECT_NAME}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/package/exefs ${CMAKE_BINARY_DIR}/package/control
  COMMAND ${NX_NACPTOOL_EXE} --create "${APP_TITLE}" "SAF contributors" "1.1.1" ${CMAKE_BINARY_DIR}/package/control/control.nacp --titleid=${PROJECT_TITLEID}
  COMMAND ${DEVKITPRO}/tools/bin/elf2nso ${PROJECT_NAME}.elf ${CMAKE_BINARY_DIR}/package/exefs/main
  COMMAND ${DEVKITPRO}/tools/bin/npdmtool ${CMAKE_CURRENT_SOURCE_DIR}/npdm.json ${CMAKE_BINARY_DIR}/package/exefs/main.npdm
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/icon.jpg ${CMAKE_BINARY_DIR}/package/control/icon_AmericanEnglish.dat
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${BOREALIS_LIBRARY}/../resources ${CMAKE_BINARY_DIR}/package/romfs
  COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/package/romfs/font ${CMAKE_BINARY_DIR}/package/romfs/img/pokemon ${CMAKE_BINARY_DIR}/package/romfs/xml ${CMAKE_BINARY_DIR}/package/romfs/icon
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/romfs ${CMAKE_BINARY_DIR}/package/romfs
  COMMAND ${NX_ELF2NRO_EXE} ${PROJECT_NAME}.elf ${PROJECT_NAME}.nro --icon=${CMAKE_CURRENT_SOURCE_DIR}/icon.jpg --nacp=${CMAKE_BINARY_DIR}/package/control/control.nacp --romfsdir=${CMAKE_BINARY_DIR}/package/romfs
)
