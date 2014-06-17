/*
 * Copyright (C) 2014 Freie Universität Berlin
 *
 * This file is subject to the terms and conditions of the GNU Lesser General
 * Public License. See the file LICENSE in the top level directory for more
 * details.
 */

/**
 * @author      Kevin Funk <kevin.funk@fu-berlin.de>
 */

#include "../unittests.h"

#include "cbor.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <stdlib.h>

#define CBOR_SERIALIZE(stream, cbor_serialize_function, input) do { \
    cbor_clear(&stream); \
    cbor_serialize_function(&stream, input); \
} while(0)

#define CBOR_CHECK_SERIALIZED(test_id, stream, expected_value_container, size) do { \
    if (memcmp(stream.data, expected_value_container, size) != 0) { \
        printf("  CBOR encoded data: "); cbor_stream_print(&stream); printf("\n"); \
        cbor_stream_t tmp = {expected_value_container, size, size}; \
        printf("  Expected data    : "); cbor_stream_print(&tmp); printf("\n"); \
        TEST_FAIL("Test failed: " test_id); \
    } \
} while(0)

#define CBOR_DESERIALIZE(type, cbor_deserialize_function, stream, destination) do { \
    if (!cbor_deserialize_function(stream, 0, destination)) { \
        TEST_FAIL("Test failed: " #cbor_deserialize_function); \
    } \
} while(0)

#define CBOR_CHECK_DESERIALIZED(test_id, expected_value, actual_value, comparator_function) do { \
    if (!comparator_function(expected_value, actual_value)) { \
        TEST_FAIL("Test failed: " test_id); \
    } \
} while(0)

#define CONCAT(...) __VA_ARGS__

