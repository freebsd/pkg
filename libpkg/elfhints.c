/*-
 * Copyright (c) 1998 John D. Polstra
 * Copyright (c) 2012 Matthew Seaman <matthew@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: stable/8/sbin/ldconfig/elfhints.c 76224 2001-05-02 23:56:21Z obrien $
 */

#include <bsd_compat.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <uthash.h>

#include "pkg.h"
#include "private/ldconfig.h"

#define MAXDIRS		1024		/* Maximum directories in path */
#define MAXFILESIZE	(16*1024)	/* Maximum hints file size */

struct shlib_list {
	UT_hash_handle	 hh;
	const char	*name;
	char		 path[];
};

static int	shlib_list_add(struct shlib_list **shlib_list,
				const char *dir, const char *shlib_file);
static int	scan_dirs_for_shlibs(struct shlib_list **shlib_list,
				     int numdirs, const char **dirlist,
	                             bool strictnames);
static void	add_dir(const char *, const char *, int);
static void	read_dirs_from_file(const char *, const char *);
static void	read_elf_hints(const char *, int);
static void	write_elf_hints(const char *);

static const char	*dirs[MAXDIRS];
static int		 ndirs;
int			 insecure;

/* Known shlibs on the standard system search path.  Persistent,
   common to all applications */
static struct shlib_list *shlibs = NULL;

/* Known shlibs on the specific RPATH or RUNPATH of one binary.
   Evanescent. */
static struct shlib_list *rpath = NULL;

void
shlib_list_init(void)
{
	assert(HASH_COUNT(shlibs) == 0);
}

void
rpath_list_init(void)
{
	assert(HASH_COUNT(rpath) == 0);
}

static int
shlib_list_add(struct shlib_list **shlib_list, const char *dir,
    const char *shlib_file)
{
	struct shlib_list	*sl;
	size_t	path_len, dir_len;

	/* If shlib_file is already in the shlib_list table, don't try
	 * and add it again */
	HASH_FIND_STR(*shlib_list, shlib_file, sl);
	if (sl != NULL)
		return (EPKG_OK);

	path_len = strlen(dir) + strlen(shlib_file) + 2;

	sl = calloc(1, sizeof(struct shlib_list) + path_len);
	if (sl == NULL) {
		warnx("Out of memory");
		return (EPKG_FATAL);
	}

	strlcpy(sl->path, dir, path_len);
	dir_len = strlcat(sl->path, "/", path_len);
	strlcat(sl->path, shlib_file, path_len);
	
	sl->name = sl->path + dir_len;

	HASH_ADD_KEYPTR(hh, *shlib_list, sl->name,
			strlen(sl->name), sl);

	return (EPKG_OK);
}

const char *
shlib_list_find_by_name(const char *shlib_file)
{
	struct shlib_list *sl;

	if (HASH_COUNT(shlibs) == 0)
		return (NULL);

	HASH_FIND_STR(rpath, shlib_file, sl);
	if (sl != NULL)
		return (sl->path);

	HASH_FIND_STR(shlibs, shlib_file, sl);
	if (sl != NULL)
		return (sl->path);
		
	return (NULL);
}

void
shlib_list_free(void)
{
	struct shlib_list	*sl1, *sl2;

	HASH_ITER(hh, shlibs, sl1, sl2) {
		HASH_DEL(shlibs, sl1);
		free(sl1);
	}
	shlibs = NULL;
}

void
rpath_list_free(void)
{
	struct shlib_list	*sl1, *sl2;

	HASH_ITER(hh, rpath, sl1, sl2) {
		HASH_DEL(rpath, sl1);
		free(sl1);
	}
	rpath = NULL;
}

static void
add_dir(const char *hintsfile, const char *name, int trusted)
{
	struct stat 	stbuf;
	int		i;

	/* Do some security checks */
	if (!trusted && !insecure) {
		if (stat(name, &stbuf) == -1) {
			warn("%s", name);
			return;
		}
		if (stbuf.st_uid != 0) {
			warnx("%s: ignoring directory not owned by root", name);
			return;
		}
		if ((stbuf.st_mode & S_IWOTH) != 0) {
			warnx("%s: ignoring world-writable directory", name);
			return;
		}
		if ((stbuf.st_mode & S_IWGRP) != 0) {
			warnx("%s: ignoring group-writable directory", name);
			return;
		}
	}

	for (i = 0;  i < ndirs;  i++)
		if (strcmp(dirs[i], name) == 0)
			return;
	if (ndirs >= MAXDIRS)
		errx(1, "\"%s\": Too many directories in path", hintsfile);
	dirs[ndirs++] = name;
}

