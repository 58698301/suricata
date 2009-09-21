#include "eidps-common.h"
#include "util-byte.h"
#include "util-unittest.h"

int ByteExtract(uint64_t *res, int e, uint16_t len, const uint8_t *bytes)
{
    uint64_t b = 0;
    int i;

    if ((e != BYTE_BIG_ENDIAN) && (e != BYTE_LITTLE_ENDIAN)) {
        /** \todo Need standard return values */
        return -1;
    }

    *res = 0;

    /* Go through each byte and merge it into the result in the correct order */
    /** \todo Probably a more efficient way to do this. */
    for (i = 0; i < len; i++) {

        if (e == BYTE_LITTLE_ENDIAN) {
            b = bytes[i];
        }
        else {
            b = bytes[len - i - 1];
        }

        *res |= (b << ((i & 7) << 3));

        //printf("ByteExtractUint64: %016" PRIx64 "/%016" PRIx64 "\n", (b << ((i & 7) << 3)), *res);
    }

    return len;
}

inline int ByteExtractUint64(uint64_t *res, int e, uint16_t len, const uint8_t *bytes)
{
    uint64_t i64;
    int ret;

    /* Uint64 is limited to 8 bytes */
    if ((len < 0) || (len > 8)) {
        /** \todo Need standard return values */
        return -1;
    }

    ret = ByteExtract(&i64, e, len, bytes);
    if (ret <= 0) {
        return ret;
    }

    *res = (uint64_t)i64;

    return ret;
}

inline int ByteExtractUint32(uint32_t *res, int e, uint16_t len, const uint8_t *bytes)
{
    uint64_t i64;
    int ret;

    /* Uint32 is limited to 4 bytes */
    if ((len < 0) || (len > 4)) {
        /** \todo Need standard return values */
        return -1;
    }

    ret = ByteExtract(&i64, e, len, bytes);
    if (ret <= 0) {
        return ret;
    }

    *res = (uint32_t)i64;

    return ret;
}

inline int ByteExtractUint16(uint16_t *res, int e, uint16_t len, const uint8_t *bytes)
{
    uint64_t i64;
    int ret;

    /* Uint16 is limited to 2 bytes */
    if ((len < 0) || (len > 2)) {
        /** \todo Need standard return values */
        return -1;
    }

    ret = ByteExtract(&i64, e, len, bytes);
    if (ret <= 0) {
        return ret;
    }

    *res = (uint16_t)i64;

    return ret;
}

int ByteExtractString(uint64_t *res, int base, uint16_t len, const char *str)
{
    const char *ptr = str;
    char *endptr = NULL;

    /* 23 - This is the largest string (octal, with a zero prefix) that
     *      will not overflow uint64_t.  The only way this length
     *      could be over 23 and still not overflow is if it were zero
     *      prefixed and we only support 1 byte of zero prefix for octal.
     *
     * "01777777777777777777777" = 0xffffffffffffffff
     */
    char strbuf[24];

//printf("ByteExtractString(%p,%d,%d,\"%s\")", res, base, len, str);
    if (len > 23) {
        printf("ByteExtractString: len too large (23 max)\n");
        return -1;
    }

    if (len) {
        /* Extract out the string so it can be null terminated */
        memcpy(strbuf, str, len);
        strbuf[len] = '\0';
        ptr = strbuf;
    }

    errno = 0;
    *res = strtoull(ptr, &endptr, base);

    if (errno == ERANGE) {
        printf("ByteExtractString: Numeric value out of range.\n");
        return -1;
    } else if (endptr == str) {
        printf("ByteExtractString: Invalid numeric value.\n");
        return -1;
    }
    /* This will interfere with some rules that do not know the length
     * in advance and instead are just using the max.
     */
#if 0
    else if (len && *endptr != '\0') {
        printf("ByteExtractString: Extra characters following numeric value.\n");
        return -1;
    }
#endif

    //printf("ByteExtractString: Extracted base %d: 0x%" PRIx64 "\n", base, *res);

    return (endptr - ptr);
}

