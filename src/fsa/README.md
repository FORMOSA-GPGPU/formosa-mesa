# fsa

## Build
```shell
# Mesa requires at least a gallium driver for x11 platform, therefore we have to configure at least one.
# fsa tools will be automatically include in the build
$ meson setup build \
    --prefix=$(pwd)/build \
    --buildtype=debug \
    -Dplatforms=x11 \
    -Dgallium-drivers=softpipe \
    -Dvulkan-drivers=[]
$ ninja -C build install # prefix with "bear --" to generate compile_commands.json
or
# The executable will be located at `build/bin`
$ ./build/bin/fsa_compiler example.vert
```
