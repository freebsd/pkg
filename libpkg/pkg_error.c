#include <err.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pkg.h"
#include "pkg_error.h"

struct pkg_error {
	pkg_error_t number;
	char *string;
};

pthread_once_t pkg_error_once = PTHREAD_ONCE_INIT;
pthread_key_t pkg_error_key;

static struct pkg_error * pkg_error_init(void);
static void pkg_error_init_once(void);
static void pkg_error_key_free(void *);

pkg_error_t
_pkg_error_set(pkg_error_t num, const char *fmt, ...)
{
	struct pkg_error *e;
	char *oldstring;
	va_list ap;

	e = pkg_error_init();

	oldstring = e->string;

	e->number = num;

	va_start(ap, fmt);
	vasprintf(&e->string, fmt, ap);
	va_end(ap);

	if (oldstring != NULL)
		free(oldstring);

	return (num);
}

pkg_error_t
pkg_error_number(void)
{
	struct pkg_error *e;

	e = pkg_error_init();

	return (e->number);
}

const char *
pkg_error_string(void)
{
	struct pkg_error *e;

	e = pkg_error_init();

	if (e->number == EPKG_OK)
		return ("(Empty error message)");
	else
		return (e->string);
}

void
pkg_error_warn(const char *fmt, ...)
{
	va_list ap;
	char *str = NULL;

	va_start(ap, fmt);
	vasprintf(&str, fmt, ap);
	va_end(ap);

	warnx("%s: %s\n", str, pkg_error_string());
	free(str);
}

static struct pkg_error *
pkg_error_init(void)
{
	struct pkg_error *e;

	if (pthread_once(&pkg_error_once, pkg_error_init_once) != 0)
		err(1, "pthread_once()");

	e = pthread_getspecific(pkg_error_key);

	if (e == NULL) {
		e = malloc(sizeof(struct pkg_error));

		if (e == NULL)
			err(1, "malloc()");

		e->number = EPKG_OK;
		e->string = NULL;

		if (pthread_setspecific(pkg_error_key, e) != 0)
			err(1, "pthread_setspecific()");
	}

	return (e);
}

static void
pkg_error_init_once(void)
{
	if (pthread_key_create(&pkg_error_key, pkg_error_key_free) != 0)
		err(1, "pthread_key_create()");
}

static void
pkg_error_key_free(void *data)
{
	struct pkg_error *e = data;

	free(e->string);
	free(e);
}
