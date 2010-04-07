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

#define MIN_ARRAY_SIZE 1

#ifndef NDEBUG
#   define DEBUG(...) do { \
        fprintf(stderr, __VA_ARGS__); \
    } while(0)
#else
#   define DEBUG(...)
#endif

#define BYTE_OFFSET(i) ((i) / 8)
#define BIT_OFFSET(i) ((i) % 8)
#define BYTE_SLOT(b, i) (b->array[BYTE_OFFSET(i) - b->offset])
#define MASK(i) (1 << BIT_OFFSET(i))

static int64_t _get_time(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((int64_t)tv.tv_sec * 1000000) + tv.tv_usec;
}

// private bitarray functions

static void bitarray_init_data(bitarray_t * b, int64_t start_bit)
{
    b->offset = start_bit/8;
    b->size = MIN_ARRAY_SIZE;
    b->array = (uint8_t *)calloc(b->size, 1);
    assert(b->array);
}

static bitarray_t * bitarray_new(const char * key, int64_t start_bit)
{
    bitarray_t * b = (bitarray_t *)malloc(sizeof(bitarray_t));
    assert(b);

    b->last_access = _get_time();
    b->offset = 0;
    b->size = 0;
    b->array = NULL;
    b->key = strdup(key);

    if(start_bit > -1)
        bitarray_init_data(b, start_bit);

    return b;
}

static void bitarray_free(bitarray_t * b)
{
    assert(b);
    assert(b->key);
    free(b->key);
    if(b->array)
        free(b->array);
    free(b);
}

