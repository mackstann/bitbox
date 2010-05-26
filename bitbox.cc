#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <assert.h>

#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>

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

void Bitarray::init_data(int64_t start_bit)
{
    this->offset = start_bit/8;
    this->size = MIN_ARRAY_SIZE;
    this->array = (uint8_t *)calloc(this->size, 1);
    assert(this->array);
}

Bitarray::Bitarray(const char * key, int64_t start_bit)
    : array(NULL), size(0), offset(0)
{
    this->last_access = _get_time();
    this->key = strdup(key);

    if(start_bit > -1)
        this->init_data(start_bit);
}

Bitarray::~Bitarray()
{
    assert(this->key);
    free(this->key);
    if(this->array)
        free(this->array);
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

SerializedBitarray::~SerializedBitarray()
{
    if(this->buffer)
        free(this->buffer);
}

//namespace boost {
//namespace serialization {
//    template<class Archive>
//    void serialize(Archive & ar, gps_position & g, const unsigned int version)
//    {
//        ar & g.degrees;
//        ar & g.minutes;
//        ar & g.seconds;
//    }
//}
//}

SerializedBitarray::SerializedBitarray(Bitarray * b)
    : b(b), key(b->key), buffer(NULL), bufsize(0), uncompressed_size(0), is_compressed(0)
{
    assert((b->size && b->array) || (!b->size && !b->array));
    this->uncompressed_size = sizeof(int64_t)*2 + b->size;
    uint8_t * buffer = (uint8_t *)malloc(this->uncompressed_size);
    ((int64_t *)buffer)[0] = b->size;
    ((int64_t *)buffer)[1] = b->offset;

    if(b->array)
        memcpy(buffer + sizeof(int64_t)*2, b->array, b->size);

    this->buffer = (uint8_t *)malloc(this->uncompressed_size);
    this->bufsize = lzf_compress(buffer, this->uncompressed_size, this->buffer, this->uncompressed_size);

    if(this->bufsize > 0)
    {
        // compression succeeded
        free(buffer);
        this->is_compressed = 1;
    }
    else
    {
        // compression resulted in larger data (fairly common for tiny values), so
        // return uncompressed data.
        free(this->buffer);
        this->buffer = buffer;
        this->bufsize = this->uncompressed_size;
        this->is_compressed = 0;
    }
}

SerializedBitarray::SerializedBitarray(const char * key, uint8_t * buffer, int64_t bufsize, int64_t uncompressed_size, uint8_t is_compressed)
    : b(NULL), key(key), buffer(buffer), bufsize(bufsize), uncompressed_size(uncompressed_size), is_compressed(is_compressed)
{
    if(!buffer)
        return;

    if(this->is_compressed)
    {
        uint8_t * tmp_buffer = (uint8_t *)malloc(this->uncompressed_size);
        assert(lzf_decompress(this->buffer, this->bufsize, tmp_buffer, this->uncompressed_size) == this->uncompressed_size);
        free(this->buffer);
        this->buffer = tmp_buffer;
    }
    else
        assert(this->uncompressed_size == this->bufsize);

    this->b = new Bitarray(key, -1);
    this->b->size   = ((int64_t *)this->buffer)[0];
    this->b->offset = ((int64_t *)this->buffer)[1];
    this->b->array = NULL;
    if(this->b->size)
    {
        this->b->array = (uint8_t *)malloc(b->size);
        memcpy(this->b->array, this->buffer + sizeof(int64_t)*2, this->b->size);
    }
}

// XXX: g_file_set_contents writes to a temp file called
// "key.RANDOM-GIBBERISH", which theoretically could be loaded accidentally if
// someone requested that exact key at the right moment.  a more robust file
// writing mechanism should eventually be used.
void Bitarray::save_frozen(const char * key, SerializedBitarray& ser)
{
    int64_t file_size = sizeof(uint8_t) // is_compressed boolean
                      + sizeof(int64_t) // uncompressed_size
                      + ser.bufsize;

    uint8_t * contents = (uint8_t *)malloc(file_size);

    memcpy(contents,                   &ser.is_compressed,     sizeof(uint8_t));
    memcpy(contents + sizeof(uint8_t), &ser.uncompressed_size, sizeof(int64_t));
    memcpy(contents + sizeof(uint8_t)
                    + sizeof(int64_t), ser.buffer, ser.bufsize);

    char * filename = g_strdup_printf("data/%s", key);

    g_file_set_contents(filename, (char *)contents, file_size, NULL); // XXX error handling

    g_free(filename);
    free(contents);
}

SerializedBitarray Bitarray::load_frozen(const char * key)
{
    char * filename = g_strdup_printf("data/%s", key);
    int64_t file_size;
    uint8_t * contents;

    uint8_t * buffer = NULL;
    uint8_t is_compressed = 0;
    uint64_t bufsize = 0;
    uint64_t uncompressed_size = 0;

    gboolean got_contents = g_file_get_contents(filename, (char **)&contents, (gsize *)&file_size, NULL);
    g_free(filename);

    if(got_contents)
    {
        is_compressed = contents[0];
        uncompressed_size = ((int64_t *)(contents + sizeof(uint8_t)))[0];
        bufsize = file_size - (sizeof(char) + sizeof(int64_t));
        buffer = (uint8_t *)malloc(bufsize);
        memcpy(buffer, contents + sizeof(uint8_t) + sizeof(int64_t), bufsize);
        g_free(contents);
    }

    return SerializedBitarray(key, buffer, bufsize, uncompressed_size, is_compressed);
}

void Bitarray::save_to_disk()
{
    SerializedBitarray ser(this);
    Bitarray::save_frozen(this->key, ser);
}

Bitarray * Bitarray::find_on_disk(const char * key)
{
    SerializedBitarray ser = Bitarray::load_frozen(key);
    return ser.b;
}

void Bitarray::grow_up(int64_t size)
{
    uint8_t * new_array = (uint8_t *)calloc(size, 1);
    memcpy(new_array, this->array, this->size);
    free(this->array);
    this->array = new_array;
    this->size = size;
}

void Bitarray::grow_down(int64_t new_size)
{
    // move the starting point back
    int64_t grow_by = new_size - this->size;
    int64_t new_begin = this->offset - grow_by;

    // if we went too far, past 0, then just reset the start to 0.
    if(new_begin < 0)
    {
        new_begin = 0;
        grow_by = this->offset;
        new_size = this->offset + this->size;
    }

    uint8_t * new_array = (uint8_t *)calloc(new_size, 1);
    memcpy(new_array + grow_by, this->array, this->size);
    free(this->array);

    this->array = new_array;
    this->size = new_size;
    this->offset = new_begin;
}

void Bitarray::adjust_size_to_reach(int64_t new_index)
{
    if(BYTE_OFFSET(new_index) >= this->offset + this->size)
    {
        int64_t min_increase_needed = BYTE_OFFSET(new_index) - (this->offset + this->size - 1);
        int64_t new_size = MAX(this->size + min_increase_needed, this->size * 2);
        this->grow_up(new_size);
    }
    if(BYTE_OFFSET(new_index) - this->offset < 0)
    {
        int64_t min_increase_needed = this->offset - BYTE_OFFSET(new_index);
        int64_t new_size = MAX(this->size + min_increase_needed, this->size * 2);
        this->grow_down(new_size);
    }
}

// public bitarray api

int Bitarray::get_bit(int64_t index)
{
    this->last_access = _get_time();

    if(!this->array || this->offset + this->size < index/8+1 || index/8 < this->offset)
        return 0;

    return BYTE_SLOT(this, index) & MASK(index) ? 1 : 0;
}

void Bitarray::set_bit(int64_t index)
{
    this->last_access = _get_time();

    if(!this->array)
        this->init_data(index);

    this->adjust_size_to_reach(index);

    assert(index >= 0);
    assert(this->offset >= 0);
    if(BYTE_OFFSET(index) - this->offset < 0)
    {
        abort();
    }
    assert(BYTE_OFFSET(index) - this->offset <  this->size);
    BYTE_SLOT(this, index) |= MASK(index);
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
    this->hash.set_deleted_key("");

    this->need_disk_write.set_deleted_key(NULL);
}

Bitbox::~Bitbox()
{
    Bitbox::hash_t::iterator it = this->hash.begin();
    for(; it != this->hash.end(); ++it)
    {
        delete it->second;
        this->hash.erase(it);
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

Bitarray * Bitbox::find_array_in_memory(const char * key)
{
    Bitbox::hash_t::iterator it = this->hash.find(key);
    return it == this->hash.end() ? NULL : it->second;
}

void Bitbox::add_array_to_hash(Bitarray * b)
{
    this->hash[b->key] = b;
    this->update_key_in_lru(b->key, 0, b->last_access);
}

Bitarray * Bitbox::find_array(const char * key)
{
    Bitarray * b = this->find_array_in_memory(key);
    if(b)
        return b;

    b = Bitarray::find_on_disk(key);
    if(b)
        this->add_array_to_hash(b);

    return b;
}

Bitarray * Bitbox::find_or_create_array(const char * key)
{
    Bitarray * b = Bitbox::find_array(key);
    if(!b)
    {
        b = new Bitarray(key, -1);
        this->add_array_to_hash(b);
    }
    return b;
}

void Bitbox::downsize_if_angry()
{
    // ok, that's it.  even if really busy, bring memory usage down below the
    // "angry" limit before proceeding.  we'll never be very far past the
    // limit, so the while loop isn't as scary as it might look.
    while(this->hash.size() >= BITBOX_ITEM_PEAK_LIMIT && this->lru.size())
        this->downsize_single_step(BITBOX_ITEM_PEAK_LIMIT);
}

void Bitbox::set_bit_nolookup(Bitarray * b, int64_t bit)
{
    assert(b);
    int64_t old_timestamp = b->last_access;

    b->set_bit(bit);

    this->update_key_in_lru(b->key, old_timestamp, b->last_access);

    this->need_disk_write.insert(b);
}

void Bitbox::set_bit(const char * key, int64_t bit)
{
    Bitarray * b = Bitbox::find_or_create_array(key);
    this->set_bit_nolookup(b, bit);
    this->downsize_if_angry();
}

int Bitbox::get_bit(const char * key, int64_t bit)
{
    Bitarray * b = this->find_array(key);

    if(!b)
        return 0;

    int64_t old_timestamp = b->last_access;
    int retval = b->get_bit(bit);

    this->update_key_in_lru(key, old_timestamp, b->last_access);

    return retval;
}

void Bitbox::banish_oldest_item_to_disk()
{
    assert(this->hash.size() == this->lru.size());
    Bitbox::lru_map_t::iterator it = this->lru.begin();

    if(it == this->lru.end())
        return;

    char * key = it->second;
    this->lru.erase(it);

    Bitarray * b = this->find_array_in_memory(key);
    assert(b);
    b->save_to_disk();
    this->hash.erase(key);
    this->need_disk_write.erase(b);

    delete b;
    free(key);
}

void Bitbox::downsize_single_step(int64_t item_limit)
{
    //DEBUG("************ box too big? %d\n", g_hash_table_size(box->hash) >= item_limit);
    //DEBUG("************ lru size? %d\n", box->lru.size());
    //DEBUG("************ hash size? %d\n", g_hash_table_size(box->hash));
    if(this->hash.size() >= (Bitbox::hash_t::size_type)item_limit)
        this->banish_oldest_item_to_disk();
}

void Bitbox::write_one_to_disk()
{
    Bitbox::need_disk_write_set_t::iterator it = this->need_disk_write.begin();
    if(it != this->need_disk_write.end())
    {
        (*it)->save_to_disk();
        this->need_disk_write.erase(it);
        DEBUG("wrote one to disk. %lu left\n", this->need_disk_write.size());
    }
}

bool Bitbox::run_maintenance_step()
{
    this->downsize_single_step(BITBOX_ITEM_LIMIT);
    this->write_one_to_disk();
    return this->hash.size() >= BITBOX_ITEM_LIMIT || !this->need_disk_write.empty();
}

void Bitbox::shutdown()
{
    while(!this->need_disk_write.empty())
        this->write_one_to_disk();
}
