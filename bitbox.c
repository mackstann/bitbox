#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <assert.h>

#include <glib.h>

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

static void bitarray_init_data(bitarray_t * b, int start_bit)
{
    b->offset = start_bit/8;
    b->size = MIN_ARRAY_SIZE;
    b->array = (unsigned char *)calloc(b->size, 1);
    CHECK(b->array);
}

static bitarray_t * bitarray_new(int start_bit)
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

static char * bitarray_freeze(bitarray_t * b)
{
    char * buffer = malloc(sizeof(int)*2 + b->size);
    ((int *)buffer)[0] = b->size;
    ((int *)buffer)[1] = b->offset;
    if(b->array)
        memcpy(buffer + sizeof(int)*2, b->array, b->size);
    return buffer;
}

static bitarray_t * bitarray_thaw(const char * buffer)
{
    bitarray_t * b = bitarray_new(-1);
    b->size = ((int *)buffer)[0];
    b->offset = ((int *)buffer)[1];
    if(b->size)
    {
        b->array = malloc(b->size);
        memcpy(b->array, buffer + sizeof(int)*2, b->size);
    }
    return b;
}

static void bitarray_grow_up(bitarray_t * b, int size)
{
    DEBUG("bitarray_grow_up(new_size %d)\n", size);
    unsigned char * new_array = (unsigned char *)calloc(size, 1);
    memcpy(new_array, b->array, b->size);
    free(b->array);
    b->array = new_array;
    b->size = size;
}

static void bitarray_grow_down(bitarray_t * b, int new_size)
{
    DEBUG("bitarray_grow_down(new_size %d) (formerly size %d)\n", new_size, b->size);

    // move the starting point back
    int grow_by = new_size - b->size;
    int new_begin = b->offset - grow_by;

    // if we went too far, past 0, then just reset the start to 0.
    if(new_begin < 0)
    {
        new_begin = 0;
        grow_by = b->offset;
        new_size = b->offset + b->size;
    }

    unsigned char * new_array = (unsigned char *)calloc(new_size, 1);
    memcpy(new_array + grow_by, b->array, b->size);
    free(b->array);

    b->array = new_array;
    b->size = new_size;
    b->offset = new_begin;
    DEBUG("after grow down -- new_size: %d new_begin: %d\n", new_size, new_begin);
}

static void bitarray_adjust_size_to_reach(bitarray_t * b, int new_index)
{
    if(BYTE_OFFSET(new_index) >= b->offset + b->size)
    {
        int min_increase_needed = BYTE_OFFSET(new_index) - (b->offset + b->size - 1);
        int new_size = MAX(b->size + min_increase_needed, b->size * 2);
        bitarray_grow_up(b, new_size);
    }
    if(BYTE_OFFSET(new_index) - b->offset < 0)
    {
        int min_increase_needed = b->offset - BYTE_OFFSET(new_index);
        int new_size = MAX(b->size + min_increase_needed, b->size * 2);
        bitarray_grow_down(b, new_size);
    }
}

// public bitarray api

int bitarray_get_bit(bitarray_t * b, int index)
{
    DEBUG("bitarray_get_bit(index %d)\n", index);
    b->last_access = _get_second();

    // the bug is in bitarray_thaw!
    // char * buf = bitarray_freeze(b);
    // bitarray_free(b);
    // b = bitarray_thaw(buf);
    // free(buf);

    if(!b->array || b->offset + b->size < index/8+1 || index/8 < b->offset)
    {
        DEBUG("end of bitarray_get_bit\n");
        return 0;
    }

    DEBUG("end of bitarray_get_bit\n");
    return BYTE_SLOT(b, index) & MASK(index) ? 1 : 0;
}

void bitarray_set_bit(bitarray_t * b, int index)
{
    DEBUG("bitarray_set_bit(index %d)\n", index);
    b->last_access = _get_second();

    // char * buf = bitarray_freeze(b);
    // bitarray_free(b);
    // b = bitarray_thaw(buf);
    // free(buf);

    if(!b->array)
        bitarray_init_data(b, index);

    bitarray_adjust_size_to_reach(b, index);

    assert(index >= 0);
    assert(b->offset >= 0);
    if(BYTE_OFFSET(index) - b->offset < 0)
    {
        DEBUG("index: %d\n", index);
        DEBUG("b->offset: %d\n", b->offset);
        abort();
    }
    assert(BYTE_OFFSET(index) - b->offset <  b->size);
    BYTE_SLOT(b, index) |= MASK(index);
    DEBUG("end of bitarray_set_bit\n");
}

// public bitbox api

bitbox_t * bitbox_new(void)
{
    bitbox_t * box = (bitbox_t *)malloc(sizeof(bitbox_t));
    CHECK(box);
    box->hash = g_hash_table_new(g_str_hash, g_str_equal);
    CHECK(box->hash);
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

void bitbox_set_bit(bitbox_t * box, const char * key, int bit)
{
    bitarray_t * b = bitbox_find_array(box, key);
    bitarray_set_bit(b, bit);
}

int bitbox_get_bit(bitbox_t * box, const char * key, int bit)
{
    bitarray_t * b = (bitarray_t *)g_hash_table_lookup(box->hash, key);
    return !b ? 0 : bitarray_get_bit(b, bit);
}