inline int ByteExtractStringUint64(uint64_t *res, int base, uint16_t len, const char *str)
{
    return ByteExtractString(res, base, len, str);
}

inline int ByteExtractStringUint32(uint32_t *res, int base, uint16_t len, const char *str)
{
    uint64_t i64;
    int ret;

    ret = ByteExtractString(&i64, base, len, str);
    if (ret <= 0) {
        return ret;
    }

    *res = (uint32_t)i64;

    if ((uint64_t)(*res) != i64) {
        printf("ByteExtractStringUint32: Numeric value out of range (%" PRIx64 " != %" PRIx64 ").", (uint64_t)(*res), i64);
        return -1;
    }

    return ret;
}

inline int ByteExtractStringUint16(uint16_t *res, int base, uint16_t len, const char *str)
{
    uint64_t i64;
    int ret;

    ret = ByteExtractString(&i64, base, len, str);
    if (ret <= 0) {
        return ret;
    }

    *res = (uint16_t)i64;

    if ((uint64_t)(*res) != i64) {
        printf("ByteExtractStringUint16: Numeric value out of range (%" PRIx64 " != %" PRIx64 ").", (uint64_t)(*res), i64);
        return -1;
    }

    return ret;
}

inline int ByteExtractStringUint8(uint8_t *res, int base, uint16_t len, const char *str)
{
    uint64_t i64;
    int ret;

    ret = ByteExtractString(&i64, base, len, str);
    if (ret <= 0) {
        return ret;
    }

    *res = (uint8_t)i64;

    if ((uint64_t)(*res) != i64) {
        printf("ByteExtractStringUint8: Numeric value out of range (%" PRIx64 " != %" PRIx64 ").", (uint64_t)(*res), i64);
        return -1;
    }

    return ret;
}

int ByteExtractStringSigned(int64_t *res, int base, uint16_t len, const char *str)
{
    const char *ptr = str;
    char *endptr;

    /* 23 - This is the largest string (octal, with a zero prefix) that
     *      will not overflow int64_t.  The only way this length
     *      could be over 23 and still not overflow is if it were zero
     *      prefixed and we only support 1 byte of zero prefix for octal.
     *
     * "-0777777777777777777777" = 0xffffffffffffffff
     */
    char strbuf[24];

    if (len > 23) {
        printf("ByteExtractStringSigned: len too large (23 max)\n");
        return -1;
    }

    if (len) {
        /* Extract out the string so it can be null terminated */
        memcpy(strbuf, str, len);
        strbuf[len] = '\0';
        ptr = strbuf;
    }

    errno = 0;
    *res = strtoll(ptr, &endptr, base);

    if (errno == ERANGE) {
        printf("ByteExtractStringSigned: Numeric value out of range.\n");
        return -1;
    } else if (endptr == str) {
        printf("ByteExtractStringSigned: Invalid numeric value.\n");
        return -1;
    }
    /* This will interfere with some rules that do not know the length
     * in advance and instead are just using the max.
     */
#if 0
    else if (len && *endptr != '\0') {
        printf("ByteExtractStringSigned: Extra characters following numeric value.\n");
        return -1;
    }
#endif

    //printf("ByteExtractStringSigned: Extracted base %d: 0x%" PRIx64 "\n", base, *res);

    return (endptr - ptr);
}

inline int ByteExtractStringInt64(int64_t *res, int base, uint16_t len, const char *str)
{
    return ByteExtractStringSigned(res, base, len, str);
}

inline int ByteExtractStringInt32(int32_t *res, int base, uint16_t len, const char *str)
{
    int64_t i64;
    int ret;

    ret = ByteExtractStringSigned(&i64, base, len, str);
    if (ret <= 0) {
        return ret;
    }

    *res = (int32_t)i64;

    if ((int64_t)(*res) != i64) {
        printf("ByteExtractStringUint32: Numeric value out of range (%" PRIx64 " != %" PRIx64 ").", (int64_t)(*res), i64);
        return -1;
    }

    return ret;
}

