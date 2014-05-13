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

#include "cbor.h"

#include <embUnit/embUnit.h>
#include <textui/TextUIRunner.h>
#include <textui/TextOutputter.h>

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define CBOR_CHECK_SERIALIZE(stream, cbor_serialize_function, input, expected_value) do { \
    cbor_clear(&stream); \
    cbor_serialize_function(&stream, input); \
    unsigned char expected_value_container[] = expected_value; \
    const size_t size = sizeof(expected_value_container); \
    if (memcmp(stream.data, expected_value_container, size) != 0) { \
        printf("  CBOR encoded data: "); cbor_stream_print(&stream); printf("\n"); \
        cbor_stream_t tmp = {expected_value_container, size, size}; \
        printf("  Expected data    : "); cbor_stream_print(&tmp); printf("\n"); \
        TEST_FAIL("Test failed: " #cbor_serialize_function); \
    } \
} while(0)

#define CBOR_CHECK_DESERIALIZE(type, cbor_deserialize_function, input, expected_value, comparator) do { \
    type value; \
    unsigned char data[] = input; \
    cbor_stream_t tmp = {data, sizeof(data), sizeof(data)}; \
    cbor_deserialize_function(&tmp, 0, &value); \
    if (!comparator(expected_value, value)) { \
        TEST_FAIL("Test failed: " #cbor_deserialize_function); \
    } \
} while(0)

#define CONCAT(...) __VA_ARGS__

#define CBOR_CHECK(type, function_suffix, stream, input, data, comparator) do { \
    CBOR_CHECK_SERIALIZE(stream, cbor_serialize_##function_suffix, input, CONCAT(data)); \
    CBOR_CHECK_DESERIALIZE(type, cbor_deserialize_##function_suffix, CONCAT(data), input, comparator); \
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

cbor_stream_t stream;

static void setUp(void)
{
    cbor_init(&stream, 128);
}

static void tearDown(void)
{
    cbor_destroy(&stream);
}

static void test_major_type_0(void)
{
    CBOR_CHECK(int, int, stream, 0,  HEX_LITERAL(0x00), EQUAL_INT);
    CBOR_CHECK(int, int, stream, 23, HEX_LITERAL(0x17), EQUAL_INT);

    CBOR_CHECK(int, int, stream, 24,   HEX_LITERAL(0x18, 0x18), EQUAL_INT);
    CBOR_CHECK(int, int, stream, 0xff, HEX_LITERAL(0x18, 0xff), EQUAL_INT);

    CBOR_CHECK(int, int, stream, 0xff+1, HEX_LITERAL(0x19, 0x01, 0x00), EQUAL_INT);
    CBOR_CHECK(int, int, stream, 0xffff, HEX_LITERAL(0x19, 0xff, 0xff), EQUAL_INT);

    CBOR_CHECK(int, int, stream, 0xffff+1,   HEX_LITERAL(0x1a, 0x00, 0x01, 0x00, 0x00), EQUAL_INT);
    CBOR_CHECK(int, int, stream, 0x7fffffff, HEX_LITERAL(0x1a, 0x7f, 0xff, 0xff, 0xff), EQUAL_INT);

    CBOR_CHECK(uint64_t, uint64_t, stream, 0x0,                   HEX_LITERAL(0x00), EQUAL_INT);
    CBOR_CHECK(uint64_t, uint64_t, stream, 0xff,                  HEX_LITERAL(0x18, 0xff), EQUAL_INT);
    CBOR_CHECK(uint64_t, uint64_t, stream, 0xffff,                HEX_LITERAL(0x19, 0xff, 0xff), EQUAL_INT);

    CBOR_CHECK(uint64_t, uint64_t, stream, 0xffffffffull,         HEX_LITERAL(0x1a, 0xff, 0xff, 0xff, 0xff), EQUAL_INT);
    CBOR_CHECK(uint64_t, uint64_t, stream, 0xffffffffffffffffull, HEX_LITERAL(0x1b, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff), EQUAL_INT);
}

static void test_major_type_1(void)
{
    CBOR_CHECK(int, int, stream, -1,  HEX_LITERAL(0x20), EQUAL_INT);
    CBOR_CHECK(int, int, stream, -24, HEX_LITERAL(0x37), EQUAL_INT);

    CBOR_CHECK(int, int, stream, -25,     HEX_LITERAL(0x38, 0x18), EQUAL_INT);
    CBOR_CHECK(int, int, stream, -0xff-1, HEX_LITERAL(0x38, 0xff), EQUAL_INT);

    CBOR_CHECK(int, int, stream, -0xff-2,   HEX_LITERAL(0x39, 0x01, 0x00), EQUAL_INT);
    CBOR_CHECK(int, int, stream, -0xffff-1, HEX_LITERAL(0x39, 0xff, 0xff), EQUAL_INT);

    CBOR_CHECK(int, int, stream, -0xffff-2,     HEX_LITERAL(0x3a, 0x00, 0x01, 0x00, 0x00), EQUAL_INT);
    CBOR_CHECK(int, int, stream, -0x7fffffff-1, HEX_LITERAL(0x3a, 0x7f, 0xff, 0xff, 0xff), EQUAL_INT);

    CBOR_CHECK(int64_t, int64_t, stream, -1,                      HEX_LITERAL(0x20), EQUAL_INT);
    CBOR_CHECK(int64_t, int64_t, stream, -0xff-1,                 HEX_LITERAL(0x38, 0xff), EQUAL_INT);
    CBOR_CHECK(int64_t, int64_t, stream, -0xffff-1,               HEX_LITERAL(0x39, 0xff, 0xff), EQUAL_INT);
    CBOR_CHECK(int64_t, int64_t, stream, -0xffffffffll-1,         HEX_LITERAL(0x3a, 0xff, 0xff, 0xff, 0xff), EQUAL_INT);
    CBOR_CHECK(int64_t, int64_t, stream, -0x7fffffffffffffffll-1, HEX_LITERAL(0x3b, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff), EQUAL_INT);
}

static void test_major_type_2(void)
{
    CBOR_CHECK(char*, byte_string, stream, "", HEX_LITERAL(0x40), EQUAL_STRING);
    CBOR_CHECK(char*, byte_string, stream, "a", HEX_LITERAL(0x41, 0x61), EQUAL_STRING);
}

static void test_major_type_7(void)
{
    // simple values
    CBOR_CHECK(bool, bool, stream, false, HEX_LITERAL(0xf4), EQUAL_INT);
    CBOR_CHECK(bool, bool, stream, true,  HEX_LITERAL(0xf5), EQUAL_INT);

    CBOR_CHECK(float, float, stream, .0f, HEX_LITERAL(0xfa, 0x00, 0x00, 0x00, 0x00), EQUAL_FLOAT);
    CBOR_CHECK(float, float, stream, INFINITY, HEX_LITERAL(0xfa, 0x7f, 0x80, 0x00, 0x00), EQUAL_FLOAT);
    CBOR_CHECK(float, float, stream, NAN, HEX_LITERAL(0xfa, 0x7f, 0xc0, 0x00, 0x00), EQUAL_FLOAT);
    CBOR_CHECK(float, float, stream, -INFINITY, HEX_LITERAL(0xfa, 0xff, 0x80, 0x00, 0x00), EQUAL_FLOAT);
}

/**
 * See examples from CBOR RFC (cf. Appendix A. Examples)
 */
TestRef CborTest_tests(void)
{
    EMB_UNIT_TESTFIXTURES(fixtures) {
        new_TestFixture(test_major_type_0),
        new_TestFixture(test_major_type_1),
        new_TestFixture(test_major_type_2),
        new_TestFixture(test_major_type_7),
    };

    EMB_UNIT_TESTCALLER(CborTest, setUp, tearDown, fixtures);
    return (TestRef)&CborTest;
}


static void manual_test(void)
{
    cbor_stream_t stream;
    cbor_init(&stream, 1024);
    //cbor_serialize_byte_string(&stream, "foo");

    char* myarray[] = {"abc", "def", "AA"};
    cbor_serialize_byte_string_array(&stream, myarray, (uint64_t) 3);
    cbor_stream_print(&stream);
    printf("\n");
}


int main()
{
    manual_test();

    TextUIRunner_setOutputter(TextOutputter_outputter());
    TextUIRunner_start();
    TextUIRunner_runTest(CborTest_tests());
    TextUIRunner_end();

    return 0;
}
