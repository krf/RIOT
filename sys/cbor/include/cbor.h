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

/**
 * @brief Struct containing CBOR-encoded data
 *
 * A typical usage of CBOR looks like:
 * @code{.cpp}
 * cbor_stream_t stream;
 * cbor_init(&stream, 1024); // allocate array with 1024 bytes
 *
 * cbor_serialize_int(&stream, 5);
 * <do something with 'stream.data'>
 *
 * cbor_destroy(&stream);
 * @endcode
 *
 * @sa cbor_init
 * @sa cbor_clear
 * @sa cbor_destroy
 */
typedef struct cbor_stream_t
{
    /// Array containing CBOR encoded data
    unsigned char* data;
    /// Size of the array
    size_t size;
    /// Index to the next free byte
    size_t pos;
} cbor_stream_t;

/**
 * Initialize cbor struct
 *
 * @param size The size in bytes that should be allocated for this struct
 */
void cbor_init(cbor_stream_t* stream, size_t size);

/**
 * Clear cbor struct
 *
 * Sets pos to zero
 */
void cbor_clear(cbor_stream_t* stream);

/**
 * Free memory hold by the cbor struct
 *
 * Frees memory hold by stream->data and resets the struct
 */
void cbor_destroy(cbor_stream_t* stream);

/**
 * Print @p stream in hex representation
 */
void cbor_stream_print(cbor_stream_t* stream);

/**
 * Decode CBOR from @p stream
 */
void cbor_stream_decode(cbor_stream_t* stream);

size_t cbor_deserialize_int(const cbor_stream_t* stream, size_t offset, int* val);
size_t cbor_serialize_int(cbor_stream_t* s, int val);
size_t cbor_deserialize_uint64_t(const cbor_stream_t* stream, size_t offset, uint64_t* val);
size_t cbor_serialize_uint64_t(cbor_stream_t* s, uint64_t val);
size_t cbor_deserialize_int64_t(const cbor_stream_t* stream, size_t offset, int64_t* val);
size_t cbor_serialize_int64_t(cbor_stream_t* s, int64_t val);
size_t cbor_deserialize_bool(const cbor_stream_t* stream, size_t offset, bool* val);
size_t cbor_serialize_bool(cbor_stream_t* s, bool val);
size_t cbor_deserialize_float_half(const cbor_stream_t* stream, size_t offset, float* val);
size_t cbor_serialize_float_half(cbor_stream_t* s, float val);
size_t cbor_deserialize_float(const cbor_stream_t* stream, size_t offset, float* val);
size_t cbor_serialize_float(cbor_stream_t* s, float val);
size_t cbor_deserialize_double(const cbor_stream_t* stream, size_t offset, double* val);
size_t cbor_serialize_double(cbor_stream_t* s, double val);

/**
 * Deserialize bytes from @p stream to @p val
 *
 * @param val Pointer to destination array
 * @param length Length of destination array
 * @return Number of bytes written into @p val
 */
size_t cbor_deserialize_byte_string(const cbor_stream_t* stream, size_t offset, char* val, size_t length);
size_t cbor_serialize_byte_string(cbor_stream_t* s, const char* val);
/**
 * Deserialize unicode string from @p stream to @p val
 *
 * @param val Pointer to destination array
 * @param length Length of destination array
 * @return Number of bytes written into @p val
 */
size_t cbor_deserialize_unicode_string(const cbor_stream_t* stream, size_t offset, char* val, size_t length);
size_t cbor_serialize_unicode_string(cbor_stream_t* s, const char* val);

size_t cbor_deserialize_array(const cbor_stream_t* s, size_t offset, size_t* array_length);
size_t cbor_serialize_array(cbor_stream_t* s, size_t array_length);

size_t cbor_deserialize_indefinite_array(const cbor_stream_t* s, size_t offset);
size_t cbor_serialize_indefinite_array(cbor_stream_t* s);

size_t cbor_deserialize_map(const cbor_stream_t* s, size_t offset, size_t* map_length);
size_t cbor_serialize_map(cbor_stream_t* s, size_t map_length);

size_t cbor_deserialize_indefinite_map(const cbor_stream_t* s, size_t offset);
size_t cbor_serialize_indefinite_map(cbor_stream_t* s);

/**
 * Write a break symbol at the current offset in stream @p s
 *
 * Used for marking the end of indefinite length CBOR items
 */
size_t cbor_write_break(cbor_stream_t* s);
/**
 * Whether we are at a break symbol in stream @p s at offset @p offset
 *
 * @return True in case the there is a break symbol at the current offset
 */
bool cbor_at_break(const cbor_stream_t* s, size_t offset);
/**
 * Whether we are at the end of the stream @p s at offset @p offset
 *
 * Useful for abort conditions in loops while deserializing CBOR items
 *
 * @return True in case @p offset marks the end of the stream
 */
bool cbor_at_end(const cbor_stream_t* s, size_t offset);

#endif

/** @} */
