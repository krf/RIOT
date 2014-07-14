/**
 * Implementation of CBOR
 *
 * Copyright (C) 2014 Freie Universität Berlin
 * Copyright (C) 2014 Kevin Funk <kfunk@kde.org>
 * Copyright (C) 2014 Jana Cavojska <jana.cavojska9@gmail.com>
 *
 * This file is subject to the terms and conditions of the GNU Lesser General
 * Public License. See the file LICENSE in the top level directory for more
 * details.
 */

#include "cbor.h"

#include "net_help.h"

#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CBOR_TYPE_MASK          0xE0    /* top 3 bits */
#define CBOR_INFO_MASK          0x1F    /* low 5 bits */


/* Jump Table for Initial Byte (cf. table 5) */
#define CBOR_UINT       0x00            /* type 0 */
#define CBOR_NEGINT     0x20            /* type 1 */
#define CBOR_BYTES      0x40            /* type 2 */
#define CBOR_TEXT       0x60            /* type 3 */
#define CBOR_ARRAY      0x80            /* type 4 */
#define CBOR_MAP        0xA0            /* type 5 */
#define CBOR_TAG        0xC0            /* type 6 */
#define CBOR_7          0xE0            /* type 7 (float and other types) */

/* Major types (cf. section 2.1) */
/* Major type 0: Unsigned integers */
#define CBOR_UINT8_FOLLOWS      24      /* 0x18 */
#define CBOR_UINT16_FOLLOWS     25      /* 0x19 */
#define CBOR_UINT32_FOLLOWS     26      /* 0x1a */
#define CBOR_UINT64_FOLLOWS     27      /* 0x1b */

/* Indefinite Lengths for Some Major types (cf. section 2.2) */
#define CBOR_VAR_FOLLOWS        31      /* 0x1f */

/* Major type 6: Semantic tagging */
#define CBOR_DATETIME_STRING_FOLLOWS        0
#define CBOR_DATETIME_EPOCH_FOLLOWS         1

/* Major type 7: Float and other types */
#define CBOR_FALSE      (CBOR_7 | 20)
#define CBOR_TRUE       (CBOR_7 | 21)
#define CBOR_NULL       (CBOR_7 | 22)
#define CBOR_UNDEFINED  (CBOR_7 | 23)
#define CBOR_FLOAT16    (CBOR_7 | 25)
#define CBOR_FLOAT32    (CBOR_7 | 26)
#define CBOR_FLOAT64    (CBOR_7 | 27)
#define CBOR_BREAK      (CBOR_7 | 31)

#define CBOR_TYPE(stream, offset) (stream->data[offset] & CBOR_TYPE_MASK)
#define CBOR_ADDITIONAL_INFO(stream, offset) (stream->data[offset] & CBOR_INFO_MASK)

/* Ensure that @p stream is big enough to fit @p bytes bytes, otherwise return 0 */
#define CBOR_ENSURE_SIZE(stream, bytes) do { \
    if (stream->pos + bytes >= stream->size) { return 0; } \
} while(0)

/* Extra defines not related to the protocol itself */
#define CBOR_STREAM_PRINT_BUFFERSIZE 1024 /* bytes */

#ifndef INFINITY
#define INFINITY (1.0/0.0)
#endif
#ifndef NAN
#define NAN (0.0/0.0)
#endif

/**
 * Convert float @p x to network format
 */
static uint32_t htonf(float x)
{
    union u {
        float f;
        uint32_t i;
    } u = { .f = x };
    return HTONL(u.i);
}

/**
 * Convert float @p x to host format
 */
static float ntohf(uint32_t x)
{
    union u {
        float f;
        uint32_t i;
    } u = { .i = NTOHL(x) };
    return u.f;
}

/**
 * Convert long long @p x to network format
 */
static uint64_t htonll(uint64_t x)
{
    return (((uint64_t)HTONL(x)) << 32) + HTONL(x >> 32);
}

/**
 * Convert long long @p x to host format
 */
static uint64_t ntohll(uint64_t x)
{
    return (((uint64_t)NTOHL(x)) << 32) + NTOHL(x >> 32);
}

/**
 * Convert double @p x to network format
 */
static uint64_t htond(double x)
{
    union u {
        double d;
        uint64_t i;
    } u = { .d = x };
    return htonll(u.i);
}

/**
 * Convert double @p x to host format
 */
