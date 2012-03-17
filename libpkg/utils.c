/*
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
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

#include <sys/stat.h>
#include <sys/param.h>
#include <stdio.h>

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "pkg.h"
#include "private/event.h"
#include "private/utils.h"

void
sbuf_init(struct sbuf **buf)
{
	if (*buf == NULL)
		*buf = sbuf_new_auto();
	else
		sbuf_clear(*buf);
}

int
sbuf_set(struct sbuf **buf, const char *str)
{
	if (*buf == NULL)
		*buf = sbuf_new_auto();

	if (str == NULL)
		return (-1);

	sbuf_cpy(*buf, str);
	sbuf_finish(*buf);
	return (0);
}

char *
sbuf_get(struct sbuf *buf)
{

	assert(buf != NULL);
	if (sbuf_done(buf) == 0)
		sbuf_finish(buf);

	return (sbuf_data(buf));
}

void
sbuf_reset(struct sbuf *buf)
{
	if (buf != NULL) {
		sbuf_clear(buf);
		sbuf_finish(buf);
	}
}

void
sbuf_free(struct sbuf *buf)
{
	if (buf != NULL)
		sbuf_delete(buf);
}

int
mkdirs(const char *_path)
{
	char path[MAXPATHLEN + 1];
	char *p;

	strlcpy(path, _path, sizeof(path));
	p = path;
	if (*p == '/')
		p++;

	for (;;) {
		if ((p = strchr(p, '/')) != NULL)
			*p = '\0';

		if (mkdir(path, S_IRWXU | S_IRWXG | S_IRWXO) < 0)
			if (errno != EEXIST && errno != EISDIR) {
				pkg_emit_errno("mkdir", path);
				return (EPKG_FATAL);
			}

		/* that was the last element of the path */
		if (p == NULL)
			break;

		*p = '/';
		p++;
	}

	return (EPKG_OK);
}