#if 0
static void bitarray_dump(bitarray_t * b)
{
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
#endif

static int bitarray_freeze(bitarray_t * b, uint8_t ** out_buffer, int64_t * out_bufsize, int64_t * uncompressed_size)
{
    assert((b->size && b->array) || (!b->size && !b->array));
    *uncompressed_size = sizeof(int64_t)*2 + b->size;
    uint8_t * buffer = (uint8_t *)malloc(*uncompressed_size);
    ((int64_t *)buffer)[0] = b->size;
    ((int64_t *)buffer)[1] = b->offset;

    if(b->array)
        memcpy(buffer + sizeof(int64_t)*2, b->array, b->size);

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

static bitarray_t * bitarray_thaw(const char * key, uint8_t * buffer, int64_t bufsize, int64_t uncompressed_size, uint8_t is_compressed)
{
    if(is_compressed)
    {
        uint8_t * tmp_buffer = (uint8_t *)malloc(uncompressed_size);
        assert(lzf_decompress(buffer, bufsize, tmp_buffer, uncompressed_size) == uncompressed_size);
        free(buffer);
        buffer = tmp_buffer;
    }
    else
        assert(uncompressed_size == bufsize);

    bitarray_t * b = bitarray_new(key, -1);
    b->size   = ((int64_t *)buffer)[0];
    b->offset = ((int64_t *)buffer)[1];
    b->array = NULL;
    if(b->size)
    {
        b->array = (uint8_t *)malloc(b->size);
        memcpy(b->array, buffer + sizeof(int64_t)*2, b->size);
    }
    free(buffer);
    return b;
}

// XXX: g_file_set_contents writes to a temp file called
// "key.RANDOM-GIBBERISH", which theoretically could be loaded accidentally if
// someone requested that exact key at the right moment.  a more robust file
// writing mechanism should eventually be used.
static void bitarray_save_frozen(const char * key, uint8_t * buffer, int64_t bufsize, int64_t uncompressed_size, uint8_t is_compressed)
{
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

    gboolean got_contents = g_file_get_contents(filename, (char **)&contents, (gsize *)&file_size, NULL);
    g_free(filename);
    if(!got_contents)
    {
        *buffer = NULL;
        *bufsize = 0;
        *uncompressed_size = 0;
        *is_compressed = 0;
        return;
    }

    *is_compressed = contents[0];
    *uncompressed_size = ((int64_t *)(contents + sizeof(uint8_t)))[0];
    *bufsize = file_size - (sizeof(char) + sizeof(int64_t));

    *buffer = (uint8_t *)malloc(*bufsize);
    memcpy(*buffer, contents + sizeof(uint8_t) + sizeof(int64_t), *bufsize);
    g_free(contents);
}

static void bitarray_grow_up(bitarray_t * b, int64_t size)
{
    uint8_t * new_array = (uint8_t *)calloc(size, 1);
    memcpy(new_array, b->array, b->size);
    free(b->array);
    b->array = new_array;
    b->size = size;
}

static void bitarray_grow_down(bitarray_t * b, int64_t new_size)
{
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
    b->last_access = _get_time();

    if(!b->array || b->offset + b->size < index/8+1 || index/8 < b->offset)
        return 0;

    return BYTE_SLOT(b, index) & MASK(index) ? 1 : 0;
}

void bitarray_set_bit(bitarray_t * b, int64_t index)
{
    b->last_access = _get_time();

    if(!b->array)
        bitarray_init_data(b, index);

    bitarray_adjust_size_to_reach(b, index);

    assert(index >= 0);
    assert(b->offset >= 0);
    if(BYTE_OFFSET(index) - b->offset < 0)
    {
        abort();
    }
    assert(BYTE_OFFSET(index) - b->offset <  b->size);
    BYTE_SLOT(b, index) |= MASK(index);
}

// public bitbox api

gint timestamp_compare(gconstpointer ap, gconstpointer bp)
{
    gint a = GPOINTER_TO_INT(ap);
    gint b = GPOINTER_TO_INT(bp);
    return a == b ? 0 : (a < b ? -1 : 1);
}

Bitbox::Bitbox()
{
    this->hash = new Bitbox::hash_t(10, bitbox_str_hasher(), eqstr());
    this->hash->set_deleted_key("");

    this->need_disk_write = new Bitbox::need_disk_write_set_t();
    this->need_disk_write->set_deleted_key(NULL);
}

Bitbox::~Bitbox()
{
    Bitbox::hash_t::iterator it = this->hash->begin();
    for(; it != this->hash->end(); ++it)
    {
        bitarray_free(it->second);
        this->hash->erase(it);
    }
}

void Bitbox::update_key_in_lru(const char * key, int64_t old_timestamp, int64_t new_timestamp)
{
    assert(new_timestamp);
    if(old_timestamp)
    {
        // multiple keys may have the same last-modified time.  so scan through
        // any siblings and find the one we're looking for.
        Bitbox::lru_map_t::iterator it = this->lru.find(old_timestamp);
        Bitbox::lru_map_t::iterator upper_bound = this->lru.upper_bound(old_timestamp);

        for(; it != upper_bound; it++)
        {
            if(!strcmp(it->second, key)) // found it
            {
                // delete it
                free(it->second);
                this->lru.erase(it);
                break;
            }
        }
    }

    // and add the key with its new timestamp
    this->lru.insert(Bitbox::lru_map_t::value_type(new_timestamp, strdup(key)));
    // could be smarter about reusing the key instead of always freeing & re-copying
}

bitarray_t * Bitbox::find_array_in_memory(const char * key)
{
    Bitbox::hash_t::iterator it = this->hash->find(key);
    return it == this->hash->end() ? NULL : it->second;
}

bitarray_t * Bitbox::find_array_on_disk(const char * key)
{
    uint8_t is_compressed, * buffer;
    int64_t bufsize, uncompressed_size;
    bitarray_load_frozen(key, &buffer, &bufsize, &uncompressed_size, &is_compressed);

    if(!buffer)
        return NULL;

    return bitarray_thaw(key, buffer, bufsize, uncompressed_size, is_compressed);
}

void Bitbox::add_array_to_hash(bitarray_t * b)
{
    (*this->hash)[b->key] = b;
    this->update_key_in_lru(b->key, 0, b->last_access);
}

bitarray_t * Bitbox::find_array(const char * key)
{
    bitarray_t * b = this->find_array_in_memory(key);
    if(b)
        return b;

    b = this->find_array_on_disk(key);
    if(b)
        this->add_array_to_hash(b);

    return b;
}

bitarray_t * Bitbox::find_or_create_array(const char * key)
{
    bitarray_t * b = Bitbox::find_array(key);
    if(!b)
    {
        b = bitarray_new(key, -1);
        this->add_array_to_hash(b);
    }
    return b;
}

void Bitbox::downsize_if_angry()
{
    // ok, that's it.  even if really busy, bring memory usage down below the
    // "angry" limit before proceeding.  we'll never be very far past the
    // limit, so the while loop isn't as scary as it might look.
    while(this->hash->size() >= BITBOX_ITEM_PEAK_LIMIT && this->lru.size())
        this->downsize_single_step(BITBOX_ITEM_PEAK_LIMIT);
}

void Bitbox::set_bit_nolookup(bitarray_t * b, int64_t bit)
{
    assert(b);
    int64_t old_timestamp = b->last_access;

    bitarray_set_bit(b, bit);

    this->update_key_in_lru(b->key, old_timestamp, b->last_access);

    this->need_disk_write->insert(b);
}

void Bitbox::set_bit(const char * key, int64_t bit)
{
    bitarray_t * b = Bitbox::find_or_create_array(key);
    this->set_bit_nolookup(b, bit);
    this->downsize_if_angry();
}

void Bitbox::set_bits(const char * key, int64_t * bits, int64_t nbits)
{
    bitarray_t * b = this->find_or_create_array(key);
    for(int64_t i = 0; i < nbits; i++)
        this->set_bit_nolookup(b, bits[i]);
    this->downsize_if_angry();
}

int Bitbox::get_bit(const char * key, int64_t bit)
{
    bitarray_t * b = this->find_array(key);

    if(!b)
        return 0;

    int64_t old_timestamp = b->last_access;
    int retval = bitarray_get_bit(b, bit);

    this->update_key_in_lru(key, old_timestamp, b->last_access);

    return retval;
}

static void bitarray_save_to_disk(bitarray_t * b)
{
    uint8_t * buffer;
    int64_t bufsize;
    int64_t uncompressed_size;
    uint8_t is_compressed = bitarray_freeze(b, &buffer, &bufsize, &uncompressed_size);
    bitarray_save_frozen(b->key, buffer, bufsize, uncompressed_size, is_compressed);
}

void Bitbox::banish_oldest_item_to_disk()
{
    assert(this->hash->size() == this->lru.size());
    Bitbox::lru_map_t::iterator it = this->lru.begin();

    if(it == this->lru.end())
        return;

    char * key = it->second;
    this->lru.erase(it);

    bitarray_t * b = this->find_array_in_memory(key);
    assert(b);
    bitarray_save_to_disk(b);
    this->hash->erase(key);
    this->need_disk_write->erase(b);

    bitarray_free(b);
    free(key);
}

void Bitbox::downsize_single_step(int64_t item_limit)
{
    //DEBUG("************ box too big? %d\n", g_hash_table_size(box->hash) >= item_limit);
    //DEBUG("************ lru size? %d\n", box->lru.size());
    //DEBUG("************ hash size? %d\n", g_hash_table_size(box->hash));
    if(this->hash->size() >= (Bitbox::hash_t::size_type)item_limit)
        this->banish_oldest_item_to_disk();
}

void Bitbox::write_one_to_disk()
{
    Bitbox::need_disk_write_set_t::iterator it = this->need_disk_write->begin();
    if(it != this->need_disk_write->end())
    {
        bitarray_save_to_disk(*it);
        this->need_disk_write->erase(it);
        DEBUG("wrote one to disk. %lu left\n", this->need_disk_write->size());
    }
}

bool Bitbox::run_maintenance_step()
{
    this->downsize_single_step(BITBOX_ITEM_LIMIT);
    this->write_one_to_disk();
    return this->hash->size() >= BITBOX_ITEM_LIMIT || !this->need_disk_write->empty();
}

void Bitbox::shutdown()
{
    while(!this->need_disk_write->empty())
        this->write_one_to_disk();
}
