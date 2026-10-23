#include <linux/kernel.h>
#include <linux/mpi.h>
#include <linux/string.h>

#include "rsa.h"

static void bn_to_be(u8 *out, size_t out_len, const bn_t *in, size_t in_digits)
{
	size_t i, j = out_len;

	for (i = 0; i < in_digits && j > 0; i++) {
		bn_t t = in[i];
		u32 u;

		for (u = 0; j > 0 && u < 32; u += 8)
			out[--j] = (u8)(t >> u);
	}

	while (j > 0)
		out[--j] = 0;
}

void rsa_public_decrypt(bn_t *out, bn_t *in, bn_t in_len, bn_t * n, bn_t * e, bn_t key_len)
{
	MPI c = NULL, m = NULL, exp = NULL, mod = NULL;
	u8 in_be[RSA_MAX_MODULUS_LEN];
	u8 n_be[RSA_MAX_MODULUS_LEN];
	u8 e_be[sizeof(bn_t)];
	u8 out_tmp[RSA_MAX_MODULUS_LEN];
	u8 out_be[RSA_MAX_MODULUS_LEN];
	unsigned out_bytes = 0;
	size_t in_bytes;
	size_t mod_bytes;
	int ret = 0;
	int sign = 0;

	if (!out || !in || !n || !e || !in_len || !key_len)
		return;

	if (key_len > (RSA_MAX_MODULUS_LEN / sizeof(bn_t)) ||
	    in_len > (RSA_MAX_MODULUS_LEN / sizeof(bn_t))) {
		pr_err("sc-v4: RSA key/input length too large\n");
		return;
	}

	mod_bytes = key_len * sizeof(bn_t);
	in_bytes = in_len * sizeof(bn_t);

	if (in_bytes > mod_bytes) {
		pr_err("sc-v4: RSA input longer than modulus\n");
		ret = -EINVAL;
		goto out_err;
	}

	bn_to_be(in_be, in_bytes, in, in_len);
	bn_to_be(n_be, mod_bytes, n, key_len);
	bn_to_be(e_be, sizeof(bn_t), e, 1);

	c = mpi_read_raw_data(in_be, in_bytes);
	mod = mpi_read_raw_data(n_be, mod_bytes);
	exp = mpi_read_raw_data(e_be, sizeof(bn_t));
	if (!c || !mod || !exp) {
		ret = -ENOMEM;
		pr_err("sc-v4: RSA MPI alloc failed\n");
		goto out_err;
	}

	if (mpi_cmp_ui(mod, 0) <= 0) {
		ret = -EINVAL;
		pr_err("sc-v4: RSA modulus invalid\n");
		goto out_err;
	}

	if (mpi_cmp(c, mod) >= 0) {
		ret = -EINVAL;
		pr_err("sc-v4: RSA input >= modulus\n");
		goto out_err;
	}

	m = mpi_alloc(0);
	if (!m) {
		ret = -ENOMEM;
		goto out_err;
	}

	ret = mpi_powm(m, c, exp, mod);
	if (ret) {
		pr_err("sc-v4: rsa public decrypt failed: %d\n", ret);
		goto out_err;
	}

	ret = mpi_read_buffer(m, out_tmp, sizeof(out_tmp), &out_bytes, &sign);
	if (ret) {
		pr_err("sc-v4: rsa output read failed: %d\n", ret);
		goto out_err;
	}

	if (out_bytes > mod_bytes) {
		pr_err("sc-v4: rsa output overflow: %u > %zu\n", out_bytes, mod_bytes);
		ret = -EOVERFLOW;
		goto out_err;
	}

	memset(out_be, 0, mod_bytes);
	memcpy(out_be + (mod_bytes - out_bytes), out_tmp, out_bytes);
	memcpy(out, out_be, mod_bytes);

	if (sign < 0)
		ret = -EBADMSG;

out_err:
	if (ret)
		memset(out, 0, mod_bytes);
	mpi_free(m);
	mpi_free(c);
	mpi_free(exp);
	mpi_free(mod);
}
void (*f_rsa_public_decrypt)(bn_t*, bn_t*, bn_t, bn_t*, bn_t*, bn_t) = rsa_public_decrypt;
