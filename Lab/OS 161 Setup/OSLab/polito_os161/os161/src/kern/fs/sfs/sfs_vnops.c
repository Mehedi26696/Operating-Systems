/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009, 2014
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * SFS filesystem
 *
 * File-level (vnode) interface routines.
 */
#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <limits.h>
#include <stat.h>
#include <lib.h>
#include <uio.h>
#include <vfs.h>
#include <sfs.h>
#include "sfsprivate.h"

/*
 * Maximum length of a symlink target. SFS stores symlink contents
 * directly in the data blocks of the inode, so it is bounded by the
 * maximum number of bytes addressable from one inode (which is plenty
 * for assignment use).
 */
#define SFS_MAX_LINK_TARGET SFS_BLOCKSIZE * (SFS_NDIRECT + SFS_DBPERIDB)

////////////////////////////////////////////////////////////
// Vnode operations.

/*
 * This is called on *each* open().
 */
static
int
sfs_eachopen(struct vnode *v, int openflags)
{
	/*
	 * At this level we do not need to handle O_CREAT, O_EXCL,
	 * O_TRUNC, or O_APPEND.
	 *
	 * Any of O_RDONLY, O_WRONLY, and O_RDWR are valid, so we don't need
	 * to check that either.
	 */

	(void)v;
	(void)openflags;

	return 0;
}

/*
 * This is called on *each* open() of a directory.
 * Directories may only be open for read.
 */
static
int
sfs_eachopendir(struct vnode *v, int openflags)
{
	switch (openflags & O_ACCMODE) {
	    case O_RDONLY:
		break;
	    case O_WRONLY:
	    case O_RDWR:
	    default:
		return EISDIR;
	}
	if (openflags & O_APPEND) {
		return EISDIR;
	}

	(void)v;
	return 0;
}

/*
 * Called for read(). sfs_io() does the work.
 */
static
int
sfs_read(struct vnode *v, struct uio *uio)
{
	struct sfs_vnode *sv = v->vn_data;
	int result;

	KASSERT(uio->uio_rw==UIO_READ);

	vfs_biglock_acquire();
	result = sfs_io(sv, uio);
	vfs_biglock_release();

	return result;
}

/*
 * Called for readlink(). The contents of the symlink are read using
 * sfs_io() just like a regular file, but we refuse to operate on
 * anything that is not actually a symlink.
 */
static
int
sfs_readlink(struct vnode *v, struct uio *uio)
{
	struct sfs_vnode *sv = v->vn_data;
	int result;

	KASSERT(uio->uio_rw==UIO_READ);

	vfs_biglock_acquire();

	if (sv->sv_i.sfi_type != SFS_TYPE_LINK) {
		vfs_biglock_release();
		return EINVAL;
	}

	result = sfs_io(sv, uio);

	vfs_biglock_release();
	return result;
}

/*
 * Called for getdirentry(). The uio_offset field is interpreted as a
 * directory slot number (not a byte offset). We read the entry at that
 * slot, copy its name into the uio, and advance the offset by one.
 */
