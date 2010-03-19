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

// private bitarray functions

static bitarray_t * bitarray_new(int first_bit)
{
    DEBUG("bitarray_new\n");
    bitarray_t * b = (bitarray_t *)malloc(sizeof(bitarray_t));
    CHECK(b);

    if(first_bit > -1)
    {
        b->offset = first_bit/8;
        b->size = MIN_ARRAY_SIZE;
        b->array = (unsigned char *)calloc(b->size, 1);
        CHECK(b->array);
    }
    else // initialize it later
    {
        b->offset = -1;
        b->size = -1;
        b->array = NULL;
    }

    return b;
}

static void bitarray_free(bitarray_t * b)
{
    DEBUG("bitarray_free\n");
    assert(b);
    assert(b->array);
    free(b->array);
    free(b);
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

static time_t _get_second(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec;
}

// public bitarray api

int bitarray_get_bit(bitarray_t * b, int index)
{
    b->last_access = _get_second();

    if(!b->array || b->offset + b->size <= index/8+1)
        return 0;

    return BYTE_SLOT(b, index) & MASK(index) ? 1 : 0;
}

void bitarray_set_bit(bitarray_t * b, int index)
{
    //DEBUG("bitarray_set_bit(index %d)\n", index);
    b->last_access = _get_second();

    if(!b->array)
    {
        b->offset = index/8;
        b->size = MIN_ARRAY_SIZE;
        b->array = (unsigned char *)calloc(b->size, 1);
        CHECK(b->array);
    }

    if(b->size < index/8+1)
        bitarray_grow_up(b, (index/8+1) * 2);
    if(index/8 < b->offset)
        bitarray_grow_down(b, b->size * 2);

    BYTE_SLOT(b, index) |= MASK(index);
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

