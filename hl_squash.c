/*
 * Copyright (c) 2014 Dave Vasilevsky <dave@vasilevsky.ca>
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
#include "squashfuse.h"
#include "fuseprivate.h"
#include "hashset.h"
#include "stat.h"
#include "nonstd.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef struct sqfs_hl sqfs_hl;
struct sqfs_hl {
	sqfs fs;
	sqfs_inode root;
};

static sqfs_err sqfs_hl_lookup(sqfs_inode *inode, const char *path) {
	int found;
	sqfs_hl *hl = fuse_get_context()->private_data;
	while(hl->fs.fd) {
		*inode = hl->root; /* copy */
		sqfs_err err = sqfs_lookup_path(inode->fs, inode, path, &found);
		if (err)
			return err;
		if (found & FOUND)
			return SQFS_OK;
		if (found & HIDDEN)
			break;
		hl++;
	}
	return SQFS_ERR;
}


static void sqfs_hl_op_destroy(void *user_data) {
	sqfs_hl *hl = (sqfs_hl*)user_data;
	while(hl->fs.fd) {
		sqfs_destroy(&hl->fs);
		hl++;
	}
	free(user_data);
}

static void *sqfs_hl_op_init(struct fuse_conn_info *conn
#if FUSE_USE_VERSION >= 30
			     ,struct fuse_config *cfg
#endif
			     ) {
	return fuse_get_context()->private_data;
}

static int sqfs_hl_op_getattr(const char *path, struct stat *st
#if FUSE_USE_VERSION >= 30
			      , struct fuse_file_info *fi
#endif
			      ) {
	sqfs_inode inode;
	if (sqfs_hl_lookup(&inode, path))
		return -ENOENT;
	
	if (sqfs_stat(inode.fs, &inode, st))
		return -ENOENT;
	
	return 0;
}

static int sqfs_hl_op_opendir(const char *path, struct fuse_file_info *fi) {
	sqfs_inode *inodes, *inode;
	int found;
	int count=0;
	sqfs_hl *hl = fuse_get_context()->private_data;
	while(hl->fs.fd) {
		hl++;
		count++;
	}
	inodes = malloc((1 + count) * sizeof(*inodes));
	if (!inodes)
		return -ENOMEM;

	hl = fuse_get_context()->private_data;
	inode = inodes;
	while(hl->fs.fd) {
		*inode = hl->root; /* copy */
		sqfs_err err = sqfs_lookup_path(inode->fs, inode, path, &found);
		if (err) {
			free(inodes);
			return -ENOENT;
		}
		if (found & FOUND) {
			if (!S_ISDIR(inode->base.mode)) {
				if (inode == inodes) {
					free(inodes);
					return -ENOTDIR;
				}
				break;
			}
			inode++;
		}
		if (found & HIDDEN)
			break;
		hl++;
	}
	if (inode == inodes) {
		free(inodes);
		return -ENOENT;
	}
	inode->fs = NULL;	
	fi->fh = (intptr_t)inodes;
	return 0;
}

static int sqfs_hl_op_releasedir(const char *path,
		struct fuse_file_info *fi) {
	free((sqfs_inode*)(intptr_t)fi->fh);
	fi->fh = 0;
	return 0;
}

