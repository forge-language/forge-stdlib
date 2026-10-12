#ifndef FORGE_DETAIL_STRING_VIEWS_H
#define FORGE_DETAIL_STRING_VIEWS_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Internal native handle layout shared by the library and checked inline
 * readers. This layout is unchanged from the exported accessor ABI. A handle
 * still must come from fr_str_view and is valid only until arena reset; this
 * definition does not make arbitrary or stale integer handles safe. */
typedef struct { const char *data; size_t len; } fr_detail_str_view;
/* Builder layout is also the existing ABI: capacity-backed appends may be
 * inline; allocation and growth stay in the exported functions. Its data is
 * an internal length-delimited buffer, not a NUL-terminated public string;
 * only fr_str_builder_finish returns a terminated immutable snapshot. */
typedef struct { char *data; size_t len, cap; } fr_detail_str_builder;

static inline int64_t fr_detail_str_view_len(int64_t handle) {
    const fr_detail_str_view *view = (const fr_detail_str_view *)(intptr_t)handle;
    return view ? (int64_t)view->len : 0;
}

static inline int64_t fr_detail_str_view_at(int64_t handle, int64_t index) {
    const fr_detail_str_view *view = (const fr_detail_str_view *)(intptr_t)handle;
    if (!view || index < 0 || (uint64_t)index >= view->len) return -1;
    return (unsigned char)view->data[index];
}

/* Internal codegen-only fast path. The caller must prove that the handle is
 * live and 0 <= index < view->len for the entire access. */
static inline int64_t fr_detail_str_view_at_unchecked(int64_t handle,
                                                       int64_t index) {
    const fr_detail_str_view *view = (const fr_detail_str_view *)(intptr_t)handle;
    return (unsigned char)view->data[index];
}

static inline int fr_detail_str_view_range(const fr_detail_str_view *view,
                                           int64_t start, int64_t len,
                                           size_t *offset, size_t *count) {
    if (!view || start < 0 || len < 0) return 0;
    *offset = (uint64_t)start < view->len ? (size_t)start : view->len;
    size_t remaining = view->len - *offset;
    *count = (uint64_t)len < remaining ? (size_t)len : remaining;
    return 1;
}

static inline int fr_detail_str_view_matches(int64_t handle, int64_t start,
                                             const char *text) {
    const fr_detail_str_view *view = (const fr_detail_str_view *)(intptr_t)handle;
    if (!view || !text || start < 0 || (uint64_t)start > view->len) return 0;
    size_t len = strlen(text);
    if (len > view->len - (size_t)start) return 0;
    return !len || memcmp(view->data + (size_t)start, text, len) == 0;
}

/* The caller has validated the handle. Compare by subtraction before adding
 * bytes so neither the required capacity nor the new length can overflow. */
static inline int fr_detail_str_builder_try_append(fr_detail_str_builder *builder,
                                                   const char *data, size_t len) {
    if (builder->cap <= builder->len || len >= builder->cap - builder->len)
        return 0;
    if (len) memmove(builder->data + builder->len, data, len);
    builder->len += len;
    return 1;
}

#endif
