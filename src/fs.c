#include "fs.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

int
iget(
	int		fd,
	fs_u64_t	inum,
	struct dinode	*ildp,
	struct dinode	**dpp)
{
	struct dinode	*dp = NULL;
	fs_u64_t	offset, off, blkno, len;
	int		error = 0;

	*dpp = NULL;
	if (inum > fs->lastino) {
		assert(0);
		fprintf(stderr, "iget: File system %s invalid inode "
			"number %llu access\n", fs->mntpt, inum);
		return EINVAL;
	}
	offset = inum * INOSIZE;
	dp = (struct dinode *)malloc(sizeof(struct dinode));
	if (!dp) {
		return ENOMEM;
	}
	if ((error = bmap(fd, fs->ildp, &blkno, &len, &off, offset)) != 0) {
		free(dp);
		return error;
	}
	offset = blkno * ONE_K + off;
	lseek(fd, offset, SEEK_SET);
	if (read(fd, (caddr_t)dp, INOSIZE) != INOSIZE) {
		fprintf(stderr, "iget: File system %s ilist read failed\n",
			fs->mntpt);
		error = errno;
		free(dp);
		return error;
	}
	*dpp = dp;
	return 0;
}

/*
 * if error is returned from this function, then the contents
 * of buffer 'buf' should not be trusted and be relied upon.
 */

int
read_dir(
	int		fd,
	struct dinode	*dp,
	fs_u64_t	offset,
	char		*buf,
	int		buflen)
{
	fs_u64_t	blkno, off, len;
	fs_u64_t	sz = dp->size, remain = buflen, count = 0;
	int		direntlen = sizeof(struct direntry);
	int		error = 0;

	/*
	 * validate the arguments first.
	 * Check specifically for 'buflen'; it should
	 * be multuple of 'sizeof(struct direntry)'.
	 */

	if (buflen <= 0 || !buf || !dp || (buflen % direntlen != 0)) {
		return EINVAL;
	}
	if (dp->mode != IFDIR) {
		return ENOTDIR;
	}

	/*
	 * if the directory is empty, we still return success
	 * along with zeroing out the passed buffer.
	 */

	if (sz == 0) {
		bzero(buf, buflen);
		return 0;
	}
	while (1) {
		assert(count <= buflen && count <= dp->size);
		if (count == buflen || count == sz) {
			break;
		}
		if ((error = bmap(fd, dp, &blkno, &off, &len, offset)) != 0) {
			return error;
		}
		readlen = MIN(len, MIN(sz, remain));
		lseek(fd, off, SEEK_SET);
		if (read(fd, (buf + count), readlen) != readlen) {
			error = errno;
			return error;
		}
		offset += readlen;
		count += readlen;
		remain -= readlen;
		sz -= readlen;
	}
	if (buflen > count) {
		bzero(buf + count, remain);
	}
	return 0;
}

/* TODO: free the *dp pointer at the end */

int
lookup_path(
	int		fd,
	char		*path,
	struct direntry	**dep)
{
	struct direntry	*de, *de1 = NULL;
	struct dinode	*dp = NULL;
	fs_u64_t	offset = 0, inum = MNTPT_INO;
	char		*buf = NULL;
	int		i = 0, j, nentries;
	int		error = 0, buflen, low, high;

	buf = (char *)malloc(INDIR_BLKSZ);
	if (!buf) {
		return ENOMEM;
	}
	if (dep) {
		de1 = (struct direntry *)malloc(sizeof(struct direntry));
		if (!de1) {
			free(buf);
			return ENOMEM;
		}
	}
	for (;;) {
		if ((error = iget(fd, inum, &dp)) != 0) {
			goto out;
		}
		if (dp->mode != IFDIR) {
			error = ENOTDIR;
			goto out;
		}
		low = i;
		while (path[i] != '/' && path[i] != '\0') {
			i++;
		}
		high = i - 1;
		offset = 0;
		buflen = (int)((dp->size - offset > INDIR_BLKSZ) ?
			INDIR_BLKSZ  : (dp->size - offset));
		while (1) {
			if ((error = read_dir(fd, dp, offset, buf,
					      buflen)) != 0) {
				goto out;
			}
			de = (struct direntry *)buf;
			nentries = buflen/sizeof(struct direntry);
			for (j = 0; j < nentries; j++) {
				assert(de->inum != 0);
				if (bcmp(path + low, de->name,
					 high - low + 1) == 0) {
					if (dep) {
						bcopy(de, de1,
						      sizeof(struct direntry));
					}
					break;
				}
				de++;
			}
			if (j < nentries) {
				break;
			}
			offset += buflen;
			if (offset == dp->size) {
				error = ENOENT:
				goto out;
			}
			buflen = (int)((dp->size - offset > INDIR_BLKSZ) ?
					INDIR_BLKSZ  : (dp->size - offset));
		}
		inum = de->inumber;
		if (path[i] == '\0') {
			break;
		}
	}

out:
	free(buf);
	if (dp) {
		free(dp);
	}
	return error;
}

