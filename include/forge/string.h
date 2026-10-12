#ifndef FORGE_STRING_H
#define FORGE_STRING_H

#include <stdint.h>
#include "forge/detail/string_views.h"

#ifdef __cplusplus
extern "C" {
#endif

int64_t fr_str_len(const char *s);
char *fr_str_concat(const char *a, const char *b);
int fr_str_eq(const char *a, const char *b);
char *fr_str_sub(const char *s, int64_t start, int64_t len);
int fr_str_contains(const char *haystack, const char *needle);
char *fr_str_trim(const char *s);
int64_t fr_str_char_at(const char *s, int64_t i);
char *fr_str_append(const char *s, int64_t ch);
char *fr_str_append_str(const char *s, const char *t);
char *fr_str_from_int(int64_t n);
void fr_str_arena_reset(void);
/* Opaque handles in the current coroutine arena, otherwise the thread arena:
 * valid only until fr_str_arena_reset(). Views
 * borrow immutable NUL-terminated bytes; the source must outlive the view.
 * Zero is a failed/empty handle. These are trusted runtime handles, not IDs
 * that can be reconstructed from arbitrary integers. */
int64_t fr_str_view(const char *s);
int64_t fr_str_view_len(int64_t view);
int64_t fr_str_view_at(int64_t view, int64_t index);
/* Keep the exported functions for existing binaries and function pointers.
 * Newly compiled callers expose the same checked reads to their optimizer.
 * Function-like aliases evaluate each argument once. Define
 * FORGE_STRING_NO_INLINE before including this header to use the ABI calls. */
#ifndef FORGE_STRING_NO_INLINE
#define fr_str_view_len(view) fr_detail_str_view_len((view))
#define fr_str_view_at(view, index) fr_detail_str_view_at((view), (index))
#endif
/* Byte ranges clamp at the cached end. Negative start/length and zero handles
 * fail (NULL for sub, zero for append); starts beyond the end produce an empty
 * range. matches returns zero for an invalid start or NULL text, and accepts
 * an empty text at the end. JavaScript sub requires a valid UTF-8 range. */
char *fr_str_view_sub(int64_t view, int64_t start, int64_t len);
int fr_str_view_matches(int64_t view, int64_t start, const char *text);
/* Copy one legacy append result using the source view's cached byte length.
 * Native byte narrowing/NUL visibility match fr_str_append. JavaScript keeps
 * its existing String.fromCharCode behavior and measures the UTF-8 view bytes.
 * The returned view and immutable snapshot live until arena reset. */
int64_t fr_str_view_append_byte(int64_t view, int64_t ch);
#ifndef FORGE_STRING_NO_INLINE
#define fr_str_view_matches(view, start, text) \
    fr_detail_str_view_matches((view), (start), (text))
#endif
/* Arena-backed geometric growth; append returns the same handle or zero on
 * failure. char accepts bytes 1..255 (NUL is rejected). finish copies an
 * immutable snapshot, so further appends cannot modify previously returned
 * strings. Native strings may contain arbitrary nonzero bytes. JavaScript
 * finish requires complete, valid UTF-8 and throws for malformed byte sequences.
 * Builders, snapshots and views are all invalidated by arena reset. */
int64_t fr_str_builder(void);
int64_t fr_str_builder_append(int64_t builder, const char *s);
int64_t fr_str_builder_append_view(int64_t builder, int64_t view,
                                   int64_t start, int64_t len);
int64_t fr_str_builder_char(int64_t builder, int64_t ch);
#ifndef FORGE_STRING_NO_INLINE
static inline int64_t fr_detail_str_builder_append(int64_t handle, const char *s) {
    fr_detail_str_builder *builder = (fr_detail_str_builder *)(intptr_t)handle;
    if (!builder) return 0;
    size_t len = s ? strlen(s) : 0;
    if (fr_detail_str_builder_try_append(builder, s, len)) return handle;
    return fr_str_builder_append(handle, s);
}
static inline int64_t fr_detail_str_builder_append_view(int64_t handle,
                                                        int64_t view_handle,
                                                        int64_t start,
                                                        int64_t len) {
    fr_detail_str_builder *builder = (fr_detail_str_builder *)(intptr_t)handle;
    const fr_detail_str_view *view = (const fr_detail_str_view *)(intptr_t)view_handle;
    size_t offset, count;
    if (!builder || !fr_detail_str_view_range(view, start, len, &offset, &count))
        return 0;
    const char *data = count ? view->data + offset : NULL;
    if (fr_detail_str_builder_try_append(builder, data, count)) return handle;
    return fr_str_builder_append_view(handle, view_handle, start, len);
}
static inline int64_t fr_detail_str_builder_char(int64_t handle, int64_t ch) {
    fr_detail_str_builder *builder = (fr_detail_str_builder *)(intptr_t)handle;
    if (!builder || ch <= 0 || ch > 255) return 0;
    /* Subtraction keeps the spare-capacity test free of size_t overflow.
     * Valid builders have data whenever capacity is nonzero. */
    if (builder->cap > builder->len && builder->cap - builder->len > 1) {
        builder->data[builder->len++] = (char)ch;
        return handle;
    }
    return fr_str_builder_char(handle, ch);
}
#define fr_str_builder_char(builder, ch) fr_detail_str_builder_char((builder), (ch))
#define fr_str_builder_append(builder, s) fr_detail_str_builder_append((builder), (s))
#define fr_str_builder_append_view(builder, view, start, len) \
    fr_detail_str_builder_append_view((builder), (view), (start), (len))
#endif
char *fr_str_builder_finish(int64_t builder);

#ifdef __cplusplus
}
#endif

#endif
