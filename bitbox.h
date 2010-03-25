#ifndef __BITBOX_H__
#define __BITBOX_H__

// we must explicitly request the PRId64 etc. macros in C++.
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <sys/time.h>
#include <string.h>
#include <glib.h>
#include <map>

#define BITBOX_ITEM_LIMIT       1500
#define BITBOX_ITEM_PEAK_LIMIT  2000

// bitarray

typedef struct {
    uint8_t * array;
    int64_t size; // actual number of bytes allocated in array

    // an optimization to prevent a bunch of unused zeroes at the beginning of
    // an array that doesn't use them.
    int64_t offset;

    // so we can flush less-used data to disk.
    int64_t last_access;
    char * key;
} bitarray_t;

// bitbox

typedef std::multimap<const int64_t, char *> bitbox_lru_map_t;

typedef struct {
    // the main way we access data.  the key is an arbitrary string and the
    // value is a bitarray_t.
    GHashTable * hash;

    // we use this to implement efficient dump-to-disk behavior to keep memory
    // usage reasonable.  the key is a timestamp and the value corresponds to a
    // key in the hash.
    bitbox_lru_map_t lru;

    // this is to prevent having memory get too out of sync with the disk,
    // causing lots of data loss in case of an unclean shutdown.  it is used as
    // a set.  the key is the bitbox_t* and the value is a dummy.
    GHashTable * need_disk_write;
} bitbox_t;

bitbox_t * bitbox_new(void);
void       bitbox_free(bitbox_t * box);
void bitbox_shutdown(bitbox_t * box);

int  bitbox_get_bit (bitbox_t * box, const char * key, int64_t bit);
void bitbox_set_bit (bitbox_t * box, const char * key, int64_t bit);
void bitbox_set_bits(bitbox_t * box, const char * key, int64_t * bits, int64_t nbits);

void bitbox_downsize_single_step(bitbox_t * box, int64_t memory_limit);
int  bitbox_downsize_needed     (bitbox_t * box);

void bitbox_diskwrite_single_step(bitbox_t * box);
int  bitbox_needs_disk_write     (bitbox_t * box);

bitarray_t * bitbox_find_array          (bitbox_t * box, const char * key);
bitarray_t * bitbox_find_or_create_array(bitbox_t * box, const char * key);

#endif
