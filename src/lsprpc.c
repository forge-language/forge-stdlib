#include "forge/lsprpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FR_LSP_BUF_SIZE (1024 * 1024)
static char g_lsp_read_buf[FR_LSP_BUF_SIZE];

static int fr_starts_with_ci(const char *s, const char *prefix) {
    while (*prefix) {
        char a = *s;
        char b = *prefix;
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) return 0;
        s++;
        prefix++;
    }
    return 1;
}

const char *fr_lsp_read_message(void) {
    g_lsp_read_buf[0] = '\0';

    long content_length = -1;
    char line[4096];

    while (fgets(line, sizeof(line), stdin)) {
        size_t l = strlen(line);
        while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) {
            line[--l] = '\0';
        }
        if (l == 0) break;
        if (fr_starts_with_ci(line, "content-length:")) {
            content_length = atol(line + 15);
        }
    }

    if (content_length <= 0) {
        return g_lsp_read_buf;
    }
    if (content_length >= FR_LSP_BUF_SIZE) {
        content_length = FR_LSP_BUF_SIZE - 1;
    }

    size_t got = fread(g_lsp_read_buf, 1, (size_t)content_length, stdin);
    g_lsp_read_buf[got] = '\0';
    return g_lsp_read_buf;
}

void fr_lsp_write_message(const char *body) {
    if (!body) body = "";
    size_t len = strlen(body);
    fprintf(stdout, "Content-Length: %zu\r\n\r\n", len);
    fwrite(body, 1, len, stdout);
    fflush(stdout);
}

/* Incremental transport framing only. Never parses JSON or dispatches methods.
 * Do not mix lsp_read with lsp_poll on the same standard-input stream. */
#if !defined(_WIN32)
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#define FR_LSP_HEADER_LIMIT 8192
#define FR_LSP_BODY_LIMIT (1024 * 1024)
static unsigned char pending[FR_LSP_HEADER_LIMIT + FR_LSP_BODY_LIMIT];
static size_t pending_size;
static char message[FR_LSP_BODY_LIMIT + 1];
static int input_ready, input_eof, input_error;

static int parse_frame(void) {
    size_t header_end = 0;
    for (size_t i = 3; i < pending_size; i++) {
        if (pending[i - 3] == '\r' && pending[i - 2] == '\n' &&
            pending[i - 1] == '\r' && pending[i] == '\n') {
            header_end = i + 1; break;
        }
    }
    if (!header_end) return pending_size >= FR_LSP_HEADER_LIMIT ? -2 : 0;
    if (header_end > FR_LSP_HEADER_LIMIT) return -2;
    if (memchr(pending, 0, header_end)) return -2;
    char header[FR_LSP_HEADER_LIMIT + 1];
    memcpy(header, pending, header_end); header[header_end] = 0;
    int found = 0;
    size_t length = 0;
    char *line = header;
    while (*line) {
        char *end = strstr(line, "\r\n");
        if (!end) return -2;
        *end = 0;
        if (fr_starts_with_ci(line, "content-length:")) {
            if (found) return -2;
            found = 1;
            char *p = line + 15;
            while (*p == ' ' || *p == '\t') p++;
            if (*p < '0' || *p > '9') return -2;
            while (*p >= '0' && *p <= '9') {
                length = length * 10 + (size_t)(*p++ - '0');
                if (length > FR_LSP_BODY_LIMIT) return -2;
            }
            while (*p == ' ' || *p == '\t') p++;
            if (*p || !length) return -2;
        }
        line = end + 2;
    }
    if (!found) return -2;
    if (pending_size < header_end + length) return 0;
    if (memchr(pending + header_end, 0, length)) return -2;
    memcpy(message, pending + header_end, length); message[length] = 0;
    pending_size -= header_end + length;
    memmove(pending, pending + header_end + length, pending_size);
    return 1;
}

int64_t fr_lsp_poll(int64_t timeout_ms) {
    if (input_error) return input_error;
    if (!input_ready) {
        int flags = fcntl(STDIN_FILENO, F_GETFL);
        if (flags < 0 || fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) < 0)
            return input_error = -2;
        input_ready = 1;
    }
    int frame = parse_frame();
    if (frame) { if (frame < 0) input_error = frame; return frame; }
    if (input_eof) return pending_size ? -2 : -1;
    struct pollfd fd = { STDIN_FILENO, POLLIN, 0 };
    int timeout = timeout_ms < 0 ? 0 : timeout_ms > 1000 ? 1000 : (int)timeout_ms;
    int ready = poll(&fd, 1, timeout);
    if (ready < 0) return errno == EINTR ? 0 : (input_error = -2);
    if (!ready) return 0;
    size_t budget = 65536;
    while (budget) {
        size_t capacity = sizeof(pending) - pending_size;
        if (!capacity) return input_error = -2;
        size_t count = capacity < 4096 ? capacity : 4096;
        if (count > budget) count = budget;
        ssize_t got = read(STDIN_FILENO, pending + pending_size, count);
        if (got > 0) {
            pending_size += (size_t)got; budget -= (size_t)got;
            frame = parse_frame();
            if (frame) { if (frame < 0) input_error = frame; return frame; }
        } else if (!got) {
            input_eof = 1; return pending_size ? -2 : -1;
        } else if (errno == EINTR) {
            continue;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        } else return input_error = -2;
    }
    return 0;
}
const char *fr_lsp_message(void) { return message; }
#else
int64_t fr_lsp_poll(int64_t timeout_ms) { (void)timeout_ms; return -3; }
const char *fr_lsp_message(void) { return ""; }
#endif
