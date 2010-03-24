#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <assert.h>

// we must explicitly request the PRId64 etc. macros in C++.
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

// stat
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib.h>

extern "C" {
#include <lzf.h>
}

#include "bitbox.h"

// ugh this is totally not working.  new plan: use a c++ hash with a custom
// allocator that counts memory usage.
#define BYTES_CONSUMED_PER_HASH_KEY 100

#define MIN_ARRAY_SIZE 1

#ifndef NDEBUG
#   define DEBUG(...) do { \
        fprintf(stderr, __VA_ARGS__); \
    } while(0)
#else
#   define DEBUG(...)
#endif

// XXX get rid of this
#define CHECK(expr) do { \
    if(!(expr)) { \
        fputs("CHECK (" #expr ") failed", stderr); \
        abort(); \
    } \
} while(0)

#define BYTE_OFFSET(i) ((i) / 8)
#define BIT_OFFSET(i) ((i) % 8)
#define BYTE_SLOT(b, i) (b->array[BYTE_OFFSET(i) - b->offset])
#define MASK(i) (1 << BIT_OFFSET(i))

static int _get_second(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int)tv.tv_sec;
}

// private bitarray functions

static void bitarray_init_data(bitarray_t * b, int64_t start_bit)
{
    b->offset = start_bit/8;
    b->size = MIN_ARRAY_SIZE;
    b->array = (uint8_t *)calloc(b->size, 1);
    CHECK(b->array);
}

static bitarray_t * bitarray_new(int64_t start_bit)
{
    DEBUG("bitarray_new\n");
    bitarray_t * b = (bitarray_t *)malloc(sizeof(bitarray_t));
    CHECK(b);

    b->last_access = _get_second();
    b->offset = 0;
    b->size = 0;
    b->array = NULL;
    b->name = NULL;

    if(start_bit > -1)
        bitarray_init_data(b, start_bit);

    return b;
}

static void bitarray_free(bitarray_t * b)
{
    DEBUG("bitarray_free\n");
    assert(b);
    if(b->name)
        free(b->name);
    if(b->array)
        free(b->array);
    free(b);
}

static void bitarray_dump(bitarray_t * b)
{
#if 1
    return;
#endif
    int64_t i, j;
    DEBUG("-- DUMP starting at offset %" PRId64 " --\n", b->offset);
    DEBUG("-- size: %" PRId64 " array address: %p --\n", b->size, b->array);
    for(i = 0; i < b->size; i++)
    {
        if(b->array[i] == 0)
            continue;
        DEBUG("%08" PRId64 " ", i);
        for(j = 0; j < 8; j++)
            DEBUG((b->array[i] & MASK(j)) ? "1 " : "0 ");
        DEBUG("\n");
    }
    DEBUG("\n");
}

static int bitarray_freeze(bitarray_t * b, uint8_t ** out_buffer, int64_t * out_bufsize, int64_t * uncompressed_size)
{
    DEBUG("bitarray_freeze\n");
    bitarray_dump(b);
    CHECK((b->size && b->array) || (!b->size && !b->array));
    *uncompressed_size = sizeof(int64_t)*2 + b->size;
    uint8_t * buffer = (uint8_t *)malloc(*uncompressed_size);
    ((int64_t *)buffer)[0] = b->size;
    ((int64_t *)buffer)[1] = b->offset;

    if(b->array)
    {
        DEBUG("copying %" PRId64 " bytes, offset by %u bytes\n", b->size, sizeof(int64_t)*2);
        memcpy(buffer + sizeof(int64_t)*2, b->array, b->size);
    }

    *out_buffer = (uint8_t *)malloc(*uncompressed_size);
    *out_bufsize = lzf_compress(buffer, *uncompressed_size, *out_buffer, *uncompressed_size);

    // compression succeeded
    if(*out_bufsize > 0)
    {
        free(buffer);
        return 1;
    }

    // compression resulted in larger data (fairly common for tiny values), so
    // return uncompressed data.
    free(*out_buffer);
    *out_buffer = buffer;
    *out_bufsize = *uncompressed_size;
    return 0;
}

