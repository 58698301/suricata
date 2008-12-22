/* Copyright (c) 2008 by Victor Julien <victor@inliniac.net> */

/* Counting Bloom Filter implementation. Can be used with 8, 16, 32 bits
 * counters.
 */

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

#include "util-bloomfilter-counting.h"

#include "util-unittest.h"

/* type: 1, 2 or 4 for 8, 16, or 32 bit counters
 *
 */
BloomFilterCounting *BloomFilterCountingInit(u_int32_t size, u_int8_t type, u_int8_t iter, u_int32_t (*Hash)(void *, u_int16_t, u_int8_t, u_int32_t)) {
    BloomFilterCounting *bf = NULL;

    if (iter == 0)
        goto error;

    if (Hash == NULL || size == 0) {
        //printf("ERROR: BloomFilterCountingInit no Hash function\n");
        goto error;
    }

    if (type != 1 && type != 2 && type != 4) {
        //printf("ERROR: BloomFilterCountingInit only 1, 2 and 4 bytes are supported\n");
        goto error;
    }

    /* setup the filter */
    bf = malloc(sizeof(BloomFilterCounting));
    if (bf == NULL)
        goto error;
    memset(bf,0,sizeof(BloomFilterCounting));
    bf->type = type; /* size of the type: 1, 2, 4 */
    bf->array_size = size;
    bf->hash_iterations = iter;
    bf->Hash = Hash;

    /* setup the bitarray */
    bf->array = malloc(bf->array_size * bf->type);
    if (bf->array == NULL)
        goto error;
    memset(bf->array,0,bf->array_size * bf->type);

    return bf;

error:
    if (bf != NULL) {
        if (bf->array != NULL)
            free(bf->array);

        free(bf);
    }
    return NULL;
}

void BloomFilterCountingFree(BloomFilterCounting *bf) {
    if (bf != NULL) {
        if (bf->array != NULL)
            free(bf->array);

        free(bf);
    }
}

void BloomFilterCountingPrint(BloomFilterCounting *bf) {
    printf("\n------ Counting Bloom Filter Stats ------\n");
    printf("Buckets:               %u\n", bf->array_size);
    printf("Counter size:          %u\n", bf->type);
    printf("Memory size:           %u bytes\n", bf->array_size * bf->type);
    printf("Hash function pointer: %p\n", bf->Hash);
    printf("Hash functions:        %u\n", bf->hash_iterations);
    printf("-----------------------------------------\n");
}

int BloomFilterCountingAdd(BloomFilterCounting *bf, void *data, u_int16_t datalen) {
    u_int8_t iter = 0;
    u_int32_t hash = 0;

    if (bf == NULL || data == NULL || datalen == 0)
        return -1;

    for (iter = 0; iter < bf->hash_iterations; iter++) {
        hash = bf->Hash(data, datalen, iter, bf->array_size) * bf->type;
        if (bf->type == 1) {
            u_int8_t *u8 = (u_int8_t *)&bf->array[hash];
            if ((*u8) != 255)
                (*u8)++;
        } else if (bf->type == 2) {
            u_int16_t *u16 = (u_int16_t *)&bf->array[hash];
            if ((*u16) != 65535)
                (*u16)++;
        } else if (bf->type == 4) {
            u_int32_t *u32 = (u_int32_t *)&bf->array[hash];
            if ((*u32) != 4294967295UL)
                (*u32)++;
        }
    }

    return 0;
}

int BloomFilterCountingRemove(BloomFilterCounting *bf, void *data, u_int16_t datalen) {
    u_int8_t iter = 0;
    u_int32_t hash = 0;

    if (bf == NULL || data == NULL || datalen == 0)
        return -1;

    /* only remove data that was actually added */
    if (BloomFilterCountingTest(bf, data, datalen) == 0) {
        printf("ERROR: BloomFilterCountingRemove tried to remove data "
               "that was never added to the set or was already removed.\n");
        return -1;
    }

    /* decrease counters for every iteration */
    for (iter = 0; iter < bf->hash_iterations; iter++) {
        hash = bf->Hash(data, datalen, iter, bf->array_size) * bf->type;
        if (bf->type == 1) {
            u_int8_t *u8 = (u_int8_t *)&bf->array[hash];
            if ((*u8) > 0)
                (*u8)--;
            else {
                printf("ERROR: BloomFilterCountingRemove tried to decrease a "
                       "counter below zero.\n");
                return -1;
            }
        } else if (bf->type == 2) {
            u_int16_t *u16 = (u_int16_t *)&bf->array[hash];
            if ((*u16) > 0)
                (*u16)--;
            else {
                printf("ERROR: BloomFilterCountingRemove tried to decrease a "
                       "counter below zero.\n");
                return -1;
            }
        } else if (bf->type == 4) {
            u_int32_t *u32 = (u_int32_t *)&bf->array[hash];
            if ((*u32) > 0)
                (*u32)--;
            else {
                printf("ERROR: BloomFilterCountingRemove tried to decrease a "
                       "counter below zero.\n");
                return -1;
            }
        }
    }

    return 0;
}

/* Test if data matches our filter and is likely to be in the set
 *
 * returns 0: for no match
 *         1: match
 */
int BloomFilterCountingTest(BloomFilterCounting *bf, void *data, u_int16_t datalen) {
    u_int8_t iter = 0;
    u_int32_t hash = 0;
    int hit = 1;

    /* check each hash iteration */
    for (iter = 0; iter < bf->hash_iterations; iter++) {
        hash = bf->Hash(data, datalen, iter, bf->array_size) * bf->type;
        if (!(bf->array[hash])) {
            hit = 0;
            break;
        }
    }

    return hit;
}

