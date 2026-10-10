# Native event-loop primitives

The process and LSP framing APIs supply OS services to a Forge event loop.
Document state, JSON-RPC routing, debounce and diagnostic publication belong to
the application. These APIs are single-threaded and have no internal locking.
The implementation is POSIX; Windows returns explicit unsupported codes.

`proc_start_forge(compiler, file, flag, root, lib_dir, include_paths)` executes an
argument array without a shell. Include paths are newline-separated and become
individual `-I` arguments. Children read `/dev/null`, so they cannot consume the
editor's standard-input stream. A positive return value is an opaque generation
handle; `-1` means unsupported, `-2` exhausted capacity and `-3` setup/spawn error.

There are four unreleased handles and one MiB of output storage per stream per
handle. `proc_poll(handle)` returns zero while pending, one after the child is
reaped and pipes are drained/closed, or minus one for an invalid handle. Each
poll drains at most 64 KiB per stream to allow other event-loop work to run.
`proc_status`, `proc_signal` and `proc_error_kind` distinguish exit codes, signal
termination and collection failures. Error kinds are zero for normal completion,
two for output limits, three for cancellation and four for I/O/wait errors.
`proc_stdout` and `proc_stderr` retain separate output; their borrowed strings
remain valid until `proc_release`. Release succeeds only for terminal handles.

Each child owns a fresh process group. Cancellation kills the group. Natural
leader exit also kills remaining group members while the unreaped leader still
reserves its PID/PGID; reaping then releases that identity. A descendant that
deliberately leaves the process group is outside this contract. Available output
is drained after leader exit, then inherited pipes are closed.

`proc_watch_termination` installs SIGTERM/SIGINT handlers that set a flag;
`proc_termination_requested` exposes it. The caller decides when to cancel and
reap children and exit. This changes the embedding process's signal handlers;
it is optional. SIGKILL of the embedding process cannot run cleanup.

`lsp_poll(timeout_ms)` consumes partial or coalesced Content-Length frames without
waiting for an incomplete body. It returns one for a message, zero for pending,
minus one for EOF, minus two for malformed input/I/O errors and minus three on
unsupported platforms. Headers are bounded at 8 KiB and bodies at one MiB;
timeouts are clamped to 0–1000 ms. `lsp_message()` returns the last complete body.
It remains valid until another message replaces it. Poll changes stdin to
nonblocking mode; do not mix it with `lsp_read` or another stdin reader.

`time_monotonic_ms` reads the OS monotonic clock (GetTickCount64 on Windows),
returning minus one if the POSIX clock read fails. Use it for elapsed deadlines;
`time_now_ms` retains its wall-clock meaning.

`async_process_regressions` covers separate output, nonzero exit, spawn failure,
closed standard descriptors, capacity, stale handles, cancellation, process-group
descendants and output limits. `lsprpc_poll_regressions` covers partial/coalesced
frames, malformed lengths and truncated EOF. Both are registered in CTest.
These are lifecycle tests; they do not claim thread safety or sandbox isolation.