static int
scan_dirs_for_shlibs(struct shlib_list **shlib_list, int numdirs,
		     const char **dirlist, bool strictnames)
{
	int	i;

	/* Expect shlibs to follow the name pattern libfoo.so.N if
	   strictnames is true -- ie. when searching the default
	   library search path.

	   Otherwise, allow any name ending in .so or .so.N --
	   ie. when searching RPATH or RUNPATH and assuming it
	   contains private shared libraries which can follow just
	   about any naming convention */

	for (i = 0;  i < numdirs;  i++) {
		DIR		*dirp;
		struct dirent	*dp;

		if ((dirp = opendir(dirlist[i])) == NULL)
			continue;
		while ((dp = readdir(dirp)) != NULL) {
			int		 len;
			int		 ret;
			const char	*vers;

			/* Only regular files and sym-links. On some
			   filesystems d_type is not set, on these the d_type
			   field will be DT_UNKNOWN. */
			if (dp->d_type != DT_REG && dp->d_type != DT_LNK &&
			    dp->d_type != DT_UNKNOWN)
				continue;

			len = strlen(dp->d_name);
			if (strictnames) {
				/* Name can't be shorter than "libx.so" */
				if (len < 7 ||
				    strncmp(dp->d_name, "lib", 3) != 0)
					continue;
			}

			vers = dp->d_name + len;
			while (vers > dp->d_name &&
			       (isdigit(*(vers-1)) || *(vers-1) == '.'))
				vers--;
			if (vers == dp->d_name + len) {
				if (strncmp(vers - 3, ".so", 3) != 0)
					continue;
			} else if (vers < dp->d_name + 3 ||
			    strncmp(vers - 3, ".so.", 4) != 0)
				continue;

			/* We have a valid shared library name. */
			ret = shlib_list_add(shlib_list, dirlist[i],
					      dp->d_name);
			if (ret != EPKG_OK) {
				closedir(dirp);
				return ret;
			}
		}
		closedir(dirp);
	}
	return 0;
}

#define ORIGIN	"$ORIGIN"

int shlib_list_from_rpath(const char *rpath_str, const char *dirpath)
{
	const char    **dirlist;
	char	       *buf;
	size_t		buflen;
	int		i, numdirs;
	int		ret;
	const char     *c, *cstart;
	
	/* The special token $ORIGIN should be replaced by the
	   dirpath: adjust buflen calculation to account for this */

	numdirs = 1;
	for (c = rpath_str; *c != '\0'; c++)
		if (*c == ':')
			numdirs++;
	buflen = numdirs * sizeof(char *) + strlen(rpath_str) + 1;
	i = strlen(dirpath) - strlen(ORIGIN);
	if (i > 0)
		buflen += i * numdirs;

	dirlist = calloc(1, buflen);
	if (dirlist == NULL) {
		warnx("Out of memory");
		return (EPKG_FATAL);
	}
	buf = (char *)dirlist + numdirs * sizeof(char *);

	buf[0] = '\0';
	cstart = rpath_str;
	while ( (c = strstr(cstart, ORIGIN)) != NULL ) {
		strncat(buf, cstart, c - cstart);
		strlcat(buf, dirpath, buflen);
		cstart = c + strlen(ORIGIN);
	}
	strlcat(buf, cstart, buflen);

	i = 0;
	while ((c = strsep(&buf, ":")) != NULL) {
		if (strlen(c) > 0)
			dirlist[i++] = c;
	}

	assert(i <= numdirs);

	ret = scan_dirs_for_shlibs(&rpath, i, dirlist, false);

	free(dirlist);

	return (ret);
}

int 
shlib_list_from_elf_hints(const char *hintsfile)
{
	read_elf_hints(hintsfile, 1);

	return (scan_dirs_for_shlibs(&shlibs, ndirs, dirs, true));
}

void
list_elf_hints(const char *hintsfile)
{
	int	i;
	int	nlibs;

	read_elf_hints(hintsfile, 1);
	printf("%s:\n", hintsfile);
	printf("\tsearch directories:");
	for (i = 0;  i < ndirs;  i++)
		printf("%c%s", i == 0 ? ' ' : ':', dirs[i]);
	printf("\n");

	nlibs = 0;
	for (i = 0;  i < ndirs;  i++) {
		DIR		*dirp;
		struct dirent	*dp;

		if ((dirp = opendir(dirs[i])) == NULL)
			continue;
		while ((dp = readdir(dirp)) != NULL) {
			int		 len;
			int		 namelen;
			const char	*name;
			const char	*vers;

			/* Name can't be shorter than "libx.so.0" */
			if ((len = strlen(dp->d_name)) < 9 ||
			    strncmp(dp->d_name, "lib", 3) != 0)
				continue;
			name = dp->d_name + 3;
			vers = dp->d_name + len;
			while (vers > dp->d_name && isdigit(*(vers-1)))
				vers--;
			if (vers == dp->d_name + len)
				continue;
			if (vers < dp->d_name + 4 ||
			    strncmp(vers - 4, ".so.", 4) != 0)
				continue;

			/* We have a valid shared library name. */
			namelen = (vers - 4) - name;
			printf("\t%d:-l%.*s.%s => %s/%s\n", nlibs,
			    namelen, name, vers, dirs[i], dp->d_name);
			nlibs++;
		}
		closedir(dirp);
	}
}

