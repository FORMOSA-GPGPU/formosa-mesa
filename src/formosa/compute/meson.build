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

if with_formosa_vk
  sdk = get_option('formosa-sdk')
  if sdk == ''
    error('The Formosa Vulkan driver requires -Dformosa-sdk=/path/to/installed/sdk')
  endif
  cpp = meson.get_compiler('cpp')
  formosa = declare_dependency(
    include_directories: include_directories(sdk / 'include'),
    dependencies: [
      cpp.find_library('formosa-hal.lv', dirs: [sdk / 'lib']),
      cpp.find_library('formosa-real.lv', dirs: [sdk / 'lib']),
      cpp.find_library('falloc', dirs: [sdk / 'lib']),
    ],
  )
  entrypoints = custom_target(
    'fsa_entrypoints', input: [vk_entrypoints_gen, vk_api_xml],
    output: ['fsa_entrypoints.h', 'fsa_entrypoints.c'],
    command: [prog_python, '@INPUT0@', '--xml', '@INPUT1@', '--proto', '--weak',
              '--out-h', '@OUTPUT0@', '--out-c', '@OUTPUT1@', '--prefix', 'fsa',
              '--beta', with_vulkan_beta.to_string()],
    depend_files: vk_entrypoints_gen_depend_files,
  )
  shared_library('vulkan_formosa',
    ['vk_device.c', 'vk_resources.c', 'vk_images.c', 'vk_queries.c', 'vk_queue.c', entrypoints],
    include_directories: [inc_include, inc_src],
    dependencies: [compute_compiler_dep, formosa, idep_vulkan_runtime, idep_vulkan_util],
    link_language: 'cpp',
    gnu_symbol_visibility: 'hidden',
    install: true,
  )
  custom_target('formosa_icd',
    input: [vk_icd_gen, vk_api_xml], output: 'formosa_icd.json',
    command: [prog_python, '@INPUT0@', '--api-version', '1.1', '--xml', '@INPUT1@',
              '--lib-path', meson.current_build_dir() / 'libvulkan_formosa.so',
              '--out', '@OUTPUT@'],
    build_by_default: true,
  )
  custom_target('formosa_install_icd',
    input: [vk_icd_gen, vk_api_xml],
    output: 'formosa_icd.' + host_machine.cpu() + '.json',
    command: [prog_python, '@INPUT0@', '--api-version', '1.1', '--xml', '@INPUT1@',
              '--lib-path', get_option('prefix') / get_option('libdir') / 'libvulkan_formosa.so',
              '--out', '@OUTPUT@'],
    build_by_default: true,
    install: true,
    install_dir: with_vulkan_icd_dir,
    install_tag: 'runtime',
  )
endif
