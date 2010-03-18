#include <stdlib.h>
#include <stdio.h>

int main(void)
{
    unsigned char * a = calloc(1, (int)(1000000/8.0 + 1));
    printf("num bytes:        %d\n", (int)(1000000/8.0 + 1));

    int id = 1000001;
    int full_bit_offset = id - 1;

    int byte_offset = full_bit_offset / 8;
    int bit_offset = full_bit_offset % 8;

    a[byte_offset] |= (1 << bit_offset);




    printf("used byte offset: %d\n", (int)byte_offset);
    printf("used bit  offset: %d\n", (int)bit_offset);

    printf("found byte value: %d\n", (int)a[byte_offset]);
    printf("found bit  value: %d\n", (int)a[byte_offset] & (1 << bit_offset));

}
