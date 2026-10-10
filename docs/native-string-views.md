# Native string view accessors

Newly compiled native callers inline the checked `fr_str_view_len` and
`fr_str_view_at` reads through the installed `forge/string.h` header. This lets
the caller optimize repeated byte scans without an external function call for
every byte. The exported functions remain available for existing binaries,
function pointers, and callers defining `FORGE_STRING_NO_INLINE` before including
the header. Existing binaries require recompilation to use the inline path.

The library and inline readers share the layout in
`forge/detail/string_views.h`: a borrowed byte pointer followed by its cached
`size_t` length. That layout matches the previous implementation. Because new
callers read it directly, future native layout changes require ABI compatibility
review; the internal header is not an API for manufacturing handles.

Zero handles yield length zero and byte -1. Negative and out-of-range indices
also yield -1; valid bytes are returned unsigned, including partial UTF-8 bytes.
A null source produces an empty view. Each macro argument is evaluated once.
Views borrow immutable NUL-terminated bytes and remain valid only while their
source and arena remain alive. Arena reset invalidates views, builders, and
snapshots. Arbitrary or stale integer handles remain outside the contract; these
checks do not establish general memory safety.

Builder snapshots still copy immutable bytes, and this optimization changes no
builder allocation, UTF-8, coroutine arena, or reset behavior. The same contract
suite runs against both the inline header and the external ABI. Performance
measurements live in the separate forge-benchmarks repository with preserved
compiler, archive, and header hashes.

The byte builder now also exposes a checked capacity fast path to newly compiled
callers. It accepts only bytes 1..255 and valid nonzero handles, writes only when
there is room for the byte and NUL terminator, and delegates capacity growth to
the retained `fr_str_builder_char` ABI function. Subtraction avoids overflow in
the capacity check. The shared builder pointer/length/capacity layout matches the
previous implementation and has the same ABI review requirement as views.
Snapshot immutability, invalid-byte rejection, argument evaluation and growth
boundary behavior run against both inline and external callers.
