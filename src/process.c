#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "forge/process.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define FR_POPEN _popen
#define FR_PCLOSE _pclose
#include <io.h>
#include <process.h>
#else
#define FR_POPEN popen
#define FR_PCLOSE pclose
#include <sys/wait.h>
#include <unistd.h>
#endif

#define FR_PROC_BUF_SIZE (1024 * 1024)
static char g_proc_output[FR_PROC_BUF_SIZE];

static void fr_proc_append_output(const char *buf, size_t *total, size_t n) {
    size_t available = FR_PROC_BUF_SIZE - 1 - *total;
    if (n > available) n = available;
    if (n > 0) {
        memcpy(g_proc_output + *total, buf, n);
        *total += n;
    }
}

static int fr_proc_finish_status(int status) {
#if defined(_WIN32)
    return status < 0 ? -1 : status;
#else
    if (status == -1) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
#endif
}

int64_t fr_proc_run(const char *command) {
    g_proc_output[0] = '\0';
    if (!command) return -1;

    FILE *p = FR_POPEN(command, "r");
    if (!p) return -1;

    size_t total = 0;
    size_t n;
    while (total < FR_PROC_BUF_SIZE - 1 &&
           (n = fread(g_proc_output + total, 1, FR_PROC_BUF_SIZE - 1 - total, p)) > 0) {
        total += n;
    }
    g_proc_output[total] = '\0';

    return (int64_t)fr_proc_finish_status(FR_PCLOSE(p));
}

/* Run the compiler with an argument vector instead of passing user-controlled
 * paths through a shell. include_paths is a newline-delimited list of paths. */
int64_t fr_proc_run_forge(const char *forge, const char *file, const char *flag,
                          const char *forge_root, const char *lib_dir,
                          const char *include_paths) {
    g_proc_output[0] = '\0';
    if (!forge || !file || !flag) return -1;

    const char *paths = include_paths ? include_paths : "";
    size_t path_count = 0;
    for (const char *p = paths; *p; p++) {
        if (p == paths || p[-1] == '\n') path_count++;
    }

    size_t argc = 4 + (forge_root && *forge_root ? 2 : 0) +
                  (lib_dir && *lib_dir ? 2 : 0) + path_count * 2;
    char **argv = (char **)calloc(argc + 1, sizeof(char *));
    if (!argv) return -1;

    size_t i = 0;
    argv[i++] = (char *)forge;
    argv[i++] = (char *)file;
    argv[i++] = (char *)flag;
    if (forge_root && *forge_root) {
        argv[i++] = "--forge-root";
        argv[i++] = (char *)forge_root;
    }
    if (lib_dir && *lib_dir) {
        argv[i++] = "--lib-dir";
        argv[i++] = (char *)lib_dir;
    }
    size_t first_path_arg = i;
    const char *start = paths;
    for (const char *p = paths;; p++) {
        if (*p != '\n' && *p != '\0') continue;
        if (p > start) {
            size_t len = (size_t)(p - start);
            char *path = (char *)malloc(len + 1);
            if (!path) {
                for (size_t j = first_path_arg + 1; j < i; j += 2)
                    free(argv[j]);
                free(argv);
                return -1;
            }
            memcpy(path, start, len);
            path[len] = '\0';
            argv[i++] = "-I";
            argv[i++] = path;
        }
        if (*p == '\0') break;
        start = p + 1;
    }
    argv[i] = NULL;

    int status = -1;
#if defined(_WIN32)
    int saved_out = _dup(_fileno(stdout));
    int saved_err = _dup(_fileno(stderr));
    FILE *capture = tmpfile();
    if (saved_out >= 0 && saved_err >= 0 && capture &&
        _dup2(_fileno(capture), _fileno(stdout)) == 0 &&
        _dup2(_fileno(capture), _fileno(stderr)) == 0) {
        status = _spawnvp(_P_WAIT, argv[0], (const char *const *)argv);
        fflush(stdout);
        fflush(stderr);
        _dup2(saved_out, _fileno(stdout));
        _dup2(saved_err, _fileno(stderr));
        if (fseek(capture, 0, SEEK_SET) == 0) {
            char buf[4096];
            size_t total = 0;
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), capture)) > 0)
                fr_proc_append_output(buf, &total, n);
            g_proc_output[total] = '\0';
        }
    }
    if (capture) fclose(capture);
    if (saved_out >= 0) _close(saved_out);
    if (saved_err >= 0) _close(saved_err);
