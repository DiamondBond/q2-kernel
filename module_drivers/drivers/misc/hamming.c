#include <linux/kernel.h>

#include "hamming.h"

static void bitcpy(const unsigned int *s, unsigned int *d,
	const int ss, const int ds, int bsz)
{
	int ss_int = ss / 32;
	int ss_bit = ss % 32;
	int ds_int = ds / 32;
	int ds_bit = ds % 32;

#define MMIN(a, b) (a) > (b) ? (b) : (a)
	while (bsz != 0) {
		unsigned int src, dst, bmsk;
		int min = MMIN(32 - ss_bit, 32 - ds_bit);
		min = MMIN(min, bsz);
		bmsk = 0xffffffff >> (32 - min);
		src = s[ss_int] >> ss_bit;
		src &= bmsk;

		dst = d[ds_int];
		dst &= ~(bmsk << ds_bit);
		dst |= src << ds_bit;

		d[ds_int] = dst;
		ds_bit += min;
		if (ds_bit >= 32) {
			ds_int++;
			ds_bit = 0;
		}
		ss_bit += min;
		if (ss_bit >= 32) {
			ss_int++;
			ss_bit = 0;
		}
		bsz -= min;
	}
}

static int bit32_cal_xor(const unsigned int *s, int bitsz, unsigned int *d)
{
	int j;

	if (bitsz > 64) {
		unsigned int bit32 = s[0];
		for (j = 32; j < bitsz / 32 * 32; j += 32)
			bit32 ^= s[j / 32];
		d[0] = bit32;
		if (bitsz % 32)
			d[1] = s[j / 32];

		return 32 + (bitsz % 32);
	} else if (bitsz > 32) {
		d[0] = s[0];
		d[1] = s[1];
	} else {
		d[0] = s[0];
	}
	return bitsz;
}

static int bit_index_cal_xor(const unsigned int *d, int bitsz, int index,
	unsigned int xor)
{
	int sz = bitsz;
	int gap = (1 << index);
	int j, i;
	int pre_g = 0;
	unsigned int bit32;

	j = gap - 1;
	bit32 = d[pre_g];
	while (j < sz) {
		for (i = 0; i < (1 << index); i++) {
			int g = (j + i) / 32;
			int b = (j + i) % 32;
			if (g != pre_g) {
				pre_g = g;
				bit32 = d[pre_g];
			}
			xor ^= (bit32 >> b) & 1;
		}
		gap += 2 * (1 << index);
		j += 2 * (1 << index);
	}
	return xor;
}

static unsigned int get_highest_bit(const unsigned int *s, int pos)
{
	unsigned int xor;
	unsigned int bit32 = s[pos / 32];

	xor = bit32 >> 31;
	return xor;
}

static unsigned int bit_cal_xor(const unsigned int *s, int bitsz, int pos,
	unsigned int xor)
{
	unsigned int bit32 = s[pos / 32];
	int bitpos = pos % 32;
	int i;

	if (bitpos) {
		int endpos = bitsz > (32 - bitpos) ? 32 : bitsz + bitpos;
		int sz = endpos - bitpos;
		for (i = bitpos; i < endpos; i++)
			xor ^= (bit32 >> i) & 1;
		bitsz -= sz;
		pos += sz;
	}
	if (bitsz > 32) {
		for (i = 32; i < bitsz / 32 * 32; i += 32)
			bit32 ^= s[(pos + i) / 32];
		pos += i;
		for (i = 0; i < 32; i++)
			xor ^= (bit32 >> i) & 1;
		bitsz = bitsz % 32;
	}
	if (bitsz)
		bit32 = s[pos / 32];

	for (i = 0; i < bitsz; i++)
		xor ^= (bit32 >> i) & 1;
	return xor;
}

