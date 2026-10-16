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

HTTP worker startup retains the original listening socket and its queued clients.
The listener remains exclusive before multi-worker serving begins. On Linux and
macOS, startup tries to enable port sharing and add worker listeners; if a bind
fails, workers share the original nonblocking listener. Other platforms retain
the original worker when additional listeners cannot be created. Allocation or
first-thread failure serves on the original socket in the caller. Port zero is
resolved to the actual bound port before creating additional listeners. Sharing
is enabled only after the first worker starts, so those early fallbacks keep an
exclusive listener. These servers and their detached-worker contexts have
process lifetime; this is not a
graceful shutdown API or a cross-platform throughput guarantee.

HTTP workers inherit the caller's CPU affinity rather than imposing a CPU-ID
mapping. With a matching runtime advertising
`FORGE_PLATFORM_HAS_AVAILABLE_CPU_COUNT`, automatic HTTP pool sizes use the
caller's allowed CPU count. Older pinned runtime headers retain the prior online
count fallback, without introducing an unresolved new runtime symbol. Explicit
thread counts retain their existing meaning. `thread_cpu_count()` continues to
report the runtime's online CPU count. `thread_pin()` is an explicit best-effort
Linux request, may replace an inherited mask, and ignores out-of-range int64 IDs
before narrowing them to C int. Placement success must be checked through the OS.
Actual pinning depends on the selected runtime; the older pinned Linux helper
remains a no-op.
Existing backend sizing rules still apply, including TLS's minimum and multiplier
and io_uring's multiplier; an affinity-aware input does not remove their deliberate
oversubscription.
