#include <sys/stat.h>
#include <sys/param.h>
#include <stdio.h>

#include <assert.h>
#include <dirent.h>
#include <err.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fetch.h>
#include <libutil.h>

#include "util.h"

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

	if (free_elm != NULL)
		for (size_t i = 0; i < a->len; i++)
			free_elm(a->data[i]);
	free(a->data);
	a->data = NULL;
	a->len = 0;
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

off_t
file_to_buffer(const char *path, char **buffer)
{
	int fd;
	struct stat st;

	assert(path != NULL);
	assert(buffer != NULL);

	if ((fd = open(path, O_RDONLY)) == -1) {
		warn("open(%s)", path);
		return (-1);
	}

	if (fstat(fd, &st) == -1) {
		warn("fstat(%d)", fd);
		close(fd);
		return (-1);
	}

	if ((*buffer = malloc(st.st_size + 1)) == NULL) {
		warn("malloc(%llu)", (unsigned long long)st.st_size + 1);
		close(fd);
		return (-1);
	}

	if (read(fd, *buffer, st.st_size) == -1) {
		warn("read()");
		close(fd);
		return (-1);
	}

	close(fd);

	/* NULL terminate the buffer so it can be used by stdio.h functions */
	(*buffer)[st.st_size] = '\0';

	return (st.st_size);
}

char *
str_replace(char *string, const char *find, char *replace)
{
	char *str, *end, *begin;
	size_t offset, replace_len, find_len;

	replace_len = strlen(replace);
	find_len = strlen(find);
	begin = string;
	str = NULL;
	offset = 0;

	while ((end = strstr(begin, find)) != NULL) {
		str = realloc(str, offset + replace_len + (end - begin));
		memcpy(str + offset, begin, end - begin);
		memcpy(str + offset + (end - begin), replace, replace_len);
		offset += (end - begin + replace_len);
		begin = end + find_len;
	}
	str = realloc(str, offset + strlen(begin) +1);
	memcpy(str+offset, begin, strlen(begin)+1);

	return (str);
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
	char buf[BUFSIZ], sz[8];

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

		if ((now - begin_dl) > 0)
			humanize_number(sz, 8, (int64_t)(tfetched / (now - begin_dl)),
					"Bps", HN_AUTOSCALE, HN_DECIMAL);
		else
			humanize_number(sz, 8, 0,
					"Bps", HN_AUTOSCALE, HN_DECIMAL);
		printf("\r%s\t%s %d%%", url, sz, (int)(((float)tfetched / (float)st.size) * 100));
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
