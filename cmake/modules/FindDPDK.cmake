# SPDX-License-Identifier: BSD-3-Clause

include(FindPackageHandleStandardArgs)
find_package(PkgConfig QUIET)

if(PkgConfig_FOUND)
  pkg_check_modules(PC_DPDK QUIET dpdk libdpdk)
endif()

set(DPDK_VERSION "${PC_DPDK_VERSION}")
set(DPDK_INCLUDE_DIRS ${PC_DPDK_INCLUDE_DIRS})
set(DPDK_LIBRARIES ${PC_DPDK_LINK_LIBRARIES})

find_package_handle_standard_args(DPDK
  REQUIRED_VARS DPDK_INCLUDE_DIRS DPDK_LIBRARIES
  VERSION_VAR DPDK_VERSION
)

if(DPDK_FOUND AND NOT TARGET DPDK::dpdk)
  add_library(DPDK::dpdk INTERFACE IMPORTED)
  target_include_directories(DPDK::dpdk INTERFACE ${DPDK_INCLUDE_DIRS})
  target_compile_options(DPDK::dpdk INTERFACE ${PC_DPDK_CFLAGS_OTHER})
  target_link_libraries(DPDK::dpdk INTERFACE ${DPDK_LIBRARIES})
endif()