static bitarray_t * bitarray_thaw(uint8_t * buffer, int64_t bufsize, int64_t uncompressed_size, uint8_t is_compressed)
{
    DEBUG("bitarray_thaw\n");
    if(is_compressed)
    {
        uint8_t * tmp_buffer = (uint8_t *)malloc(uncompressed_size);
        CHECK(lzf_decompress(buffer, bufsize, tmp_buffer, uncompressed_size) == uncompressed_size);
        free(buffer);
        buffer = tmp_buffer;
    }
    else
        CHECK(uncompressed_size == bufsize);

    bitarray_t * b = bitarray_new(-1);
    b->size   = ((int64_t *)buffer)[0];
    b->offset = ((int64_t *)buffer)[1];
    b->array = NULL;
    if(b->size)
    {
        b->array = (uint8_t *)malloc(b->size);
        memcpy(b->array, buffer + sizeof(int64_t)*2, b->size);
    }
    free(buffer);
    bitarray_dump(b);
    return b;
}

// XXX: g_file_set_contents writes to a temp file called
// "key.RANDOM-GIBBERISH", which theoretically could be loaded accidentally if
// someone requested that exact key at the right moment.  a more robust file
// writing mechanism should eventually be used.
static void bitarray_save_frozen(const char * key, uint8_t * buffer, int64_t bufsize, int64_t uncompressed_size, uint8_t is_compressed)
{
    DEBUG("uncompressed_size at save: %" PRId64 "\n", (int64_t)uncompressed_size);

    int64_t file_size = sizeof(uint8_t) // is_compressed boolean
                      + sizeof(int64_t) // uncompressed_size
                      + bufsize;

    uint8_t * contents = (uint8_t *)malloc(file_size);

    memcpy(contents,                   &is_compressed,     sizeof(uint8_t));
    memcpy(contents + sizeof(uint8_t), &uncompressed_size, sizeof(int64_t));
    memcpy(contents + sizeof(uint8_t)
                    + sizeof(int64_t), buffer, bufsize);

    char * filename = g_strdup_printf("data/%s", key);

    g_file_set_contents(filename, (char *)contents, file_size, NULL); // XXX error handling

    g_free(filename);
    free(contents);
    free(buffer);
}

static void bitarray_load_frozen(const char * key, uint8_t ** buffer, int64_t * bufsize, int64_t * uncompressed_size, uint8_t * is_compressed)
{
    char * filename = g_strdup_printf("data/%s", key);
    int64_t file_size;
    uint8_t * contents;

    g_file_get_contents(filename, (char **)&contents, (gsize *)&file_size, NULL); // XXX error handling
    g_free(filename);

    *is_compressed = contents[0];
    *uncompressed_size = ((int64_t *)(contents + sizeof(uint8_t)))[0];
    *bufsize = file_size - (sizeof(char) + sizeof(int64_t));

    *buffer = (uint8_t *)malloc(*bufsize);
    memcpy(*buffer, contents + sizeof(uint8_t) + sizeof(int64_t), *bufsize);
    g_free(contents);

    DEBUG("uncompressed_size at load: %" PRId64 "\n", (int64_t)*uncompressed_size);
}

static void bitarray_grow_up(bitarray_t * b, int64_t size)
{
    //DEBUG("bitarray_grow_up(new_size %" PRId64 ")\n", size);
    uint8_t * new_array = (uint8_t *)calloc(size, 1);
    memcpy(new_array, b->array, b->size);
    free(b->array);
    b->array = new_array;
    b->size = size;
}

static void bitarray_grow_down(bitarray_t * b, int64_t new_size)
{
    //DEBUG("bitarray_grow_down(new_size %" PRId64 ") (formerly size %" PRId64 ")\n", new_size, b->size);

    // move the starting point back
    int64_t grow_by = new_size - b->size;
    int64_t new_begin = b->offset - grow_by;

    // if we went too far, past 0, then just reset the start to 0.
    if(new_begin < 0)
    {
        new_begin = 0;
        grow_by = b->offset;
        new_size = b->offset + b->size;
    }

    uint8_t * new_array = (uint8_t *)calloc(new_size, 1);
    memcpy(new_array + grow_by, b->array, b->size);
    free(b->array);

    b->array = new_array;
    b->size = new_size;
    b->offset = new_begin;
    //DEBUG("after grow down -- new_size: %" PRId64 " new_begin: %" PRId64 "\n", new_size, new_begin);
}

