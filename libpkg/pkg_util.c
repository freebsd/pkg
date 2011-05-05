#include <sys/stat.h>
#include <sys/param.h>
#include <stdio.h>

#include <assert.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>


#include <openssl/sha.h>

#include "pkg.h"
#include "pkg_error.h"
#include "pkg_util.h"

void
array_init(struct array *a, size_t c)
{
	assert(c > 0);

	/* if the array is already initialized, do nothing */
	if (a->cap > 0 && a->data != NULL)
		return;

	a->cap = c;
	a->len = 0;
	a->data = malloc(sizeof(void*) * a->cap);
	a->data[0] = NULL;
}

void
array_append(struct array *a, void *d)
{
	assert(a->cap > 0);
	assert(a->data != NULL);

	if (a->cap <= a->len + 1) {
		a->cap *= 2;
		a->data = realloc(a->data, sizeof(void*) * a->cap);
	}
	a->data[a->len] = d;
	a->data[a->len+1] = NULL;
	a->len++;
}

void
array_reset(struct array *a, void (*free_elm)(void*))
{
	if (a->data == NULL)
		return;

	if (free_elm != NULL)
		for (size_t i = 0; i < a->len; i++)
			free_elm(a->data[i]);

	a->len = 0;
	a->data[0] = NULL;
}

void
array_free(struct array *a, void (*free_elm)(void*))
{
	if (a->data == NULL)
		return;

	array_reset(a, free_elm);

	free(a->data);
	a->data = NULL;
	a->cap = 0;
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

const char *
sbuf_get(struct sbuf *buf)
{
	if (buf == NULL)
		return (NULL);

	return sbuf_data(buf);
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
file_to_buffer(const char *path, char **buffer, off_t *sz)
{
	int fd;
	struct stat st;

	if (path == NULL || path[0] == '\0')
		return (ERROR_BAD_ARG("path"));

	if (buffer == NULL)
		return (ERROR_BAD_ARG("buffer"));

	if ((fd = open(path, O_RDONLY)) == -1) {
		return (pkg_error_set(EPKG_FATAL, "can not open %s: %s", path,
				strerror(errno)));
	}

	if (fstat(fd, &st) == -1) {
		close(fd);
		return (pkg_error_set(EPKG_FATAL, "fstat(): %s", strerror(errno)));
	}

	if ((*buffer = malloc(st.st_size + 1)) == NULL) {
		close(fd);
		return (pkg_error_set(EPKG_FATAL, "malloc(): %s", strerror(errno)));
	}

	if (read(fd, *buffer, st.st_size) == -1) {
		close(fd);
		return (pkg_error_set(EPKG_FATAL, "read(): %s", strerror(errno)));
	}

	close(fd);

	/* NULL terminate the buffer so it can be used by stdio.h functions */
	(*buffer)[st.st_size] = '\0';

	*sz = st.st_size;
	return (EPKG_OK);
}

int
format_exec_cmd(char **dest, const char *in, const char *prefix, const char *plist_file)
{
	struct sbuf *buf = sbuf_new_auto();
	char path[MAXPATHLEN];
	char *cp;

	while (in[0] != '\0') {
		if (in[0] == '%') {
			in++;
			switch(in[0]) {
				case 'D':
					sbuf_cat(buf, prefix);
					break;
				case 'F':
					sbuf_cat(buf, plist_file);
					break;
				case 'f':
					if (prefix[strlen(prefix) - 1] == '/')
						snprintf(path, MAXPATHLEN, "%s%s", prefix, plist_file);
					else
						snprintf(path, MAXPATHLEN, "%s/%s", prefix, plist_file);
					cp = strrchr(path, '/');
					cp ++;
					sbuf_cat(buf, cp);
					break;
				case 'B':
					if (prefix[strlen(prefix) - 1] == '/')
						snprintf(path, MAXPATHLEN, "%s%s", prefix, plist_file);
					else
						snprintf(path, MAXPATHLEN, "%s/%s", prefix, plist_file);
					cp = strrchr(path, '/');
					cp[0] = '\0';
					sbuf_cat(buf, path);
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
	
	return (0);
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
sha256_hash(unsigned char hash[SHA256_DIGEST_LENGTH], char out[65])
{
	int i;
	for (i = 0; i < SHA256_DIGEST_LENGTH; i++)
		sprintf(out + (i * 2), "%02x", hash[i]);

	out[64] = '\0';
}

void
sha256_str(const char *string, char out[65])
{
	unsigned char hash[SHA256_DIGEST_LENGTH];
	SHA256_CTX sha256;

	SHA256_Init(&sha256);
	SHA256_Update(&sha256, string, strlen(string));
	SHA256_Final(hash, &sha256);

	sha256_hash(hash, out);
}

int
sha256_file(const char *path, char out[65])
{
	FILE *fp;
	char buffer[BUFSIZ];
	unsigned char hash[SHA256_DIGEST_LENGTH];
	size_t r = 0;
	SHA256_CTX sha256;

	if ((fp = fopen(path, "rb")) == NULL)
		return (pkg_error_set(EPKG_FATAL, "fopen(%s): %s", path,
							  strerror(errno)));

	SHA256_Init(&sha256);

	while ((r = fread(buffer, 1, BUFSIZ, fp)) > 0)
		SHA256_Update(&sha256, buffer, r);

	if (ferror(fp) != 0) {
		fclose(fp);
		out[0] = '\0';
		return (pkg_error_set(EPKG_FATAL, "fread(%s): %s", path,
							  strerror(errno)));

	}

	fclose(fp);

	SHA256_Final(hash, &sha256);
	sha256_hash(hash, out);

	return (EPKG_OK);
}

int
is_conf_file(const char *path, char newpath[MAXPATHLEN])
{
	size_t n;
	char *p = NULL;

	n = strlen(path);

	if (n < 8)
		return (0);

	p = strrchr(path, '.');

	if (p != NULL && !strcmp(p, ".pkgconf")) {
		strlcpy(newpath, path, MAXPATHLEN);
		newpath[n - 8] = '\0';
		return (1);
	}

	return (0);
}
