/*-
 * Copyright (c) 2011-2013 Baptiste Daroussin <bapt@FreeBSD.org>
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
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <yaml.h>
#include <ucl.h>
#include <uthash.h>
#include <utlist.h>
#include <ctype.h>
#include <fnmatch.h>

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
	if (buf == NULL)
		return (__DECONST(char *, ""));

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
	char path[MAXPATHLEN];
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
		pkg_emit_errno("fstat", path);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	if ((*buffer = malloc(st.st_size + 1)) == NULL) {
		pkg_emit_errno("malloc", "");
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	if (read(fd, *buffer, st.st_size) == -1) {
		pkg_emit_errno("read", path);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	cleanup:
	if (fd >= 0)
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
format_exec_cmd(char **dest, const char *in, const char *prefix,
    const char *plist_file, char *line)
{
	struct sbuf *buf = sbuf_new_auto();
	char path[MAXPATHLEN];
	char *cp;

	while (in[0] != '\0') {
		if (in[0] != '%') {
			sbuf_putc(buf, in[0]);
			in++;
			continue;
		}
		in++;
		switch(in[0]) {
		case 'D':
			sbuf_cat(buf, prefix);
			break;
		case 'F':
			if (plist_file == NULL || plist_file[0] == '\0') {
				pkg_emit_error("No files defined %%F couldn't "
				    "be expanded, ignoring %s", in);
				sbuf_finish(buf);
				sbuf_free(buf);
				return (EPKG_FATAL);
			}
			sbuf_cat(buf, plist_file);
			break;
		case 'f':
			if (plist_file == NULL || plist_file[0] == '\0') {
				pkg_emit_error("No files defined %%f couldn't "
				    "be expanded, ignoring %s", in);
				sbuf_finish(buf);
				sbuf_free(buf);
				return (EPKG_FATAL);
			}
			if (prefix[strlen(prefix) - 1] == '/')
				snprintf(path, sizeof(path), "%s%s",
				    prefix, plist_file);
			else
				snprintf(path, sizeof(path), "%s/%s",
				    prefix, plist_file);
			cp = strrchr(path, '/');
			cp ++;
			sbuf_cat(buf, cp);
			break;
		case 'B':
			if (plist_file == NULL || plist_file[0] == '\0') {
				pkg_emit_error("No files defined %%B couldn't "
				    "be expanded, ignoring %s", in);
				sbuf_finish(buf);
				sbuf_free(buf);
				return (EPKG_FATAL);
			}
			if (prefix[strlen(prefix) - 1] == '/')
				snprintf(path, sizeof(path), "%s%s", prefix,
				    plist_file);
			else
				snprintf(path, sizeof(path), "%s/%s", prefix,
				    plist_file);
			cp = strrchr(path, '/');
			cp[0] = '\0';
			sbuf_cat(buf, path);
			break;
		case '%':
			sbuf_putc(buf, '%');
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

		in++;
	}

	sbuf_finish(buf);
	*dest = strdup(sbuf_data(buf));
	sbuf_free(buf);
	
	return (EPKG_OK);
}

int
is_dir(const char *path)
{
	struct stat st;

	return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

static void
md5_hash(unsigned char hash[MD5_DIGEST_LENGTH],
    char out[MD5_DIGEST_LENGTH * 2 + 1])
{
	int i;
	for (i = 0; i < MD5_DIGEST_LENGTH; i++)
		sprintf(out + (i *2), "%02x", hash[i]);

	out[MD5_DIGEST_LENGTH * 2] = '\0';
}

int
md5_file(const char *path, char out[MD5_DIGEST_LENGTH * 2 + 1])
{
	FILE *fp;
	char buffer[BUFSIZ];
	unsigned char hash[SHA256_DIGEST_LENGTH];
	size_t r = 0;
	MD5_CTX md5;

	if ((fp = fopen(path, "rb")) == NULL) {
		pkg_emit_errno("fopen", path);
		return EPKG_FATAL;
	}

	MD5_Init(&md5);

	while ((r = fread(buffer, 1, BUFSIZ, fp)) > 0)
		MD5_Update(&md5, buffer, r);

	if (ferror(fp) != 0) {
		fclose(fp);
		out[0] = '\0';
		pkg_emit_errno("fread", path);
		return EPKG_FATAL;
	}

	fclose(fp);

	MD5_Final(hash, &md5);
	md5_hash(hash, out);

	return (EPKG_OK);
}

static void
sha256_hash(unsigned char hash[SHA256_DIGEST_LENGTH],
    char out[SHA256_DIGEST_LENGTH * 2 + 1])
{
	int i;
	for (i = 0; i < SHA256_DIGEST_LENGTH; i++)
		sprintf(out + (i * 2), "%02x", hash[i]);

	out[SHA256_DIGEST_LENGTH * 2] = '\0';
}

int
sha256_file(const char *path, char out[SHA256_DIGEST_LENGTH * 2 + 1])
{
	int fd;
	int ret;

	if ((fd = open(path, O_RDONLY)) == -1) {
		pkg_emit_errno("fopen", path);
		return (EPKG_FATAL);
	}

	ret = sha256_fd(fd, out);

	close(fd);

	return (ret);
}

void
sha256_buf(char *buf, size_t len, char out[SHA256_DIGEST_LENGTH * 2 + 1])
{
	unsigned char hash[SHA256_DIGEST_LENGTH];
	sha256_buf_bin(buf, len, hash);
	out[0] = '\0';
	sha256_hash(hash, out);
}

void
sha256_buf_bin(char *buf, size_t len, char hash[SHA256_DIGEST_LENGTH])
{
	SHA256_CTX sha256;

	SHA256_Init(&sha256);
	SHA256_Update(&sha256, buf, len);
	SHA256_Final(hash, &sha256);
}

int
sha256_fd(int fd, char out[SHA256_DIGEST_LENGTH * 2 + 1])
{
	int my_fd = -1;
	FILE *fp = NULL;
	char buffer[BUFSIZ];
	unsigned char hash[SHA256_DIGEST_LENGTH];
	size_t r = 0;
	int ret = EPKG_OK;
	SHA256_CTX sha256;

	out[0] = '\0';

	/* Duplicate the fd so that fclose(3) does not close it. */
	if ((my_fd = dup(fd)) == -1) {
		pkg_emit_errno("dup", "");
		ret = EPKG_FATAL;
		goto cleanup;
	}

	if ((fp = fdopen(my_fd, "rb")) == NULL) {
		pkg_emit_errno("fdopen", "");
		ret = EPKG_FATAL;
		goto cleanup;
	}

	SHA256_Init(&sha256);

	while ((r = fread(buffer, 1, BUFSIZ, fp)) > 0)
		SHA256_Update(&sha256, buffer, r);

	if (ferror(fp) != 0) {
		pkg_emit_errno("fread", "");
		ret = EPKG_FATAL;
		goto cleanup;
	}

	SHA256_Final(hash, &sha256);
	sha256_hash(hash, out);