int
file_to_buffer(const char *path, char **buffer, off_t *sz)
{
	int fd = -1;
	struct stat st;
	int retcode = EPKG_OK;

	assert(path != NULL && path[0] != '\0');
	assert(buffer != NULL);
	assert(sz != NULL);

	if ((fd = open(path, O_RDONLY)) == -1) {
		pkg_emit_errno("open", path);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	if (fstat(fd, &st) == -1) {
		close(fd);
		pkg_emit_errno("fstat", path);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	if ((*buffer = malloc(st.st_size + 1)) == NULL) {
		close(fd);
		pkg_emit_errno("malloc", "");
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	if (read(fd, *buffer, st.st_size) == -1) {
		close(fd);
		pkg_emit_errno("read", path);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	cleanup:
	if (fd > 0)
		close(fd);

	if (retcode == EPKG_OK) {
		(*buffer)[st.st_size] = '\0';
		*sz = st.st_size;
	} else {
		*buffer = NULL;
		*sz = -1;
	}
	return (retcode);
}

int
format_exec_cmd(char **dest, const char *in, const char *prefix, const char *plist_file, char *line)
{
	struct sbuf *buf = sbuf_new_auto();
	char path[MAXPATHLEN + 1];
	char *cp;

	while (in[0] != '\0') {
		if (in[0] == '%') {
			in++;
			switch(in[0]) {
				case 'D':
					sbuf_cat(buf, prefix);
					break;
				case 'F':
					if (plist_file == NULL) {
						pkg_emit_error("No files defined %%F couldn't be expanded, ignoring %s", in);
						sbuf_finish(buf);
						sbuf_free(buf);
						return (EPKG_FATAL);
					}
					sbuf_cat(buf, plist_file);
					break;
				case 'f':
					if (plist_file == NULL) {
						pkg_emit_error("No files defined %%f couldn't be expanded, ignoring %s", in);
						sbuf_finish(buf);
						sbuf_free(buf);
						return (EPKG_FATAL);
					}
					if (prefix[strlen(prefix) - 1] == '/')
						snprintf(path, sizeof(path), "%s%s", prefix, plist_file);
					else
						snprintf(path, sizeof(path), "%s/%s", prefix, plist_file);
					cp = strrchr(path, '/');
					cp ++;
					sbuf_cat(buf, cp);
					break;
				case 'B':
					if (plist_file == NULL) {
						pkg_emit_error("No files defined %%B couldn't be expanded, ignoring %s", in);
						sbuf_finish(buf);
						sbuf_free(buf);
						return (EPKG_FATAL);
					}
					if (prefix[strlen(prefix) - 1] == '/')
						snprintf(path, sizeof(path), "%s%s", prefix, plist_file);
					else
						snprintf(path, sizeof(path), "%s/%s", prefix, plist_file);
					cp = strrchr(path, '/');
					cp[0] = '\0';
					sbuf_cat(buf, path);
					break;
				case '@':
					if (line != NULL) {
						sbuf_cat(buf, line);
						break;
					}

					/*
					 * no break here because if line is not
					 * given (default exec) %@ does not
					 * exists
					 */
				default:
					sbuf_putc(buf, '%');
					sbuf_putc(buf, in[0]);
					break;
			}

		} else {
			sbuf_putc(buf, in[0]);
		}

		in++;
	}

	sbuf_finish(buf);
	*dest = strdup(sbuf_data(buf));
	sbuf_free(buf);
	
	return (EPKG_OK);
}

int
split_chr(char *str, char sep)
{
	char *next;
	char *buf = str;
	int nbel = 0;

	while ((next = strchr(buf, sep)) != NULL) {
		nbel++;
		buf = next;
		buf[0] = '\0';
		buf++;
	}

	return nbel;
}

int
is_dir(const char *path)
{
	struct stat st;

	return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

static void
sha256_hash(unsigned char hash[SHA256_DIGEST_LENGTH], char out[SHA256_DIGEST_LENGTH * 2 + 1])
{
	int i;
	for (i = 0; i < SHA256_DIGEST_LENGTH; i++)
		sprintf(out + (i * 2), "%02x", hash[i]);

	out[SHA256_DIGEST_LENGTH * 2] = '\0';
}

void
sha256_str(const char *string, char out[SHA256_DIGEST_LENGTH * 2 + 1])
{
	unsigned char hash[SHA256_DIGEST_LENGTH];
	SHA256_CTX sha256;

	SHA256_Init(&sha256);
	SHA256_Update(&sha256, string, strlen(string));
	SHA256_Final(hash, &sha256);

	sha256_hash(hash, out);
}

int
sha256_file(const char *path, char out[SHA256_DIGEST_LENGTH * 2 + 1])
{
	FILE *fp;
	char buffer[BUFSIZ];
	unsigned char hash[SHA256_DIGEST_LENGTH];
	size_t r = 0;
	SHA256_CTX sha256;

	if ((fp = fopen(path, "rb")) == NULL) {
		pkg_emit_errno("fopen", path);
		return EPKG_FATAL;
	}

	SHA256_Init(&sha256);

	while ((r = fread(buffer, 1, BUFSIZ, fp)) > 0)
		SHA256_Update(&sha256, buffer, r);

	if (ferror(fp) != 0) {
		fclose(fp);
		out[0] = '\0';
		pkg_emit_errno("fread", path);
		return EPKG_FATAL;
	}

	fclose(fp);

	SHA256_Final(hash, &sha256);
	sha256_hash(hash, out);

	return (EPKG_OK);
}

int
is_conf_file(const char *path, char *newpath, size_t len)
{
	size_t n;
	char *p = NULL;

	n = strlen(path);

	if (n < 8)
		return (0);

	p = strrchr(path, '.');

	if (p != NULL && !strcmp(p, ".pkgconf")) {
		strlcpy(newpath, path, len);
		newpath[n - 8] = '\0';
		return (1);
	}

	return (0);
}

bool is_hardlink(struct hardlinks *hl, struct stat *st)
{
	size_t i;

	for (i = 0; i < hl->len; i++) {
		if (hl->inodes[i] == st->st_ino) {
			if (hl->cap <= hl->len) {
				hl->cap |= 1;
				hl->cap *= 2;
				hl->inodes = reallocf(hl->inodes,
						hl->cap * sizeof(ino_t));
			}
			hl->inodes[hl->len++] = st->st_ino;
			return false;
		}
	}
	return true;
}
