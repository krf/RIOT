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
 * @ingroup     cbor
 * @{
 */

/**
 * @file
 * @brief       Implementation of a CBOR serializer/deserializer in C
 *
 * @author      Freie Universität Berlin, Computer Systems & Telematics
 * @author      Kevin Funk <kevin.funk@fu-berlin.de>
 * @author      Jana Cavojska <jana.cavojska9@gmail.com>
 */

#ifndef CBOR_H
#define CBOR_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct cbor_stream_t
{
    /// Array containing CBOR encoded data
    unsigned char* data;
    /// Size of the array
    size_t size;
    /// Index point to the next free byte
    size_t pos;
} cbor_stream_t;

void cbor_init(cbor_stream_t* stream, size_t size);
void cbor_clear(cbor_stream_t* stream);
void cbor_destroy(cbor_stream_t* stream);

/// Print @p stream in CBOR representation
void cbor_stream_print(cbor_stream_t* stream);
/// Decode CBOR from @p stream
void cbor_stream_decode(cbor_stream_t* stream);

size_t cbor_deserialize_int(cbor_stream_t* stream, size_t offset, int* val);
void cbor_serialize_int(cbor_stream_t* s, int val);
size_t cbor_deserialize_uint64_t(cbor_stream_t* stream, size_t offset, uint64_t* val);
void cbor_serialize_uint64_t(cbor_stream_t* s, uint64_t val);
size_t cbor_deserialize_int64_t(cbor_stream_t* stream, size_t offset, int64_t* val);
void cbor_serialize_int64_t(cbor_stream_t* s, int64_t val);
size_t cbor_deserialize_bool(cbor_stream_t* stream, size_t offset, bool* val);
void cbor_serialize_bool(cbor_stream_t* s, bool val);
size_t cbor_deserialize_float(cbor_stream_t* stream, size_t offset, float* val);
void cbor_serialize_float(cbor_stream_t* s, float val);
size_t cbor_deserialize_byte_string(cbor_stream_t* stream, size_t offset, char** val);
void cbor_serialize_byte_string(cbor_stream_t* s, const char* val);
size_t cbor_deserialize_unicode_string(cbor_stream_t* stream, size_t offset, wchar_t** val);
void cbor_serialize_unicode_string(cbor_stream_t* s, const wchar_t* val);

size_t cbor_deserialize_array(cbor_stream_t* stream, size_t offset, wchar_t** val);
void cbor_serialize_array(cbor_stream_t* s, const wchar_t* val);

#endif

/** @} */
