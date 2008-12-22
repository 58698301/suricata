/* Copyright (c) 2008 by Victor Julien <victor@inliniac.net> */

/* Chained hash table implementation
 *
 * The 'Free' pointer can be used to have the API free your
 * hashed data. If it's NULL it's the callers responsebility */

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

#include "util-hash.h"

#include "util-unittest.h"

HashTable* HashTableInit(u_int32_t size, u_int32_t (*Hash)(struct _HashTable *, void *, u_int16_t), void (*Free)(void *)) {

    HashTable *ht = NULL;

    if (size == 0) {
        goto error;
    }

    if (Hash == NULL) {
        //printf("ERROR: HashTableInit no Hash function\n");
        goto error;
    }

    /* setup the filter */
    ht = malloc(sizeof(HashTable));
    if (ht == NULL)
        goto error;
    memset(ht,0,sizeof(HashTable));
    ht->array_size = size;
    ht->Hash = Hash;
    ht->Free = Free;

    /* setup the bitarray */
    ht->array = malloc(ht->array_size * sizeof(HashTableBucket *));
    if (ht->array == NULL)
        goto error;
    memset(ht->array,0,ht->array_size * sizeof(HashTableBucket *));

    return ht;

error:
    if (ht != NULL) {
        if (ht->array != NULL)
            free(ht->array);

        free(ht);
    }
    return NULL;
}

void HashTableFree(HashTable *ht) {
    u_int32_t i = 0;

    if (ht == NULL)
        return;

    /* free the buckets */
    for (i = 0; i < ht->array_size; i++) {
        HashTableBucket *hashbucket = ht->array[i];
        while (hashbucket != NULL) {
            HashTableBucket *next_hashbucket = hashbucket->next;
            if (ht->Free != NULL)
                ht->Free(hashbucket->data);
            free(hashbucket);
            hashbucket = next_hashbucket;
        }
    }

    /* free the arrray */
    if (ht->array != NULL)
        free(ht->array);

    free(ht);
}

void HashTablePrint(HashTable *ht) {
    printf("\n----------- Hash Table Stats ------------\n");
    printf("Buckets:               %u\n", ht->array_size);
    printf("Hash function pointer: %p\n", ht->Hash);
    printf("-----------------------------------------\n");
}

int HashTableAdd(HashTable *ht, void *data, u_int16_t datalen) {
    if (ht == NULL || data == NULL || datalen == 0)
        return -1;

    u_int32_t hash = ht->Hash(ht, data, datalen);

    HashTableBucket *hb = malloc(sizeof(HashTableBucket));
    if (hb == NULL) {
        goto error;
    }
    memset(hb, 0, sizeof(HashTableBucket));
    hb->data = data;
    hb->size = datalen;
    hb->next = NULL;

    if (ht->array[hash] == NULL) {
        ht->array[hash] = hb;
    } else {
        hb->next = ht->array[hash];
        ht->array[hash] = hb;
    }

    return 0;

error:
    return -1;
}

int HashTableRemove(HashTable *ht, void *data, u_int16_t datalen) {
    u_int32_t hash = ht->Hash(ht, data, datalen);

    if (ht->array[hash] == NULL) {
        return -1;
    }

    if (ht->array[hash]->next == NULL) {
        if (ht->Free != NULL)
            ht->Free(ht->array[hash]->data);
        free(ht->array[hash]);
        ht->array[hash] = NULL;
        return 0;
    }

    HashTableBucket *hashbucket = ht->array[hash], *prev_hashbucket = NULL;
    do {
        if (hashbucket->size != datalen) {
            prev_hashbucket = hashbucket;
            hashbucket = hashbucket->next;
            continue;
        }

        if (memcmp(hashbucket->data,data,datalen) == 0) {
            if (prev_hashbucket == NULL) {
                /* root bucket */
                ht->array[hash] = hashbucket->next;
            } else {
                /* child bucket */
                prev_hashbucket->next = hashbucket->next;
            }

            /* remove this */
            if (ht->Free != NULL)
                ht->Free(hashbucket->data);
            free(hashbucket);
            return 0;
        }

        prev_hashbucket = hashbucket;
        hashbucket = hashbucket->next;
    } while (hashbucket != NULL);

    return -1;
}

void *HashTableLookup(HashTable *ht, void *data, u_int16_t datalen) {
    u_int32_t hash = ht->Hash(ht, data, datalen);

    if (ht->array[hash] == NULL)
        return NULL;

    HashTableBucket *hashbucket = ht->array[hash];
    do {
        if (hashbucket->size != datalen) {
            hashbucket = hashbucket->next;
            continue;
        }

        if (memcmp(hashbucket->data,data,datalen) == 0)
            return hashbucket->data;

        hashbucket = hashbucket->next;
    } while (hashbucket != NULL);

    return NULL;
}