cleanup:

	if (fp != NULL)
		fclose(fp);
	else if (my_fd != -1)
		close(my_fd);
	(void)lseek(fd, 0, SEEK_SET);

	return (ret);
}

int
is_conf_file(const char *path, char *newpath, size_t len)
{
	size_t n;
	const char *p = NULL;

	n = strlen(path);

	if (n < 8)
		return (0);

	p = &path[n - 8];

	if (strcmp(p, ".pkgconf") == 0) {
		strlcpy(newpath, path, len);
		newpath[n - 8] = '\0';
		return (1);
	}

	return (0);
}

bool
is_hardlink(struct hardlinks *hl, struct stat *st)
{
	struct hardlinks *h;

	HASH_FIND_INO(hl, &st->st_ino, h);
	if (h != NULL)
		return false;

	h = malloc(sizeof(struct hardlinks));
	h->inode = st->st_ino;
	HASH_ADD_INO(hl, inode, h);

	return (true);
}

bool
is_valid_abi(const char *arch, bool emit_error) {
	const char *myarch;

	pkg_config_string(PKG_CONFIG_ABI, &myarch);

	if (fnmatch(arch, myarch, FNM_CASEFOLD) == FNM_NOMATCH &&
	    strncmp(arch, myarch, strlen(myarch)) != 0) {
		if (emit_error)
			pkg_emit_error("wrong architecture: %s instead of %s",
			    arch, myarch);
		return (false);
	}

	return (true);
}

static ucl_object_t *yaml_mapping_to_object(ucl_object_t *obj, yaml_document_t *doc, yaml_node_t *node);

