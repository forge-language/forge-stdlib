#ifndef FORGE_PROCESS_H
#define FORGE_PROCESS_H

#include <stdint.h>

int64_t fr_proc_run(const char *command);
int64_t fr_proc_run_forge(const char *forge, const char *file, const char *flag,
                          const char *forge_root, const char *lib_dir,
                          const char *include_paths);
const char *fr_proc_output(void);

/* Single-thread/event-loop API; calls are not synchronized.
 * POSIX asynchronous children: 4 live handles, 1 MiB per output stream.
 * poll: 0 pending, 1 terminal/reaped, -1 invalid. release only accepts terminal.
 * start: positive handle, -1 unsupported, -2 capacity, -3 spawn/setup failure.
 * error_kind: 0 normal, 2 output limit, 3 cancelled, 4 I/O or wait failure.
 * Children own a new process group. Cancel kills that group. Natural leader
 * exit also kills remaining group members before reaping its reserved PID/PGID.
 * Descendants that deliberately leave the group are outside this contract.
 * After leader exit, drain available output then close inherited pipes.
 * Returned output strings remain owned by the handle until release. */
int64_t fr_proc_start_forge(const char *, const char *, const char *, const char *, const char *, const char *);
/* Optional SIGTERM/SIGINT flag for an embedding event loop. Handlers only set
 * sig_atomic_t state; the caller owns cancellation, cleanup and exit policy. */
int64_t fr_proc_watch_termination(void);
int64_t fr_proc_termination_requested(void);
int64_t fr_proc_poll(int64_t);
int64_t fr_proc_cancel(int64_t);
int64_t fr_proc_status(int64_t);
int64_t fr_proc_signal(int64_t);
int64_t fr_proc_error_kind(int64_t);
const char *fr_proc_stdout(int64_t);
const char *fr_proc_stderr(int64_t);
int64_t fr_proc_release(int64_t);

#endif
