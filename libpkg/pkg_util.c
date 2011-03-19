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
#include <fetch.h>
#include <libutil.h>

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
file_fetch(const char *url, const char *dest)
{
	int fd;
	FILE *remote = NULL;
	struct url_stat st;
	off_t tfetched, rfetched, wfetched;
	int retry = 3;
	time_t begin_dl, now;
	char buf[BUFSIZ];

	if ((fetchStatURL(url, &st, "") < 0) || st.size == -1) {
		/* TODO error handling */
		return (-1);
	}

	while (remote == NULL) {
		remote = fetchXGetURL(url, &st, "");
		if (remote == NULL) {
			/* TODO err handling */
			sleep(1);
			--retry;
		}

		if (retry == 0) {
			/* TODO err handling */
			return (-1);
		}
	}

	if (st.size > SSIZE_MAX - 1) {
		/* TODO err handling */
		return (-1);
	}

	if ((fd = open(dest, O_WRONLY|O_CREAT|O_TRUNC, 0644)) == -1) {
		/* TODO err handling */
		return (-1);
	}

	tfetched = 0;
	begin_dl = time(NULL);
	while (tfetched < st.size) {
		if ((rfetched = fread(buf, 1, sizeof(buf), remote)) < 1)
			break;

		if ((wfetched = write(fd, buf, rfetched)) != rfetched)
			break;

		tfetched +=  rfetched;
		now = time(NULL);
/* TODO: callback, this is the job of the UI */
#if 0
		if ((now - begin_dl) > 0)
			humanize_number(sz, 8, (int64_t)(tfetched / (now - begin_dl)),
					"Bps", HN_AUTOSCALE, HN_DECIMAL);
		else
			humanize_number(sz, 8, 0,
					"Bps", HN_AUTOSCALE, HN_DECIMAL);
		printf("\r%s\t%s %d%%", url, sz, (int)(((float)tfetched / (float)st.size) * 100));
#endif
	}
	printf("\n");

	if (ferror(remote)) {
		/* TODO err handling */
		return (-1);
	}

	close(fd);
	fclose(remote);

	return (0);
}

int
is_dir(const char *path) {
	struct stat st;

	return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}