static double ntohd(uint64_t x)
{
    union u {
        double d;
        uint64_t i;
    } u = { .i = ntohll(x) };
    return u.d;
}

/**
 * Source: CBOR RFC reference implementation
 */
double decode_float_half(unsigned char *halfp)
{
    int half = (halfp[0] << 8) + halfp[1];
    int exp = (half >> 10) & 0x1f;
    int mant = half & 0x3ff;
    double val;

    if (exp == 0) {
        val = ldexp(mant, -24);
    }
    else if (exp != 31) {
        val = ldexp(mant + 1024, exp - 25);
    }
    else {
        val = mant == 0 ? INFINITY : NAN;
    }

    return half & 0x8000 ? -val : val;
}

/**
 * Source: According to http://gamedev.stackexchange.com/questions/17326/conversion-of-a-number-from-single-precision-floating-point-representation-to-a
 */
static uint16_t encode_float_half(float x)
{
    union u {
        float f;
        uint32_t i;
    } u = { .f = x };

    uint16_t bits = (u.i >> 16) & 0x8000; /* Get the sign */
    uint16_t m = (u.i >> 12) & 0x07ff; /* Keep one extra bit for rounding */
    unsigned int e = (u.i >> 23) & 0xff; /* Using int is faster here */

    /* If zero, or denormal, or exponent underflows too much for a denormal
     * half, return signed zero. */
    if (e < 103) {
        return bits;
    }

    /* If NaN, return NaN. If Inf or exponent overflow, return Inf. */
    if (e > 142) {
        bits |= 0x7c00u;
        /* If exponent was 0xff and one mantissa bit was set, it means NaN,
         * not Inf, so make sure we set one mantissa bit too. */
        bits |= e == 255 && (u.i & 0x007fffffu);
        return bits;
    }

    /* If exponent underflows but not too much, return a denormal */
    if (e < 113) {
        m |= 0x0800u;
        /* Extra rounding may overflow and set mantissa to 0 and exponent
         * to 1, which is OK. */
        bits |= (m >> (114 - e)) + ((m >> (113 - e)) & 1);
        return bits;
    }

    bits |= ((e - 112) << 10) | (m >> 1);
    /* Extra rounding. An overflow will set mantissa to 0 and increment
     * the exponent, which is OK. */
    bits += m & 1;
    return bits;
}

/**
 * Print @p size bytes at @p data in hexadecimal display format
 */
void dump_memory(unsigned char *data, size_t size)
{
    if (!data || !size) {
        return;
    }

    printf("0x");

    for (size_t i = 0; i < size; ++i) {
        printf("%02X", data[i]);
    }
}

void cbor_init(cbor_stream_t *stream, unsigned char *buffer, size_t size)
{
    if (!stream) {
        return;
    }

    stream->data = buffer;
    stream->size = size;
    stream->pos = 0;
}

void cbor_clear(cbor_stream_t *stream)
{
    if (!stream) {
        return;
    }

    stream->pos = 0;
}

void cbor_destroy(cbor_stream_t *stream)
{
    if (!stream) {
        return;
    }

    stream->data = 0;
    stream->size = 0;
    stream->pos = 0;
}

static size_t encode_int(unsigned char major_type, cbor_stream_t *s, uint64_t val)
{
    if (!s) {
        return 0;
    }

    if (val <= 23) {
        CBOR_ENSURE_SIZE(s, 1);
        s->data[s->pos++] = major_type | val;
        return 1;
    }
    else if (val <= 0xff) {
        CBOR_ENSURE_SIZE(s, 2);
        s->data[s->pos++] = major_type | CBOR_UINT8_FOLLOWS;
        s->data[s->pos++] = val & 0xff;
        return 2;
    }
    else if (val <= 0xffff) {
        CBOR_ENSURE_SIZE(s, 3);
        s->data[s->pos++] = major_type | CBOR_UINT16_FOLLOWS;
        s->data[s->pos++] = (val >> 8) & 0xff;
        s->data[s->pos++] = val & 0xff;
        return 3;
    }
    else if (val <= 0xffffffffL) {
        CBOR_ENSURE_SIZE(s, 5);
        s->data[s->pos++] = major_type | CBOR_UINT32_FOLLOWS;
        s->data[s->pos++] = (val >> 24) & 0xff;
        s->data[s->pos++] = (val >> 16) & 0xff;
        s->data[s->pos++] = (val >>  8) & 0xff;
        s->data[s->pos++] = val & 0xff;
        return 5;
    }
    else {
        CBOR_ENSURE_SIZE(s, 9);
        s->data[s->pos++] = major_type | CBOR_UINT64_FOLLOWS;
        s->data[s->pos++] = (val >> 56) & 0xff;
        s->data[s->pos++] = (val >> 48) & 0xff;
        s->data[s->pos++] = (val >> 40) & 0xff;
        s->data[s->pos++] = (val >> 32) & 0xff;
        s->data[s->pos++] = (val >> 24) & 0xff;
        s->data[s->pos++] = (val >> 16) & 0xff;
        s->data[s->pos++] = (val >>  8) & 0xff;
        s->data[s->pos++] = val & 0xff;
        return 9;
    }
}

