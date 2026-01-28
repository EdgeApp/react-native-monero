/**
 * Provide the ___clear_cache symbol for arm64-apple-ios.
 *
 * Apple clang lowers __builtin___clear_cache to a call to ___clear_cache,
 * but does not ship the symbol in any system or compiler-rt library.
 * The randomx JIT compiler references it; JIT itself is inoperable on
 * iOS, but the symbol must still resolve at link time.
 */
#if defined(__APPLE__) && defined(__aarch64__)

#include <stdint.h>
#include <libkern/OSCacheControl.h>

#ifdef __cplusplus
extern "C" {
#endif

void __clear_cache(void *start, void *end) {
    uintptr_t s = (uintptr_t)start;
    uintptr_t e = (uintptr_t)end;
    if (e <= s) return;

    size_t len = (size_t)(e - s);
    sys_dcache_flush((void *)s, len);
    sys_icache_invalidate((void *)s, len);
}

#ifdef __cplusplus
}
#endif

#endif
