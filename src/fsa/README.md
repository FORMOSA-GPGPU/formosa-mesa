# fsa

## Requirements
- mesa requirements
- ncurses
- formosa-llvm
- spike deps
- go (OPTIONAL, for tools)

## Build
```shell
# Mesa requires at least a gallium driver for x11 platform, therefore we have to configure at least one.
# native-file specifies the path to `llvm-config`
# llvm must support X86 backend (used by native JIT)
# fsa-bench and fsa-tools are optional dev tools
$ meson setup build --prefix=$(pwd)/build/install \
	--libdir=$(pwd)/build/install/lib \
	--buildtype=debug \
	-Dplatforms=x11 \
	-Dgallium-drivers="softpipe,formosa" \
	-Dvulkan-drivers=[] \
	-Dglvnd=disabled \
	-Dc_cpp_args="-ggdb3" \
	-Dtools="glsl,fsa-bench,fsa-tools" \
	-Dcpp_rtti=false \
	--native-file caslab-tt.ini
$ ninja -C build install # prefix with "bear --" to generate compile_commands.json
$ LD_LIBRARY_PATH=$LLVM_LIB_DIR ./build/bin/fsa_compiler
```
## Thirdparties
- cofyc/argparse for CLI subcommands

## tools
Useful development tools will be housed under `tools` dir
- asm_annotate: Annotates the output RISC-V asm with LLVM IR for better debugging