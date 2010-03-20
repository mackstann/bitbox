#ifndef __BITBOX_H__
#define __BITBOX_H__

#include <sys/time.h>
#include <glib.h>

// bitarray

typedef struct {
    unsigned char * array;
    int size; // actual number of bytes allocated in array

    // an optimization to prevent a bunch of unused zeroes at the beginning of
    // an array that doesn't use them.
    int offset;

    // so we can flush less-used data to disk.
    time_t last_access;
} bitarray_t;

// bitbox

typedef struct {
    // this should probably be opaque to the outside world but i'm not sure of
    // the C-foo to do that correctly.
    GHashTable * hash;
    int size; // sum of the sizes of all bitarrays it holds
} bitbox_t;

bitbox_t * bitbox_new(void);
void bitbox_free(bitbox_t * box);
void bitbox_set_bit(bitbox_t * box, const char * key, int bit);
void bitbox_set_bit_nolookup(bitbox_t * box, const char * key, bitarray_t * b, int bit);
int bitbox_get_bit(bitbox_t * box, const char * key, int bit);
void bitbox_cleanup_single_step(bitbox_t * box);
int bitbox_cleanup_needed(bitbox_t * box);
bitarray_t * bitbox_find_array(bitbox_t * box, const char * key);

#endif
