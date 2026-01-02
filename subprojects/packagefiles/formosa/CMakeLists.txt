cmake_minimum_required(VERSION 3.24)
project(uscsim_shim)

# Add uscsim thirdparty deps

include(third-party/fmt.cmake)
include(third-party/softfloat.cmake)
include(third-party/thread-pool.cmake)
add_subdirectory(third-party/elfio)

# uscsim source
add_library(uscsim STATIC
  gfx/uscsim/decode.cc
  gfx/uscsim/execute.cc
  gfx/uscsim/instr.cc
  gfx/uscsim/simt.cc
  gfx/uscsim/cbind.cc
)

set_target_properties(uscsim PROPERTIES
  CXX_STANDARD 20
  # Fixes clangd -fmodules unknown option error
  CXX_SCAN_FOR_MODULES OFF
)

target_link_libraries(uscsim
  PUBLIC softfloat
  PUBLIC fmt
  PUBLIC thread-pool
  PUBLIC elfio
)

target_include_directories(uscsim
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/gfx/"
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/gfx/uscsim"
)

target_compile_options(uscsim PRIVATE
  -ggdb3
  $<$<CONFIG:Debug>:-O0>
  $<$<BOOL:${USCSIM_DEBUG_LOG}>:-DDEBUG_LOG>
)