static void bitarray_adjust_size_to_reach(bitarray_t * b, int64_t new_index)
{
    if(BYTE_OFFSET(new_index) >= b->offset + b->size)
    {
        int64_t min_increase_needed = BYTE_OFFSET(new_index) - (b->offset + b->size - 1);
        int64_t new_size = MAX(b->size + min_increase_needed, b->size * 2);
        bitarray_grow_up(b, new_size);
    }
    if(BYTE_OFFSET(new_index) - b->offset < 0)
    {
        int64_t min_increase_needed = b->offset - BYTE_OFFSET(new_index);
        int64_t new_size = MAX(b->size + min_increase_needed, b->size * 2);
        bitarray_grow_down(b, new_size);
    }
}

// public bitarray api

int bitarray_get_bit(bitarray_t * b, int64_t index)
{
    //DEBUG("bitarray_get_bit(index %" PRId64 ")\n", index);
    b->last_access = _get_second();

    if(!b->array || b->offset + b->size < index/8+1 || index/8 < b->offset)
    {
        //DEBUG("end of bitarray_get_bit\n");
        return 0;
    }

    //DEBUG("end of bitarray_get_bit\n");
    return BYTE_SLOT(b, index) & MASK(index) ? 1 : 0;
}

void bitarray_set_bit(bitarray_t * b, int64_t index)
{
    //DEBUG("bitarray_set_bit(index %" PRId64 ")\n", index);
    b->last_access = _get_second();

    if(!b->array)
        bitarray_init_data(b, index);

    bitarray_adjust_size_to_reach(b, index);

    assert(index >= 0);
    assert(b->offset >= 0);
    if(BYTE_OFFSET(index) - b->offset < 0)
    {
        //DEBUG("index: %" PRId64 "\n", index);
        //DEBUG("b->offset: %" PRId64 "\n", b->offset);
        abort();
    }
    assert(BYTE_OFFSET(index) - b->offset <  b->size);
    BYTE_SLOT(b, index) |= MASK(index);
    bitarray_dump(b);
    //DEBUG("end of bitarray_set_bit\n");
}

// public bitbox api

gint timestamp_compare(gconstpointer ap, gconstpointer bp)
{
    gint a = GPOINTER_TO_INT(ap);
    gint b = GPOINTER_TO_INT(bp);
    return a == b ? 0 : (a < b ? -1 : 1);
}

bitbox_t * bitbox_new(void)
{
    bitbox_t * box = (bitbox_t *)malloc(sizeof(bitbox_t));
    CHECK(box);

    box->hash = g_hash_table_new(g_str_hash, g_str_equal);
    CHECK(box->hash);

    box->size = 0;

    box->lru = bitbox_lru_map_t();

    return box;
}

void bitbox_free(bitbox_t * box)
{
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init(&iter, box->hash);
    while(g_hash_table_iter_next(&iter, &key, &value)) 
    {
        free((char *)key);
        bitarray_free((bitarray_t *)value);
    }

    g_hash_table_destroy(box->hash);
    free(box);
}

bitarray_t * bitbox_find_array(bitbox_t * box, const char * key)
{
    bitarray_t * b = (bitarray_t *)g_hash_table_lookup(box->hash, key);
    if(!b)
    {
        b = bitarray_new(-1);
        b->name = strdup(key);
        g_hash_table_insert(box->hash, b->name, b);
        box->size += BYTES_CONSUMED_PER_HASH_KEY;
    }
    return b;
}

static void bitbox_update_key_in_lru(bitbox_t * box, char * key, int old_timestamp, int new_timestamp)
{
    //DEBUG("------------- UPDATE BEGIN ------------\n");
    if(old_timestamp)
    {
        // multiple keys may have the same last-modified time.  so scan through any
        // siblings and find the one we're looking for.
        bitbox_lru_map_t::iterator it = box->lru.find(old_timestamp);
        bitbox_lru_map_t::iterator upper_bound = box->lru.upper_bound(old_timestamp);

        for(; it != upper_bound; it++)
        {
            if(!strcmp(it->second, key)) // found it
            {
                // delete it
                //DEBUG("<<<<<<<<<<<<DELETE %d %s (%s and %s are the same) FROM LRU>>>>>>>>>>>>>\n", old_timestamp, key, it->second, key);
                free(it->second);
                box->lru.erase(it);
                break;
            }
        }
    }

    CHECK(new_timestamp);
    // and add the key with its new timestamp
    //DEBUG("<<<<<<<<<<<<INSERT %d %s INTO LRU>>>>>>>>>>>>>\n", new_timestamp, key);
    box->lru.insert(bitbox_lru_map_t::value_type(new_timestamp, key));

    // TODO: duplicates will be reduced greatly by using time precision
    // greater than 1 second.
    //DEBUG("------------- UPDATE END ------------\n");
}

