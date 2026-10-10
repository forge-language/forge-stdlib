#ifndef FORGE_JSON_H
#define FORGE_JSON_H

#include <stdint.h>

/* String getters return arena-backed UTF-8. Malformed string escapes, lone
 * surrogates and decoded U+0000 return "" with errno=EINVAL (allocation failure:
 * ENOMEM). The NUL-terminated API cannot represent U+0000, although JSON permits
 * it. Successful string decoding clears errno. Missing values remain "";
 * these getters do not validate the complete JSON document. */
const char *fr_json_get_string(const char *json, const char *key);
int64_t fr_json_get_int(const char *json, const char *key);
char *fr_json_stringify_str(const char *key, const char *value);
char *fr_json_stringify_int(const char *key, int64_t value);

const char *fr_json_get_path_raw(const char *json, const char *path);
const char *fr_json_get_path_string(const char *json, const char *path);
int64_t fr_json_get_path_int(const char *json, const char *path);
int64_t fr_json_array_len(const char *json_array);
const char *fr_json_array_item(const char *json_array, int64_t index);

#endif