/* a very simple read routine.
 */

int
fsread(
	int		fd,
	fs_u64_t	offset,
	int		len,
	char		*buf)
{
	fs_u64_t	off, foff, sz, blkno;
	int		remain = len, readlen;
	int		nread = 0, error = 0;

	/* TODO: add a code to check whether offset
	   is greater than maximum supported file size
	   set errno to EOVERFLOW if that turns out to be true.
	 */

	if (!buffer || !len) {
		errno = EINVAL;
		return 0;
	}
	while (nread < len) {
		if ((error = bmap(fd, &blkno, &sz, &off, offset)) != 0) {
			errno = error;
			goto out;
		}
		readlen = MIN(remain, (int)sz);
		foff = (blkno * ONE_K) + off;
		lseek(fd, foff, SEEK_SET);
		if (read(fd, (buf + nread), readlen) != readlen) {
			goto out;
		}
		offset += (fs_u64_t)readlen;
		nread += readlen;
	}

out:
	return nread;
}

int
read_sb(
	struct fsmem	*fs)
{
	lseek(fs->fd, SB_OFFSET, SEEK_SET);
	if (read(fs->fd, &fs->sb, sizeof(struct super_block)) !=
	    sizeof(struct super_block)) {
		fprintf(stderr, "Failed to read superblock for %s: %s\n",
			fs->devf, strerror(errno));
		return errno;
	}
	if (fs->sb.magic != 0xf001cab) {
		fprintf(stderr, "%s: Not a valid file system\n", fs->devf);
		return EINVAL;
	}

	return 0;
}

struct fsmem *
fsmount(
	char		*fsname,
	char		*mntpt)
{
	struct fsmem	*fs = NULL;
	int		fd, mntlen = strlen(mntpt);
	int		devlen = strlen(fsname);

	if (access(fsname, F_OK) != 0) {
		fprintf(stderr, "Failed to access %s: %s\n",
			fsname, strerror(errno));
		return NULL;
	}
	if ((fd = open(fsname, O_RDWR)) < 0) {
		fprintf(stderr, "Failed to open %s: %s\n",
			fsname, strerror(errno));
		goto errout;
	}
	fs = (struct fsmem *)malloc(sizeof(struct fsmem));
	if (!fs) {
		fprintf(stderr, "Failed to allocate memory while mounting "
			"%s: %s\n", fsname, strerror(errno));
		goto errout;
	}
	bzero(fs, sizeof(struct fsmem));
	fs->fd = fd;
	fs->mntpt = (char *)malloc(mntlen + 1);
	if (!fs->mntpt) {
		fprintf(stderr, "Failed to allocate memory for mntpt %s: %s\n",
			mntpt, strerror(errno));
		goto errout;
	}
	bcopy(mntpt, fs->mntpt, mntlen + 1);
	fs->devf = (char *)malloc(devlen + 1);
	if (!fs->devlen) {
		fprintf(stderr, "Failed to allocate memory for fsname %s: %s\n",
			fsname, strerror(errno));
		goto errout;
	}
	if (read_sb(fs) != 0) {
		goto errout;
	}
	if (iget(fs, &fs->ilip, ILIST_INO) || iget(fs, &fs->emapip, EMAP_INO) ||
	    iget(fs, &fs->imapip, IMAP_INO)) {
		goto errout;
	}
	return fs;

errout:
	if (fd >= 0) {
		close(fd);
	}
	if (fs) {
		if (fs->mntpt) {
			free(fs->mntpt);
		}
		if (fs->devf) {
			free(fs->devf);
		}
		free(fs);
	}
	return NULL;
}