static size_t decode_int(const cbor_stream_t *s, size_t offset, uint64_t *val)
{
    if (!s) {
        return 0;
    }

    unsigned char *data = &s->data[offset];
    unsigned char additional_info = CBOR_ADDITIONAL_INFO(s, offset);

    *val = 0; /* clear val first */

    if (additional_info <= 23) {
        *val = (data[0] & CBOR_INFO_MASK);
        return 1;
    }
    else if (additional_info == CBOR_UINT8_FOLLOWS) {
        *val = data[1];
        return 2;
    }
    else if (additional_info == CBOR_UINT16_FOLLOWS) {
        *val = (data[1] << 8) |
               (data[2]);
        return 3;
    }
    else if (additional_info == CBOR_UINT32_FOLLOWS) {
        (*val) = ((uint64_t)data[1] << 24) |
                 (data[2] << 16) |
                 (data[3] <<  8) |
                 (data[4]);
        return 5;
    }
    else if (additional_info == CBOR_UINT64_FOLLOWS) {
        *val = ((uint64_t)data[1] << 56) |
               ((uint64_t)data[2] << 48) |
               ((uint64_t)data[3] << 40) |
               ((uint64_t)data[4] << 32) |
               ((uint64_t)data[5] << 24) |
               (data[6] << 16) |
               (data[7] <<  8) |
               (data[8]);
        return 9;
    }

    return 0;
}

static size_t encode_bytes(unsigned char major_type, cbor_stream_t *s, const char *data, size_t length)
{
    size_t bytes_start = encode_int(major_type, s, (uint64_t) length);

    if (!bytes_start) {
        return 0;
    }

    CBOR_ENSURE_SIZE(s, length);

    memcpy(&(s->data[s->pos]), data, length); /* copy byte string into our cbor struct */
    s->pos += length;
    return bytes_start + length;
}

static size_t decode_bytes(const cbor_stream_t *s, size_t offset, char *out, size_t length)
{
    if ((CBOR_TYPE(s, offset) != CBOR_BYTES && CBOR_TYPE(s, offset) != CBOR_TEXT) || !s || !out) {
        return 0;
    }

    uint64_t bytes_length;
    size_t bytes_start = decode_int(s, offset, &bytes_length);

    if (!bytes_start) {
        return 0;
    }

    if (length + 1 < bytes_length) {
        return 0;
    }

    memcpy(out, &s->data[offset + bytes_start], bytes_length);
    out[bytes_length] = '\0';
    return bytes_start + bytes_length;
}

size_t cbor_deserialize_int(const cbor_stream_t *stream, size_t offset, int *val)
{
    if ((CBOR_TYPE(stream, offset) != CBOR_UINT && CBOR_TYPE(stream, offset) != CBOR_NEGINT) || !val) {
        return 0;
    }

    uint64_t buf;
    size_t read_bytes = decode_int(stream, offset, &buf);

    if (CBOR_TYPE(stream, offset) == CBOR_UINT) {
        *val = buf; /* resolve as CBOR_UINT */
    }
    else {
        *val = -1 - buf; /* resolve as CBOR_NEGINT */
    }

    return read_bytes;
}

size_t cbor_serialize_int(cbor_stream_t *s, int val)
{
    if (val >= 0) {
        /* Major type 0: an unsigned integer */
        return encode_int(CBOR_UINT, s, val);
    }
    else {
        /* Major type 1: an negative integer */
        return encode_int(CBOR_NEGINT, s, -1 - val);
    }
}

