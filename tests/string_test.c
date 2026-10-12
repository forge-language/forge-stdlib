#include "forge/string.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void checked_append_paths(void) {
    int64_t (*abi_append)(int64_t, const char *) = &fr_str_builder_append;
    int64_t (*abi_range)(int64_t, int64_t, int64_t, int64_t) = &fr_str_builder_append_view;
    int (*abi_matches)(int64_t, int64_t, const char *) = &fr_str_view_matches;
    const char *texts[] = {NULL, "", "a", "abc", "bc", "abcd", "한글"};
    int64_t starts[] = {INT64_MIN, -1, 0, 1, 2, 3, 4, INT64_MAX};
    int64_t lengths[] = {INT64_MIN, -1, 0, 1, 2, 3, 4, INT64_MAX};
    int64_t view = fr_str_view("abc");
    int64_t fast = fr_str_builder(), external = fr_str_builder();
    for (size_t i = 0; i < sizeof(starts) / sizeof(starts[0]); ++i) {
        for (size_t j = 0; j < sizeof(texts) / sizeof(texts[0]); ++j) {
            assert(fr_str_view_matches(view, starts[i], texts[j]) ==
                   abi_matches(view, starts[i], texts[j]));
            assert(fr_str_view_matches(0, starts[i], texts[j]) ==
                   abi_matches(0, starts[i], texts[j]));
        }
        for (size_t j = 0; j < sizeof(lengths) / sizeof(lengths[0]); ++j) {
            int64_t a = fr_str_builder_append_view(fast, view, starts[i], lengths[j]);
            int64_t b = abi_range(external, view, starts[i], lengths[j]);
            assert((a == 0) == (b == 0));
            assert(strcmp(fr_str_builder_finish(fast), fr_str_builder_finish(external)) == 0);
        }
    }
    /* Variable-sized fragments cross spare-capacity and growth boundaries;
     * finished strings also serve as immutable sources for later appends. */
    char *snapshot = fr_str_builder_finish(fast);
    char saved[128];
    assert(strlen(snapshot) < sizeof(saved));
    strcpy(saved, snapshot);
    for (int i = 0; i < 2048; ++i) {
        const char *text = texts[(size_t)i % (sizeof(texts) / sizeof(texts[0]))];
        assert(fr_str_builder_append(fast, text) == fast);
        assert(abi_append(external, text) == external);
        assert(fr_str_builder_append_view(fast, view, i % 4, i % 5) == fast);
        assert(abi_range(external, view, i % 4, i % 5) == external);
        if ((i & (i + 1)) == 0)
            assert(strcmp(fr_str_builder_finish(fast), fr_str_builder_finish(external)) == 0);
    }
    assert(fr_str_builder_append(fast, snapshot) == fast);
    assert(abi_append(external, snapshot) == external);
    assert(strcmp(fr_str_builder_finish(fast), fr_str_builder_finish(external)) == 0);
    assert(strcmp(snapshot, saved) == 0);
    assert(fr_str_builder_append(0, "a") == abi_append(0, "a"));
    assert(fr_str_builder_append_view(0, view, 0, 1) == abi_range(0, view, 0, 1));
    assert(fr_str_builder_append_view(fast, 0, 0, 1) == abi_range(external, 0, 0, 1));

    int builder_once = 0, text_once = 2;
    int64_t builders[] = {fast, external};
    assert(fr_str_builder_append(builders[builder_once++], texts[text_once++]) == fast);
    assert(builder_once == 1 && text_once == 3);
    builder_once = 0;
    int64_t views[] = {view, 0};
    int view_once = 0;
    int64_t start_once = 0, len_once = 1;
    assert(fr_str_builder_append_view(builders[builder_once++], views[view_once++],
                                      start_once++, len_once++) == fast);
    assert(builder_once == 1 && view_once == 1 && start_once == 1 && len_once == 2);
    view_once = 0;
    start_once = 0;
    text_once = 2;
    assert(fr_str_view_matches(views[view_once++], start_once++, texts[text_once++]));
    assert(view_once == 1 && start_once == 1 && text_once == 3);
}

static void cached_immutable_append_paths(void) {
    int64_t (*append_view)(int64_t, int64_t) = &fr_str_view_append_byte;
    const int64_t values[] = {65, 0, 66, 127, 128, 255, -1, 256, INT64_MIN, INT64_MAX};
    char *legacy = "seed";
    int64_t cached = fr_str_view(legacy);
    int64_t old = cached;
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        legacy = fr_str_append(legacy, values[i]);
        int64_t next = append_view(cached, values[i]);
        assert(next);
        cached = next;
        assert(fr_str_view_len(cached) == (int64_t)strlen(legacy));
        assert(strcmp(fr_str_view_sub(cached, 0, INT64_MAX), legacy) == 0);
        /* Each prior view remains an immutable snapshot after later appends. */
        if (i == 0) {
            assert(fr_str_view_len(old) == 4);
            assert(strcmp(fr_str_view_sub(old, 0, INT64_MAX), "seed") == 0);
        }
        old = cached;
    }
    assert(fr_str_view_append_byte(0, 65) == 0);
    assert(fr_str_view_append_byte(fr_str_view(NULL), 0) != 0);
    int64_t once = fr_str_view("x");
    assert(fr_str_view_append_byte(once, 66) != 0);
    assert(fr_str_view_len(once) == 1);
}

int main(void) {
    checked_append_paths();
    cached_immutable_append_paths();
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
    assert(fr_str_builder_with_capacity(-1) == 0);
    int64_t reserved = fr_str_builder_with_capacity(3);
    assert(reserved && fr_str_builder_char(reserved, 'a') == reserved);
    assert(fr_str_builder_char(reserved, 'b') == reserved);
    assert(fr_str_builder_char(reserved, 'c') == reserved);
    assert(strcmp(fr_str_builder_finish(reserved), "abc") == 0);
    assert(fr_str_builder_char(reserved, 'd') == reserved);
    assert(strcmp(fr_str_builder_finish(reserved), "abcd") == 0);
    int64_t empty_reserved = fr_str_builder_with_capacity(0);
    assert(empty_reserved && strcmp(fr_str_builder_finish(empty_reserved), "") == 0);
    int64_t take_builder = fr_str_builder_with_capacity(4);
    assert(take_builder && fr_str_builder_append(take_builder, "take") == take_builder);
    char *taken = fr_str_builder_finish_take(take_builder);
    assert(taken && strcmp(taken, "take") == 0);
    assert(fr_str_builder_char(take_builder, 'x') == take_builder);
    assert(strcmp(taken, "take") == 0);
    assert(strcmp(fr_str_builder_finish(take_builder), "x") == 0);
    assert(strcmp(fr_str_builder_finish_take(take_builder), "x") == 0);
    assert(strcmp(fr_str_builder_finish_take(take_builder), "") == 0);
    assert(fr_str_builder_finish_take(0) == NULL);
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
