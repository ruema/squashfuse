#include "fs.h"
sqfs_err crypt_init_key(sqfs *fs, const char *key);
void crypt_decrypt(sqfs *fs, void *buf, size_t count, sqfs_off_t off);