size_t cbor_deserialize_uint64_t(const cbor_stream_t *stream, size_t offset, uint64_t *val)
{
    if (CBOR_TYPE(stream, offset) != CBOR_UINT || !val) {
        return 0;
    }

    return decode_int(stream, offset, val);
}

size_t cbor_serialize_uint64_t(cbor_stream_t *s, uint64_t val)
{
    return encode_int(CBOR_UINT, s, val);
}

size_t cbor_deserialize_int64_t(const cbor_stream_t *stream, size_t offset, int64_t *val)
{
    if ((CBOR_TYPE(stream, offset) != CBOR_UINT && CBOR_TYPE(stream, offset) != CBOR_NEGINT) || !val) {
        return 0;
    }

    uint64_t buf;
    size_t read_bytes = decode_int(stream, offset, &buf);

    if (CBOR_TYPE(stream, offset) == CBOR_UINT) {
        *val = buf; /* resolve as CBOR_UINT */
    }
    else {
        *val = -1 - buf; /* resolve as CBOR_NEGINT */
    }

    return read_bytes;
}

size_t cbor_serialize_int64_t(cbor_stream_t *s, int64_t val)
{
    if (val >= 0) {
        /* Major type 0: an unsigned integer */
        return encode_int(CBOR_UINT, s, val);
    }
    else {
        /* Major type 1: an negative integer */
        return encode_int(CBOR_NEGINT, s, -1 - val);
    }
}

size_t cbor_deserialize_bool(const cbor_stream_t *stream, size_t offset, bool *val)
{
    if (CBOR_TYPE(stream, offset) != CBOR_7 || !val) {
        return 0;
    }

    unsigned char byte = stream->data[offset];
    *val = (byte == CBOR_TRUE);
    return 1;
}

size_t cbor_serialize_bool(cbor_stream_t *s, bool val)
{
    CBOR_ENSURE_SIZE(s, 1);
    s->data[s->pos++] = val ? CBOR_TRUE : CBOR_FALSE;
    return 1;
}

size_t cbor_deserialize_float_half(const cbor_stream_t *stream, size_t offset, float *val)
{
    if (CBOR_TYPE(stream, offset) != CBOR_7 || !val) {
        return 0;
    }

    unsigned char *data = &stream->data[offset];

    if (*data == CBOR_FLOAT16) {
        *val = (float)decode_float_half(data + 1);
        return 3;
    }

    return 0;
}

size_t cbor_serialize_float_half(cbor_stream_t *s, float val)
{
    CBOR_ENSURE_SIZE(s, 3);
    s->data[s->pos++] = CBOR_FLOAT16;
    uint16_t encoded_val = HTONS(encode_float_half(val));
    memcpy(s->data + s->pos, &encoded_val, 2);
    s->pos += 2;
    return 3;
}

size_t cbor_deserialize_float(const cbor_stream_t *stream, size_t offset, float *val)
{
    if (CBOR_TYPE(stream, offset) != CBOR_7 || !val) {
        return 0;
    }

    unsigned char *data = &stream->data[offset];

    if (*data == CBOR_FLOAT32) {
        *val = ntohf(*(uint32_t *)(data + 1));
        return 4;
    }

    return 0;
}

size_t cbor_serialize_float(cbor_stream_t *s, float val)
{
    CBOR_ENSURE_SIZE(s, 5);
    s->data[s->pos++] = CBOR_FLOAT32;
    uint32_t encoded_val = htonf(val);
    memcpy(s->data + s->pos, &encoded_val, 4);
    s->pos += 4;
    return 5;
}

size_t cbor_deserialize_double(const cbor_stream_t *stream, size_t offset, double *val)
{
    if (CBOR_TYPE(stream, offset) != CBOR_7 || !val) {
        return 0;
    }

    unsigned char *data = &stream->data[offset];

    if (*data == CBOR_FLOAT64) {
        *val = ntohd(*(uint64_t *)(data + 1));
        return 9;
    }

    return 0;
}

size_t cbor_serialize_double(cbor_stream_t *s, double val)
{
    CBOR_ENSURE_SIZE(s, 9);
    s->data[s->pos++] = CBOR_FLOAT64;
    uint64_t encoded_val = htond(val);
    memcpy(s->data + s->pos, &encoded_val, 8);
    s->pos += 8;
    return 9;
}

