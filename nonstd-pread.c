/*
 * Copyright (c) 2012 Dave Vasilevsky <dave@vasilevsky.ca>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <string.h>
#include "config.h"
#include "fs.h"

#ifdef _WIN32
	#include "win32.h"

	ssize_t sqfs_pread_raw(HANDLE file, void *buf, size_t count, sqfs_off_t off) {
		DWORD bread;
		OVERLAPPED ov = { 0 };
		ov.Offset = (DWORD)off;
		ov.OffsetHigh = (DWORD)(off >> 32);

		if (ReadFile(file, buf, count, &bread, &ov) == FALSE)
			return -1;
		return bread;
	}
#else
	#define SQFEATURE NONSTD_PREAD_DEF
	#include "nonstd-internal.h"

	#include <unistd.h>

	#include "common.h"

	ssize_t sqfs_pread_raw(sqfs_fd_t fd, void *buf, size_t count, sqfs_off_t off) {
		return pread(fd, buf, count, off);
	}
#endif

#ifdef MINILUKS

#define BLOCK_SIZE 512
#define BLOCK_MASK (~(BLOCK_SIZE - 1))
int luks_decrypt(void* luks_file, unsigned char* data, int size, int offset);

ssize_t read_block(sqfs *fs, void *buf, size_t count, sqfs_off_t off) {
	static sqfs_off_t current = -1;
	static unsigned char cache[BLOCK_SIZE];
	sqfs_off_t block_nr = off & BLOCK_MASK;
	size_t index = off & (BLOCK_SIZE - 1);
	if (index ==0 && count >= BLOCK_SIZE) {
		count &= BLOCK_MASK;
		luks_decrypt(fs->luks, buf, count, block_nr);
	} else {
		if(current != block_nr) {
			current = block_nr;
			luks_decrypt(fs->luks, cache, BLOCK_SIZE, block_nr);
		}
		if (count > BLOCK_SIZE - index) {
			count = BLOCK_SIZE - index;
		}
		memcpy(buf, cache+index, count);
	}
	return count;
}
#endif

ssize_t sqfs_pread(sqfs *fs, void *buf, size_t count, sqfs_off_t off) {
	if(fs->luks == NULL) {
		return sqfs_pread_raw(fs->fd, buf, count, off + fs->offset);
	} else {
#ifdef MINILUKS
		size_t total = count;
		while(count > 0) {
			size_t cnt = read_block(fs, buf, count, off);
			if(cnt==0) break;
			buf += cnt;
			off += cnt;
			count -= cnt;
		}
		return total - count;
#else
		return -1;
#endif
	}
}
