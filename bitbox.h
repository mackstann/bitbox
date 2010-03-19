#ifndef __BITBOX_H__
#define __BITBOX_H__

#include <sys/time.h>
#include <glib.h>

typedef struct {
    // this should probably be opaque to the outside world but i'm not sure of
    // the C-foo to do that correctly.
    GHashTable * hash;
} bitbox_t;

bitbox_t * bitbox_new(void);
void bitbox_free(bitbox_t * box);
void bitbox_set_bit(bitbox_t * box, const char * key, int bit);
int bitbox_get_bit(bitbox_t * box, const char * key, int bit);

#endif
