cmake_minimum_required(VERSION 3.16)
project(cotamer CXX)

option(SAN "Enable AddressSanitizer+UBSanitizer" OFF)
option(ASAN "Enable AddressSanitizer" OFF)
option(UBSAN "Enable UBSanitizer" OFF)
option(TSAN "Enable ThreadSanitizer" OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
  add_compile_options(/W4 $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/O2>)
  add_link_options(/DEBUG)
else()
  add_compile_options(-g -O3 -W -Wall)
  add_link_options(-g -O3 -W -Wall)
endif()

if(NOT MSVC)
  if((SAN AND NOT TSAN) OR UBSAN)
    add_compile_options(-fsanitize=undefined)
    add_link_options(-fsanitize=undefined)
  endif()
  if((SAN AND NOT TSAN) OR ASAN)
    add_compile_options(-fsanitize=address)
    add_link_options(-fsanitize=address)
  endif()
  if(TSAN)
    add_compile_options(-fsanitize=thread)
    add_link_options(-fsanitize=thread)
  endif()
endif()

if(MSVC)
  set(GETOPT_WIN_SRCS detail/getopt_win.cc)
else()
  set(GETOPT_WIN_SRCS "")
endif()

add_library(Cotamer OBJECT
    detail/cotamer.cc
    detail/utils.cc
)

add_executable(ping
    ping.cc
    $<TARGET_OBJECTS:Cotamer>
    ${GETOPT_WIN_SRCS}
)

add_executable(ctconsensus
    ctconsensus.cc
    $<TARGET_OBJECTS:Cotamer>
    ${GETOPT_WIN_SRCS}
)

add_executable(ctconsensus_weak_fd
    ctconsensus_weak_fd.cc
    $<TARGET_OBJECTS:Cotamer>
    ${GETOPT_WIN_SRCS}
)

add_executable(ctstubborn
    ctstubborn.cc
    $<TARGET_OBJECTS:Cotamer>
    ${GETOPT_WIN_SRCS}
)