void bitbox_cleanup_if_angry(bitbox_t * box)
{
    DEBUG("size is %" PRId64 " and angry limit is %d\n", box->size, BITBOX_MEMORY_ANGRY_LIMIT);
    if(box->size >= BITBOX_MEMORY_ANGRY_LIMIT && box->lru.size())
        bitbox_cleanup_single_step(box, BITBOX_MEMORY_ANGRY_LIMIT);
}

void bitbox_set_bit_nolookup(bitbox_t * box, const char * key, bitarray_t * b, int64_t bit)
{
    int old_timestamp = b->last_access;
    int64_t old_size = b->size;

    bitarray_set_bit(b, bit);

    box->size += b->size - old_size;
    bitbox_update_key_in_lru(box, strdup(key), old_timestamp, b->last_access);

    bitbox_cleanup_if_angry(box);

    // uint8_t * buffer;
    // int64_t bufsize;
    // int64_t uncompressed_size;

    // // freeze
    // uint8_t is_compressed = bitarray_freeze(b, &buffer, &bufsize, &uncompressed_size);
    // bitarray_free(b);

    // // save
    // bitarray_save_frozen(key, buffer, bufsize, uncompressed_size, is_compressed);

    // // load
    // bitarray_load_frozen(key, &buffer, &bufsize, &uncompressed_size, &is_compressed);

    // // unfreeze
    // b = bitarray_thaw(buffer, bufsize, uncompressed_size, is_compressed);

    // g_hash_table_remove(box->hash, key); // XXX the key gets leaked here -- should use g_hash_table_new_full
    // g_hash_table_insert(box->hash, strdup(key), b);
}

void bitbox_set_bit(bitbox_t * box, const char * key, int64_t bit)
{
    bitarray_t * b = bitbox_find_array(box, key);
    bitbox_set_bit_nolookup(box, key, b, bit);
}

int bitbox_get_bit(bitbox_t * box, const char * key, int64_t bit)
{
    bitarray_t * b = (bitarray_t *)g_hash_table_lookup(box->hash, key);
    if(!b)
        return 0;
    int old_timestamp = b->last_access;
    int retval = bitarray_get_bit(b, bit);

    bitbox_update_key_in_lru(box, strdup(key), old_timestamp, b->last_access);

    return retval;
}

void bitbox_cleanup_single_step(bitbox_t * box, int64_t memory_limit)
{
    DEBUG("************ box too big? %d\n", box->size >= memory_limit);
    DEBUG("************ lru size? %d\n", box->lru.size());
    DEBUG("************ hash size? %d\n", g_hash_table_size(box->hash));
    while(box->size >= memory_limit && box->lru.size())
    {
        bitbox_lru_map_t::iterator it = box->lru.begin();

        DEBUG("CLEANUP! using about %" PRId64 " bytes\n", box->size);

        char * key = it->second;
        box->lru.erase(it);

        bitarray_t * b = (bitarray_t *)g_hash_table_lookup(box->hash, key);
        CHECK(b);

        uint8_t * buffer;
        int64_t bufsize;
        int64_t uncompressed_size;
        uint8_t is_compressed = bitarray_freeze(b, &buffer, &bufsize, &uncompressed_size);
        bitarray_save_frozen(key, buffer, bufsize, uncompressed_size, is_compressed);

        box->size -= BYTES_CONSUMED_PER_HASH_KEY;
        box->size -= b->size;

        g_hash_table_remove(box->hash, key);
        bitarray_free(b);
        free(key);
        // and what about the key we allocated for the hash table?
        DEBUG("################## removed something! now using about %" PRId64 " bytes\n", box->size);
    }
}

int bitbox_cleanup_needed(bitbox_t * box)
{
    return box->size >= BITBOX_MEMORY_LIMIT && box->lru.size();
}