static
int
sfs_getdirentry(struct vnode *v, struct uio *uio)
{
	struct sfs_vnode *sv = v->vn_data;
	struct sfs_fs *sfs = sv->sv_absvn.vn_fs->fs_data;
	struct sfs_direntry sd;
	off_t size;
	int slot, result, nentries;
	size_t namelen;

	KASSERT(v != NULL);
	KASSERT(uio != NULL);

	vfs_biglock_acquire();

	if (sv->sv_i.sfi_type != SFS_TYPE_DIR) {
		vfs_biglock_release();
		return ENOTDIR;
	}

	slot = (int)uio->uio_offset;
	if (slot < 0) {
		vfs_biglock_release();
		return EINVAL;
	}

	/* Number of direntry slots in this directory (inlined from
	 * sfs_dir_nentries because that helper is file-static). */
	size = sv->sv_i.sfi_size;
	if (size % sizeof(struct sfs_direntry) != 0) {
		vfs_biglock_release();
		panic("sfs: %s: directory %u: Invalid size %llu\n",
		      sfs->sfs_sb.sb_volname, sv->sv_ino, size);
		return EINVAL;
	}
	nentries = size / sizeof(struct sfs_direntry);

	if (slot >= nentries) {
		/* Past the end: standard behavior is to return 0 bytes
		 * read (so userland stops iterating). */
		vfs_biglock_release();
		uio->uio_offset = slot;
		return 0;
	}

	/* Read the direntry at the requested slot (inlined from
	 * sfs_readdir because that helper is file-static). */
	result = sfs_metaio(sv, slot * sizeof(struct sfs_direntry),
			    &sd, sizeof(sd), UIO_READ);
	if (result) {
		vfs_biglock_release();
		return result;
	}

	/* Skip empty slots — they have SFS_NOINO. */
	if (sd.sfd_ino == SFS_NOINO) {
		vfs_biglock_release();
		uio->uio_offset = slot + 1;
		/* 0 bytes read but offset advances — caller will retry. */
		return 0;
	}

	namelen = strlen(sd.sfd_name) + 1;
	if (namelen > uio->uio_resid) {
		vfs_biglock_release();
		return EINVAL;
	}

	result = uiomove(sd.sfd_name, namelen, uio);
	if (result) {
		vfs_biglock_release();
		return result;
	}

	/* Advance the slot index for the next call. */
	uio->uio_offset = slot + 1;

	vfs_biglock_release();
	return 0;
}

/*
 * Called for write(). sfs_io() does the work.
 */
static
int
sfs_write(struct vnode *v, struct uio *uio)
{
	struct sfs_vnode *sv = v->vn_data;
	int result;

	KASSERT(uio->uio_rw==UIO_WRITE);

	vfs_biglock_acquire();
	result = sfs_io(sv, uio);
	vfs_biglock_release();

	return result;
}

/*
 * Called for ioctl()
 */
static
int
sfs_ioctl(struct vnode *v, int op, userptr_t data)
{
	/*
	 * No ioctls.
	 */

	(void)v;
	(void)op;
	(void)data;

	return EINVAL;
}

/*
 * Called for stat/fstat/lstat.
 */
static
int
sfs_stat(struct vnode *v, struct stat *statbuf)
{
	struct sfs_vnode *sv = v->vn_data;
	int result;

	/* Fill in the stat structure */
	bzero(statbuf, sizeof(struct stat));

	result = VOP_GETTYPE(v, &statbuf->st_mode);
	if (result) {
		return result;
	}

	statbuf->st_size = sv->sv_i.sfi_size;
	statbuf->st_nlink = sv->sv_i.sfi_linkcount;
	statbuf->st_ino = sv->sv_ino;

	/* We don't support this yet */
	statbuf->st_blocks = 0;

	/* Fill in other fields as desired/possible... */

	return 0;
}

/*
 * Return the type of the file (types as per kern/stat.h)
 */
static
int
sfs_gettype(struct vnode *v, uint32_t *ret)
{
	struct sfs_vnode *sv = v->vn_data;
	struct sfs_fs *sfs = v->vn_fs->fs_data;

	vfs_biglock_acquire();

	switch (sv->sv_i.sfi_type) {
	case SFS_TYPE_FILE:
		*ret = S_IFREG;
		vfs_biglock_release();
		return 0;
	case SFS_TYPE_DIR:
		*ret = S_IFDIR;
		vfs_biglock_release();
		return 0;
	case SFS_TYPE_LINK:
		*ret = S_IFLNK;
		vfs_biglock_release();
		return 0;
	}
	panic("sfs: %s: gettype: Invalid inode type (inode %u, type %u)\n",
	      sfs->sfs_sb.sb_volname, sv->sv_ino, sv->sv_i.sfi_type);
	return EINVAL;
}

/*
 * Check if seeking is allowed. The answer is "yes".
 */
static
bool
sfs_isseekable(struct vnode *v)
{
	(void)v;
	return true;
}

/*
 * Called for fsync(), and also on filesystem unmount, global sync(),
 * and some other cases.
 */
