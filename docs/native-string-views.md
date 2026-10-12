# Native string view accessors

Newly compiled native callers inline the checked `fr_str_view_len` and
`fr_str_view_at` reads through the installed `forge/string.h` header. Checked
`fr_str_view_matches` is also inline, allowing constant text lengths and short
comparisons to be optimized at the call site. This lets
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

`fr_str_view_append_byte(view, value)` is an additive immutable operation. It
uses the source view's cached length, copies the visible prefix, and returns a
new view whose metadata and NUL-terminated snapshot share one arena allocation.
It does not change the source view or its bytes. Native values narrow to one
byte as `fr_str_append` does; a narrowed NUL leaves the visible length unchanged.
The result remains valid until arena reset, and allocation or size failure
returns a zero handle. This avoids a `strlen` on each chained append but still
copies and retains each prefix.

Builder snapshots still copy immutable bytes, and this optimization changes no
builder allocation, UTF-8, coroutine arena, or reset behavior. The same contract
suite runs against both the inline header and the external ABI. Performance
measurements live in the separate forge-benchmarks repository with preserved
compiler, archive, and header hashes.

The builder exposes checked capacity fast paths for byte, string and view-range
appends to newly compiled callers. The byte path accepts only bytes 1..255 and
valid nonzero handles. The range path keeps the same negative-range rejection
and end clamping as the retained ABI function. Each fast path writes only when
capacity includes the new bytes and one reserved terminator byte; growth stays
in the retained ABI functions. Subtraction avoids overflow in the capacity
checks. The shared builder pointer/length/capacity layout matches the previous
implementation and has the same ABI review requirement as views.
Snapshot immutability, invalid-byte rejection, argument evaluation and growth
boundary behavior run against both inline and external callers.

A builder is an opaque length-delimited buffer. Its internal `data` pointer is
not a public C string, and append operations do not write a trailing NUL after
every fragment. `fr_str_builder_finish` copies exactly the recorded bytes into
a fresh immutable snapshot and terminates that snapshot. The empty-builder
case, all public byte and bounds behavior, arena lifetime, and the reserved
capacity byte remain unchanged. The internal layout header does not authorize
clients to reinterpret a handle and read or mutate its private buffer.