/// Macro for checking PODs (int, float, ...)
#define CBOR_CHECK(type, function_suffix, stream, input, expected_value, comparator) do { \
    type buffer; \
    unsigned char data[] = expected_value; \
    CBOR_SERIALIZE(stream, cbor_serialize_##function_suffix, input); \
    CBOR_CHECK_SERIALIZED("cbor_serialize_" #function_suffix, stream, data, sizeof(data)); \
    cbor_stream_t tmp = {data, sizeof(data), sizeof(data)}; \
    CBOR_DESERIALIZE(type, cbor_deserialize_##function_suffix, &tmp, &buffer); \
    CBOR_CHECK_DESERIALIZED("cbor_serialize_" #function_suffix, input, buffer, comparator); \
} while(0)

#define HEX_LITERAL(...) {__VA_ARGS__}

// BEGIN: Comparator functions
#define EQUAL_INT(a, b) \
    (a == b)
#define EQUAL_FLOAT(a, b) ( \
    (isinf(a) && isinf(b)) || \
    (isnan(a) && isnan(b)) || \
    (fabs(a - b) < 0.00001))
#define EQUAL_STRING(a, b) \
    (strcmp(a, b) == 0)
// END: Comparator functions

unsigned char data[1024];
cbor_stream_t stream = {data, sizeof(data), 0};

static void setUp(void)
{
    cbor_clear(&stream);
}

static void tearDown(void)
{
}

static void test_major_type_0(void)
{
    {
        CBOR_CHECK(int, int, stream, 0,  HEX_LITERAL(0x00), EQUAL_INT);
        CBOR_CHECK(int, int, stream, 23, HEX_LITERAL(0x17), EQUAL_INT);

        CBOR_CHECK(int, int, stream, 24,   HEX_LITERAL(0x18, 0x18), EQUAL_INT);
        CBOR_CHECK(int, int, stream, 0xff, HEX_LITERAL(0x18, 0xff), EQUAL_INT);

        CBOR_CHECK(int, int, stream, 0xff+1, HEX_LITERAL(0x19, 0x01, 0x00), EQUAL_INT);
        CBOR_CHECK(int, int, stream, 0xffff, HEX_LITERAL(0x19, 0xff, 0xff), EQUAL_INT);

        CBOR_CHECK(int, int, stream, 0xffff+1,   HEX_LITERAL(0x1a, 0x00, 0x01, 0x00, 0x00), EQUAL_INT);
        CBOR_CHECK(int, int, stream, 0x7fffffff, HEX_LITERAL(0x1a, 0x7f, 0xff, 0xff, 0xff), EQUAL_INT);
    }

    {
        CBOR_CHECK(uint64_t, uint64_t, stream, 0x0,                   HEX_LITERAL(0x00), EQUAL_INT);
        CBOR_CHECK(uint64_t, uint64_t, stream, 0xff,                  HEX_LITERAL(0x18, 0xff), EQUAL_INT);
        CBOR_CHECK(uint64_t, uint64_t, stream, 0xffff,                HEX_LITERAL(0x19, 0xff, 0xff), EQUAL_INT);

        CBOR_CHECK(uint64_t, uint64_t, stream, 0xffffffffull,         HEX_LITERAL(0x1a, 0xff, 0xff, 0xff, 0xff), EQUAL_INT);
        CBOR_CHECK(uint64_t, uint64_t, stream, 0xffffffffffffffffull, HEX_LITERAL(0x1b, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff), EQUAL_INT);
    }
}

static void test_major_type_0_invalid(void)
{
    {
        // check writing to stream that is not large enough
        // basically tests internal 'encode_int' function

        cbor_stream_t stream;
        cbor_init(&stream, 0);

        // check each possible branch in 'encode_int'
        // (value in first byte, uint8 follows, uint16 follows, uint64 follows)
        {
            const size_t written_bytes = cbor_serialize_int(&stream, 0);
            TEST_ASSERT_EQUAL_INT(0, written_bytes);
            TEST_ASSERT_EQUAL_INT(0, stream.pos);
        }
        {
            const size_t written_bytes = cbor_serialize_int(&stream, 24);
            TEST_ASSERT_EQUAL_INT(0, written_bytes);
            TEST_ASSERT_EQUAL_INT(0, stream.pos);
        }
        {
            const size_t written_bytes = cbor_serialize_int(&stream, 0xff+1);
            TEST_ASSERT_EQUAL_INT(0, written_bytes);
            TEST_ASSERT_EQUAL_INT(0, stream.pos);
        }
        {
            const size_t written_bytes = cbor_serialize_int(&stream, 0xffff+1);
            TEST_ASSERT_EQUAL_INT(0, written_bytes);
            TEST_ASSERT_EQUAL_INT(0, stream.pos);
        }

        // let's do this for 'cbor_serialize_int64_t', too
        // this uses 'encode_int' internally, as well, so let's just test if the
        // 'cbor_serialize_int64_t' wrapper is sane
        {
            const size_t written_bytes = cbor_serialize_uint64_t(&stream, 0);
            TEST_ASSERT_EQUAL_INT(0, written_bytes);
            TEST_ASSERT_EQUAL_INT(0, stream.pos);
        }

        cbor_destroy(&stream);
    }

    {
        // check reading from stream that contains other type of data

        unsigned char data[] = {0x40}; // empty string encoded in CBOR
        cbor_stream_t stream = {data, 1, 1};

        {
            int val = 0;
            const size_t read_bytes = cbor_deserialize_int(&stream, 0, &val);
            TEST_ASSERT_EQUAL_INT(0, read_bytes);
        }
        {
            uint64_t val = 0;
            const size_t read_bytes = cbor_deserialize_uint64_t(&stream, 0, &val);
            TEST_ASSERT_EQUAL_INT(0, read_bytes);
        }
    }
}

static void test_major_type_1(void)
{
    {
        CBOR_CHECK(int, int, stream, -1,  HEX_LITERAL(0x20), EQUAL_INT);
        CBOR_CHECK(int, int, stream, -24, HEX_LITERAL(0x37), EQUAL_INT);

        CBOR_CHECK(int, int, stream, -25,     HEX_LITERAL(0x38, 0x18), EQUAL_INT);
        CBOR_CHECK(int, int, stream, -0xff-1, HEX_LITERAL(0x38, 0xff), EQUAL_INT);

        CBOR_CHECK(int, int, stream, -0xff-2,   HEX_LITERAL(0x39, 0x01, 0x00), EQUAL_INT);
        CBOR_CHECK(int, int, stream, -0xffff-1, HEX_LITERAL(0x39, 0xff, 0xff), EQUAL_INT);

        CBOR_CHECK(int, int, stream, -0xffff-2,     HEX_LITERAL(0x3a, 0x00, 0x01, 0x00, 0x00), EQUAL_INT);
        CBOR_CHECK(int, int, stream, -0x7fffffff-1, HEX_LITERAL(0x3a, 0x7f, 0xff, 0xff, 0xff), EQUAL_INT);
    }

    {
        CBOR_CHECK(int64_t, int64_t, stream, -1,                      HEX_LITERAL(0x20), EQUAL_INT);
        CBOR_CHECK(int64_t, int64_t, stream, -0xff-1,                 HEX_LITERAL(0x38, 0xff), EQUAL_INT);
        CBOR_CHECK(int64_t, int64_t, stream, -0xffff-1,               HEX_LITERAL(0x39, 0xff, 0xff), EQUAL_INT);
        CBOR_CHECK(int64_t, int64_t, stream, -0xffffffffll-1,         HEX_LITERAL(0x3a, 0xff, 0xff, 0xff, 0xff), EQUAL_INT);
        CBOR_CHECK(int64_t, int64_t, stream, -0x7fffffffffffffffll-1, HEX_LITERAL(0x3b, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff), EQUAL_INT);
    }
}

static void test_major_type_1_invalid(void)
{
    {
        // check writing to stream that is not large enough (also see test_major_type_0_invalid)

        cbor_stream_t stream;
        cbor_init(&stream, 0);

        {
            const size_t written_bytes = cbor_serialize_int64_t(&stream, 0);
            TEST_ASSERT_EQUAL_INT(0, written_bytes);
            TEST_ASSERT_EQUAL_INT(0, stream.pos);
        }

        cbor_destroy(&stream);
    }

    {
        // check reading from stream that contains other type of data

        unsigned char data[] = {0x40}; // empty string encoded in CBOR
        cbor_stream_t stream = {data, 1, 1};

        {
            int64_t val = 0;
            const size_t read_bytes = cbor_deserialize_int64_t(&stream, 0, &val);
            TEST_ASSERT_EQUAL_INT(0, read_bytes);
        }
    }
}

static void test_major_type_2(void)
{
    char buffer[128];

    {
        const char* input = "";
        unsigned char data[] = {0x40};
        CBOR_SERIALIZE(stream, cbor_serialize_byte_string, input);
        CBOR_CHECK_SERIALIZED("cbor_serialize_byte_string", stream, data, sizeof(data));
        if (!cbor_deserialize_byte_string(&stream, 0, buffer, sizeof(buffer))) {
            TEST_FAIL("Test failed: cbor_deserialize_byte_string");
        }
        CBOR_CHECK_DESERIALIZED("cbor_serialize_byte_string", input, buffer, EQUAL_STRING);
    }

    {
        const char* input = "a";
        unsigned char data[] = {0x41, 0x61};
        CBOR_SERIALIZE(stream, cbor_serialize_byte_string, input);
        CBOR_CHECK_SERIALIZED("cbor_serialize_byte_string", stream, data, sizeof(data));
        if (!cbor_deserialize_byte_string(&stream, 0, buffer, sizeof(buffer))) {
            TEST_FAIL("Test failed: cbor_deserialize_byte_string");
        }
        CBOR_CHECK_DESERIALIZED("cbor_serialize_byte_string", input, buffer, EQUAL_STRING);
    }
}

static void test_major_type_2_invalid(void)
{
    {
        // check writing to stream that is not large enough

        cbor_stream_t stream;
        cbor_init(&stream, 0);

        {
            const size_t written_bytes = cbor_serialize_byte_string(&stream, "foo");
            TEST_ASSERT_EQUAL_INT(0, written_bytes);
        }

        cbor_destroy(&stream);
    }
}


static void test_major_type_3(void)
{
    char buffer[128];

    {
        const char* input = "";
        unsigned char data[] = {0x60};
        CBOR_SERIALIZE(stream, cbor_serialize_unicode_string, input);
        CBOR_CHECK_SERIALIZED("cbor_serialize_unicode_string", stream, data, sizeof(data));
        if (!cbor_deserialize_unicode_string(&stream, 0, buffer, sizeof(buffer))) {
            TEST_FAIL("Test failed: cbor_deserialize_unicode_string");
        }
        CBOR_CHECK_DESERIALIZED("cbor_serialize_unicode_string", input, buffer, EQUAL_STRING);
    }

    {
        const char* input = "a";
        unsigned char data[] = {0x61, 0x61};
        CBOR_SERIALIZE(stream, cbor_serialize_unicode_string, input);
        CBOR_CHECK_SERIALIZED("cbor_serialize_unicode_string", stream, data, sizeof(data));
        if (!cbor_deserialize_unicode_string(&stream, 0, buffer, sizeof(buffer))) {
            TEST_FAIL("Test failed: cbor_deserialize_unicode_string");
        }
        CBOR_CHECK_DESERIALIZED("cbor_serialize_unicode_string", input, buffer, EQUAL_STRING);
    }
}

static void test_major_type_3_invalid(void)
{
    {
        // check writing to stream that is not large enough

        cbor_stream_t stream;
        cbor_init(&stream, 0);

        {
            const size_t written_bytes = cbor_serialize_unicode_string(&stream, "foo");
            TEST_ASSERT_EQUAL_INT(0, written_bytes);
        }

        cbor_destroy(&stream);
    }
}

static void test_major_type_4(void)
{
    // uniform types
    {
        cbor_clear(&stream);

        // serialization
        TEST_ASSERT(cbor_serialize_array(&stream, 2));
        TEST_ASSERT(cbor_serialize_int(&stream, 1));
        TEST_ASSERT(cbor_serialize_int(&stream, 2));
        unsigned char data[] = {0x82, 0x01, 0x02};
        CBOR_CHECK_SERIALIZED("serialize_check_uniform_types", stream, data, sizeof(data));

        // deserialization
        uint64_t array_length;
        size_t offset = cbor_deserialize_array(&stream, 0, &array_length);
        TEST_ASSERT_EQUAL_INT(2, array_length);
        int i;
        offset += cbor_deserialize_int(&stream, offset, &i);
        TEST_ASSERT_EQUAL_INT(1, i);
        offset += cbor_deserialize_int(&stream, offset, &i);
        TEST_ASSERT_EQUAL_INT(2, i);
    }

    // mixed types
    {
        cbor_clear(&stream);
        TEST_ASSERT(cbor_serialize_array(&stream, 2));
        TEST_ASSERT(cbor_serialize_int(&stream, 1));
        TEST_ASSERT(cbor_serialize_byte_string(&stream, "a"));
        unsigned char data[] = {0x82, 0x01, 0x41, 0x61};
        CBOR_CHECK_SERIALIZED("serialize_check_mixed_types", stream, data, sizeof(data));

        // deserialization
        uint64_t array_length;
        size_t offset = cbor_deserialize_array(&stream, 0, &array_length);
        TEST_ASSERT_EQUAL_INT(2, array_length);
        int i;
        offset += cbor_deserialize_int(&stream, offset, &i);
        TEST_ASSERT_EQUAL_INT(1, i);
        char buffer[1024];
        offset += cbor_deserialize_byte_string(&stream, offset, buffer, sizeof(buffer));
        TEST_ASSERT_EQUAL_STRING("a", buffer);
    }
}

static void test_major_type_7(void)
{
    {
        // simple values
        CBOR_CHECK(bool, bool, stream, false, HEX_LITERAL(0xf4), EQUAL_INT);
        CBOR_CHECK(bool, bool, stream, true,  HEX_LITERAL(0xf5), EQUAL_INT);
    }

    {
        // check border conditions
        CBOR_CHECK(float, float, stream, .0f, HEX_LITERAL(0xfa, 0x00, 0x00, 0x00, 0x00), EQUAL_FLOAT);
        CBOR_CHECK(float, float, stream, INFINITY, HEX_LITERAL(0xfa, 0x7f, 0x80, 0x00, 0x00), EQUAL_FLOAT);
        CBOR_CHECK(float, float, stream, NAN, HEX_LITERAL(0xfa, 0x7f, 0xc0, 0x00, 0x00), EQUAL_FLOAT);
        CBOR_CHECK(float, float, stream, -INFINITY, HEX_LITERAL(0xfa, 0xff, 0x80, 0x00, 0x00), EQUAL_FLOAT);

        // check examples from the CBOR RFC
        CBOR_CHECK(float, float, stream, 100000.f, HEX_LITERAL(0xfa, 0x47, 0xc3, 0x50, 0x00), EQUAL_FLOAT);
        CBOR_CHECK(float, float, stream, 3.4028234663852886e+38, HEX_LITERAL(0xfa, 0x7f, 0x7f, 0xff, 0xff), EQUAL_FLOAT);
    }

    {
        // check border conditions
        CBOR_CHECK(double, double, stream, .0f, HEX_LITERAL(0xfb, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00), EQUAL_FLOAT);
        CBOR_CHECK(double, double, stream, INFINITY, HEX_LITERAL(0xfb, 0x7f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00), EQUAL_FLOAT);
        CBOR_CHECK(double, double, stream, NAN, HEX_LITERAL(0xfb, 0x7f, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00), EQUAL_FLOAT);
        CBOR_CHECK(double, double, stream, -INFINITY, HEX_LITERAL(0xfb, 0xff, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00), EQUAL_FLOAT);

        // check examples from the CBOR RFC
        CBOR_CHECK(double, double, stream, 1.1, HEX_LITERAL(0xfb, 0x3f, 0xf1, 0x99, 0x99, 0x99, 0x99, 0x99, 0x9a), EQUAL_FLOAT);
        CBOR_CHECK(double, double, stream, 1.e+300, HEX_LITERAL(0xfb, 0x7e, 0x37, 0xe4, 0x3c, 0x88, 0x00, 0x75, 0x9c), EQUAL_FLOAT);
        CBOR_CHECK(double, double, stream, -4.1, HEX_LITERAL(0xfb, 0xc0, 0x10, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66), EQUAL_FLOAT);
    }
}

static void test_major_type_7_invalid(void)
{
    {
        // check writing to stream that is not large enough

        cbor_stream_t stream;
        cbor_init(&stream, 0);

        {
            const size_t written_bytes = cbor_serialize_bool(&stream, true);
            TEST_ASSERT_EQUAL_INT(0, written_bytes);
            TEST_ASSERT_EQUAL_INT(0, stream.pos);
        }
        {
            const size_t written_bytes = cbor_serialize_float(&stream, 0.f);
            TEST_ASSERT_EQUAL_INT(0, written_bytes);
            TEST_ASSERT_EQUAL_INT(0, stream.pos);
        }
        {
            const size_t written_bytes = cbor_serialize_double(&stream, 0);
            TEST_ASSERT_EQUAL_INT(0, written_bytes);
            TEST_ASSERT_EQUAL_INT(0, stream.pos);
        }

        cbor_destroy(&stream);
    }

    {
        // check reading from stream that contains other type of data

        unsigned char data[] = {0x40}; // empty string encoded in CBOR
        cbor_stream_t stream = {data, 1, 1};

        {
            float val = 0;
            const size_t read_bytes = cbor_deserialize_float(&stream, 0, &val);
            TEST_ASSERT_EQUAL_INT(0, read_bytes);
        }
        {
            double val = 0;
            const size_t read_bytes = cbor_deserialize_double(&stream, 0, &val);
            TEST_ASSERT_EQUAL_INT(0, read_bytes);
        }
    }
}

/**
 * See examples from CBOR RFC (cf. Appendix A. Examples)
 */
TestRef tests_cbor_all(void)
{
    EMB_UNIT_TESTFIXTURES(fixtures) {
        new_TestFixture(test_major_type_0),
        new_TestFixture(test_major_type_0_invalid),
        new_TestFixture(test_major_type_1),
        new_TestFixture(test_major_type_1_invalid),
        new_TestFixture(test_major_type_2),
        new_TestFixture(test_major_type_2_invalid),
        new_TestFixture(test_major_type_3),
        new_TestFixture(test_major_type_3_invalid),
        new_TestFixture(test_major_type_4),
        new_TestFixture(test_major_type_7),
        new_TestFixture(test_major_type_7_invalid)
    };

    EMB_UNIT_TESTCALLER(CborTest, setUp, tearDown, fixtures);
    return (TestRef)&CborTest;
}

static void manual_test(void)
{
    // working cbor_(de)serialize_byte_string test:
    cbor_stream_t stream;
    cbor_init(&stream, 1024);
    cbor_serialize_byte_string(&stream, "abc");
    cbor_stream_print(&stream);
    char res[1024];
    cbor_deserialize_byte_string(&stream, (size_t)0, res, sizeof(res));
    printf("\ndeserialized string: %s\n", res);
    cbor_destroy(&stream);


}

void tests_cbor(void)
{
    (void)manual_test; // fix unused warning
    //manual_test();

    TESTS_RUN(tests_cbor_all());
}