static
int
sfs_fsync(struct vnode *v)
{
	struct sfs_vnode *sv = v->vn_data;
	int result;

	vfs_biglock_acquire();
	result = sfs_sync_inode(sv);
	vfs_biglock_release();

	return result;
}

/*
 * Called for mmap().
 */
static
int
sfs_mmap(struct vnode *v   /* add stuff as needed */)
{
	(void)v;
	return ENOSYS;
}

/*
 * Truncate a file.
 */
static
int
sfs_truncate(struct vnode *v, off_t len)
{
	struct sfs_vnode *sv = v->vn_data;

	return sfs_itrunc(sv, len);
}

/*
 * Get the full pathname for a file. This only needs to work on directories.
 * Since we don't support subdirectories, assume it's the root directory
 * and hand back the empty string. (The VFS layer takes care of the
 * device name, leading slash, etc.)
 *
 * We also validate that uio has room for a NUL terminator so userland
 * __getcwd never reads past the end of its buffer.
 */
static
int
sfs_namefile(struct vnode *vv, struct uio *uio)
{
	struct sfs_vnode *sv = vv->vn_data;
	KASSERT(sv->sv_ino == SFS_ROOTDIR_INO);

	(void)uio;

	/*
	 * Always require at least one byte of space so we can signal EOF
	 * by sending back exactly nothing (uio_resid unchanged would mean
	 * the caller may spin). Caller expects to be able to append a NUL
	 * afterwards.
	 */
	if (uio == NULL || uio->uio_resid <= 0) {
		return EINVAL;
	}

	return 0;
}

/*
 * Create a file. If EXCL is set, insist that the filename not already
 * exist; otherwise, if it already exists, just open it.
 */
static
int
sfs_creat(struct vnode *v, const char *name, bool excl, mode_t mode,
	  struct vnode **ret)
{
	struct sfs_fs *sfs = v->vn_fs->fs_data;
	struct sfs_vnode *sv = v->vn_data;
	struct sfs_vnode *newguy;
	uint32_t ino;
	int result;

	vfs_biglock_acquire();

	/* Look up the name */
	result = sfs_dir_findname(sv, name, &ino, NULL, NULL);
	if (result!=0 && result!=ENOENT) {
		vfs_biglock_release();
		return result;
	}

	/* If it exists and we didn't want it to, fail */
	if (result==0 && excl) {
		vfs_biglock_release();
		return EEXIST;
	}

	if (result==0) {
		/* We got something; load its vnode and return */
		result = sfs_loadvnode(sfs, ino, SFS_TYPE_INVAL, &newguy);
		if (result) {
			vfs_biglock_release();
			return result;
		}
		*ret = &newguy->sv_absvn;
		vfs_biglock_release();
		return 0;
	}

	/* Didn't exist - create it */
	result = sfs_makeobj(sfs, SFS_TYPE_FILE, &newguy);
	if (result) {
		vfs_biglock_release();
		return result;
	}

	/* We don't currently support file permissions; ignore MODE */
	(void)mode;

	/* Link it into the directory */
	result = sfs_dir_link(sv, name, newguy->sv_ino, NULL);
	if (result) {
		VOP_DECREF(&newguy->sv_absvn);
		vfs_biglock_release();
		return result;
	}

	/* Update the linkcount of the new file */
	newguy->sv_i.sfi_linkcount++;

	/* and consequently mark it dirty. */
	newguy->sv_dirty = true;

	*ret = &newguy->sv_absvn;

	vfs_biglock_release();
	return 0;
}

/*
 * Make a hard link to a file.
 * The VFS layer should prevent this being called unless both
 * vnodes are ours.
 */
static
int
sfs_link(struct vnode *dir, const char *name, struct vnode *file)
{
	struct sfs_vnode *sv = dir->vn_data;
	struct sfs_vnode *f = file->vn_data;
	int result;

	KASSERT(file->vn_fs == dir->vn_fs);

	vfs_biglock_acquire();

	/* Hard links to directories aren't allowed. */
	if (f->sv_i.sfi_type == SFS_TYPE_DIR) {
		vfs_biglock_release();
		return EINVAL;
	}

	/* Create the link */
	result = sfs_dir_link(sv, name, f->sv_ino, NULL);
	if (result) {
		vfs_biglock_release();
		return result;
	}

	/* and update the link count, marking the inode dirty */
	f->sv_i.sfi_linkcount++;
	f->sv_dirty = true;

	vfs_biglock_release();
	return 0;
}

