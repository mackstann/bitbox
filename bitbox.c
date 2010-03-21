#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <inttypes.h>
#include <assert.h>

// stat
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib.h>
#include <lzf.h>

#include "bitbox.h"

#ifndef NDEBUG
#   define DEBUG(...) do { \
        fprintf(stderr, __VA_ARGS__); \
    } while(0)
#else
#   define DEBUG(...)
#endif

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

#define MIN_ARRAY_SIZE 1

static time_t _get_second(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec;
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

    if(start_bit > -1)
        bitarray_init_data(b, start_bit);

    return b;
}

static void bitarray_free(bitarray_t * b)
{
    DEBUG("bitarray_free\n");
    assert(b);
    if(b->array)
        free(b->array);
    free(b);
}

static void bitarray_dump(bitarray_t * b)
{
#ifdef NDEBUG
    return;
#endif
    int64_t i, j;
    DEBUG("-- DUMP starting at offset %ld --\n", b->offset);
    DEBUG("-- size: %ld array address: %lx --\n", b->size, (long)b->array);
    for(i = 0; i < b->size; i++)
    {
        if(b->array[i] == 0)
            continue;
        DEBUG("%08ld ", i);
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
    uint8_t * buffer = malloc(*uncompressed_size);
    ((int64_t *)buffer)[0] = b->size;
    ((int64_t *)buffer)[1] = b->offset;

    if(b->array)
    {
        DEBUG("copying %ld bytes, offset by %ld bytes\n", b->size, sizeof(int64_t)*2);
        memcpy(buffer + sizeof(int64_t)*2, b->array, b->size);
    }

    *out_buffer = malloc(*uncompressed_size);
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

static bitarray_t * bitarray_thaw(uint8_t * buffer, int64_t bufsize, int64_t uncompressed_size, int is_compressed)
{
    DEBUG("bitarray_thaw\n");
    if(is_compressed)
    {
        uint8_t * tmp_buffer = malloc(uncompressed_size);
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
        b->array = malloc(b->size);
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
static void bitarray_save_frozen(const char * key, uint8_t * buffer, int64_t bufsize, int64_t uncompressed_size, int is_compressed)
{
    DEBUG("uncompressed_size at save: %ld\n", (int64_t)uncompressed_size);

    int64_t file_size = sizeof(uint8_t) // is_compressed boolean
                      + sizeof(int64_t) // uncompressed_size
                      + bufsize;

    uint8_t * contents = malloc(file_size);

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

static void bitarray_load_frozen(const char * key, uint8_t ** buffer, int64_t * bufsize, int64_t * uncompressed_size, int * is_compressed)
{
    char * filename = g_strdup_printf("data/%s", key);
    int64_t file_size;
    uint8_t * contents;

    g_file_get_contents(filename, (char **)&contents, (gsize *)&file_size, NULL); // XXX error handling
    g_free(filename);

    *is_compressed = contents[0];
    *uncompressed_size = ((int64_t *)(contents + sizeof(uint8_t)))[0];
    *bufsize = file_size - (sizeof(char) + sizeof(int64_t));

    *buffer = malloc(*bufsize);
    memcpy(*buffer, contents + sizeof(uint8_t) + sizeof(int64_t), *bufsize);
    g_free(contents);

    DEBUG("uncompressed_size at load: %ld\n", (int64_t)*uncompressed_size);
}

static void bitarray_grow_up(bitarray_t * b, int64_t size)
{
    DEBUG("bitarray_grow_up(new_size %ld)\n", size);
    uint8_t * new_array = (uint8_t *)calloc(size, 1);
    memcpy(new_array, b->array, b->size);
    free(b->array);
    b->array = new_array;
    b->size = size;
}

static void bitarray_grow_down(bitarray_t * b, int64_t new_size)
{
    DEBUG("bitarray_grow_down(new_size %ld) (formerly size %ld)\n", new_size, b->size);

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
    DEBUG("after grow down -- new_size: %ld new_begin: %ld\n", new_size, new_begin);
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
    DEBUG("bitarray_get_bit(index %ld)\n", index);
    b->last_access = _get_second();

    if(!b->array || b->offset + b->size < index/8+1 || index/8 < b->offset)
    {
        DEBUG("end of bitarray_get_bit\n");
        return 0;
    }

    DEBUG("end of bitarray_get_bit\n");
    return BYTE_SLOT(b, index) & MASK(index) ? 1 : 0;
}

void bitarray_set_bit(bitarray_t * b, int64_t index)
{
    DEBUG("bitarray_set_bit(index %ld)\n", index);
    b->last_access = _get_second();

    if(!b->array)
        bitarray_init_data(b, index);

    bitarray_adjust_size_to_reach(b, index);

    assert(index >= 0);
    assert(b->offset >= 0);
    if(BYTE_OFFSET(index) - b->offset < 0)
    {
        DEBUG("index: %ld\n", index);
        DEBUG("b->offset: %ld\n", b->offset);
        abort();
    }
    assert(BYTE_OFFSET(index) - b->offset <  b->size);
    BYTE_SLOT(b, index) |= MASK(index);
    bitarray_dump(b);
    DEBUG("end of bitarray_set_bit\n");
}

// public bitbox api

bitbox_t * bitbox_new(void)
{
    bitbox_t * box = (bitbox_t *)malloc(sizeof(bitbox_t));
    CHECK(box);
    box->hash = g_hash_table_new(g_str_hash, g_str_equal);
    CHECK(box->hash);
    box->size = 0;
    box->memory_limit = 0;
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
        g_hash_table_insert(box->hash, strdup(key), b);
    }
    return b;
}

void bitbox_set_bit_nolookup(bitbox_t * box, const char * key, bitarray_t * b, int64_t bit)
{
    int64_t old_size = b->size;
    bitarray_set_bit(b, bit);
    box->size += b->size - old_size;

    uint8_t * buffer;
    int64_t bufsize;
    int64_t uncompressed_size;

    // freeze
    int is_compressed = bitarray_freeze(b, &buffer, &bufsize, &uncompressed_size);
    bitarray_free(b);

    // save
    bitarray_save_frozen(key, buffer, bufsize, uncompressed_size, is_compressed);

    // load
    bitarray_load_frozen(key, &buffer, &bufsize, &uncompressed_size, &is_compressed);

    // unfreeze
    b = bitarray_thaw(buffer, bufsize, uncompressed_size, is_compressed);

    g_hash_table_remove(box->hash, key); // XXX the key gets leaked here -- should use g_hash_table_new_full
    g_hash_table_insert(box->hash, strdup(key), b);
}

void bitbox_set_bit(bitbox_t * box, const char * key, int64_t bit)
{
    bitarray_t * b = bitbox_find_array(box, key);
    bitbox_set_bit_nolookup(box, key, b, bit);
}

int bitbox_get_bit(bitbox_t * box, const char * key, int64_t bit)
{
    bitarray_t * b = (bitarray_t *)g_hash_table_lookup(box->hash, key);
    return !b ? 0 : bitarray_get_bit(b, bit);
}

void bitbox_cleanup_single_step(bitbox_t * box)
{
    DEBUG("using %ld bytes in bitarrays\n", box->size);
}

int bitbox_cleanup_needed(bitbox_t * box)
{
    return box->size > box->memory_limit;
}