static int sqfs_hl_op_readdir(const char *path, void *buf,
		fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi
#if FUSE_USE_VERSION >= 30
	,enum fuse_readdir_flags flags
#endif
	) {
	sqfs_err err;
	sqfs_inode *inode;
	sqfs_dir dir;
	sqfs_name namebuf;
	sqfs_dir_entry entry;
	struct stat st;
	memset(&st, 0, sizeof(st));

	inode = (sqfs_inode*)(intptr_t)fi->fh;
	if(inode[1].fs == 0) {
		/* only one layer */
		sqfs *fs = inode->fs;
			
		if (sqfs_dir_open(fs, inode, &dir, offset))
			return -EINVAL;
		
		sqfs_dentry_init(&entry, namebuf);
		while (sqfs_dir_next(fs, &dir, &entry, &err)) {
			const char* name = sqfs_dentry_name(&entry);
			if (strncmp(".wh.", name, 4) != 0) {
				sqfs_off_t doff = sqfs_dentry_next_offset(&entry);
				st.st_mode = sqfs_dentry_mode(&entry);
				if (filler(buf, sqfs_dentry_name(&entry), &st, doff
#if FUSE_USE_VERSION >= 30
				   , 0
#endif
				   )) {
					return 0;
				}
			}
		}
		if (err)
			return -EIO;
		return 0;
	}

	hashset m;
	hashset_init(&m);
	int level = 1;
	while(inode->fs) {
		if (sqfs_dir_open(inode->fs, inode, &dir, 0)) {
			hashset_free(&m);
			return -EINVAL;
		}
		
		sqfs_dentry_init(&entry, namebuf);
		while (sqfs_dir_next(inode->fs, &dir, &entry, &err)) {
			const char* name = sqfs_dentry_name(&entry);
			if (strncmp(".wh.", name, 4) != 0) {
				int r = hashset_add(&m, name, level);
				if(r < 0) goto error;
				if(r == level) {
	  			  st.st_mode = sqfs_dentry_mode(&entry);
				  /* sqfs_off_t coff = sqfs_dentry_offset(&entry);
				  sqfs_off_t doff = sqfs_dentry_next_offset(&entry);
				  off_t new_offset = current_offset + doff - coff;
	  			  if(offset<=current_offset && filler(buf, name, &st, new_offset)) {
					goto finish;
				  }
				  current_offset = new_offset;*/
	  			  if(filler(buf, name, &st, 0
#if FUSE_USE_VERSION >= 30
				   , 0
#endif
	  			  	))
					goto finish;
				}
			} else if(strcmp(".wh..wh..opq", name) == 0) {
				// this is the last layer
				inode[1].fs = NULL;
			} else {
				// add this name to the seen names
				// e.g. hide this name
				int r = hashset_add(&m, name + 4, level);
				if(r<0) goto error;
			}
		}
		if (err) goto error;
		inode++;
		level++;
	}
finish:
	hashset_free(&m);
	return 0;
error:
	hashset_free(&m);
	return -EIO;
}

static int sqfs_hl_op_open(const char *path, struct fuse_file_info *fi) {
	sqfs_inode *inode;
	
	if (fi->flags & (O_WRONLY | O_RDWR))
		return -EROFS;
	
	inode = malloc(sizeof(*inode));
	if (!inode)
		return -ENOMEM;
	
	if (sqfs_hl_lookup(inode, path)) {
		free(inode);
		return -ENOENT;
	}
	
	if (!S_ISREG(inode->base.mode)) {
		free(inode);
		return -EISDIR;
	}
	
	fi->fh = (intptr_t)inode;
	fi->keep_cache = 1;
	return 0;
}

static int sqfs_hl_op_create(const char* unused_path, mode_t unused_mode,
		struct fuse_file_info *unused_fi) {
	return -EROFS;
}
static int sqfs_hl_op_release(const char *path, struct fuse_file_info *fi) {
	free((sqfs_inode*)(intptr_t)fi->fh);
	fi->fh = 0;
	return 0;
}

static int sqfs_hl_op_read(const char *path, char *buf, size_t size,
		off_t off, struct fuse_file_info *fi) {
	sqfs_inode *inode = (sqfs_inode*)(intptr_t)fi->fh;
	off_t osize = size;
	if (sqfs_read_range(inode->fs, inode, off, &osize, buf))
		return -EIO;
	return osize;
}

static int sqfs_hl_op_readlink(const char *path, char *buf, size_t size) {
	sqfs_inode inode;
	if (sqfs_hl_lookup(&inode, path))
		return -ENOENT;
	
	if (!S_ISLNK(inode.base.mode)) {
		return -EINVAL;
	} else if (sqfs_readlink(inode.fs, &inode, buf, &size)) {
		return -EIO;
	}	
	return 0;
}

static int sqfs_hl_op_listxattr(const char *path, char *buf, size_t size) {
	sqfs_inode inode;
	int ferr;
	
	if (sqfs_hl_lookup(&inode, path))
		return -ENOENT;

	ferr = sqfs_listxattr(inode.fs, &inode, buf, &size);
	if (ferr)
		return -ferr;
	return size;
}

