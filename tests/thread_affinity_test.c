/* A language int64 CPU ID must not wrap into a valid C int CPU ID. */
#include "forge/threading.h"
#include <assert.h>
#include <limits.h>
#include <stdint.h>

static int calls, last;
void __wrap_fr_thread_pin_cpu(int cpu) { calls++; last = cpu; }
int main(void) {
    const int64_t invalid[] = {-1, INT64_MIN, INT64_MAX, (int64_t)INT_MAX + 1,
                               INT64_C(1) << 32, (INT64_C(1) << 32) + 7};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        fr_threading_pin(invalid[i]);
        assert(calls == 0);
    }
    fr_threading_pin(7);
    assert(calls == 1 && last == 7);
    return 0;
}
