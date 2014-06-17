/**
 * Implementation of CBOR
 *
 * Copyright (C) 2014, Freie Universitaet Berlin (FUB).
 *
 * This file is subject to the terms and conditions of the GNU Lesser General
 * Public License. See the file LICENSE in the top level directory for more
 * details.
 */

/**
 * @file
 * @brief       Implementation of a CBOR serializer/deserializer in C
 *
 * @author      Freie Universität Berlin, Computer Systems & Telematics
 * @author      Kevin Funk <kevin.funk@fu-berlin.de>
 * @author      Jana Cavojska <jana.cavojska9@gmail.com>
 */

#include "cbor.h"

#include <arpa/inet.h>
#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CBOR_TYPE_MASK          0xE0    // top 3 bits
#define CBOR_INFO_MASK          0x1F    // low 5 bits

// Jump Table for Initial Byte (cf. table 5)
#define CBOR_UINT       0x00            // type 0
#define CBOR_NEGINT     0x20            // type 1
#define CBOR_BYTES      0x40            // type 2
#define CBOR_TEXT       0x60            // type 3
#define CBOR_ARRAY      0x80            // type 4
#define CBOR_MAP        0xA0            // type 5
#define CBOR_TAG        0xC0            // type 6
#define CBOR_7          0xE0            // type 7 (float and other types)

// Major types (cf. section 2.1)
// Major type 0: Unsigned integers
#define CBOR_UINT8_FOLLOWS      24      // 0x18
#define CBOR_UINT16_FOLLOWS     25      // 0x19
#define CBOR_UINT32_FOLLOWS     26      // 0x1a
#define CBOR_UINT64_FOLLOWS     27      // 0x1b

// Indefinite Lengths for Some Major types (cf. section 2.2)
#define CBOR_VAR_FOLLOWS        31      // 0x1f

// Major type 7: Float and other types
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

/// Ensure that @p stream is big enough to fit @p bytes bytes, otherwise return 0
#define CBOR_ENSURE_SIZE(stream, bytes) do { \
    if (stream->pos + bytes >= stream->size) { return 0; } \
} while(0)

// Extra defines not related to the protocol itself
#define CBOR_STREAM_PRINT_BUFFERSIZE 1024 // bytes

/**
 * Convert float @p x to network format
 */
static float htonf(float x)
{
    union u { float f; uint32_t i; } u = { .f = x };
    u.i = htonl(u.i);
    return u.f;
}

/**
 * Convert float @p x to host format
 */
static float ntohf(float x)
{
    union u { float f; uint32_t i; } u = { .f = x };
    u.i = ntohl(u.i);
    return u.f;
}

/**
 * Convert long long @p x to network format
 */
static uint64_t htonll(uint64_t x)
{
    return (((uint64_t)htonl(x)) << 32) + htonl(x >> 32);
}

/**
 * Convert long long @p x to host format
 */
static uint64_t ntohll(uint64_t x)
{
    return (((uint64_t)ntohl(x)) << 32) + ntohl(x >> 32);
}

/**
 * Convert double @p x to network format
 */
static double htond(double x)
{
   double res;
   uint64_t* in = (uint64_t*)&x;
   uint64_t* out = (uint64_t*)&res;

   out[0] = htonll(in[0]);
   return res;
}

/**
 * Convert double @p x to host format
 */
static double ntohd(double x)
{
   double res;
   uint64_t* in = (uint64_t*)&x;
   uint64_t* out = (uint64_t*)&res;

   out[0] = ntohll(in[0]);
   return res;
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
    if (exp == 0)
        val = ldexp(mant, -24);
    else if (exp != 31)
        val = ldexp(mant + 1024, exp - 25);
    else
        val = mant == 0 ? INFINITY : NAN;
    return half & 0x8000 ? -val : val;
}

/**
 * Source: According to http://gamedev.stackexchange.com/questions/17326/conversion-of-a-number-from-single-precision-floating-point-representation-to-a
 */
