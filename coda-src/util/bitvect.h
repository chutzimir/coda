


/* this code owes several ideas to Hanson's C Interfaces and Implementations, 
 * Addison-Wesley, 1997 
 */

#ifndef BITVECT_INCLUDED
#define BITVECT_INCLUDED

typedef struct Bitv_s * Bitv;

Bitv Bitv_new(int length);
void Bitv_free(Bitv *b);
int Bitv_length(Bitv b);
int Bitv_count(Bitv b); /* how many bits are set to 1 */
int Bitv_put(Bitv b, int n, int bit); /* set n to bit, return previous value */
void Bitv_clear(Bitv b, int loc);
void Bitv_set(Bitv b, int loc);
int Bitv_getfree(Bitv b); /* get first 0 index and set it to 1 */
void Bitv_print(Bitv b, FILE *fd);


#endif
