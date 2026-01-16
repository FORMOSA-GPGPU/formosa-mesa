# lld
llvm_libdir = dep_llvm.get_variable(configtool : 'libdir')
cpp = meson.get_compiler('cpp')
lib_lld_elf = cpp.find_library('lldELF', dirs : llvm_libdir)
lib_lld_common = cpp.find_library('lldCommon', dirs : llvm_libdir)

dep_lld = declare_dependency(
  dependencies : [
    lib_lld_elf,
    lib_lld_common,
    dep_llvm 
  ],
)

# GPU libm
files_libm = files(
  'libm/sin.c',
  'libm/cos.c',
  'libm/ldexp.c',
  'libm/sinf.c',
  'libm/cosf.c',
  'libm/ldexpf.c',
  'libm/exp2f.c',
  'libm/log2f.c',
  'libm/floor.c',
  # Internal
  'libm/error.c',
  'libm/__sin.c',
  'libm/__cos.c',
  'libm/__rem_pio2.c',
  'libm/__rem_pio2f.c',
  'libm/__rem_pio2_large.c',
  'libm/sindf.c',
  'libm/cosdf.c',
  'libm/exp2f_data.c',
  'libm/log2f_data.c',
  'libm/scalbn.c',
  'libm/scalbnf.c',
)

# formosa for uscsim
cmake = import('cmake')
formosa_opt = cmake.subproject_options()
formosa_opt.add_cmake_defines({
  'CMAKE_BUILD_TYPE': 'Debug',
})
formosa = cmake.subproject('formosa', options: formosa_opt)
message('CMake targets:\n - ' + '\n - '.join(formosa.target_list()))
dep_uscsim = formosa.dependency('uscsim')

# Force use fsa-llvm
llvm_bindir = dep_llvm.get_variable(configtool: 'bindir')
fsa_clang = find_program('clang', dirs: [llvm_bindir])

clang_c_to_bc_cpu = generator(
  fsa_clang,
  arguments : [
    '-emit-llvm',
    '-c', '@INPUT@',
    '-o', '@OUTPUT@',
    '-target', 'riscv64-unknown-elf',
    '-march=rv64imfd_zicsr',
    '-nostdinc',
    '-ffreestanding',
    '-fno-builtin',
    '-ffunction-sections',
    '-fdata-sections',
    '-O3',
    '-I@CURRENT_SOURCE_DIR@/libm',
  ],
  output : '@BASENAME@.bc'
)

clang_c_to_bc_gpu = generator(
  fsa_clang,
  arguments : [
    '-emit-llvm',
    '-c', '@INPUT@',
    '-o', '@OUTPUT@',
    '-target', 'riscv64-unknown-elf',
    '-mcpu=formosa-gpgpu',
    '-nostdinc',
    '-ffreestanding',
    '-fno-builtin',
    '-ffunction-sections',
    '-fdata-sections',
    '-O3',
    '-I@CURRENT_SOURCE_DIR@/libm',
  ],
  output : '@BASENAME@.bc'
)

fsa_libm_bc_files = clang_c_to_bc_cpu.process(
  files_libm,
  preserve_path_from : meson.current_source_dir()
)

fsa_libm_gpu_bc_files = clang_c_to_bc_gpu.process(
  files_libm,
  preserve_path_from : meson.current_source_dir()
)

# Link all math bc
fsa_llvm_link = find_program('llvm-link', dirs: [llvm_bindir])
fsa_libm_linked_bc = custom_target(
  'fsa_libm.bc',
  input: fsa_libm_bc_files,
  output: 'fsa_libm.bc',
  command : [
    fsa_llvm_link,
    '@INPUT@',
    '-o', '@OUTPUT@'
  ],
)

fsa_libm_bc_h = custom_target(
  'fsa_libm_bc.h',
  input : fsa_libm_linked_bc,
  output : 'fsa_libm_bc.h',
  command : [
    prog_python,
    join_paths(meson.global_source_root(), 'src/util/xxd.py'),
    '@INPUT@',
    '@OUTPUT@',
    '-b',
    '-n', 'fsa_libm_bc'
  ]
)