#else
    int pipefd[2];
    if (pipe(pipefd) == 0) {
        pid_t pid = fork();
        if (pid == 0) {
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[1]);
            execvp(argv[0], argv);
            _exit(127);
        }
        close(pipefd[1]);
        if (pid > 0) {
            char buf[4096];
            size_t total = 0;
            ssize_t n;
            do {
                n = read(pipefd[0], buf, sizeof(buf));
                if (n > 0) fr_proc_append_output(buf, &total, (size_t)n);
            } while (n > 0 || (n < 0 && errno == EINTR));
            close(pipefd[0]);
            int raw_status = 0;
            if (waitpid(pid, &raw_status, 0) >= 0)
                status = fr_proc_finish_status(raw_status);
            g_proc_output[total] = '\0';
        } else {
            close(pipefd[0]);
        }
    }
#endif

    for (size_t j = first_path_arg + 1; j < i; j += 2)
        free(argv[j]);
    free(argv);
    return (int64_t)status;
}

const char *fr_proc_output(void) {
    return g_proc_output;
}

/* Opaque generation handles prevent a late cancel from targeting a reused slot.
 * These primitives have no knowledge of editors, documents or JSON-RPC. */
#define FR_ASYNC_JOBS 4
#define FR_ASYNC_OUTPUT (1024 * 1024)
#if !defined(_WIN32)
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
extern char **environ;
typedef struct {
    int64_t handle;
    pid_t pid;
    int fd[2];
    char *output[2];
    size_t size[2];
    int reaped, status, signal, error;
} fr_async_job;
static fr_async_job async_jobs[FR_ASYNC_JOBS];
static int64_t async_generation;
static volatile sig_atomic_t termination_requested;
static void async_termination_signal(int signal_number) {
    (void)signal_number; termination_requested = 1;
}
int64_t fr_proc_watch_termination(void) {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = async_termination_signal;
    sigemptyset(&action.sa_mask);
    return sigaction(SIGTERM, &action, NULL) == 0 &&
           sigaction(SIGINT, &action, NULL) == 0;
}
int64_t fr_proc_termination_requested(void) { return termination_requested; }


static fr_async_job *async_find(int64_t handle) {
    if (handle <= 0) return NULL;
    for (int i = 0; i < FR_ASYNC_JOBS; i++)
        if (async_jobs[i].handle == handle) return &async_jobs[i];
    return NULL;
}

static void async_close(fr_async_job *job) {
    for (int i = 0; i < 2; i++) {
        if (job->fd[i] >= 0) close(job->fd[i]);
        free(job->output[i]);
    }
    memset(job, 0, sizeof(*job));
}

