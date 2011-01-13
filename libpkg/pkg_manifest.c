#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/sbuf.h>

#include "util.h"
#include "pkg.h"
#include "pkg_private.h"

static int m_parse_name(struct pkg *pkg, char *buf);
static int m_parse_origin(struct pkg *pkg, char *buf);
static int m_parse_version(struct pkg *pkg, char *buf);
static int m_parse_arch(struct pkg *pkg, char *buf);
static int m_parse_osrelease(struct pkg *pkg, char *buf);
static int m_parse_osversion(struct pkg *pkg, char *buf);
static int m_parse_build_time(struct pkg *pkg, char *buf);
static int m_parse_www(struct pkg *pkg, char *buf);
static int m_parse_comment(struct pkg *pkg, char *buf);
static int m_parse_license(struct pkg *pkg, char *buf);
static int m_parse_option(struct pkg *pkg, char *buf);
static int m_parse_dep(struct pkg *pkg, char *buf);
static int m_parse_conflict(struct pkg *pkg, char *buf);

#define MANIFEST_FORMAT_KEY "@pkg_format_version"

static struct manifest_key {
	const char *key;
	int (*parse)(struct pkg *pkg, char *buf);
} manifest_key[] = {
	{ "@name", m_parse_name},
	{ "@origin", m_parse_origin},
	{ "@version", m_parse_version},
	{ "@arch", m_parse_arch},
	{ "@osrelease", m_parse_osrelease},
	{ "@osversion", m_parse_osversion},
	{ "@build_time", m_parse_build_time},
	{ "@www", m_parse_www},
	{ "@comment", m_parse_comment},
	{ "@license", m_parse_license},
	{ "@option", m_parse_option},
	{ "@dep", m_parse_dep},
	{ "@conflict", m_parse_conflict},
};

#define manifest_key_len (int)(sizeof(manifest_key)/sizeof(manifest_key[0]))

static int
m_parse_name(struct pkg *pkg, char *buf)
{
	/* remove trailing spaces */
	while (isspace(*buf))
		buf++;

	pkg_setname(pkg, buf);

	return (0);
}

static int
m_parse_origin(struct pkg *pkg, char *buf)
{
	while (isspace(*buf))
		buf++;

	pkg_setorigin(pkg, buf);

	return (0);
}

static int
m_parse_version(struct pkg *pkg, char *buf)
{
	while (isspace(*buf))
		buf++;

	pkg_setversion(pkg, buf);

	return (0);
}

static int
m_parse_arch(struct pkg *pkg, char *buf)
{
	/* TODO */
	(void)pkg;
	(void)buf;
	return (0);
}

static int
m_parse_osrelease(struct pkg *pkg, char *buf)
{
	/* TODO */
	(void)pkg;
	(void)buf;
	return (0);
}

static int
m_parse_osversion(struct pkg *pkg, char *buf)
{
	/* TODO */
	(void)pkg;
	(void)buf;
	return (0);
}

static int
m_parse_build_time(struct pkg *pkg, char *buf)
{
	/* TODO */
	(void)pkg;
	(void)buf;
	return (0);
}

static int
m_parse_www(struct pkg *pkg, char *buf)
{
	/* TODO */
	(void)pkg;
	(void)buf;
	return (0);
}

static int
m_parse_comment(struct pkg *pkg, char *buf)
{
	while (isspace(*buf))
		buf++;

	pkg_setcomment(pkg, buf);

	return (0);
}

static int
m_parse_license(struct pkg *pkg, char *buf)
{
	/* TODO */
	(void)pkg;
	(void)buf;
	return (0);
}

static int
m_parse_option(struct pkg *pkg, char *buf)
{
	/* TODO */
	(void)pkg;
	(void)buf;
	return (0);
}

static int
m_parse_dep(struct pkg *pkg, char *buf)
{
	struct pkg *dep;
	char *buf_ptr;
	int nbel, i;
	size_t next;

	while (isspace(*buf))
		buf++;

	buf_ptr = buf;

	nbel = split_chr(buf_ptr, ' ');

	pkg_new(&dep);

	next = strlen(buf_ptr);
	for (i = 0; i <= nbel; i++) {
		switch(i) {
			case 0:
				pkg_setname(dep, buf_ptr);
				break;
			case 1:
				pkg_setorigin(dep, buf_ptr);
				break;
			case 2:
				pkg_setversion(dep, buf_ptr);
				break;
		}
		buf_ptr += next + 1;
		next = strlen(buf_ptr);
	}

	dep->type = PKG_NOTFOUND;
	array_append(&pkg->deps, dep);

	return (0);
}

static int
m_parse_conflict(struct pkg *pkg, char *buf)
{
	/* TODO */
	(void)pkg;
	(void)buf;
	return (0);
}

int
pkg_parse_manifest(struct pkg *pkg, char *buf)
{
	int nbl;
	int i, j;
	char *buf_ptr;
	size_t next;


	nbl = split_chr(buf, '\n');

	buf_ptr = buf;
	if (!STARTS_WITH(buf, MANIFEST_FORMAT_KEY)) {
		warn("Not a package manifest");
		return (-1);
	}

	next = strlen(buf_ptr);
	buf_ptr += next + 1;
	next = strlen(buf_ptr);
	for (i = 1; i <= nbl; i++) {
		for (j = 0; j < manifest_key_len; j++) {
			if (STARTS_WITH(buf_ptr, manifest_key[j].key)) {
				manifest_key[j].parse(pkg, buf_ptr + strlen(manifest_key[j].key));
				break;
			}
		}
		/* We need to compute `next' only if we are not at the end of the manifest */
		if (i != nbl) {
			buf_ptr += next + 1;
			next = strlen(buf_ptr);
		}
	}

	return (0);
}
