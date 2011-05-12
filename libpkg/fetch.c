#include <sys/param.h>

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <fetch.h>

#include "pkg.h"
#include "pkg_error.h"

int
pkg_fetch_file(const char *url, const char *dest, void *data, fetch_cb cb)
{
	int fd = -1;
	FILE *remote = NULL;
	struct url_stat st;
	off_t done = 0;
	off_t r;
	int retry = 3;
	time_t begin_dl;
	time_t now;
	time_t last = 0;
	char buf[10240];
	int retcode = EPKG_OK;

	if ((fd = open(dest, O_WRONLY|O_CREAT|O_TRUNC, 0644)) == -1) {
		retcode = pkg_error_set(EPKG_FATAL, "open(%s): %s", dest,
								strerror(errno));
		goto cleanup;
	}

	while (remote == NULL) {
		remote = fetchXGetURL(url, &st, "");
		if (remote == NULL) {
			--retry;
			if (retry == 0) {
				retcode = pkg_error_set(EPKG_FATAL, "%s", fetchLastErrString);
				goto cleanup;
			}
			sleep(1);
		}
	}

	begin_dl = time(NULL);
	while (done < st.size) {
		if ((r = fread(buf, 1, sizeof(buf), remote)) < 1)
			break;

		if (write(fd, buf, r) != r) {
			retcode = pkg_error_set(EPKG_FATAL, "write(%s): %s", dest,
									strerror(errno));
			goto cleanup;
		}

		done += r;
		now = time(NULL);
		/* Only call the callback every second */
		if (cb != NULL && (now > last || done == st.size)) {
			cb(data, url, st.size, done, (now - begin_dl));
			last = now;
		}
	}

	if (ferror(remote)) {
		retcode = pkg_error_set(EPKG_FATAL, "%s", fetchLastErrString);
		goto cleanup;
	}

	cleanup:

	if (fd > 0)
		close(fd);

	if (remote != NULL)
		fclose(remote);

	/* Remove local file if fetch failed */
	if (retcode != EPKG_OK)
		unlink(dest);

	return (retcode);
}

int
pkg_fetch_buffer(const char *url, char **buffer, void *data, fetch_cb cb)
{
	FILE *remote = NULL;
	struct url_stat st;
	off_t done = 0;
	off_t r;
	int retry = 3;
	time_t begin_dl;
	time_t now;
	time_t last = 0;
	int retcode = EPKG_OK;

	while (remote == NULL) {
		remote = fetchXGetURL(url, &st, "");
		if (remote == NULL) {
			--retry;
			if (retry == 0) {
				pkg_error_set(EPKG_FATAL, "%s", fetchLastErrString);
				goto cleanup;
			}
			sleep(1);
		}
	}

	*buffer = malloc(st.size + 1);

	begin_dl = time(NULL);
	while (done < st.size) {
		if ((r = fread(*buffer + done, 1, 10240, remote)) < 1)
			break;

		done += r;

		now = time(NULL);
		/* Only call the callback every second */

		if (cb != NULL && (now > last || done == st.size)) {
			cb(data, url, st.size, done, (now - begin_dl));
			last = now;
		}
	}

	if (ferror(remote)) {
		retcode = pkg_error_set(EPKG_FATAL, "%s", fetchLastErrString);
		goto cleanup;
	}

	cleanup:

	if (remote != NULL)
		fclose(remote);

	return (retcode);
}
