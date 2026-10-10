#ifndef FORGE_DETAIL_STRING_VIEWS_H
#define FORGE_DETAIL_STRING_VIEWS_H

#include <stddef.h>
#include <stdint.h>

/* Internal native handle layout shared by the library and checked inline
 * readers. This layout is unchanged from the exported accessor ABI. A handle
 * still must come from fr_str_view and is valid only until arena reset; this
 * definition does not make arbitrary or stale integer handles safe. */
typedef struct { const char *data; size_t len; } fr_detail_str_view;
/* Builder layout is also the existing ABI: only its capacity-backed byte
 * append is inline; allocation and growth stay in the exported function. */
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

#endif