u_int32_t HashTableGenericHash(HashTable *ht, void *data, u_int16_t datalen) {
     u_int8_t *d = (u_int8_t *)data;
     u_int32_t i;
     u_int32_t hash = 0;

     for (i = 0; i < datalen; i++) {
         if (i == 0)      hash += (((u_int32_t)*d++));
         else if (i == 1) hash += (((u_int32_t)*d++) * datalen);
         else             hash *= (((u_int32_t)*d++) * i) + datalen + i;
     }

     hash *= datalen;
     hash %= ht->array_size;
     return hash;
}

/*
 * ONLY TESTS BELOW THIS COMMENT
 */

static int HashTableTestInit01 (void) {
    HashTable *ht = HashTableInit(1024, HashTableGenericHash, NULL);
    if (ht == NULL)
        return 0;

    HashTableFree(ht);
    return 1;
}

/* no hash function, so it should fail */
static int HashTableTestInit02 (void) {
    HashTable *ht = HashTableInit(1024, NULL, NULL);
    if (ht == NULL)
        return 1;

    HashTableFree(ht);
    return 0;
}

static int HashTableTestInit03 (void) {
    int result = 0;
    HashTable *ht = HashTableInit(1024, HashTableGenericHash, NULL);
    if (ht == NULL)
        return 0;

    if (ht->Hash == HashTableGenericHash)
        result = 1;

    HashTableFree(ht);
    return result;
}

static int HashTableTestInit04 (void) {
    HashTable *ht = HashTableInit(0, HashTableGenericHash, NULL);
    if (ht == NULL)
        return 1;

    HashTableFree(ht);
    return 0;
}

static int HashTableTestAdd01 (void) {
    int result = 0;
    HashTable *ht = HashTableInit(32, HashTableGenericHash, NULL);
    if (ht == NULL)
        goto end;

    int r = HashTableAdd(ht, "test", 0);
    if (r == 0)
        goto end;

    /* all is good! */
    result = 1;
end:
    if (ht != NULL) HashTableFree(ht);
    return result;
}

static int HashTableTestAdd02 (void) {
    int result = 0;
    HashTable *ht = HashTableInit(32, HashTableGenericHash, NULL);
    if (ht == NULL)
        goto end;

    int r = HashTableAdd(ht, NULL, 4);
    if (r == 0)
        goto end;

    /* all is good! */
    result = 1;
end:
    if (ht != NULL) HashTableFree(ht);
    return result;
}

static int HashTableTestFull01 (void) {
    int result = 0;
    HashTable *ht = HashTableInit(32, HashTableGenericHash, NULL);
    if (ht == NULL)
        goto end;

    int r = HashTableAdd(ht, "test", 4);
    if (r != 0)
        goto end;

    char *rp = HashTableLookup(ht, "test", 4);
    if (rp == NULL)
        goto end;

    r = HashTableRemove(ht, "test", 4);
    if (r != 0)
        goto end;

    /* all is good! */
    result = 1;
end:
    if (ht != NULL) HashTableFree(ht);
    return result;
}

static int HashTableTestFull02 (void) {
    int result = 0;
    HashTable *ht = HashTableInit(32, HashTableGenericHash, NULL);
    if (ht == NULL)
        goto end;

    int r = HashTableAdd(ht, "test", 4);
    if (r != 0)
        goto end;

    char *rp = HashTableLookup(ht, "test", 4);
    if (rp == NULL)
        goto end;

    r = HashTableRemove(ht, "test2", 5);
    if (r == 0)
        goto end;

    /* all is good! */
    result = 1;
end:
    if (ht != NULL) HashTableFree(ht);
    return result;
}

void HashTableRegisterTests(void) {
    UtRegisterTest("HashTableTestInit01", HashTableTestInit01, 1);
    UtRegisterTest("HashTableTestInit02", HashTableTestInit02, 1);
    UtRegisterTest("HashTableTestInit03", HashTableTestInit03, 1);
    UtRegisterTest("HashTableTestInit04", HashTableTestInit04, 1);

    UtRegisterTest("HashTableTestAdd01", HashTableTestAdd01, 1);
    UtRegisterTest("HashTableTestAdd02", HashTableTestAdd02, 1);

    UtRegisterTest("HashTableTestFull01", HashTableTestFull01, 1);
    UtRegisterTest("HashTableTestFull02", HashTableTestFull02, 1);
}
