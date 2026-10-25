go_prog = find_program('go', required : false)

if not go_prog.found()
  warning('Go compiler not found, skipping annotate_asm')
  subdir_done()
endif

asm_annotate_sources = files(
  'asm_annotate.go',
  'prompt.tmpl',
)

custom_target('asm_annotate',
  output : 'asm_annotate',
  input : asm_annotate_sources,
  command : [
    'sh', '-c',
    'cd "@CURRENT_SOURCE_DIR@" && "$1" build -o "$2/@OUTPUT@" .',
    '--', 
    go_prog.full_path(),
    meson.project_build_root()
  ],
  install : true,
  install_dir : get_option('bindir'),
  build_by_default : true
)