inline int ByteExtractStringInt16(int16_t *res, int base, uint16_t len, const char *str)
{
    int64_t i64;
    int ret;

    ret = ByteExtractStringSigned(&i64, base, len, str);
    if (ret <= 0) {
        return ret;
    }

    *res = (int16_t)i64;

    if ((int64_t)(*res) != i64) {
        printf("ByteExtractStringInt16: Numeric value out of range (%" PRIx64 " != %" PRIx64 ").", (int64_t)(*res), i64);
        return -1;
    }

    return ret;
}

inline int ByteExtractStringInt8(int8_t *res, int base, uint16_t len, const char *str)
{
    int64_t i64;
    int ret;

    ret = ByteExtractStringSigned(&i64, base, len, str);
    if (ret <= 0) {
        return ret;
    }

    *res = (int8_t)i64;

    if ((int64_t)(*res) != i64) {
        printf("ByteExtractStringInt8: Numeric value out of range (%" PRIx64 " != %" PRIx64 ").", (int64_t)(*res), i64);
        return -1;
    }

    return ret;
}

/* UNITTESTS */
#ifdef UNITTESTS

static int ByteTest01 (void) {
    uint16_t val = 0x0102;
    uint16_t i16 = 0xbfbf;
    uint8_t bytes[2] = { 0x02, 0x01 };
    int ret = ByteExtractUint16(&i16, BYTE_LITTLE_ENDIAN, sizeof(bytes), bytes);

    if ((ret == 2) && (i16 == val)) {
        return 1;
    }

    return 0;
}

static int ByteTest02 (void) {
    uint16_t val = 0x0102;
    uint16_t i16 = 0xbfbf;
    uint8_t bytes[2] = { 0x01, 0x02 };
    int ret = ByteExtractUint16(&i16, BYTE_BIG_ENDIAN, sizeof(bytes), bytes);

    if ((ret == 2) && (i16 == val)) {
        return 1;
    }

    return 0;
}

static int ByteTest03 (void) {
    uint32_t val = 0x01020304;
    uint32_t i32 = 0xbfbfbfbf;
    uint8_t bytes[4] = { 0x04, 0x03, 0x02, 0x01 };
    int ret = ByteExtractUint32(&i32, BYTE_LITTLE_ENDIAN, sizeof(bytes), bytes);

    if ((ret == 4) && (i32 == val)) {
        return 1;
    }

    return 0;
}

static int ByteTest04 (void) {
    uint32_t val = 0x01020304;
    uint32_t i32 = 0xbfbfbfbf;
    uint8_t bytes[4] = { 0x01, 0x02, 0x03, 0x04 };
    int ret = ByteExtractUint32(&i32, BYTE_BIG_ENDIAN, sizeof(bytes), bytes);

    if ((ret == 4) && (i32 == val)) {
        return 1;
    }

    return 0;
}

static int ByteTest05 (void) {
    uint64_t val = 0x0102030405060708ULL;
    uint64_t i64 = 0xbfbfbfbfbfbfbfbfULL;
    uint8_t bytes[8] = { 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01 };
    int ret = ByteExtractUint64(&i64, BYTE_LITTLE_ENDIAN, sizeof(bytes), bytes);

    if ((ret == 8) && (i64 == val)) {
        return 1;
    }

    return 0;
}

static int ByteTest06 (void) {
    uint64_t val = 0x0102030405060708ULL;
    uint64_t i64 = 0xbfbfbfbfbfbfbfbfULL;
    uint8_t bytes[8] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    int ret = ByteExtractUint64(&i64, BYTE_BIG_ENDIAN, sizeof(bytes), bytes);

    if ((ret == 8) && (i64 == val)) {
        return 1;
    }

    return 0;
}

