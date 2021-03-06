/*
 * Common values for AES algorithms
 */

#ifndef _CRYPTO_AES_H
#define _CRYPTO_AES_H

#include <linux/types.h>
#include <linux/crypto.h>

#define AES_MIN_KEY_SIZE	16
#define AES_MAX_KEY_SIZE	32
#define AES_KEYSIZE_128		16
#define AES_KEYSIZE_192		24
#define AES_KEYSIZE_256		32
#define AES_BLOCK_SIZE		16

struct crypto_aes_ctx {
	u32 key_length;
	u32 key_enc[60];
	u32 key_dec[60];
};

extern u32 crypto_ft_tab[4][256];
extern u32 crypto_fl_tab[4][256];
extern u32 crypto_it_tab[4][256];
extern u32 crypto_il_tab[4][256];

int crypto_aes_set_key(struct crypto_tfm *tfm, const u8 *in_key,
		unsigned int key_len);
#endif
