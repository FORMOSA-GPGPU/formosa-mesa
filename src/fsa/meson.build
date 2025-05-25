files_fsa_compiler= files(
  'main.cc',
  'fsa_compiler.h',
  'fsa_compiler.cc',
)

fsa_compiler = executable(
  'fsa_compiler',
  files_fsa_compiler,
  c_args: ['-ggdb3'],
  cpp_args: ['-ggdb3'],
  dependencies: [idep_getopt, idep_mesautil],
  include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux],
  link_with : [libglsl_standalone],
  install: true
)