int64_t fr_proc_start_forge(const char *forge, const char *file, const char *flag,
                           const char *root, const char *lib, const char *paths) {
    fr_async_job *job = NULL;
    for (int i = 0; i < FR_ASYNC_JOBS; i++)
        if (!async_jobs[i].handle) { job = &async_jobs[i]; break; }
    if (!job) return -2;
    if (!forge || !*forge || !file || !flag) return -3;
    char *includes = strdup(paths ? paths : "");
    if (!includes) return -3;
    size_t capacity = 10 + strlen(includes) * 2;
    char **argv = calloc(capacity, sizeof(*argv));
    if (!argv) { free(includes); return -3; }
    size_t n = 0;
    argv[n++] = (char *)forge; argv[n++] = (char *)file; argv[n++] = (char *)flag;
    if (root && *root) { argv[n++] = "--forge-root"; argv[n++] = (char *)root; }
    if (lib && *lib) { argv[n++] = "--lib-dir"; argv[n++] = (char *)lib; }
    char *save = NULL;
    for (char *p = strtok_r(includes, "\n", &save); p; p = strtok_r(NULL, "\n", &save)) {
        argv[n++] = "-I"; argv[n++] = p;
    }
    int pipes[2][2] = {{-1, -1}, {-1, -1}};
    memset(job, 0, sizeof(*job));
    job->fd[0] = job->fd[1] = -1;
    job->status = -1;
    int ok = 1;
    for (int i = 0; i < 2; i++) {
        job->output[i] = calloc(FR_ASYNC_OUTPUT + 1, 1);
        if (!job->output[i] || pipe(pipes[i]) != 0) { ok = 0; break; }
        for (int end = 0; end < 2; end++) {
            if (pipes[i][end] < 3) {
                int replacement = fcntl(pipes[i][end], F_DUPFD, 3);
                if (replacement < 0) { ok = 0; break; }
                close(pipes[i][end]); pipes[i][end] = replacement;
            }
        }
        if (!ok) break;
        if (fcntl(pipes[i][0], F_SETFD, FD_CLOEXEC) < 0 ||
            fcntl(pipes[i][1], F_SETFD, FD_CLOEXEC) < 0 ||
            fcntl(pipes[i][0], F_SETFL, O_NONBLOCK) < 0) { ok = 0; break; }
    }
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    int attributes_ready = posix_spawnattr_init(&attributes) == 0;
    if (!attributes_ready || posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP) != 0
        || posix_spawnattr_setpgroup(&attributes, 0) != 0) ok = 0;
    int actions_ready = 0;
    if (ok && posix_spawn_file_actions_init(&actions) == 0) {
        actions_ready = 1;
        ok = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0) == 0;
        for (int i = 0; i < 2 && ok; i++) {
            ok = posix_spawn_file_actions_adddup2(&actions, pipes[i][1], i + 1) == 0 &&
                 posix_spawn_file_actions_addclose(&actions, pipes[i][0]) == 0 &&
                 posix_spawn_file_actions_addclose(&actions, pipes[i][1]) == 0;
        }
        if (ok) ok = posix_spawnp(&job->pid, forge, &actions, &attributes, argv, environ) == 0;
    } else ok = 0;
    if (actions_ready) posix_spawn_file_actions_destroy(&actions);
    if (attributes_ready) posix_spawnattr_destroy(&attributes);
    free(argv); free(includes);
    for (int i = 0; i < 2; i++) {
        if (pipes[i][1] >= 0) close(pipes[i][1]);
        if (ok) job->fd[i] = pipes[i][0];
        else if (pipes[i][0] >= 0) close(pipes[i][0]);
    }
    if (!ok) { async_close(job); return -3; }
    if (async_generation == INT64_MAX) async_generation = 0;
    job->handle = ++async_generation;
    return job->handle;
}

int64_t fr_proc_cancel(int64_t handle) {
    fr_async_job *job = async_find(handle);
    if (!job) return 0;
    if (!job->reaped) {
        if (!job->error) job->error = 3;
        if (kill(-job->pid, SIGKILL) != 0 && errno != ESRCH) return 0;
    }
    return 1;
}

