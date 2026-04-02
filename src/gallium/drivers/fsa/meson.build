files_libfsa = files(
  'fsa_screen.c',
  'fsa_emu_resource.c',
  'falloc.c',
  'fsa_context.c',
  'fsa_shader.c',
  'fsa_draw.c',
)

libfsa = static_library(
  'fsa',
  files_libfsa,
  gnu_symbol_visibility : 'hidden',
  include_directories : [
    inc_include,
    inc_src,
    inc_mapi,
    inc_mesa,
    inc_gallium,
    inc_gallium_aux,
  ],
  dependencies : [idep_mesautil, idep_fsa_compiler, dep_uscsim],
)

driver_fsa = declare_dependency(
  compile_args : '-DGALLIUM_FSA',
  link_with : [libfsa],
  dependencies : [dep_uscsim],
)

if with_tests
  subdir('tests')
endif