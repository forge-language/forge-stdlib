# Forge standard library

Independent native C library extracted from [Forge](https://github.com/forge-language/forge)
commit `725aefb`. Apache-2.0; bundled third-party files keep their original notices.

```sh
cmake -S . -B build -DFORGE_RUNTIME_SOURCE_DIR=../forge-runtime
cmake --build build -j2
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$PWD/build/install"
```

Consume an installed package with `find_package(ForgeStd CONFIG REQUIRED)` and
link `ForgeStd::stdlib`. Headers install under `include/` and archives under `lib/`.

The standard library depends on ForgeRuntime. Set `FORGE_RUNTIME_SOURCE_DIR` for a
local runtime checkout, install ForgeRuntime into `CMAKE_PREFIX_PATH`, or let
FetchContent obtain the commit pinned in `cmake/dependencies.cmake`.

OpenCL, io_uring and OpenSSL are optional. Disable them with `FORGE_ENABLE_GPU`,
`FORGE_ENABLE_IO_URING` and `FORGE_ENABLE_TLS`. Missing dependencies use the existing
runtime fallbacks. `lib/forge.link` records additional libraries for Forge's driver.