static int sqfs_hl_op_getxattr(const char *path, const char *name,
		char *value, size_t size
#ifdef FUSE_XATTR_POSITION
		, uint32_t position
#endif
		) {
	sqfs_inode inode;
	size_t real = size;

#ifdef FUSE_XATTR_POSITION
	if (position != 0) /* We don't support resource forks */
		return -EINVAL;
#endif

	if (sqfs_hl_lookup(&inode, path))
		return -ENOENT;
	
	if ((sqfs_xattr_lookup(inode.fs, &inode, name, value, &real)))
		return -EIO;
	if (real == 0)
		return -sqfs_enoattr();
	if (size != 0 && size < real)
		return -ERANGE;
	return real;
}

static int sqfs_hl_op_statfs(const char *path, struct statvfs *st) {
	sqfs_hl *hl = fuse_get_context()->private_data;
	return sqfs_statfs(&hl->fs, st);
}

int sqfs_hl_open(sqfs_hl *hl, const char *path, size_t offset) {
	memset(hl, 0, sizeof(*hl));
	if (sqfs_open_image(&hl->fs, path, offset) == SQFS_OK) {
		if (sqfs_inode_get(&hl->fs, &hl->root, sqfs_inode_root(&hl->fs)))
			fprintf(stderr, "Can't find the root of this filesystem!\n");
		else
			return 0;
		sqfs_destroy(&hl->fs);
	}
	return -1;
}

int squash_main(int argc, char *argv[]) __attribute__ ((visibility ("default")));

int squash_main(int argc, char *argv[]) {
	struct fuse_args args;
	sqfs_opts opts;
	sqfs_hl *hl;
	int ret, i;
	
	struct fuse_opt fuse_opts[] = {
		{"offset=%zu", offsetof(sqfs_opts, offset), 0},
		FUSE_OPT_END
	};

	struct fuse_operations sqfs_hl_ops;
	memset(&sqfs_hl_ops, 0, sizeof(sqfs_hl_ops));
	sqfs_hl_ops.init		= sqfs_hl_op_init;
	sqfs_hl_ops.destroy		= sqfs_hl_op_destroy;
	sqfs_hl_ops.getattr		= sqfs_hl_op_getattr;
	sqfs_hl_ops.opendir		= sqfs_hl_op_opendir;
	sqfs_hl_ops.releasedir	= sqfs_hl_op_releasedir;
	sqfs_hl_ops.readdir		= sqfs_hl_op_readdir;
	sqfs_hl_ops.open		= sqfs_hl_op_open;
	sqfs_hl_ops.create		= sqfs_hl_op_create;
	sqfs_hl_ops.release		= sqfs_hl_op_release;
	sqfs_hl_ops.read		= sqfs_hl_op_read;
	sqfs_hl_ops.readlink	= sqfs_hl_op_readlink;
	sqfs_hl_ops.listxattr	= sqfs_hl_op_listxattr;
	sqfs_hl_ops.getxattr	= sqfs_hl_op_getxattr;
	sqfs_hl_ops.statfs    = sqfs_hl_op_statfs;
  
	args.argc = argc;
	args.argv = argv;
	args.allocated = 0;
	
	opts.progname = argv[0];
	opts.images = malloc(argc * sizeof(char*)); /* enough room for all images */
	opts.image_count = 0;
	opts.offset = 0;
	if (fuse_opt_parse(&args, &opts, fuse_opts, sqfs_opt_proc) == -1)
		sqfs_usage(argv[0], true);
	if (opts.image_count < 2)
		sqfs_usage(argv[0], true);
    fuse_opt_add_arg(&args, opts.images[--opts.image_count]); /* add mountpoint to args */
	sqfs_hl *hls;
	hls = malloc(sizeof(sqfs_hl) * (1 + opts.image_count));
	if (!hls) {
		perror("Can't allocate memory");
		return -1;
	}
	hl = hls;
	for(i=0; i<opts.image_count; i++) {
		if(sqfs_hl_open(hl, opts.images[i], opts.offset)<0)
			return -1;
		hl++;
	}
	hl->fs.fd = 0;
	fuse_opt_add_arg(&args, "-s"); /* single threaded */
	ret = fuse_main(args.argc, args.argv, &sqfs_hl_ops, hls);
	fuse_opt_free_args(&args);
	return ret;
}
