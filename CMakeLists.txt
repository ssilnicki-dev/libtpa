cmake_minimum_required(VERSION 3.18)
project(libtpa C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(TPA_VERSION "1.0-rc0" CACHE STRING "libtpa version string")
find_package(PkgConfig REQUIRED)
pkg_check_modules(DPDK QUIET IMPORTED_TARGET dpdk)
if(NOT DPDK_FOUND)
  pkg_check_modules(DPDK REQUIRED IMPORTED_TARGET libdpdk)
endif()

set(TPA_SOURCES
  lib/utils.c
  src/archive.c
  src/arp.c
  src/cfg.c
  src/ctrl.c
  src/dev.c
  src/dpdk.c
  src/eth.c
  src/event.c
  src/log.c
  src/mem_file.c
  src/ndp.c
  src/neigh.c
  src/offload.c
  src/port_alloc.c
  src/shell.c
  src/sock.c
  src/stats.c
  src/tpa.c
  src/tpad.c
  src/trace.c
  src/worker.c
  src/tcp/tcp_input.c
  src/tcp/tcp_output.c
  src/tcp/tcp_timeout.c
  src/pktfuzz/pktfuzz.c
  src/pktfuzz/fuzzer_cut.c
  src/pktfuzz/fuzzer_delay.c
  src/pktfuzz/fuzzer_drop.c
  src/pktfuzz/fuzzer_dup.c
  src/pktfuzz/fuzzer_reorder.c
)


add_library(tpa STATIC ${TPA_SOURCES})
set_target_properties(tpa PROPERTIES OUTPUT_NAME tpa)

target_compile_features(tpa PRIVATE c_std_11)
target_compile_definitions(tpa PRIVATE ALLOW_EXPERIMENTAL_API TPA_DEBUG)

configure_file(${CMAKE_SOURCE_DIR}/cmake/version.h.in ${CMAKE_BINARY_DIR}/generated/include/version.h @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/cmake/build-info.h.in ${CMAKE_BINARY_DIR}/generated/include/build-info.h @ONLY)

target_include_directories(tpa
  PUBLIC
    ${CMAKE_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_BINARY_DIR}/generated/include
)

target_link_libraries(tpa PUBLIC PkgConfig::DPDK)

if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
  target_compile_definitions(tpa PRIVATE TPA_FREEBSD=1)
endif()

if(CMAKE_EXPORT_COMPILE_COMMANDS)
  add_custom_target(sync_compile_commands ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${CMAKE_BINARY_DIR}/compile_commands.json
      ${CMAKE_SOURCE_DIR}/compile_commands.json
    DEPENDS ${CMAKE_BINARY_DIR}/compile_commands.json
    VERBATIM
  )
endif()

