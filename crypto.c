#include "fs.h"
#include "aes.h"
#include "crypto.h"
#include <string.h>
#include <stdlib.h>

struct crypto {
        struct AES_ctx ctx;
        unsigned char nonce[AES_BLOCKLEN];
};

const unsigned char b64_dec[] = {
        97, 99, 99, 99, 99, 99, 99, 99, 99, 98, 98, 99, 99, 98, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
        98, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 62, 99, 99, 99, 63,
        52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 99, 99, 99, 96, 99, 99,
        99, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 99, 99, 99, 99, 99,
        99, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
        41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};



unsigned int b64_decode_length(const char* in, int length) {
        int size = 0;
        int i;
        for(i=0; i < length; i++) {
                unsigned char c = b64_dec[(unsigned char)in[i]];
                if(c<64) size += 1;
                else if(c == 98) {}
                else if(c<99) break;
                else {
                	return -1;
                }
        }
        int bytes = size / 4 * 3;
        int r = size & 3;
        if(r == 0) {}
        else if(r == 1) return -1;
        else bytes += r - 1;
        return bytes;
}

void b64_decode(const char* in, int length, unsigned char *result) {
        int j = 0, k = 0;
        int i, r = 0;
        for(i=0; i < length ; i++) {
                unsigned char c = b64_dec[(unsigned char)in[i]];
                if(c<64) {
                        j = (j << 6) | c;
                        if(r < 3) r += 1;
                        else {
                                result[k + 0] = j >> 16;
                                result[k + 1] = (j >> 8) & 255;
                                result[k + 2] = j & 255;
                                k += 3;
                                j = r = 0;
                        }
                } else if(c != 98) break;
        }
        if(r==2) {
                result[k + 0] = j >> 4;
        } else if (r==3) {
                result[k + 0] = j >> 10;
                result[k + 1] = (j >> 2) & 255;
        }
}

sqfs_err crypt_init_key(sqfs *fs, const char *key) {
        char *chr = strchr(key, ',');
        if(strncmp("AES_256_CTR", key, chr-key) == 0) {
                char crypt_key[32];
                int length;
                char *symkey = chr + 1;
                char *nonce = strchr(symkey, ',') + 1;
                chr = strchr(nonce, 0);
                length = b64_decode_length(symkey, nonce - symkey - 1);
                if (length != 32) return SQFS_ERR;
                length = b64_decode_length(nonce, chr - nonce);
                if (length != 16) return SQFS_ERR;
                struct crypto *crypto = malloc(sizeof(struct crypto));
                b64_decode(symkey, nonce - symkey - 1, crypt_key);
                AES_init_ctx(&crypto->ctx, crypt_key);
                b64_decode(nonce, chr - nonce, crypto->nonce);
                fs->crypto = crypto;
        } else {
                return SQFS_ERR;
        }
        return 0;
}

void crypt_decrypt(sqfs *fs, void *buf, size_t count, sqfs_off_t off) {
        /* Increment Iv and handle overflow */
        struct crypto *crypto = (struct crypto*)fs->crypto;
        int bi;
        unsigned long long b = off >> 4;
        for (bi = (AES_BLOCKLEN - 1); bi >= 0; --bi)
        {
            b += crypto->nonce[bi];
            crypto->ctx.Iv[bi] = (unsigned char)b;
            b >>= 8;
        }
        AES_CTR_xcrypt_buffer(&crypto->ctx, buf, count, off & 15);
}
