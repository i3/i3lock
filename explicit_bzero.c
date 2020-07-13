#include <config.h>

#ifndef HAVE_EXPLICIT_BZERO
/* https://raw.githubusercontent.com/jedisct1/libsodium/d47ded1867af69965b2374b8fb90aee01e6ff291/src/libsodium/sodium/utils.c */

/*
 * ISC License
 *
 * Copyright (c) 2013-2019
 * Frank Denis <j at pureftpd dot org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include <stddef.h>
#include <string.h>

/* LCOV_EXCL_START */
#ifdef HAVE_WEAK_SYMBOLS
__attribute__((weak)) void
_sodium_dummy_symbol_to_prevent_memzero_lto(void *const  pnt,
                                            const size_t len);
__attribute__((weak)) void
_sodium_dummy_symbol_to_prevent_memzero_lto(void *const  pnt,
                                            const size_t len)
{
    (void) pnt; /* LCOV_EXCL_LINE */
    (void) len; /* LCOV_EXCL_LINE */
}
#endif
/* LCOV_EXCL_STOP */

void
explicit_bzero(void *const pnt, const size_t len)
{
#ifdef _WIN32
    SecureZeroMemory(pnt, len);
#elif defined(HAVE_MEMSET_S)
    if (len > 0U && memset_s(pnt, (rsize_t) len, 0, (rsize_t) len) != 0) {
        err("EXIT_FAILURE, explicit_bzero misuse"); /* LCOV_EXCL_LINE */
    }
#elif defined(HAVE_EXPLICIT_MEMSET)
    explicit_memset(pnt, 0, len);
#elif HAVE_WEAK_SYMBOLS
    if (len > 0U) {
        memset(pnt, 0, len);
        _sodium_dummy_symbol_to_prevent_memzero_lto(pnt, len);
    }
# ifdef HAVE_INLINE_ASM
    __asm__ __volatile__ ("" : : "r"(pnt) : "memory");
# endif
#else
    volatile unsigned char *volatile pnt_ =
        (volatile unsigned char *volatile) pnt;
    size_t i = (size_t) 0U;

    while (i < len) {
        pnt_[i++] = 0U;
    }
#endif
}

#endif /* HAVE_EXPLICIT_BZERO */
