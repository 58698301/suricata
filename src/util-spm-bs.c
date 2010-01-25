/**
 * Copyright (c) 2009 Open Information Security Foundation
 *
 * \author Victor Julien <victor@inliniac.net>
 * \author Pablo Rincon Crespo <pablo.rincon.crespo@gmail.com>
 *
 * bs is a bruteforce search. It will try to search the pattern
 * from all characters until the available text len is less
 * than the length of the pattern. It needs no context but it
 * time cost is not good.
 */

#include "suricata-common.h"
#include "suricata.h"
#include "util-spm-bs.h"
#include <time.h>
#include <limits.h>
#include <string.h>

/**
 * \brief Basic search improved. Limits are better handled, so
 * it doesn't start searches that wont fit in the remaining buffer
 *
 * \param haystack pointer to the buffer to search in
 * \param haystack_len length limit of the buffer
 * \param neddle pointer to the pattern we ar searching for
 * \param needle_len length limit of the needle
 *
 * \retval ptr to start of the match; NULL if no match
 */
inline uint8_t *BasicSearch(const uint8_t *haystack, uint32_t haystack_len, const uint8_t *needle, uint32_t needle_len) {
    const uint8_t *h, *n;
    const uint8_t *hmax = haystack + haystack_len;
    const uint8_t *nmax = needle + needle_len;

    if (needle_len == 0 || needle_len > haystack_len)
        return NULL;

    for (n = needle; nmax - n <= hmax - haystack; haystack++) {
        if (*haystack != *n) {
            continue;
        }
        /* one byte needles */
        if (needle_len == 1)
            return (uint8_t *)haystack;

        for (h = haystack+1, n++; nmax - n <= hmax - haystack; h++, n++) {
            if (*h != *n) {
                break;
            }
            /* if we run out of needle we fully matched */
            if (n == nmax - 1) {
                return (uint8_t *)haystack;
            }
        }
        n = needle;
    }

    return NULL;
}

/**
 * \brief Basic search case less
 *
 * \param haystack pointer to the buffer to search in
 * \param haystack_len length limit of the buffer
 * \param neddle pointer to the pattern we ar searching for
 * \param needle_len length limit of the needle
 *
 * \retval ptr to start of the match; NULL if no match
 */
inline uint8_t *BasicSearchNocase(const uint8_t *haystack, uint32_t haystack_len, const uint8_t *needle, uint32_t needle_len) {
    const uint8_t *h, *n;
    const uint8_t *hmax = haystack + haystack_len;
    const uint8_t *nmax = needle + needle_len;

    if (needle_len == 0 || needle_len > haystack_len)
        return NULL;

    n = needle;
    for (n = needle; nmax - n <= hmax - haystack; haystack++) {
        if (u8_tolower(*haystack) != u8_tolower(*n)) {
            continue;
        }
        /* one byte needles */
        if (needle_len == 1) {
            return (uint8_t *)haystack;
        }

        for (h = haystack+1, n++; nmax - n <= hmax - h ; h++, n++) {
            if (u8_tolower(*h) != u8_tolower(*n)) {
                break;
            }
            /* if we run out of needle we fully matched */
            if (n == nmax - 1) {
                return (uint8_t *)haystack;
            }
        }
        n = needle;
    }

    return NULL;
}

inline void BasicSearchInit (void) {
    /* nothing no more */
}
