# fsa

## Requirements
- mesa requirements
- ncurses
- formosa-llvm

## Build
```shell
# Mesa requires at least a gallium driver for x11 platform, therefore we have to configure at least one.
# native-file specifies the path to `llvm-config`
$ meson setup build --prefix=$(pwd)/build/install \
	--libdir=$(pwd)/builddir/install/lib \
	--buildtype=debug \
	-Dplatforms=x11 \
	-Dgallium-drivers=softpipe \
	-Dvulkan-drivers=[] \
	-Dglvnd=disabled \
	-Dc_cpp_args="-ggdb3" \
	-Dtools="glsl" \
	-Dcpp_rtti=false \
	--native-file caslab-tt.ini
$ ninja -C build install # prefix with "bear --" to generate compile_commands.json
$ LD_LIBRARY_PATH=$LLVM_LIB_DIR ./build/bin/fsa_compiler
```
## Thirdparties
- cofyc/argparse for CLI subcommands