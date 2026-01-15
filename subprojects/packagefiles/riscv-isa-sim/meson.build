project('spike', 'cpp', 'c', license : 'BSD', default_options: ['default_library=static'])

mod = import('unstable-external_project')

p = mod.add_project('configure',
  configure_options : [
    '--without-boost',
    '--prefix=@PREFIX@',
  ],
  verbose : true,
)

libriscv = p.dependency('riscv') 
libdisasm = p.dependency('disasm')
libfesvr = p.dependency('fesvr')

spike_inc = include_directories('.', 'riscv')

dep_spike = declare_dependency(
  dependencies: [libriscv, libdisasm, libfesvr],
  include_directories: [spike_inc],
)

meson.override_dependency('riscv-isa-sim', dep_spike)