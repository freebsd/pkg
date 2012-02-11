
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/stat.h>

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <grp.h>
#include <inttypes.h>
#include <libutil.h>
#include <paths.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "private/gr_util.h"

static int lockfd = -1;
static char group_dir[PATH_MAX];
static char group_file[PATH_MAX];
static char tempname[PATH_MAX];
static int initialized;

int
gr_init(const char *dir, const char *group)
{
	if (dir == NULL) {
		strcpy(group_dir, _PATH_ETC);
	} else {
		if (strlen(dir) >= sizeof(group_dir)) {
			errno = ENAMETOOLONG;
			return (-1);
		}
		strcpy(group_dir, dir);
	}

	if (group == NULL) {
		if (dir == NULL) {
			strcpy(group_file, _PATH_GROUP);
		} else if (snprintf(group_file, sizeof(group_file), "%s/group",
			group_dir) > (int)sizeof(group_file)) {
			errno = ENAMETOOLONG;
			return (-1);
		}
	} else {
		if (strlen(group) >= sizeof(group_file)) {
			errno = ENAMETOOLONG;
			return (-1);
		}
		strcpy(group_file, group);
	}
	initialized = 1;
	return (0);
}

/*
 * Lock the group file
 */
int
gr_lock(void)
{
	if (*group_file == '\0')
		return (-1);

	for (;;) {
		struct stat st;

		lockfd = open(group_file, O_RDONLY, 0);
		if (lockfd < 0 || fcntl(lockfd, F_SETFD, 1) == -1)
			err(1, "%s", group_file);
		if (flock(lockfd, LOCK_EX|LOCK_NB) == -1) {
			if (errno == EWOULDBLOCK) {
				errx(1, "the group file is busy");
			} else {
				err(1, "could not lock the group file: ");
			}
		}
		if (fstat(lockfd, &st) == -1)
			err(1, "fstat() failed: ");
		if (st.st_nlink != 0)
			break;
		close(lockfd);
		lockfd = -1;
	}
	return (lockfd);
}

/*
 * Create and open a presmuably safe temp file for editing group data
 */
int
gr_tmp(int mfd)
{
	char buf[8192];
	ssize_t nr;
	const char *p;
	int tfd;

	if (*group_file == '\0')
		return (-1);
	if ((p = strrchr(group_file, '/')))
		++p;
	else
		p = group_file;
	if (snprintf(tempname, sizeof(tempname), "%.*sgroup.XXXXXX",
		(int)(p - group_file), group_file) >= (int)sizeof(tempname)) {
		errno = ENAMETOOLONG;
		return (-1);
	}
	if ((tfd = mkstemp(tempname)) == -1)
		return (-1);
	if (mfd != -1) {
		while ((nr = read(mfd, buf, sizeof(buf))) > 0)
			if (write(tfd, buf, (size_t)nr) != nr)
				break;
		if (nr != 0) {
			unlink(tempname);
			*tempname = '\0';
			close(tfd);
			return (-1);
		}
	}
	return (tfd);
}

/*
 * Copy the group file from one descriptor to another, replacing, deleting
 * or adding a single record on the way.
 */
int
gr_copy(int ffd, int tfd, const struct group *gr, struct group *old_gr)
{
	char buf[8192], *end, *line, *p, *q, *r, t;
	struct group *fgr;
	const struct group *sgr;
	size_t len;
	int eof, readlen;

	sgr = gr;
	if (gr == NULL) {
		line = NULL;
		if (old_gr == NULL)
			return (-1);
		sgr = old_gr;
	} else if ((line = gr_make(gr)) == NULL)
		return (-1);

	eof = 0;
	len = 0;
	p = q = end = buf;
	for (;;) {
		/* find the end of the current line */
		for (p = q; q < end && *q != '\0'; ++q)
			if (*q == '\n')
				break;

		/* if we don't have a complete line, fill up the buffer */
		if (q >= end) {
			if (eof)
				break;
			if ((size_t)(q - p) >= sizeof(buf)) {
				warnx("group line too long");
				errno = EINVAL; /* hack */
				goto err;
			}
			if (p < end) {
				q = memmove(buf, p, end -p);
				end -= p - buf;
			} else {
				p = q = end = buf;
			}
			readlen = read(ffd, end, sizeof(buf) - (end -buf));
			if (readlen == -1)
				goto err;
			else
				len = (size_t)readlen;
			if (len == 0 && p == buf)
				break;
			end += len;
			len = end - buf;
			if (len < (ssize_t)sizeof(buf)) {
				eof = 1;
				if (len > 0 && buf[len -1] != '\n')
					++len, *end++ = '\n';
			}
			continue;
		}

		/* is it a blank line or a comment? */
		for (r = p; r < q && isspace(*r); ++r)
			/* nothing */;
		if (r == q || *r == '#') {
			/* yep */
			if (write(tfd, p, q -p + 1) != q - p + 1)
				goto err;
			++q;
			continue;
		}

		/* is it the one we're looking for? */

		t = *q;
		*q = '\0';

		fgr = gr_scan(r);

		/* fgr is either a struct group for the current line,
		 * or NULL if the line is malformed.
		 */

		*q = t;
		if (fgr == NULL || fgr->gr_gid != sgr->gr_gid) {
			/* nope */
			if (fgr != NULL)
				free(fgr);
			if (write(tfd, p, q - p + 1) != q - p + 1)
				goto err;
			++q;
			continue;
		}
		if (old_gr && !gr_equal(fgr, old_gr)) {
			warnx("entry inconsistent");
			free(fgr);
			errno = EINVAL; /* hack */
			goto err;
		}
		free(fgr);

		/* it is, replace or remove it */
		if (line != NULL) {
			len = strlen(line);
			if (write(tfd, line, len) != (int) len)
				goto err;
		} else {
			/* when removed, avoid the \n */
			q++;
		}
		/* we're done, just copy the rest over */
		for (;;) {
			if (write(tfd, q, end - q) != end - q)
				goto err;
			q = buf;
			readlen = read(ffd, buf, sizeof(buf));
			if (readlen == 0)
				break;
			else
				len = (size_t)readlen;
			if (readlen == -1)
				goto err;
			end = buf + len;
		}
		goto done;
	}

	/* if we got here, we didn't find the old entry */
	if (line == NULL) {
		errno = ENOENT;
		goto err;
	}
	len = strlen(line);
	if ((size_t)write(tfd, line, len) != len ||
	   write(tfd, "\n", 1) != 1)
		goto err;
 done:
	if (line != NULL)
		free(line);
	return (0);
 err:
	if (line != NULL)
		free(line);
	return (-1);
}

/*
 * Regenerate the group file
 */
int
gr_mkdb(void)
{
	return (rename(tempname, group_file));
}

/*
 * Clean up. Preserver errno for the caller's convenience.
 */
void
gr_fini(void)
{
	int serrno;

	if (!initialized)
		return;
	initialized = 0;
	serrno = errno;
	if (*tempname != '\0') {
		unlink(tempname);
		*tempname = '\0';
	}
	if (lockfd != -1)
		close(lockfd);
	errno = serrno;
}
