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
#include "pkg_event.h"
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
		EMIT_ERRNO("open", dest);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	while (remote == NULL) {
		remote = fetchXGetURL(url, &st, "");
		if (remote == NULL) {
			--retry;
			if (retry == 0) {
				EMIT_PKG_ERROR("%s: %s", url, fetchLastErrString);
				retcode = EPKG_FATAL;
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
			EMIT_ERRNO("write", dest);
			retcode = EPKG_FATAL;
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
		EMIT_PKG_ERROR("%s: %s", url, fetchLastErrString);
		retcode = EPKG_FATAL;
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