static u_int32_t BloomHash(void *data, u_int16_t datalen, u_int8_t iter, u_int32_t hash_size) {
     u_int8_t *d = (u_int8_t *)data;
     u_int32_t i;
     u_int32_t hash = 0;

     for (i = 0; i < datalen; i++) {
         if (i == 0)      hash += (((u_int32_t)*d++));
         else if (i == 1) hash += (((u_int32_t)*d++) * datalen);
         else             hash *= (((u_int32_t)*d++) * i);
     }

     hash *= (iter + datalen);
     hash %= hash_size;
     return hash;
}

/*
 * ONLY TESTS BELOW THIS COMMENT
 */

static int BloomFilterCountingTestInit01 (void) {
    BloomFilterCounting *bf = BloomFilterCountingInit(1024, 4, 4, BloomHash);
    if (bf == NULL)
        return 0;

    BloomFilterCountingFree(bf);
    return 1;
}

/* no hash function, so it should fail */
static int BloomFilterCountingTestInit02 (void) {
    BloomFilterCounting *bf = BloomFilterCountingInit(1024, 4, 4, NULL);
    if (bf == NULL)
        return 1;

    BloomFilterCountingFree(bf);
    return 0;
}

static int BloomFilterCountingTestInit03 (void) {
    int result = 0;
    BloomFilterCounting *bf = BloomFilterCountingInit(1024, 4, 4, BloomHash);
    if (bf == NULL)
        return 0;

    if (bf->Hash == BloomHash)
        result = 1;

    BloomFilterCountingFree(bf);
    return result;
}

static int BloomFilterCountingTestInit04 (void) {
    BloomFilterCounting *bf = BloomFilterCountingInit(1024, 0, 4, BloomHash);
    if (bf == NULL)
        return 1;

    BloomFilterCountingFree(bf);
    return 0;
}

static int BloomFilterCountingTestInit05 (void) {
    BloomFilterCounting *bf = BloomFilterCountingInit(0, 4, 4, BloomHash);
    if (bf == NULL)
        return 1;

    BloomFilterCountingFree(bf);
    return 0;
}

static int BloomFilterCountingTestInit06 (void) {
    BloomFilterCounting *bf = BloomFilterCountingInit(32, 3, 4, BloomHash);
    if (bf == NULL)
        return 1;

    BloomFilterCountingFree(bf);
    return 0;
}

static int BloomFilterCountingTestAdd01 (void) {
    int result = 0;
    BloomFilterCounting *bf = BloomFilterCountingInit(1024, 4, 4, BloomHash);
    if (bf == NULL)
        return 0;

    int r = BloomFilterCountingAdd(bf, "test", 0);
    if (r == 0)
        goto end;

    /* all is good! */
    result = 1;
end:
    if (bf != NULL) BloomFilterCountingFree(bf);
    return result;
}

static int BloomFilterCountingTestAdd02 (void) {
    int result = 0;
    BloomFilterCounting *bf = BloomFilterCountingInit(1024, 4, 4, BloomHash);
    if (bf == NULL)
        return 0;

    int r = BloomFilterCountingAdd(bf, NULL, 4);
    if (r == 0)
        goto end;

    /* all is good! */
    result = 1;
end:
    if (bf != NULL) BloomFilterCountingFree(bf);
    return result;
}

static int BloomFilterCountingTestFull01 (void) {
    int result = 0;
    BloomFilterCounting *bf = BloomFilterCountingInit(32, 4, 4, BloomHash);
    if (bf == NULL)
        goto end;

    int r = BloomFilterCountingAdd(bf, "test", 4);
    if (r != 0)
        goto end;

    r = BloomFilterCountingTest(bf, "test", 4);
    if (r != 1)
        goto end;

    r = BloomFilterCountingRemove(bf, "test", 4);
    if (r != 0)
        goto end;

    /* all is good! */
    result = 1;
end:
    if (bf != NULL) BloomFilterCountingFree(bf);
    return result;
}

static int BloomFilterCountingTestFull02 (void) {
    int result = 0;
    BloomFilterCounting *bf = BloomFilterCountingInit(32, 4, 4, BloomHash);
    if (bf == NULL)
        goto end;

    int r = BloomFilterCountingTest(bf, "test", 4);
    if (r != 0)
        goto end;

    /* all is good! */
    result = 1;
end:
    if (bf != NULL) BloomFilterCountingFree(bf);
    return result;
}

void BloomFilterCountingRegisterTests(void) {
    UtRegisterTest("BloomFilterCountingTestInit01", BloomFilterCountingTestInit01, 1);
    UtRegisterTest("BloomFilterCountingTestInit02", BloomFilterCountingTestInit02, 1);
    UtRegisterTest("BloomFilterCountingTestInit03", BloomFilterCountingTestInit03, 1);
    UtRegisterTest("BloomFilterCountingTestInit04", BloomFilterCountingTestInit04, 1);
    UtRegisterTest("BloomFilterCountingTestInit05", BloomFilterCountingTestInit05, 1);
    UtRegisterTest("BloomFilterCountingTestInit06", BloomFilterCountingTestInit06, 1);

    UtRegisterTest("BloomFilterCountingTestAdd01", BloomFilterCountingTestAdd01, 1);
    UtRegisterTest("BloomFilterCountingTestAdd02", BloomFilterCountingTestAdd02, 1);

    UtRegisterTest("BloomFilterCountingTestFull01", BloomFilterCountingTestFull01, 1);
    UtRegisterTest("BloomFilterCountingTestFull02", BloomFilterCountingTestFull02, 1);
}