static int cal_xor_ge32(const unsigned int *d, int dsz, int index, int xor)
{
	int i;
	int step = 1 << index;
	int min;

	for (i = step - 1; i < dsz; i += step * 2) {
		xor ^= get_highest_bit(d, i);
		min = dsz - i - 1 > step - 1 ? step - 1 : dsz - i - 1;
		if (min > 0)
			xor = bit_cal_xor(d, min, i + 1, xor);
	}
	return xor;
}

int checkbit(unsigned int *s, unsigned int *d, int ss, int ds, int bsz)
{
	int sg32, sb32, dg32, db32;

	while (bsz > 0) {
		sg32 = ss / 32;
		sb32 = ss % 32;
		dg32 = ds / 32;
		db32 = ds % 32;
		if (((s[sg32] >> sb32) & 1) != ((d[dg32] >> db32) & 1))
			break;
		ss++;
		ds++;
		bsz--;
	}
	return bsz;
}

int dump(unsigned int *s, int st, int en)
{
	int i;

	for (i = st; i < en; i++) {
		if ((i - st) % 32 == 0) {
			printk("\n");
		} else if ((i - st) % 8 == 0) {
			printk(" ");
		}

		if (s[i / 32] & (1 << (i % 32)))
			printk("1");
		else
			printk("0");
	}
	printk("\n");
	return 0;
}

int dumphex(unsigned int *s, int sz)
{
	int i;

	for (i = 0; i < sz; i++)
		printk("%08x\n", s[i]);
	return 0;
}

int cal_k(int bits)
{
	int k = 0;
	unsigned int cur = 1;

	while (cur - 1 < bits + k) {
		cur <<= 1;
		k++;
	}
	return k;
}

int encode(unsigned int *s, int bits, unsigned int *d)
{
	int k = cal_k(bits);
	int i, j, p;
	int bitsz;
	unsigned int xor = 0;

	i = 0;
	p = 0;
	j = 0;
	bits += k;

	while (j < bits) {
		int curindex, bsz;
		curindex = (1 << p);
		if (j + 1 != curindex) {
			bsz = (curindex - j - 1);
			if ((bits - j) < bsz)
				bsz = bits - j;
			bitcpy(s, d, i, j, bsz);
			i += bsz;
			j += bsz;
		} else {
			unsigned int dst;
			unsigned int msk = 1 << (j % 32);
			dst = d[j / 32];
			dst &= ~msk;
			d[j / 32] = dst;
			j++;
			p++;
		}
	}

	bitsz = bit32_cal_xor(d, bits, s);

	i = 0;
	while ((1 << i) < bits) {
		if (i < 5)
			xor = bit_index_cal_xor(s, bitsz, i, 0);
		else
			xor = cal_xor_ge32(d, bits, i, 0);
		p = (1 << i) - 1;
		i++;
		d[p / 32] &= ~(1 << (p % 32));
		d[p / 32] |= (xor << (p % 32));
	}
	return bits;
}

int decode(unsigned int *s, int bits, unsigned int *d)
{
	int i, p, j;
	int xor = 0;
	int errbit = 0;
	int bitsz;

	bitsz = bit32_cal_xor(s, bits, d);

	i = 0;
	while ((1 << i) < bits) {
		if (i < 5)
			xor = bit_index_cal_xor(d, bitsz, i, 0);
		else
			xor = cal_xor_ge32(s, bits, i, 0);
		errbit |= xor << i;
		i++;
	}

	if (errbit > 0) {
		errbit = errbit - 1;
		xor = 1 << (errbit % 32);
		s[errbit / 32] ^= xor;
		s[errbit / 32] ^= 0;
	}

	i = 0;
	p = 0;
	j = 0;
	while (j < bits) {
		int curindex, bsz;
		curindex = (1 << p);
		if (j + 1 != curindex) {
			bsz = (curindex - j - 1);
			if ((bits - j) < bsz)
				bsz = bits - j;
			bitcpy(s, d, j, i, bsz);
			i += bsz;
			j += bsz;
		} else {
			j++;
			p++;
		}
	}

	return i;
}
