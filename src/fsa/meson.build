# libriscv dep
cmake = import('cmake')
riscv_opts = cmake.subproject_options()
riscv_opts.add_cmake_defines({
  'RISCV_32I': false,
  'RISCV_BINARY_TRANSLATION': false,
  'RISCV_LIBTCC': false,
  'RISCV_EXT_A': false,
  'RISCV_EXT_C': false,
  'CMAKE_BUILD_TYPE': 'Release',
})
riscv_proj = cmake.subproject('libriscv', options: riscv_opts)
dep_libriscv = riscv_proj.dependency('riscv')

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
  'argparse.c',
)

dep_ncurses = dependency('ncurses')
dep_libsoftfloat = dependency('berkeley-softfloat-3')

fsa_compiler = executable(
  'fsa_compiler',
  files_fsa_compiler,
  c_args: ['-ggdb3'],
  cpp_args: ['-ggdb3', '-fno-rtti'],
  dependencies: [
    idep_getopt,
    idep_mesautil,
    idep_nir,
    dep_llvm,
    dep_ncurses,
    dep_libriscv,
    dep_libsoftfloat,
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
  install: true
)
