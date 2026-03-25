cmake_minimum_required(VERSION 3.24)
project(uscsim_shim)

# Add uscsim thirdparty deps

include(third-party/fmt.cmake)
include(third-party/softfloat.cmake)
include(third-party/thread-pool.cmake)
add_subdirectory(third-party/elfio)
add_subdirectory(third-party/glm)

# Find LLVM for AOT
execute_process(
  COMMAND llvm-config --cmakedir
  OUTPUT_VARIABLE LLVM_CMAKE_DIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

list(APPEND CMAKE_PREFIX_PATH "${LLVM_CMAKE_DIR}")
find_package(LLVM REQUIRED CONFIG)

set(LLVM_LINK_COMPONENTS
  # LLVM IR
  Analysis
  Core
  Support
  OrcJIT
  ExecutionEngine
  MC
  Target
  Passes
  TransformUtils
  ScalarOpts
  InstCombine
  IPO
  # Host
  TargetParser
  native
  nativecodegen
)

llvm_map_components_to_libnames(llvm_libs ${LLVM_LINK_COMPONENTS})

# aot_softfloat_helper
add_library(aot_softfloat_helper STATIC
  gfx/uscsim/aot_softfloat_helper.cc
)

target_link_libraries(aot_softfloat_helper PRIVATE
  softfloat
)

target_compile_options(aot_softfloat_helper PRIVATE
  -ggdb3
  -fPIC
  $<$<CONFIG:Debug>:-O0>
)

set_target_properties(aot_softfloat_helper PROPERTIES
  CXX_STANDARD 20
  CXX_SCAN_FOR_MODULES OFF
)

# uscsim source
add_library(uscsim STATIC
  # uscsim_core sources
  gfx/uscsim/decode.cc
  gfx/uscsim/execute.cc
  gfx/uscsim/instr.cc
  gfx/uscsim/simt.cc
  gfx/uscsim/kernel.cc
  gfx/uscsim/bin_raster_sim.cc
  gfx/uscsim/pix_raster_sim.cc
  gfx/uscsim/render_output_sim.cc
  gfx/uscsim/gutil.cc
  gfx/uscsim/gfxemu.cc
  gfx/uscsim/texture_sim.cc
  gfx/uscsim/riscv_aot.cc
  # uscsim wrapper sources
  gfx/uscsim/uscsim.cc
  gfx/uscsim/cbind.cc
  gfx/uscsim/rsp_stub.cc
  gfx/uscsim/uscsim_mt.cc
  gfx/uscsim/uscsim_system.cc
  gfx/uscsim/gfxemu_bin_raster_tester.cc
  gfx/uscsim/gfxemu_pix_raster_tester.cc
  gfx/uscsim/gfxemu_render_output_tester.cc
  gfx/uscsim/gfxemu_texture_tester.cc
)

set_target_properties(uscsim PROPERTIES
  CXX_STANDARD 20
  # Fixes clangd -fmodules unknown option error
  CXX_SCAN_FOR_MODULES OFF
)

# Force add LLVM include dirs for clangd
foreach(DIR IN LISTS LLVM_INCLUDE_DIRS)
  target_compile_options(uscsim PUBLIC "-I${DIR}")
endforeach()

target_compile_definitions(uscsim
  PRIVATE ${LLVM_DEFINITIONS}
)

target_link_options(uscsim
  PUBLIC "-rdynamic"
)

target_link_libraries(uscsim
  PUBLIC softfloat
  PUBLIC fmt
  PUBLIC thread-pool
  PUBLIC elfio
  PUBLIC glm
  PUBLIC ${llvm_libs}
  PUBLIC "-Wl,--whole-archive"
  PUBLIC aot_softfloat_helper
  PUBLIC "-Wl,--no-whole-archive"
)

target_include_directories(uscsim
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/gfx/"
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/gfx/uscsim"
)

target_compile_options(uscsim PRIVATE
  -ggdb3
  -fPIC
  $<$<CONFIG:Debug>:-O0>
  $<$<BOOL:${USCSIM_DEBUG_LOG}>:-DDEBUG_LOG>
  PUBLIC -Wno-ignored-attributes
)