# GPU libm
fsa_libm_gpu_linked_bc = custom_target(
  'fsa_libm_gpu.bc',
  input: fsa_libm_gpu_bc_files,
  output: 'fsa_libm_gpu.bc',
  command : [
    fsa_llvm_link,
    '@INPUT@',
    '-o', '@OUTPUT@'
  ],
)

fsa_libm_gpu_bc_h = custom_target(
  'fsa_libm_gpu_bc.h',
  input : fsa_libm_gpu_linked_bc,
  output : 'fsa_libm_gpu_bc.h',
  command : [
    prog_python,
    join_paths(meson.global_source_root(), 'src/util/xxd.py'),
    '@INPUT@',
    '@OUTPUT@',
    '-b',
    '-n', 'fsa_libm_gpu_bc'
  ]
)

fsa_spike_ld = custom_target(
  'fsa_spike.ld.h',
  input : 'fsa_spike.ld',
  output : 'fsa_spike.ld.h',
  command : [
    prog_python,
    join_paths(meson.global_source_root(), 'src/util/xxd.py'),
    '@INPUT@',
    '@OUTPUT@',
    '-n', 'fsa_spike_ld'
  ]
)

fsa_spike_startup = custom_target(
  'fsa_spike.S.h',
  input : 'fsa_spike.S',
  output : 'fsa_spike.S.h',
  command : [
    prog_python,
    join_paths(meson.global_source_root(), 'src/util/xxd.py'),
    '@INPUT@',
    '@OUTPUT@',
    '-n', 'fsa_spike_startup'
  ]
)

fsa_uscsim_ld = custom_target(
  'fsa_uscsim.ld.h',
  input : 'fsa_uscsim.ld',
  output : 'fsa_uscsim.ld.h',
  command : [
    prog_python,
    join_paths(meson.global_source_root(), 'src/util/xxd.py'),
    '@INPUT@',
    '@OUTPUT@',
    '-n', 'fsa_uscsim_ld'
  ]
)

fsa_uscsim_startup = custom_target(
  'fsa_uscsim.S.h',
  input : 'fsa_uscsim.S',
  output : 'fsa_uscsim.S.h',
  command : [
    prog_python,
    join_paths(meson.global_source_root(), 'src/util/xxd.py'),
    '@INPUT@',
    '@OUTPUT@',
    '-n', 'fsa_uscsim_startup'
  ]
)

files_fsa_compiler= files(
  'main.c',
  'fsa_compiler.c',
  'fsa_nir_to_llvm.c',
  'fsa_llvm_build.c',
  'fsa_gen_test_nir.c',
  'fsa_ubench_menu.c',
  'fsa_bench.c',
  'fsa_llvm_verify.c',
  'fsa_util.c',
  'fsa_lld_helper.cc',
  'fsa_spike_helper.cc',
  'argparse.c',
  'fsa_lower_viewport_transform.c',
  'fsa_lower_viewport_to_ubo.c'
)

dep_ncurses = dependency('ncurses')
dep_libsoftfloat = dependency('berkeley-softfloat-3')
dep_spike = dependency('riscv-isa-sim')

fsa_rpath = meson.global_build_root()/'subprojects/riscv-isa-sim/build' + ':'

fsa_compiler = executable(
  'fsa_compiler',
  [
    files_fsa_compiler,
    fsa_libm_bc_h,
    fsa_libm_gpu_bc_h,
    fsa_spike_ld,
    fsa_spike_startup,
    fsa_uscsim_ld,
    fsa_uscsim_startup,
  ],
  c_args: ['-ggdb3'],
  cpp_args: ['-ggdb3', '-fno-rtti'],
  override_options : ['cpp_std=c++20'],
  dependencies: [
    idep_getopt,
    idep_mesautil,
    idep_nir,
    dep_llvm,
    dep_ncurses,
    dep_libsoftfloat,
    dep_elf,
    dep_lld,
    dep_spike,
    dep_uscsim,
  ],
  include_directories : [
    inc_include,
    inc_src,
    inc_mapi,
    inc_mesa,
    inc_gallium,
    inc_gallium_aux
  ],
  link_with : [libglsl_standalone],
  build_rpath: fsa_rpath,
  install: true
)