int64_t fr_proc_poll(int64_t handle) {
    fr_async_job *job = async_find(handle);
    if (!job) return -1;
    for (int stream = 0; stream < 2; stream++) {
        size_t budget = 65536;
        while (job->fd[stream] >= 0 && budget) {
            char buffer[4096];
            ssize_t count = read(job->fd[stream], buffer, sizeof(buffer));
            if (count > 0) {
                budget -= (size_t)count;
                if (job->size[stream] + (size_t)count > FR_ASYNC_OUTPUT) {
                    job->error = 2;
                    fr_proc_cancel(handle);
                    close(job->fd[stream]); job->fd[stream] = -1;
                    break;
                }
                memcpy(job->output[stream] + job->size[stream], buffer, (size_t)count);
                job->size[stream] += (size_t)count;
                job->output[stream][job->size[stream]] = 0;
            } else if (!count) {
                close(job->fd[stream]); job->fd[stream] = -1;
            } else if (errno == EINTR) {
                continue;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (job->reaped) { close(job->fd[stream]); job->fd[stream] = -1; }
                break;
            } else {
                job->error = 4; fr_proc_cancel(handle);
                close(job->fd[stream]); job->fd[stream] = -1; break;
            }
        }
    }
    if (!job->reaped) {
        siginfo_t info;
        memset(&info, 0, sizeof(info));
        int observed = waitid(P_PID, (id_t)job->pid, &info, WEXITED | WNOHANG | WNOWAIT);
        if (observed == 0 && info.si_pid == job->pid) {
            /* The unreaped leader still reserves its PID/PGID. Kill remaining
             * owned group members before reaping, never after PID reuse. */
            if (kill(-job->pid, SIGKILL) != 0 && errno != ESRCH) job->error = 4;
            int raw = 0;
            pid_t result = waitpid(job->pid, &raw, WNOHANG);
            if (result == job->pid) {
                job->reaped = 1;
                if (WIFEXITED(raw)) job->status = WEXITSTATUS(raw);
                if (WIFSIGNALED(raw)) job->signal = WTERMSIG(raw);
            } else if (result < 0 && errno != EINTR) {
                job->reaped = 1; job->error = 4;
            }
        } else if (observed < 0 && errno != EINTR) {
            job->reaped = 1; job->error = 4;
        }
    }
    return job->reaped && job->fd[0] < 0 && job->fd[1] < 0 ? 1 : 0;
}
int64_t fr_proc_status(int64_t h) { fr_async_job *j = async_find(h); return j ? j->status : -1; }
int64_t fr_proc_signal(int64_t h) { fr_async_job *j = async_find(h); return j ? j->signal : 0; }
int64_t fr_proc_error_kind(int64_t h) { fr_async_job *j = async_find(h); return j ? j->error : -1; }
const char *fr_proc_stdout(int64_t h) { fr_async_job *j = async_find(h); return j ? j->output[0] : ""; }
const char *fr_proc_stderr(int64_t h) { fr_async_job *j = async_find(h); return j ? j->output[1] : ""; }
int64_t fr_proc_release(int64_t h) {
    fr_async_job *job = async_find(h);
    if (!job || !job->reaped || job->fd[0] >= 0 || job->fd[1] >= 0) return 0;
    async_close(job); return 1;
}
#else
int64_t fr_proc_watch_termination(void) { return 0; }
int64_t fr_proc_termination_requested(void) { return 0; }
int64_t fr_proc_start_forge(const char *a, const char *b, const char *c,
                           const char *d, const char *e, const char *f) {
    (void)a;(void)b;(void)c;(void)d;(void)e;(void)f; return -1;
}
int64_t fr_proc_poll(int64_t h) { (void)h; return -1; }
int64_t fr_proc_cancel(int64_t h) { (void)h; return 0; }
int64_t fr_proc_status(int64_t h) { (void)h; return -1; }
int64_t fr_proc_signal(int64_t h) { (void)h; return 0; }
int64_t fr_proc_error_kind(int64_t h) { (void)h; return -1; }
const char *fr_proc_stdout(int64_t h) { (void)h; return ""; }
const char *fr_proc_stderr(int64_t h) { (void)h; return ""; }
int64_t fr_proc_release(int64_t h) { (void)h; return 0; }
#endif
