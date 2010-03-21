#ifndef __BITBOX_H__
#define __BITBOX_H__

#include <inttypes.h>
#include <sys/time.h>
#include <glib.h>

// bitarray

typedef struct {
    uint8_t * array;
    int64_t size; // actual number of bytes allocated in array

    // an optimization to prevent a bunch of unused zeroes at the beginning of
    // an array that doesn't use them.
    int64_t offset;

    // so we can flush less-used data to disk.
    int last_access;
    char * name;
} bitarray_t;

// bitbox

typedef struct {
    // the main way we access data.  the key is an arbitrary string and the
    // value is a bitarray_t.
    GHashTable * hash;

    // we use this to implement efficient dump-to-disk behavior.  the key is a
    // timestamp and the value corresponds to a key in the hash.
    GTree * lru;

    int64_t size; // sum of the sizes of all bitarrays it holds
    int cleanup_needed;
} bitbox_t;

bitbox_t * bitbox_new(void);
void bitbox_free(bitbox_t * box);
void bitbox_set_bit(bitbox_t * box, const char * key, int64_t bit);
void bitbox_set_bit_nolookup(bitbox_t * box, const char * key, bitarray_t * b, int64_t bit);
int bitbox_get_bit(bitbox_t * box, const char * key, int64_t bit);
void bitbox_cleanup_single_step(bitbox_t * box);
int bitbox_cleanup_needed(bitbox_t * box);
bitarray_t * bitbox_find_array(bitbox_t * box, const char * key);

#endif
