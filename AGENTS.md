# AI Agent Guide for FSA Driver

## Build

```
meson install -C build
```

## Run

All environment variables have been set. Run the program directly without any env setup.

## Debugging

Use gdb batch mode to check for segfaults:

```
gdb -batch -ex run -ex bt <program>
```

## Workflow

Modify header files (`.h`) first, then C/C++ files (`.c`/`.cpp`).