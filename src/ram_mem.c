// RAM-resident memcpy / memset / memmove for hot audio and packet paths.
//
// On RP2350, the default newlib implementations live in flash. These strong
// definitions keep common memory operations available while BOOTSEL sampling or
// other flash-sensitive operations temporarily disturb XIP.

#include <stddef.h>
#include <stdint.h>

#include "pico.h"

void *__not_in_flash_func(memcpy)(void *restrict dst, const void *restrict src,
                                  size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if ((((uintptr_t)d | (uintptr_t)s) & 3u) == 0u) {
        uint32_t *dw = (uint32_t *)d;
        const uint32_t *sw = (const uint32_t *)s;
        for (size_t w = n >> 2; w; --w) {
            *dw++ = *sw++;
        }
        d = (uint8_t *)dw;
        s = (const uint8_t *)sw;
        n &= 3u;
    }
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

void *__not_in_flash_func(memset)(void *dst, int c, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t b = (uint8_t)c;
    if (((uintptr_t)d & 3u) == 0u) {
        const uint32_t w = (uint32_t)b * 0x01010101u;
        uint32_t *dw = (uint32_t *)d;
        for (size_t i = n >> 2; i; --i) {
            *dw++ = w;
        }
        d = (uint8_t *)dw;
        n &= 3u;
    }
    while (n--) {
        *d++ = b;
    }
    return dst;
}

void *__not_in_flash_func(memmove)(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d == s || n == 0u) {
        return dst;
    }
    if (d < s) {
        if ((((uintptr_t)d | (uintptr_t)s) & 3u) == 0u) {
            uint32_t *dw = (uint32_t *)d;
            const uint32_t *sw = (const uint32_t *)s;
            for (size_t w = n >> 2; w; --w) {
                *dw++ = *sw++;
            }
            d = (uint8_t *)dw;
            s = (const uint8_t *)sw;
            n &= 3u;
        }
        while (n--) {
            *d++ = *s++;
        }
    } else {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    return dst;
}
