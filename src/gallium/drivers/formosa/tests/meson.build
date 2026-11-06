# Copyright © 2026 Formosa
# SPDX-License-Identifier: MIT

executable(
  'tri_nir',
  'tri_nir.c',
  include_directories : [
    inc_include,
    inc_src,
    inc_mapi,
    inc_mesa,
    inc_gallium,
    inc_gallium_aux,
    inc_compiler,
    inc_nir,
  ],
  link_with : [libgallium, libpipe_loader_dynamic],
  dependencies : [idep_mesautil, idep_nir],
  install : false,
)
