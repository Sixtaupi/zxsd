# Host example

This example uses one source tree on Linux, macOS, and Windows. The
`zxsd_generate()` CMake helper runs `zxsdc` with the host Python interpreter,
compiles the generated validator, and links it with `libzxsd`.

## Linux and macOS

```sh
cmake -S samples/host -B build-example -DZXSD_USE_MXML=OFF
cmake --build build-example
./build-example/zxsd_example
```

## Windows

From PowerShell with Visual Studio Build Tools installed:

```powershell
cmake -S samples/host -B build-example -DZXSD_USE_MXML=OFF
cmake --build build-example --config Release
.\build-example\Release\zxsd_example.exe
```

With Ninja, the executable is instead located directly in `build-example`, as
on Linux and macOS.

Expected output:

```text
valid document: ok
invalid document: invalid primitive value (offset=45, schema item=enabled)
event stream: ok
```