/*
 * Create a symbolic link.
 *
 * Symlinks are stored like ordinary files: the CONTENTS string is written
 * to the inode's data blocks, and the inode type is set to SFS_TYPE_LINK.
 */
static
int
sfs_symlink(struct vnode *dir, const char *name, const char *contents)
{
	struct sfs_fs *sfs = dir->vn_fs->fs_data;
	struct sfs_vnode *sv = dir->vn_data;
	struct sfs_vnode *newguy;
	struct iovec iov;
	struct uio kuio;
	int result, result2;
	size_t len;

	vfs_biglock_acquire();

	/* The directory must actually be a directory */
	if (sv->sv_i.sfi_type != SFS_TYPE_DIR) {
		vfs_biglock_release();
		return ENOTDIR;
	}

	/* Reject empty targets early. */
	if (contents == NULL) {
		vfs_biglock_release();
		return EIO;
	}
	len = strlen(contents);
	if (len == 0) {
		vfs_biglock_release();
		return EINVAL;
	}
	if (len > SFS_MAX_LINK_TARGET) {
		vfs_biglock_release();
		return ENAMETOOLONG;
	}

	/* Allocate a new inode and load a vnode for the symlink. */
	result = sfs_makeobj(sfs, SFS_TYPE_LINK, &newguy);
	if (result) {
		vfs_biglock_release();
		return result;
	}

	/* Link it into the parent directory under name. */
	result = sfs_dir_link(sv, name, newguy->sv_ino, NULL);
	if (result) {
		VOP_DECREF(&newguy->sv_absvn);
		vfs_biglock_release();
		return result;
	}

	/* Bump the symlink's linkcount (just like sfs_creat does). */
	newguy->sv_i.sfi_linkcount++;
	newguy->sv_dirty = true;

	/* Write the link contents into the symlink inode. */
	uio_kinit(&iov, &kuio, (void *)contents, len, 0, UIO_WRITE);
	result = sfs_io(newguy, &kuio);
	if (result) {
		/*
		 * Best-effort cleanup: try to remove the directory entry
		 * and let reclaim handle the inode.
		 */
		result2 = sfs_dir_findname(sv, name, NULL, NULL, NULL);
		if (result2 == 0) {
			int slot;
			(void)sfs_dir_findname(sv, name, NULL, &slot, NULL);
			if (slot >= 0) {
				(void)sfs_dir_unlink(sv, slot);
			}
		}
		VOP_DECREF(&newguy->sv_absvn);
		vfs_biglock_release();
		return result;
	}

	/* Done. Let go of the symlink vnode. */
	VOP_DECREF(&newguy->sv_absvn);

	vfs_biglock_release();
	return 0;
}

/*
 * Delete a file.
 */
static
int
sfs_remove(struct vnode *dir, const char *name)
{
	struct sfs_vnode *sv = dir->vn_data;
	struct sfs_vnode *victim;
	int slot;
	int result;

	vfs_biglock_acquire();

	/* Look for the file and fetch a vnode for it. */
	result = sfs_lookonce(sv, name, &victim, &slot);
	if (result) {
		vfs_biglock_release();
		return result;
	}

	/* Erase its directory entry. */
	result = sfs_dir_unlink(sv, slot);
	if (result==0) {
		/* If we succeeded, decrement the link count. */
		KASSERT(victim->sv_i.sfi_linkcount > 0);
		victim->sv_i.sfi_linkcount--;
		victim->sv_dirty = true;
	}

	/* Discard the reference that sfs_lookonce got us */
	VOP_DECREF(&victim->sv_absvn);

	vfs_biglock_release();
	return result;
}

/*
 * Rename a file.
 *
 * Since we don't support subdirectories, assumes that the two
 * directories passed are the same.
 */