size_t cbor_deserialize_byte_string(const cbor_stream_t *stream, size_t offset, char *val, size_t length)
{
    if (CBOR_TYPE(stream, offset) != CBOR_BYTES) {
        return 0;
    }

    return decode_bytes(stream, offset, val, length);
}

size_t cbor_serialize_byte_string(cbor_stream_t *stream, const char *val)
{
    return encode_bytes(CBOR_BYTES, stream, val, strlen(val));
}

size_t cbor_deserialize_unicode_string(const cbor_stream_t *stream, size_t offset, char *val, size_t length)
{
    if (CBOR_TYPE(stream, offset) != CBOR_TEXT) {
        return 0;
    }

    return decode_bytes(stream, offset, val, length);
}

size_t cbor_serialize_unicode_string(cbor_stream_t *stream, const char *val)
{
    return encode_bytes(CBOR_TEXT, stream, val, strlen(val));
}

size_t cbor_deserialize_array(const cbor_stream_t *s, size_t offset, size_t *array_length)
{
    if (CBOR_TYPE(s, offset) != CBOR_ARRAY || !array_length) {
        return 0;
    }

    uint64_t val;
    size_t read_bytes = decode_int(s, offset, &val);
    *array_length = (size_t)val;
    return read_bytes;
}

size_t cbor_serialize_array(cbor_stream_t *s, size_t array_length)
{
    /* serialize number of array items */
    return encode_int(CBOR_ARRAY, s, array_length);
}

size_t cbor_serialize_indefinite_array(cbor_stream_t *s)
{
    CBOR_ENSURE_SIZE(s, 1);
    s->data[s->pos++] = CBOR_ARRAY | CBOR_VAR_FOLLOWS;
    return 1;

}

size_t cbor_deserialize_indefinite_array(const cbor_stream_t *s, size_t offset)
{
    if (s->data[offset] != (CBOR_ARRAY | CBOR_VAR_FOLLOWS)) {
        return 0;
    }

    return 1;
}

size_t cbor_serialize_indefinite_map(cbor_stream_t *s)
{
    CBOR_ENSURE_SIZE(s, 1);
    s->data[s->pos++] = CBOR_MAP | CBOR_VAR_FOLLOWS;
    return 1;
}

size_t cbor_deserialize_indefinite_map(const cbor_stream_t *s, size_t offset)
{
    if (s->data[offset] != (CBOR_MAP | CBOR_VAR_FOLLOWS)) {
        return 0;
    }

    return 1;
}

size_t cbor_write_tag(cbor_stream_t *s, uint tag)
{
    CBOR_ENSURE_SIZE(s, 1);
    s->data[s->pos++] = CBOR_TAG | tag;
    return 1;
}

bool cbor_at_tag(const cbor_stream_t *s, size_t offset)
{
    return cbor_at_end(s, offset) || CBOR_TYPE(s, offset) == CBOR_TAG;
}

size_t cbor_write_break(cbor_stream_t *s)
{
    CBOR_ENSURE_SIZE(s, 1);
    s->data[s->pos++] = CBOR_BREAK;
    return 1;
}

bool cbor_at_break(const cbor_stream_t *s, size_t offset)
{
    return cbor_at_end(s, offset) || s->data[offset] == CBOR_BREAK;
}

bool cbor_at_end(const cbor_stream_t *s, size_t offset)
{
    /* cbor_stream_t::pos points at the next *free* byte, hence the -1 */
    return s ? offset >= s->pos - 1 : true;
}


size_t cbor_deserialize_map(const cbor_stream_t *s, size_t offset, size_t *map_length)
{
    if (CBOR_TYPE(s, offset) != CBOR_MAP || !map_length) {
        return 0;
    }

    uint64_t val;
    size_t read_bytes = decode_int(s, offset, &val);
    *map_length = (size_t)val;
    return read_bytes;
}

size_t cbor_serialize_map(cbor_stream_t *s, size_t map_length)
{
    /* serialize number of item key-value pairs */
    return encode_int(CBOR_MAP, s, map_length);
}

