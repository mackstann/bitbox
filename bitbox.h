#ifndef __BITBOX_H__
#define __BITBOX_H__

#include <sys/time.h>
#include <glib.h>

// bitbox

typedef struct {
    // this should probably be opaque to the outside world but i'm not sure of
    // the C-foo to do that correctly.
    GHashTable * hash;
} bitbox_t;

bitbox_t * bitbox_new(void);
void bitbox_free(bitbox_t * box);
void bitbox_set_bit(bitbox_t * box, const char * key, int bit);
int bitbox_get_bit(bitbox_t * box, const char * key, int bit);

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

int bitarray_get_bit(bitarray_t * b, int index);
void bitarray_set_bit(bitarray_t * b, int index);
bitarray_t * bitbox_find_array(bitbox_t * box, const char * key);

#endif