static void
read_dirs_from_file(const char *hintsfile, const char *listfile)
{
	FILE	*fp;
	char	 buf[MAXPATHLEN];
	int	 linenum;

	if ((fp = fopen(listfile, "r")) == NULL)
		err(1, "%s", listfile);

	linenum = 0;
	while (fgets(buf, sizeof buf, fp) != NULL) {
		char	*cp, *sp;

		linenum++;
		cp = buf;
		/* Skip leading white space. */
		while (isspace(*cp))
			cp++;
		if (*cp == '#' || *cp == '\0')
			continue;
		sp = cp;
		/* Advance over the directory name. */
		while (!isspace(*cp) && *cp != '\0')
			cp++;
		/* Terminate the string and skip trailing white space. */
		if (*cp != '\0') {
			*cp++ = '\0';
			while (isspace(*cp))
				cp++;
		}
		/* Now we had better be at the end of the line. */
		if (*cp != '\0')
			warnx("%s:%d: trailing characters ignored",
			    listfile, linenum);

		if ((sp = strdup(sp)) == NULL)
			errx(1, "Out of memory");
		add_dir(hintsfile, sp, 0);
	}

	fclose(fp);
}

static void
read_elf_hints(const char *hintsfile, int must_exist)
{
	int	 		 fd;
	struct stat		 s;
	void			*mapbase;
	struct elfhints_hdr	*hdr;
	char			*strtab;
	char			*dirlist;
	char			*p;

	if ((fd = open(hintsfile, O_RDONLY)) == -1) {
		if (errno == ENOENT && !must_exist)
			return;
		err(1, "Cannot open \"%s\"", hintsfile);
	}
	if (fstat(fd, &s) == -1)
		err(1, "Cannot stat \"%s\"", hintsfile);
	if (s.st_size > MAXFILESIZE)
		errx(1, "\"%s\" is unreasonably large", hintsfile);
	/*
	 * We use a read-write, private mapping so that we can null-terminate
	 * some strings in it without affecting the underlying file.
	 */
	mapbase = mmap(NULL, s.st_size, PROT_READ|PROT_WRITE,
	    MAP_PRIVATE, fd, 0);
	if (mapbase == MAP_FAILED)
		err(1, "Cannot mmap \"%s\"", hintsfile);
	close(fd);

	hdr = (struct elfhints_hdr *)mapbase;
	if (hdr->magic != ELFHINTS_MAGIC)
		errx(1, "\"%s\": invalid file format", hintsfile);
	if (hdr->version != 1)
		errx(1, "\"%s\": unrecognized file version (%d)", hintsfile,
		    hdr->version);

	strtab = (char *)mapbase + hdr->strtab;
	dirlist = strtab + hdr->dirlist;

	if (*dirlist != '\0')
		while ((p = strsep(&dirlist, ":")) != NULL)
			add_dir(hintsfile, p, 1);
}

void
update_elf_hints(const char *hintsfile, int argc, char **argv, int merge)
{
	int	i;

	if (merge)
		read_elf_hints(hintsfile, 0);
	for (i = 0;  i < argc;  i++) {
		struct stat	s;

		if (stat(argv[i], &s) == -1)
			warn("warning: %s", argv[i]);
		else if (S_ISREG(s.st_mode))
			read_dirs_from_file(hintsfile, argv[i]);
		else
			add_dir(hintsfile, argv[i], 0);
	}
	write_elf_hints(hintsfile);
}

static void
write_elf_hints(const char *hintsfile)
{
	struct elfhints_hdr	 hdr;
	char			*tempname;
	int			 fd;
	FILE			*fp;
	int			 i;

	if (asprintf(&tempname, "%s.XXXXXX", hintsfile) == -1)
		errx(1, "Out of memory");
	if ((fd = mkstemp(tempname)) ==  -1)
		err(1, "mkstemp(%s)", tempname);
	if (fchmod(fd, 0444) == -1)
		err(1, "fchmod(%s)", tempname);
	if ((fp = fdopen(fd, "wb")) == NULL)
		err(1, "fdopen(%s)", tempname);

	hdr.magic = ELFHINTS_MAGIC;
	hdr.version = 1;
	hdr.strtab = sizeof hdr;
	hdr.strsize = 0;
	hdr.dirlist = 0;
	memset(hdr.spare, 0, sizeof hdr.spare);

	/* Count up the size of the string table. */
	if (ndirs > 0) {
		hdr.strsize += strlen(dirs[0]);
		for (i = 1;  i < ndirs;  i++)
			hdr.strsize += 1 + strlen(dirs[i]);
	}
	hdr.dirlistlen = hdr.strsize;
	hdr.strsize++;	/* For the null terminator */

	/* Write the header. */
	if (fwrite(&hdr, 1, sizeof hdr, fp) != sizeof hdr)
		err(1, "%s: write error", tempname);
	/* Write the strings. */
	if (ndirs > 0) {
		if (fputs(dirs[0], fp) == EOF)
			err(1, "%s: write error", tempname);
		for (i = 1;  i < ndirs;  i++)
			if (fprintf(fp, ":%s", dirs[i]) < 0)
				err(1, "%s: write error", tempname);
	}
	if (putc('\0', fp) == EOF || fclose(fp) == EOF)
		err(1, "%s: write error", tempname);

	if (rename(tempname, hintsfile) == -1)
		err(1, "rename %s to %s", tempname, hintsfile);
	free(tempname);
}
