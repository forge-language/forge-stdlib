#include "forge/string.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    int64_t v = fr_str_view("한글😀");
    assert(v && fr_str_view_len(v) == 10);
    /* Existing binaries/function pointers retain the external ABI. Compare
     * them with newly compiled inline callers at every meaningful boundary. */
    int64_t (*abi_at)(int64_t, int64_t) = &fr_str_view_at;
    int64_t (*abi_len)(int64_t) = &fr_str_view_len;
    int64_t indices[] = {INT64_MIN, -1, 0, 1, 9, 10, INT64_MAX};
    for (size_t i = 0; i < sizeof(indices) / sizeof(indices[0]); ++i) {
        assert(fr_str_view_at(v, indices[i]) == abi_at(v, indices[i]));
        assert(fr_str_view_at(0, indices[i]) == abi_at(0, indices[i]));
    }
    assert(fr_str_view_len(v) == abi_len(v));
    assert(fr_str_view_len(0) == abi_len(0));
    int64_t once_index = 0;
    assert(fr_str_view_at(v, once_index++) == 237 && once_index == 1);
    int64_t once_handle[] = {v, 0};
    int handle_index = 0;
    assert(fr_str_view_len(once_handle[handle_index++]) == 10 && handle_index == 1);
    assert(fr_str_view_at(v, 0) == 237);
    assert(fr_str_view_at(v, 9) == 128);
    assert(fr_str_view_at(v, -1) == -1);
    assert(fr_str_view_at(v, INT64_MAX) == -1);
    assert(fr_str_view_len(0) == 0 && fr_str_view_at(0, 0) == -1);
    assert(fr_str_view_len(fr_str_view(NULL)) == 0);
    int64_t empty = fr_str_view(NULL);
    assert(strcmp(fr_str_view_sub(empty, 0, INT64_MAX), "") == 0);
    assert(fr_str_view_matches(empty, 0, ""));
    assert(!fr_str_view_matches(empty, 0, "a"));
    assert(strcmp(fr_str_view_sub(v, 3, 3), "글") == 0);
    assert(strcmp(fr_str_view_sub(v, 6, INT64_MAX), "😀") == 0);
    assert(strcmp(fr_str_view_sub(v, 10, 3), "") == 0);
    assert(strcmp(fr_str_view_sub(v, INT64_MAX, INT64_MAX), "") == 0);
    assert(strcmp(fr_str_view_sub(v, 0, 0), "") == 0);
    assert(fr_str_view_sub(v, -1, 1) == NULL);
    assert(fr_str_view_sub(v, 0, -1) == NULL);
    assert(fr_str_view_sub(0, 0, 1) == NULL);
    assert(fr_str_view_matches(v, 3, "글"));
    assert(fr_str_view_matches(v, 6, "😀"));
    assert(fr_str_view_matches(v, 10, ""));
    assert(!fr_str_view_matches(v, 6, "😀!"));
    assert(!fr_str_view_matches(v, 0, "글"));
    assert(!fr_str_view_matches(v, -1, ""));
    assert(!fr_str_view_matches(v, INT64_MAX, ""));
    assert(!fr_str_view_matches(v, 11, ""));
    assert(!fr_str_view_matches(v, 0, NULL));
    assert(!fr_str_view_matches(0, 0, ""));
    /* Native ranges expose bytes, including incomplete UTF-8. */
    char *partial = fr_str_view_sub(v, 6, 1);
    assert((unsigned char)partial[0] == 240 && partial[1] == 0);
    const char nul_string[] = {'a', 0, 'b', 0};
    assert(fr_str_view_len(fr_str_view(nul_string)) == 1);
    int64_t nul_view = fr_str_view(nul_string);
    assert(strcmp(fr_str_view_sub(nul_view, 0, INT64_MAX), "a") == 0);
    assert(fr_str_view_matches(nul_view, 0, nul_string));
    assert(!fr_str_view_matches(nul_view, 1, "b"));
    /* Compare the new capacity fast path with the exported ABI through
     * multiple growth boundaries and every accepted unsigned byte. */
    int64_t (*abi_char)(int64_t, int64_t) = &fr_str_builder_char;
    int64_t fast_builder = fr_str_builder(), abi_builder = fr_str_builder();
    char *first_snapshot = NULL;
    for (int i = 0; i < 1024; ++i) {
        int byte = i % 255 + 1;
        assert(fr_str_builder_char(fast_builder, byte) == fast_builder);
        assert(abi_char(abi_builder, byte) == abi_builder);
        if (i == 62) first_snapshot = fr_str_builder_finish(fast_builder);
        if (i == 62 || i == 63 || i == 64 || i == 126 || i == 127 || i == 128 || i == 1023) {
            char *fast = fr_str_builder_finish(fast_builder);
            char *external = fr_str_builder_finish(abi_builder);
            assert(strlen(fast) == (size_t)i + 1);
            assert(memcmp(fast, external, (size_t)i + 2) == 0);
        }
    }
    assert(first_snapshot && strlen(first_snapshot) == 63);
    for (int i = 0; i < 63; ++i) assert((unsigned char)first_snapshot[i] == i + 1);
    int64_t invalid_bytes[] = {INT64_MIN, -1, 0, 256, INT64_MAX};
    for (size_t i = 0; i < sizeof(invalid_bytes) / sizeof(invalid_bytes[0]); ++i) {
        assert(fr_str_builder_char(fast_builder, invalid_bytes[i]) == 0);
        assert(abi_char(abi_builder, invalid_bytes[i]) == 0);
    }
    assert(strcmp(fr_str_builder_finish(fast_builder), fr_str_builder_finish(abi_builder)) == 0);
    assert(strlen(fr_str_builder_finish(fast_builder)) == 1024);
    assert(fr_str_builder_char(0, 65) == abi_char(0, 65));
    int64_t byte_once = 65;
    int builder_once = 0;
    int64_t builders[] = {fast_builder, abi_builder};
    assert(fr_str_builder_char(builders[builder_once++], byte_once++) == fast_builder);
    assert(builder_once == 1 && byte_once == 66);
    assert(strlen(fr_str_builder_finish(fast_builder)) == 1025);
    int64_t b = fr_str_builder();
    assert(b && fr_str_builder_append(b, NULL) == b);
    assert(strcmp(fr_str_builder_finish(b), "") == 0);
    int64_t nul_builder = fr_str_builder();
    assert(fr_str_builder_append(nul_builder, nul_string) == nul_builder);
    assert(strcmp(fr_str_builder_finish(nul_builder), "a") == 0);
    assert(fr_str_builder_append(b, "한글") == b);
    char *snapshot = fr_str_builder_finish(b);
    int bytes[] = {240, 159, 152, 128};
    for (size_t i = 0; i < 4; ++i) assert(fr_str_builder_char(b, bytes[i]) == b);
    assert(strcmp(fr_str_builder_finish(b), "한글😀") == 0);
    assert(strcmp(snapshot, "한글") == 0);
    int64_t ranges = fr_str_builder();
    assert(fr_str_builder_append_view(ranges, v, 3, 3) == ranges);
    char *range_snapshot = fr_str_builder_finish(ranges);
    int64_t snapshot_view = fr_str_view(range_snapshot);
    assert(fr_str_builder_append_view(ranges, v, 6, 1) == ranges);
    assert(fr_str_builder_append_view(ranges, v, 7, INT64_MAX) == ranges);
    assert(strcmp(fr_str_builder_finish(ranges), "글😀") == 0);
    assert(fr_str_builder_append_view(ranges, v, -1, 1) == 0);
    assert(fr_str_builder_append_view(ranges, v, 0, -1) == 0);
    assert(fr_str_builder_append_view(ranges, 0, 0, 1) == 0);
    assert(fr_str_builder_append_view(0, v, 0, 1) == 0);
    assert(fr_str_builder_append_view(ranges, empty, 0, 1) == ranges);
    assert(fr_str_builder_append_view(ranges, v, INT64_MAX, INT64_MAX) == ranges);
    assert(strcmp(fr_str_builder_finish(ranges), "글😀") == 0);
    for (int i = 0; i < 1000; ++i)
        assert(fr_str_builder_append_view(ranges, snapshot_view, 0, INT64_MAX) == ranges);
    char *range_large = fr_str_builder_finish(ranges);
    assert(strlen(range_large) == 3007);
    assert(strcmp(range_large + 3004, "글") == 0);
    assert(strcmp(range_snapshot, "글") == 0);
    assert(fr_str_view_matches(snapshot_view, 0, "글"));
    assert(fr_str_builder_char(b, 0) == 0);
    assert(fr_str_builder_char(b, -1) == 0);
    assert(fr_str_builder_char(b, 256) == 0);
    assert(strcmp(fr_str_builder_finish(b), "한글😀") == 0);
    assert(fr_str_builder_finish(0) == NULL);
    assert(fr_str_builder_append(0, "x") == 0);
    assert(fr_str_builder_char(0, 65) == 0);
    for (int i = 0; i < 100000; ++i) assert(fr_str_builder_char(b, 65) == b);
    char *large = fr_str_builder_finish(b);
    assert(strlen(large) == 100010 && large[100009] == 'A');
    assert(strcmp(snapshot, "한글") == 0);
    /* The old substring API must clamp without overflowing start+len. */
    assert(strcmp(fr_str_sub("abc", 1, INT64_MAX), "bc") == 0);
    fr_str_arena_reset();
    b = fr_str_builder();
    assert(fr_str_builder_append(b, "after reset") == b);
    assert(strcmp(fr_str_builder_finish(b), "after reset") == 0);
    puts("string view/builder tests passed");
    return 0;
}