#ifdef BOARD_NATIVE
size_t cbor_deserialize_date_time(const cbor_stream_t *stream, size_t offset, struct tm *val)
{
    if ((CBOR_TYPE(stream, offset) != CBOR_TAG) || (CBOR_ADDITIONAL_INFO(stream, offset) != CBOR_DATETIME_STRING_FOLLOWS)) {
        return 0;
    }

    char buffer[21];
    offset++;  /* skip tag byte to decode date_time */
    size_t read_bytes = cbor_deserialize_unicode_string(stream, offset, buffer, sizeof(buffer));
    const char *format = "%Y-%m-%dT%H:%M:%SZ";

    if (strptime(buffer, format, val) == 0) {
        return 0;
    }

    if (mktime(val) == -1) {
        return 0;
    }

    return read_bytes + 1;  /* + 1 tag byte */
}

size_t cbor_serialize_date_time(cbor_stream_t *stream, struct tm *val)
{
    static const int MAX_TIMESTRING_LENGTH = 21;
    CBOR_ENSURE_SIZE(stream, MAX_TIMESTRING_LENGTH + 1); /* + 1 tag byte */

    char time_str[MAX_TIMESTRING_LENGTH];
    const char *format = "%Y-%m-%dT%H:%M:%SZ";

    if (strftime(time_str, sizeof(time_str), format, val) == 0) { /* struct tm to string */
        return 0;
    }

    if (!cbor_write_tag(stream, CBOR_DATETIME_STRING_FOLLOWS)) {
        return 0;
    }

    size_t written_bytes = cbor_serialize_unicode_string(stream, time_str);
    return written_bytes + 1; /* utf8 time string length + tag length */
}

size_t cbor_deserialize_date_time_epoch(const cbor_stream_t *stream, size_t offset, time_t *val)
{
    if ((CBOR_TYPE(stream, offset) != CBOR_TAG) || (CBOR_ADDITIONAL_INFO(stream, offset) != CBOR_DATETIME_EPOCH_FOLLOWS)) {
        return 0;
    }

    offset++; /* skip tag byte */
    uint64_t epoch;
    size_t read_bytes = cbor_deserialize_uint64_t(stream, offset, &epoch);

    if (!read_bytes) {
        return 0;
    }

    *val = (time_t)epoch;
    return read_bytes + 1; /* + 1 tag byte */
}

size_t cbor_serialize_date_time_epoch(cbor_stream_t *stream, time_t val)
{
    /* we need at least 2 bytes (tag byte + at least 1 byte for the integer) */
    CBOR_ENSURE_SIZE(stream, 2);

    if (val < 0) {
        return 0; /* we currently don't support negative values for the time_t object */
    }

    if (!cbor_write_tag(stream, CBOR_DATETIME_EPOCH_FOLLOWS)) {
        return 0;
    }


    uint64_t time = (uint64_t)val;
    size_t written_bytes = encode_int(CBOR_UINT, stream, time);
    return written_bytes + 1; /* + 1 tag byte */
}
#endif

/* BEGIN: Printers */
void cbor_stream_print(cbor_stream_t *stream)
{
    dump_memory(stream->data, stream->pos);
}

/**
 * Decode CBOR data item from @p stream at position @p offset
 *
 * @return Amount of bytes consumed
 */
