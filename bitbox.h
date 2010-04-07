#ifndef __BITBOX_H__
#define __BITBOX_H__

// we must explicitly request the PRId64 etc. macros in C++.
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <sys/time.h>
#include <string.h>
#include <glib.h>
#include <google/sparse_hash_map>
#include <google/sparse_hash_set>
#include <map>

#define BITBOX_ITEM_LIMIT       1500
#define BITBOX_ITEM_PEAK_LIMIT  2000

#if __WORDSIZE == 64
uint64_t MurmurHash64A(const void * key, int len, unsigned int seed);
#define MurmurHash MurmurHash64A
#else
unsigned int MurmurHash2(const void * key, int len, unsigned int seed);
#define MurmurHash MurmurHash2
#endif

struct bitbox_str_hasher
{
    size_t operator()(const char * key) const
    {
        return MurmurHash(key, strlen(key), 0);
    }
};

struct eqstr
{
    bool operator()(const char* s1, const char* s2) const
    {
        return (s1 == s2) || (s1 && s2 && strcmp(s1, s2) == 0);
    }
};

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

class Bitbox {
private:
    typedef google::sparse_hash_map<const char *, bitarray_t *, bitbox_str_hasher, eqstr> hash_t;
    typedef std::multimap<const int64_t, char *> lru_map_t;
    typedef google::sparse_hash_set<bitarray_t *> need_disk_write_set_t;

    // the main way we access data.  the key is an arbitrary string and the
    // value is a bitarray_t.
    hash_t hash;

    // we use this to implement efficient dump-to-disk behavior to keep memory
    // usage reasonable.  the key is a timestamp and the value corresponds to a
    // key in the hash.
    lru_map_t lru;

    // this is to prevent having memory get too out of sync with the disk,
    // causing lots of data loss in case of an unclean shutdown.  it stores
    // a set of items of the type bitarray_t*
    need_disk_write_set_t need_disk_write;

public:
    Bitbox();
    ~Bitbox();
    void shutdown();

    int  get_bit (const char * key, int64_t bit);
    void set_bit (const char * key, int64_t bit);
    void set_bits(const char * key, int64_t * bits, int64_t nbits);

private:
    void downsize_single_step(int64_t memory_limit);
    void diskwrite_single_step();

public:
    bitarray_t * find_array          (const char * key);
    bitarray_t * find_or_create_array(const char * key);

    bool run_maintenance_step();

private:
    void update_key_in_lru(const char * key, int64_t old_timestamp, int64_t new_timestamp);
    void add_array_to_hash(bitarray_t * b);
    void downsize_if_angry();
    void set_bit_nolookup(bitarray_t * b, int64_t bit);
    void banish_oldest_item_to_disk();
    void write_one_to_disk();

    bitarray_t * find_array_in_memory(const char * key);
    bitarray_t * find_array_on_disk  (const char * key);
};

#endif