static ucl_object_t *
yaml_sequence_to_object(ucl_object_t *obj, yaml_document_t *doc, yaml_node_t *node)
{
	yaml_node_item_t *item;
	yaml_node_t *val;
	ucl_object_t *sub = NULL;

	item = node->data.sequence.items.start;
	while (item < node->data.sequence.items.top) {
		val = yaml_document_get_node(doc, *item);
		switch (val->type) {
		case YAML_MAPPING_NODE:
			sub = yaml_mapping_to_object(NULL, doc, val);
			break;
		case YAML_SEQUENCE_NODE:
			sub = yaml_sequence_to_object(NULL, doc, val);
			break;
		case YAML_SCALAR_NODE:
			sub = ucl_object_fromstring_common (val->data.scalar.value,
			    val->data.scalar.length, UCL_STRING_TRIM|UCL_STRING_PARSE_BOOLEAN|UCL_STRING_PARSE_INT);
			break;
		case YAML_NO_NODE:
			/* Should not happen */
			break;
		}
		obj = ucl_array_append(obj, sub);
		++item;
	}

	return (obj);
}

static ucl_object_t *
yaml_mapping_to_object(ucl_object_t *obj, yaml_document_t *doc, yaml_node_t *node)
{
	yaml_node_pair_t *pair;
	yaml_node_t *key, *val;

	ucl_object_t *sub = NULL;

	pair = node->data.mapping.pairs.start;
	while (pair < node->data.mapping.pairs.top) {
		key = yaml_document_get_node(doc, pair->key);
		val = yaml_document_get_node(doc, pair->value);

		switch (val->type) {
		case YAML_MAPPING_NODE:
			sub = yaml_mapping_to_object(NULL, doc, val);
			break;
		case YAML_SEQUENCE_NODE:
			sub = yaml_sequence_to_object(NULL, doc, val);
			break;
		case YAML_SCALAR_NODE:
			sub = ucl_object_fromstring_common (val->data.scalar.value,
			    val->data.scalar.length,
			    UCL_STRING_TRIM|UCL_STRING_PARSE_BOOLEAN|UCL_STRING_PARSE_INT);
			break;
		case YAML_NO_NODE:
			/* Should not happen */
			break;
		}
		if (sub != NULL)
			obj = ucl_object_insert_key(obj, sub, key->data.scalar.value, key->data.scalar.length, true);
		++pair;
	}

	return (obj);
}

ucl_object_t *
yaml_to_ucl(const char *file, const char *buffer, size_t len) {
	yaml_parser_t parser;
	yaml_document_t doc;
	yaml_node_t *node;
	ucl_object_t *obj = NULL;
	FILE *fp = NULL;

	memset(&parser, 0, sizeof(parser));

	yaml_parser_initialize(&parser);

	if (file != NULL) {
		fp = fopen(file, "r");
		if (fp == NULL) {
			pkg_emit_errno("fopen", file);
			return (NULL);
		}
		yaml_parser_set_input_file(&parser, fp);
	} else {
		yaml_parser_set_input_string(&parser, buffer, len);
	}

	yaml_parser_load(&parser, &doc);

	node = yaml_document_get_root_node(&doc);
	if (node != NULL) {
		switch (node->type) {
		case YAML_MAPPING_NODE:
			obj = yaml_mapping_to_object(NULL, &doc, node);
			break;
		case YAML_SEQUENCE_NODE:
			obj = yaml_sequence_to_object(NULL, &doc, node);
			break;
		case YAML_SCALAR_NODE:
		case YAML_NO_NODE:
			break;
		}
	}

	yaml_document_delete(&doc);
	yaml_parser_delete(&parser);

	if (file != NULL)
		fclose(fp);

	return (obj);
}

void
set_nonblocking(int fd)
{
	int flags;

	if ((flags = fcntl(fd, F_GETFL)) == -1)
		return;
	if (!(flags & O_NONBLOCK)) {
		flags |= O_NONBLOCK;
		fcntl(fd, F_SETFL, flags);
	}
}

void
set_blocking(int fd)
{
	int flags;

	if ((flags = fcntl(fd, F_GETFL)) == -1)
		return;
	if (flags & O_NONBLOCK) {
		flags &= ~O_NONBLOCK;
		fcntl(fd, F_SETFL, flags);
	}
}
