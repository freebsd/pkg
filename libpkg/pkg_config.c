#include <sys/types.h>

#include <err.h>
#include <fcntl.h>
#include <libutil.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pkg.h"
#include "pkg_error.h"

struct _config {
	const char *key;
	const char *def;
	const char *val;
} c[] = {
	{ "PACKAGESITE", NULL, NULL},
	{ "PKG_DBDIR", "/var/db/pkg", NULL},
	{ "PKG_CACHEDIR", "/var/lib/pkg", NULL},
	{ NULL, NULL, NULL}
};

static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static int done = 0;

static void
load_config(void)
{
	properties p = NULL;
	int fd;
	int i;

	if ((fd = open("/etc/pkg.conf", O_RDONLY)) > 0) {
		p = properties_read(fd);
		close(fd);
	}

	for (i = 0; c[i].key != NULL; i++) {
		if ((c[i].val = getenv(c[i].key)) == NULL && p != NULL)
			c[i].val = property_find(p, c[i].key);
	}

	done = 1;

	if (p != NULL)
		properties_free(p);
}

const char *
pkg_config(const char *key)
{
	int i;

	if (done == 0) {
		if (pthread_mutex_lock(&m) != 0)
			err(1, "pthread_mutex_lock()");
		if (done == 0)
			load_config();
		if (pthread_mutex_unlock(&m) != 0)
			err(1, "pthread_mutex_unlock()");
	}

	for (i = 0; c[i].key != NULL; i++) {
		if (strcmp(c[i].key, key) == 0) {
			if (c[i].val != NULL)
				return (c[i].val);
			else
				return (c[i].def);
		}
	}

	pkg_emit_event(PKG_EVENT_CONFIG_KEY_NOTFOUND, /*argc*/1, key);
	return (NULL);
}