static
int
sfs_rename(struct vnode *d1, const char *n1,
	   struct vnode *d2, const char *n2)
{
	struct sfs_vnode *sv = d1->vn_data;
	struct sfs_fs *sfs = sv->sv_absvn.vn_fs->fs_data;
	struct sfs_vnode *g1;
	int slot1, slot2;
	int result, result2;

	vfs_biglock_acquire();

	KASSERT(d1==d2);
	KASSERT(sv->sv_ino == SFS_ROOTDIR_INO);

	/* Look up the old name of the file and get its inode and slot number*/
	result = sfs_lookonce(sv, n1, &g1, &slot1);
	if (result) {
		vfs_biglock_release();
		return result;
	}

	/* We don't support subdirectories */
	KASSERT(g1->sv_i.sfi_type == SFS_TYPE_FILE);

	/*
	 * Link it under the new name.
	 *
	 * We could theoretically just overwrite the original
	 * directory entry, except that we need to check to make sure
	 * the new name doesn't already exist; might as well use the
	 * existing link routine.
	 */
	result = sfs_dir_link(sv, n2, g1->sv_ino, &slot2);
	if (result) {
		goto puke;
	}

	/* Increment the link count, and mark inode dirty */
	g1->sv_i.sfi_linkcount++;
	g1->sv_dirty = true;

	/* Unlink the old slot */
	result = sfs_dir_unlink(sv, slot1);
	if (result) {
		goto puke_harder;
	}

	/*
	 * Decrement the link count again, and mark the inode dirty again,
	 * in case it's been synced behind our back.
	 */
	KASSERT(g1->sv_i.sfi_linkcount>0);
	g1->sv_i.sfi_linkcount--;
	g1->sv_dirty = true;

	/* Let go of the reference to g1 */
	VOP_DECREF(&g1->sv_absvn);

	vfs_biglock_release();
	return 0;

 puke_harder:
	/*
	 * Error recovery: try to undo what we already did
	 */
	result2 = sfs_dir_unlink(sv, slot2);
	if (result2) {
		kprintf("sfs: %s: rename: %s\n",
			sfs->sfs_sb.sb_volname, strerror(result));
		kprintf("sfs: %s: rename: while cleaning up: %s\n",
			sfs->sfs_sb.sb_volname, strerror(result2));
		panic("sfs: %s: rename: Cannot recover\n",
		      sfs->sfs_sb.sb_volname);
	}
	g1->sv_i.sfi_linkcount--;
 puke:
	/* Let go of the reference to g1 */
	VOP_DECREF(&g1->sv_absvn);
	vfs_biglock_release();
	return result;
}

/*
 * lookparent returns the last path component as a string and the
 * directory it's in as a vnode.
 *
 * Since we don't support subdirectories, this is very easy -
 * return the root dir and copy the path.
 */
static
int
sfs_lookparent(struct vnode *v, char *path, struct vnode **ret,
		  char *buf, size_t buflen)
{
	struct sfs_vnode *sv = v->vn_data;

	vfs_biglock_acquire();

	if (sv->sv_i.sfi_type != SFS_TYPE_DIR) {
		vfs_biglock_release();
		return ENOTDIR;
	}

	if (strlen(path)+1 > buflen) {
		vfs_biglock_release();
		return ENAMETOOLONG;
	}
	strcpy(buf, path);

	VOP_INCREF(&sv->sv_absvn);
	*ret = &sv->sv_absvn;

	vfs_biglock_release();
	return 0;
}

/*
 * Lookup gets a vnode for a pathname.
 *
 * Since we don't support subdirectories, it's easy - just look up the
 * name.
 */
static
int
sfs_lookup(struct vnode *v, char *path, struct vnode **ret)
{
	struct sfs_vnode *sv = v->vn_data;
	struct sfs_vnode *final;
	int result;

	vfs_biglock_acquire();

	if (sv->sv_i.sfi_type != SFS_TYPE_DIR) {
		vfs_biglock_release();
		return ENOTDIR;
	}

	result = sfs_lookonce(sv, path, &final, NULL);
	if (result) {
		vfs_biglock_release();
		return result;
	}

	*ret = &final->sv_absvn;

	vfs_biglock_release();
	return 0;
}

