# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
# SPDX-License-Identifier: MIT

llvm_bindir = dep_llvm.get_variable(configtool: 'bindir')
fsa_clang = find_program('clang', dirs: [llvm_bindir])
fsa_clang_link = find_program('formosa-clang')

compute_start = custom_target(
  'fsa-compute-start', input: 'start.S', output: 'start.o',
  command: [fsa_clang, '--target=riscv64-unknown-elf', '-mcpu=formosa-gpgpu',
            '-mabi=lp64', '-c', '@INPUT@', '-o', '@OUTPUT@'],
)
compute_start_h = custom_target(
  'fsa-compute-start-header', input: compute_start, output: 'start.h',
  command: [prog_python, files('../../util/xxd.py'), '@INPUT@', '@OUTPUT@',
            '-b', '-n', 'fsa_compute_start'],
)
compute_fs = import('fs')
pocl_prefix = compute_fs.parent(compute_fs.parent(find_program('poclcc').full_path()))
pocl_include = include_directories(pocl_prefix / 'share/pocl/include')
pocl_kernel = pocl_prefix / 'share/pocl/kernel-riscv64-formosa.bc'
compute_compiler = static_library(
  'fsa-compute-compiler',
  ['compiler.c', '../fsa_nir_to_llvm.c', '../fsa_llvm_build.c',
   '../fsa_compute_intrinsics.c', '../fsa_util.c', compute_start_h],
  include_directories: [inc_include, inc_src, inc_mesa, include_directories('..'), pocl_include],
  dependencies: [idep_vtn, idep_nir, idep_mesautil, dep_llvm],
  c_args: ['-DFORMOSA_CLANG="' + fsa_clang_link.full_path() + '"',
           '-DFORMOSA_KERNEL="' + pocl_kernel + '"', '-DFSA_COMPUTE'],
)
compute_compiler_dep = declare_dependency(
  link_with: compute_compiler,
  dependencies: [idep_vtn, idep_nir, idep_mesautil, dep_llvm],
  include_directories: [include_directories('..'), pocl_include],
)
executable('fsa-compute', 'main.c', dependencies: compute_compiler_dep, install: true)