size_t cbor_stream_decode_at(cbor_stream_t *stream, size_t offset, int indent)
{
#define DESERIALIZE_AND_PRINT(type, suffix, format_string) { \
        type val; \
        size_t read_bytes = cbor_deserialize_##suffix(stream, offset, &val); \
        printf("("#type", "format_string")\n", val); \
        return read_bytes; \
    }

    printf("%*s", indent, "");

    switch (CBOR_TYPE(stream, offset)) {
        case CBOR_UINT:
            DESERIALIZE_AND_PRINT(int, int, "%d")
        case CBOR_BYTES: {
            char buffer[CBOR_STREAM_PRINT_BUFFERSIZE];
            size_t read_bytes = cbor_deserialize_byte_string(stream, offset, buffer, sizeof(buffer));
            printf("(byte string, \"%s\")\n", buffer);
            return read_bytes;
        }

        case CBOR_TEXT: {
            char buffer[CBOR_STREAM_PRINT_BUFFERSIZE];
            size_t read_bytes = cbor_deserialize_unicode_string(stream, offset, buffer, sizeof(buffer));
            printf("(unicode string, \"%s\")\n", buffer);
            return read_bytes;
        }

        case CBOR_ARRAY: {
            const bool is_indefinite = (stream->data[offset] == (CBOR_ARRAY | CBOR_VAR_FOLLOWS));
            uint64_t array_length;
            size_t read_bytes;

            if (is_indefinite) {
                offset += read_bytes = cbor_deserialize_indefinite_array(stream, offset);
                printf("(array, length: [indefinite])\n");
            }
            else {
                offset += read_bytes = decode_int(stream, offset, &array_length);
                printf("(array, length: %"PRIu64")\n", array_length);
            }

            size_t i = 0;

            while (is_indefinite ? !cbor_at_break(stream, offset) : i < array_length) {
                size_t inner_read_bytes;
                offset += inner_read_bytes = cbor_stream_decode_at(stream, offset, indent + 2);

                if (inner_read_bytes == 0) {
                    printf("Failed to read array item at position %d", i);
                    break;
                }

                read_bytes += inner_read_bytes;
                ++i;
            }

            read_bytes += cbor_at_break(stream, offset);
            return read_bytes;
        }

        case CBOR_MAP: {
            const bool is_indefinite = (stream->data[offset] == (CBOR_MAP | CBOR_VAR_FOLLOWS));
            uint64_t map_length;
            size_t read_bytes;

            if (is_indefinite) {
                offset += read_bytes = cbor_deserialize_indefinite_map(stream, offset);
                printf("(map, length: [indefinite])\n");
            }
            else {
                offset += read_bytes = decode_int(stream, offset, &map_length);
                printf("(map, length: %"PRIu64")\n", map_length);
            }

            size_t i = 0;

            while (is_indefinite ? !cbor_at_break(stream, offset) : i < map_length) {
                size_t key_read_bytes, value_read_bytes;
                offset += key_read_bytes = cbor_stream_decode_at(stream, offset, indent + 1); /* key */
                offset += value_read_bytes = cbor_stream_decode_at(stream, offset, indent + 2); /* value */

                if (key_read_bytes == 0 || value_read_bytes == 0) {
                    printf("Failed to read key-value pair at position %d", i);
                    break;
                }

                read_bytes += key_read_bytes + value_read_bytes;
                ++i;
            }

            read_bytes += cbor_at_break(stream, offset);
            return read_bytes;
        }

        case CBOR_TAG: {
            uint tag = CBOR_ADDITIONAL_INFO(stream, offset);
            printf("(tag: %u, ", tag);

            switch (tag) {
                    // Non-native builds likely don't have support for ctime (hence disable it there)
                    // TODO: Better check for availability of ctime functions?
#ifdef BOARD_NATIVE
                case CBOR_DATETIME_STRING_FOLLOWS: {
                    char buf[64];
                    struct tm timeinfo;
                    size_t read_bytes = cbor_deserialize_date_time(stream, offset, &timeinfo);
                    strftime(buf, sizeof(buf), "%c", &timeinfo);
                    printf("date/time string: \"%s\")\n", buf);
                    return read_bytes;
                }

                case CBOR_DATETIME_EPOCH_FOLLOWS: {
                    time_t time;
                    size_t read_bytes = cbor_deserialize_date_time_epoch(stream, offset, &time);
                    printf("date/time epoch: %d)\n", (int)time);
                    return read_bytes;
                }

#endif

                default:
                    printf("unknown content)\n");
                    return 1;
            }
        }

        case CBOR_7: {
            switch (stream->data[offset]) {
                case CBOR_FALSE:
                case CBOR_TRUE:
                    DESERIALIZE_AND_PRINT(bool, bool, "%d")
                case CBOR_FLOAT16:
                    DESERIALIZE_AND_PRINT(float, float_half, "%f")
                case CBOR_FLOAT32:
                    DESERIALIZE_AND_PRINT(float, float, "%f")
                case CBOR_FLOAT64:
                    DESERIALIZE_AND_PRINT(double, double, "%f")
                default:
                    break;
            }
        }
    }

    return 0;

#undef DESERIALIZE_AND_PRINT
}

void cbor_stream_decode(cbor_stream_t *stream)
{
    printf("Data:\n");
    size_t offset = 0;

    while (offset < stream->pos) {
        size_t read_bytes = cbor_stream_decode_at(stream, offset, 0);

        if (read_bytes == 0) {
            printf("Failed to read from stream at offset %d, start byte 0x%02X\n", offset, stream->data[offset]);
            cbor_stream_print(stream);
            return;
        }

        offset += read_bytes;
    }

    printf("\n");
}

/* END: Printers */