static int ByteTest07 (void) {
    const char *str = "1234567890";
    uint64_t val = 1234567890;
    uint64_t i64 = 0xbfbfbfbfbfbfbfbfULL;
    int ret = ByteExtractStringUint64(&i64, 10, strlen(str), str);

    if ((ret == 10) && (i64 == val)) {
        return 1;
    }

    return 0;
}

static int ByteTest08 (void) {
    const char *str = "1234567890";
    uint32_t val = 1234567890;
    uint32_t i32 = 0xbfbfbfbf;
    int ret = ByteExtractStringUint32(&i32, 10, strlen(str), str);

    if ((ret == 10) && (i32 == val)) {
        return 1;
    }

    return 0;
}

static int ByteTest09 (void) {
    const char *str = "12345";
    uint16_t val = 12345;
    uint16_t i16 = 0xbfbf;
    int ret = ByteExtractStringUint16(&i16, 10, strlen(str), str);

    if ((ret == 5) && (i16 == val)) {
        return 1;
    }

    return 0;
}

static int ByteTest10 (void) {
    const char *str = "123";
    uint8_t val = 123;
    uint8_t i8 = 0xbf;
    int ret = ByteExtractStringUint8(&i8, 10, strlen(str), str);

    if ((ret == 3) && (i8 == val)) {
        return 1;
    }

    return 0;
}

static int ByteTest11 (void) {
    const char *str = "-1234567890";
    int64_t val = -1234567890;
    int64_t i64 = 0xbfbfbfbfbfbfbfbfULL;
    int ret = ByteExtractStringInt64(&i64, 10, strlen(str), str);

    if ((ret == 11) && (i64 == val)) {
        return 1;
    }

    return 0;
}

static int ByteTest12 (void) {
    const char *str = "-1234567890";
    int32_t val = -1234567890;
    int32_t i32 = 0xbfbfbfbf;
    int ret = ByteExtractStringInt32(&i32, 10, strlen(str), str);

    if ((ret == 11) && (i32 == val)) {
        return 1;
    }

    return 0;
}

static int ByteTest13 (void) {
    const char *str = "-12345";
    int16_t val = -12345;
    int16_t i16 = 0xbfbf;
    int ret = ByteExtractStringInt16(&i16, 10, strlen(str), str);

    if ((ret == 6) && (i16 == val)) {
        return 1;
    }

    return 0;
}

static int ByteTest14 (void) {
    const char *str = "-123";
    int8_t val = -123;
    int8_t i8 = 0xbf;
    int ret = ByteExtractStringInt8(&i8, 10, strlen(str), str);

    if ((ret == 4) && (i8 == val)) {
        return 1;
    }

    return 0;
}
#endif /* UNITTESTS */

void ByteRegisterTests(void) {
#ifdef UNITTESTS
    UtRegisterTest("ByteTest01", ByteTest01, 1);
    UtRegisterTest("ByteTest02", ByteTest02, 1);
    UtRegisterTest("ByteTest03", ByteTest03, 1);
    UtRegisterTest("ByteTest04", ByteTest04, 1);
    UtRegisterTest("ByteTest05", ByteTest05, 1);
    UtRegisterTest("ByteTest06", ByteTest06, 1);
    UtRegisterTest("ByteTest07", ByteTest07, 1);
    UtRegisterTest("ByteTest08", ByteTest08, 1);
    UtRegisterTest("ByteTest09", ByteTest09, 1);
    UtRegisterTest("ByteTest10", ByteTest10, 1);
    UtRegisterTest("ByteTest11", ByteTest11, 1);
    UtRegisterTest("ByteTest12", ByteTest12, 1);
    UtRegisterTest("ByteTest13", ByteTest13, 1);
    UtRegisterTest("ByteTest14", ByteTest14, 1);
#endif /* UNITTESTS */
}

