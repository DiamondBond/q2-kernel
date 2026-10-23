#ifndef _MISC_HAMMING_H_
#define _MISC_HAMMING_H_

int checkbit(unsigned int *s, unsigned int *d, int ss, int ds, int bsz);
int dump(unsigned int *s, int st, int en);
int dumphex(unsigned int *s, int sz);
int cal_k(int bits);
int encode(unsigned int *s, int bits, unsigned int *d);
int decode(unsigned int *s, int bits, unsigned int *d);

#endif