static uint16_t encode_float_half(float x)
{
    union u { float f; uint32_t i; } u = { .f = x };

    uint16_t bits = (u.i >> 16) & 0x8000; /* Get the sign */
    uint16_t m = (u.i >> 12) & 0x07ff; /* Keep one extra bit for rounding */
    unsigned int e = (u.i >> 23) & 0xff; /* Using int is faster here */

    /* If zero, or denormal, or exponent underflows too much for a denormal
     * half, return signed zero. */
    if (e < 103)
        return bits;

    /* If NaN, return NaN. If Inf or exponent overflow, return Inf. */
    if (e > 142)
    {
        bits |= 0x7c00u;
        /* If exponent was 0xff and one mantissa bit was set, it means NaN,
         * not Inf, so make sure we set one mantissa bit too. */
        bits |= e == 255 && (u.i & 0x007fffffu);
        return bits;
    }

    /* If exponent underflows but not too much, return a denormal */
    if (e < 113)
    {
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
void dump_memory(unsigned char* data, size_t size)
{
    if (!data || !size)
        return;

    printf("0x");
    for (size_t i = 0; i < size; ++i) {
        printf("%02X", data[i]);
    }
}

void cbor_init(cbor_stream_t* stream, size_t size)
{
    if (!stream)
        return;

    stream->data = calloc(size, sizeof(stream));
    stream->size = size;
    stream->pos = 0;
}

void cbor_clear(cbor_stream_t* stream)
{
    if (!stream)
        return;

    stream->pos = 0;
}

void cbor_destroy(cbor_stream_t* stream)
{
    if (!stream)
        return;

    free(stream->data);
    stream->data = 0;
    stream->size = 0;
    stream->pos = 0;
}

static size_t encode_int(unsigned char major_type, cbor_stream_t* s, uint64_t val)
{
    assert(s);

    if (val <= 23) {
        CBOR_ENSURE_SIZE(s, 1);
        s->data[s->pos++] = major_type | val;
        return 1;
    } else if (val <= 0xff) {
        CBOR_ENSURE_SIZE(s, 2);
        s->data[s->pos++] = major_type | CBOR_UINT8_FOLLOWS;
        s->data[s->pos++] = val & 0xff;
        return 2;
    } else if (val <= 0xffff) {
        CBOR_ENSURE_SIZE(s, 3);
        s->data[s->pos++] = major_type | CBOR_UINT16_FOLLOWS;
        s->data[s->pos++] = (val >> 8) & 0xff;
        s->data[s->pos++] = val & 0xff;
        return 3;
    } else if (val <= 0xffffffffL) {
        CBOR_ENSURE_SIZE(s, 5);
        s->data[s->pos++] = major_type | CBOR_UINT32_FOLLOWS;
        s->data[s->pos++] = (val >> 24) & 0xff;
        s->data[s->pos++] = (val >> 16) & 0xff;
        s->data[s->pos++] = (val >>  8) & 0xff;
        s->data[s->pos++] = val & 0xff;
        return 5;
    } else {
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

static size_t decode_int(cbor_stream_t* s, size_t offset, uint64_t* val)
{
    assert(s);

    unsigned char* data = &s->data[offset];
    unsigned char additional_info = CBOR_ADDITIONAL_INFO(s, offset);

    *val = 0; // clear val first
    if (additional_info <= 23) {
        *val = (data[0] & CBOR_INFO_MASK);
        return 1;
    } else if (additional_info == CBOR_UINT8_FOLLOWS) {
        *val = data[1];
        return 2;
    } else if (additional_info == CBOR_UINT16_FOLLOWS) {
        *val =
            (data[1] << 8) |
            (data[2]);
        return 3;
    } else if (additional_info == CBOR_UINT32_FOLLOWS) {
        (*val) =
            ((uint64_t)data[1] << 24) |
            (data[2] << 16) |
            (data[3] <<  8) |
            (data[4]);
        return 5;
    } else if (additional_info == CBOR_UINT64_FOLLOWS) {
        *val =
            ((uint64_t)data[1] << 56) |
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

static size_t encode_bytes(unsigned char major_type, cbor_stream_t* s, const char* data, size_t length)
{
    size_t bytes_start = encode_int(major_type, s, (uint64_t) length);
    if (!bytes_start) {
        return 0;
    }

    CBOR_ENSURE_SIZE(s, length);

    memcpy(&(s->data[s->pos]), data, length); // copy byte string into our cbor struct
    s->pos += length;
    return bytes_start + length;
}

static size_t decode_bytes(cbor_stream_t* s, size_t offset, char* out, size_t length)
{
    assert(CBOR_TYPE(s, offset) == CBOR_BYTES || CBOR_TYPE(s, offset) == CBOR_TEXT);

    uint64_t bytes_length;
    size_t bytes_start = decode_int(s, offset, &bytes_length);
    if (!bytes_start)
        return 0;

    if (length+1 < bytes_length) {
        return 0;
    }

    memcpy(out, &s->data[offset+bytes_start], bytes_length);
    out[bytes_length] = '\0';
    return bytes_start+ bytes_length;
}

size_t cbor_deserialize_int(cbor_stream_t* stream, size_t offset, int* val)
{
    assert(val);
    if (CBOR_TYPE(stream, offset) != CBOR_UINT && CBOR_TYPE(stream, offset) != CBOR_NEGINT) {
        return 0;
    }

    uint64_t buf;
    size_t read_bytes = decode_int(stream, offset, &buf);
    if (CBOR_TYPE(stream, offset) == CBOR_UINT) {
        *val = buf; // resolve as CBOR_UINT
    } else {
        *val = -1 -buf; // resolve as CBOR_NEGINT
    }
    return read_bytes;
}

size_t cbor_serialize_int(cbor_stream_t* s, int val)
{
    if (val >= 0) {
        // Major type 0: an unsigned integer
        return encode_int(CBOR_UINT, s, val);
    } else {
        // Major type 1: an negative integer
        return encode_int(CBOR_NEGINT, s, -1 -val);
    }
}

size_t cbor_deserialize_uint64_t(cbor_stream_t* stream, size_t offset, uint64_t* val)
{
    assert(val);
    if (CBOR_TYPE(stream, offset) != CBOR_UINT) {
        return 0;
    }

    return decode_int(stream, offset, val);
}

size_t cbor_serialize_uint64_t(cbor_stream_t* s, uint64_t val)
{
    return encode_int(CBOR_UINT, s, val);
}

size_t cbor_deserialize_int64_t(cbor_stream_t* stream, size_t offset, int64_t* val)
{
    assert(val);
    if (CBOR_TYPE(stream, offset) != CBOR_UINT && CBOR_TYPE(stream, offset) != CBOR_NEGINT) {
        return 0;
    }

    uint64_t buf;
    size_t read_bytes = decode_int(stream, offset, &buf);
    if (CBOR_TYPE(stream, offset) == CBOR_UINT) {
        *val = buf; // resolve as CBOR_UINT
    } else {
        *val = -1 -buf; // resolve as CBOR_NEGINT
    }
    return read_bytes;
}

size_t cbor_serialize_int64_t(cbor_stream_t* s, int64_t val)
{
    if (val >= 0) {
        // Major type 0: an unsigned integer
        return encode_int(CBOR_UINT, s, val);
    } else {
        // Major type 1: an negative integer
        return encode_int(CBOR_NEGINT, s, -1 -val);
    }
}

size_t cbor_deserialize_bool(cbor_stream_t* stream, size_t offset, bool* val)
{
    assert(val);
    assert(CBOR_TYPE(stream, offset) == CBOR_7);
    unsigned char byte = stream->data[offset];

    *val = (byte == CBOR_TRUE);
    return 1;
}

size_t cbor_serialize_bool(cbor_stream_t* s, bool val)
{
    CBOR_ENSURE_SIZE(s, 1);
    s->data[s->pos++] = val ? CBOR_TRUE : CBOR_FALSE;
    return 1;
}

size_t cbor_deserialize_float(cbor_stream_t* stream, size_t offset, float* val)
{
    assert(val);
    if (CBOR_TYPE(stream, offset) != CBOR_7) {
        return 0;
    }

    unsigned char* data = &stream->data[offset];
    if (*data == CBOR_FLOAT16) {
        return 0; // TODO
    } else if (*data == CBOR_FLOAT32) {
        *val = ntohf(*(float*)(data+1));
        return 4;
    }

    return 0;
}

size_t cbor_serialize_float(cbor_stream_t* s, float val)
{
    CBOR_ENSURE_SIZE(s, 5);
    s->data[s->pos++] = CBOR_FLOAT32;
    (*(float*)(&s->data[s->pos])) = htonf(val);
    s->pos += 4;
    return 5;
}

size_t cbor_deserialize_double(cbor_stream_t* stream, size_t offset, double* val)
{
    assert(val);
    if (CBOR_TYPE(stream, offset) != CBOR_7) {
        return 0;
    }

    unsigned char* data = &stream->data[offset];
    if (*data == CBOR_FLOAT64) {
        *val = ntohd(*(double*)(data+1));
        return 9;
    }

    return 0;
}

size_t cbor_serialize_double(cbor_stream_t* s, double val)
{
    CBOR_ENSURE_SIZE(s, 9);
    s->data[s->pos++] = CBOR_FLOAT64;
    (*(double*)(&s->data[s->pos])) = htond(val);
    s->pos += 8;
    return 9;
}

size_t cbor_deserialize_byte_string(cbor_stream_t* stream, size_t offset, char* val, size_t length)
{
    if (CBOR_TYPE(stream, offset) != CBOR_BYTES)
        return 0;

    return decode_bytes(stream, offset, val, length);
}

size_t cbor_serialize_byte_string(cbor_stream_t* stream, const char* val)
{
    return encode_bytes(CBOR_BYTES, stream, val, strlen(val));
}

size_t cbor_deserialize_unicode_string(cbor_stream_t* stream, size_t offset, char* val, size_t length)
{
    if (CBOR_TYPE(stream, offset) != CBOR_TEXT)
        return 0;

    return decode_bytes(stream, offset, val, length);
}

size_t cbor_serialize_unicode_string(cbor_stream_t* stream, const char* val)
{
    return encode_bytes(CBOR_TEXT, stream, val, strlen(val));
}

size_t cbor_deserialize_array(cbor_stream_t* s, size_t offset, uint64_t* array_length)
{
    assert(array_length);
    assert(CBOR_TYPE(s, offset) == CBOR_ARRAY);

    size_t read_bytes = decode_int(s, offset, array_length);
    return read_bytes;
}

void cbor_serialize_array(cbor_stream_t* s, uint64_t array_length)
{
    size_t start = s->pos;
    cbor_serialize_uint64_t(s, array_length); // serialize number of array items
    s->data[start] = CBOR_ARRAY | (s->data[start] & CBOR_INFO_MASK); // fix major type
}

// BEGIN: Printers
void cbor_stream_print(cbor_stream_t* stream)
{
    dump_memory(stream->data, stream->pos);
}


/**
 * Decode CBOR data item from @p stream at position @p offset
 *
 * @return Amount of bytes consumed
 */
size_t cbor_stream_decode_at(cbor_stream_t* stream, size_t offset)
{
#define DESERIALIZE_AND_PRINT(Type, format_string) { \
        Type val; \
        size_t read_bytes = cbor_deserialize_##Type(stream, offset, &val); \
        printf("[type: "#Type", val: "format_string"]\n", val); \
        return read_bytes; \
    }

    printf("  Debug: Major type: %X\n", CBOR_TYPE(stream, offset) >> 5);
    switch (CBOR_TYPE(stream, offset)) {
    case CBOR_UINT:
        DESERIALIZE_AND_PRINT(int, "%d")
    case CBOR_BYTES: {
        char buffer[CBOR_STREAM_PRINT_BUFFERSIZE];
        size_t read_bytes = cbor_deserialize_byte_string(stream, offset, buffer, sizeof(buffer));
        printf("[type: byte string, val: \"%s\"]\n", buffer); \
        return read_bytes;
    }
    case CBOR_ARRAY: {
        uint64_t size;
        size_t read_bytes = decode_int(stream, offset, &size);

        printf("[type: CBOR array, length: %"PRIu64"]\n", size);

        // read array items
        for (uint64_t i = 0; i < size; ++i) {
            size_t inner_read_bytes = cbor_stream_decode_at(stream, read_bytes);
            if (inner_read_bytes == 0) {
                fprintf(stderr, "Failed to read array item at position %"PRIu64, i);
                break;
            }

            read_bytes += inner_read_bytes;
        }
        return read_bytes;
    }
    case CBOR_7: {
        switch (stream->data[offset]) {
        case CBOR_FALSE:
        case CBOR_TRUE:
            DESERIALIZE_AND_PRINT(bool, "%d")
        default:
            break;
        }
    }
    }
    return 0;

#undef DESERIALIZE_AND_PRINT
}

void cbor_stream_decode(cbor_stream_t* stream)
{
    printf("Data:\n");
    size_t offset = 0;
    while (offset < stream->pos) {
        size_t read_bytes = cbor_stream_decode_at(stream, offset);
        if (read_bytes == 0) {
            fprintf(stderr, "Failed to read from stream at offset %d\n", offset);
            return;
        }
        offset += read_bytes;
    }
    printf("\n");
}

// END: Printers
