files_fsa_compiler= files(
  'main.c',
  'fsa_compiler.c',
  'fsa_nir_to_llvm.c',
  'fsa_llvm_build.c',
  'fsa_gen_test_nir.c',
  'fsa_ubench_menu.c',
  'fsa_bench.c',
  'argparse.c',
)

dep_ncurses = dependency('ncurses')

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