////////////////////////////////////////////////////////////
// Ops tables

/*
 * Function table for sfs files.
 */
const struct vnode_ops sfs_fileops = {
	.vop_magic = VOP_MAGIC,	/* mark this a valid vnode ops table */

	.vop_eachopen = sfs_eachopen,
	.vop_reclaim = sfs_reclaim,

	.vop_read = sfs_read,
	.vop_readlink = vopfail_uio_inval,
	.vop_getdirentry = vopfail_uio_notdir,
	.vop_write = sfs_write,
	.vop_ioctl = sfs_ioctl,
	.vop_stat = sfs_stat,
	.vop_gettype = sfs_gettype,
	.vop_isseekable = sfs_isseekable,
	.vop_fsync = sfs_fsync,
	.vop_mmap = sfs_mmap,
	.vop_truncate = sfs_truncate,
	.vop_namefile = vopfail_uio_notdir,

	.vop_creat = vopfail_creat_notdir,
	.vop_symlink = vopfail_symlink_notdir,
	.vop_mkdir = vopfail_mkdir_notdir,
	.vop_link = vopfail_link_notdir,
	.vop_remove = vopfail_string_notdir,
	.vop_rmdir = vopfail_string_notdir,
	.vop_rename = vopfail_rename_notdir,

	.vop_lookup = vopfail_lookup_notdir,
	.vop_lookparent = vopfail_lookparent_notdir,
};

/*
 * Function table for the sfs directory.
 */
const struct vnode_ops sfs_dirops = {
	.vop_magic = VOP_MAGIC,	/* mark this a valid vnode ops table */

	.vop_eachopen = sfs_eachopendir,
	.vop_reclaim = sfs_reclaim,

	.vop_read = vopfail_uio_isdir,
	.vop_readlink = vopfail_uio_inval,
	.vop_getdirentry = sfs_getdirentry,
	.vop_write = vopfail_uio_isdir,
	.vop_ioctl = sfs_ioctl,
	.vop_stat = sfs_stat,
	.vop_gettype = sfs_gettype,
	.vop_isseekable = sfs_isseekable,
	.vop_fsync = sfs_fsync,
	.vop_mmap = vopfail_mmap_isdir,
	.vop_truncate = vopfail_truncate_isdir,
	.vop_namefile = sfs_namefile,

	.vop_creat = sfs_creat,
	.vop_symlink = sfs_symlink,
	.vop_mkdir = vopfail_mkdir_notdir,
	.vop_link = sfs_link,
	.vop_remove = sfs_remove,
	.vop_rmdir = vopfail_string_notdir,
	.vop_rename = sfs_rename,

	.vop_lookup = sfs_lookup,
	.vop_lookparent = sfs_lookparent,
};

/*
 * Function table for sfs symlinks.
 */
const struct vnode_ops sfs_linkops = {
	.vop_magic = VOP_MAGIC,

	.vop_eachopen = sfs_eachopen,
	.vop_reclaim = sfs_reclaim,

	.vop_read = vopfail_uio_inval,
	.vop_readlink = sfs_readlink,
	.vop_getdirentry = vopfail_uio_inval,
	.vop_write = vopfail_uio_inval,
	.vop_ioctl = sfs_ioctl,
	.vop_stat = sfs_stat,
	.vop_gettype = sfs_gettype,
	.vop_isseekable = sfs_isseekable,
	.vop_fsync = sfs_fsync,
	.vop_mmap = vopfail_mmap_nosys,
	.vop_truncate = vopfail_truncate_isdir,
	.vop_namefile = vopfail_uio_notdir,

	.vop_creat = vopfail_creat_notdir,
	.vop_symlink = vopfail_symlink_notdir,
	.vop_mkdir = vopfail_mkdir_notdir,
	.vop_link = vopfail_link_notdir,
	.vop_remove = vopfail_string_notdir,
	.vop_rmdir = vopfail_string_notdir,
	.vop_rename = vopfail_rename_notdir,

	.vop_lookup = vopfail_lookup_notdir,
	.vop_lookparent = vopfail_lookparent_notdir,
};
