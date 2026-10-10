#include "forge/json.h"
#include "forge/arena.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void string_case(const char *raw, const char *expected) {
    char json[1024];
    snprintf(json,sizeof(json),"{\"value\":%s}",raw);
    errno = ERANGE;
    const char *path = fr_json_get_path_string(json,"value");
    assert(strcmp(path,expected)==0 && errno==0);
    errno = ERANGE;
    const char *shallow = fr_json_get_string(json,"value");
    assert(strcmp(shallow,expected)==0 && errno==0);
    /* Independent arena results survive subsequent accessor calls. */
    assert(strcmp(path,expected)==0);
    assert(strcmp(fr_json_get_path_raw(json,"value"),raw)==0);
    fr_arena_reset(fr_arena_tls());
}
static void invalid_case(const char *raw) {
    char json[1024];
    snprintf(json,sizeof(json),"{\"value\":%s}",raw);
    errno = 0;
    assert(strcmp(fr_json_get_path_string(json,"value"),"")==0 && errno==EINVAL);
    errno = 0;
    assert(strcmp(fr_json_get_string(json,"value"),"")==0 && errno==EINVAL);
    fr_arena_reset(fr_arena_tls());
}
int main(void) {
    string_case("\"\"","");
    string_case("\"ASCII\"","ASCII");
    string_case("\"한글😀\"","한글😀");
    string_case("\"\\uD55C\\uAE00\\uD83D\\uDE00\"","한글😀");
    string_case("\"\\ud55c\\uae00\\ud83d\\ude00\"","한글😀");
    string_case("\"\\u0041\\u00e9\\u20AC\"","Aé€");
    string_case("\"\\u007f\\u0080\\u07ff\\u0800\"","\x7f\xc2\x80\xdf\xbf\xe0\xa0\x80");
    string_case("\"\\uD7FF\\uE000\\uDBFF\\uDFFF\\uD800\\uDC00\"",
                "\xed\x9f\xbf\xee\x80\x80\xf4\x8f\xbf\xbf\xf0\x90\x80\x80");
    string_case("\"quote\\\" slash\\/ back\\\\ end\"","quote\" slash/ back\\ end");
    string_case("\"\\b\\f\\n\\r\\t\\u0001\"","\b\f\n\r\t\x01");
    string_case("\"file:\\/\\/\\/tmp/\\uD55C\\uAE00\\uD83D\\uDE00.fg\"","file:///tmp/한글😀.fg");
    string_case("\"native main {println(\\\"\\uD55C\\uD83D\\uDE00\\\");}\\n\"",
                "native main {println(\"한😀\");}\n");
    const char *invalid[] = {
        "\"prefix\\uD800\"", "\"prefix\\uDC00\"", "\"\\uD800\\u0041\"",
        "\"\\uDC00\\uD800\"", "\"\\uD800x\"", "\"\\uD800\\uD800\"",
        "\"\\uD800\\uDC0\"", "\"\\uD800\\uDC0X\"", "\"\\u12xz\"",
        "\"prefix\\u\"", "\"prefix\\u1\"", "\"prefix\\u12\"", "\"prefix\\u123\"",
        "\"prefix\\q\"", "\"prefix\\\"", "\"prefix\\u0000suffix\"",
        "\"raw\ncontrol\"", "\"unterminated"
    };
    for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) invalid_case(invalid[i]);
    const char *nested="{\"params\":{\"textDocument\":{\"\\u0074ext\":\"\\uD55C\\uD83D\\uDE00\",\"uri\":\"file:///tmp/\\uD55C.fg\"}}}";
    assert(strcmp(fr_json_get_path_string(nested,"params.textDocument.text"),"한😀")==0);
    assert(strcmp(fr_json_get_path_string(nested,"params.textDocument.uri"),"file:///tmp/한.fg")==0);
    assert(strcmp(fr_json_get_path_string("{\"\\uD55C\":\"decoded key\"}","한"),"decoded key")==0);
    assert(strcmp(fr_json_get_path_string("{\"key\\\\path\":\"ok\"}","key\\path"),"ok")==0);
    errno=0;
    assert(strcmp(fr_json_get_path_string("{\"bad\\uDC00\":1}","missing"),"")==0 && errno==EINVAL);
    char *roundtrip=fr_json_stringify_str("value","한😀 \"slash\\\n\b\f");
    assert(roundtrip && strcmp(fr_json_get_string(roundtrip,"value"),"한😀 \"slash\\\n\b\f")==0);
    free(roundtrip);
    assert(strcmp(fr_json_get_path_string("{\"number\":42}","number"),"42")==0);
    assert(strcmp(fr_json_get_path_string("{}","missing"),"")==0);
    assert(fr_json_get_int("{\"n\":7}","n")==7);
    assert(fr_json_get_path_int("{\"n\":7}","n")==7);
    const char *array="[\"\\uD55C\",\"\\uD83D\\uDE00\"]";
    assert(fr_json_array_len(array)==2);
    assert(strcmp(fr_json_array_item(array,1),"\"\\uD83D\\uDE00\"")==0);
    fr_arena_reset(fr_arena_tls());
    puts("JSON Unicode regression tests passed");
    return 0;
}
