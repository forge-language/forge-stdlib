#ifndef FORGE_LSPRPC_H
#define FORGE_LSPRPC_H
#include <stdint.h>

const char *fr_lsp_read_message(void);
void fr_lsp_write_message(const char *body);

/* poll: 1 complete frame, 0 incomplete/no input, -1 clean EOF,
 * -2 invalid/truncated input or I/O error, -3 unsupported platform.
 * Header bound 8 KiB, body bound 1 MiB. Do not mix with lsp_read. */
int64_t fr_lsp_poll(int64_t timeout_ms);
const char *fr_lsp_message(void);

#endif
