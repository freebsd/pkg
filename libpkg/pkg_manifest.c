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

	pkg_set(pkg, PKG_NAME, buf);

	return (0);
}

static int
m_parse_origin(struct pkg *pkg, char *buf)
{
	while (isspace(*buf))
		buf++;

	pkg_set(pkg, PKG_ORIGIN, buf);

	return (0);
}

static int
m_parse_version(struct pkg *pkg, char *buf)
{
	while (isspace(*buf))
		buf++;

	pkg_set(pkg, PKG_VERSION, buf);

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

	pkg_set(pkg, PKG_COMMENT, buf);

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
				pkg_set(dep, PKG_NAME, buf_ptr);
				break;
			case 1:
				pkg_set(dep, PKG_ORIGIN, buf_ptr);
				break;
			case 2:
				pkg_set(dep, PKG_VERSION, buf_ptr);
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

int
pkg_emit_manifest(struct pkg *pkg, char **dest)
{
	struct sbuf *manifest;
	struct pkg **deps;
	struct pkg_conflict **conflicts;
	struct pkg_exec **execs;
	struct pkg_option **options;
	int i;
	int len = 0;

	manifest = sbuf_new_auto();

	sbuf_printf(manifest, "@pkg_format_version 0.9\n"
			"@name %s\n"
			"@version %s\n"
			"@origin %s\n"
			"@comment %s\n"
			"@arch %s\n"
			"@osversion %s\n"
			"@www %s\n"
			"@maintainer %s\n",
			pkg_get(pkg, PKG_NAME),
			pkg_get(pkg, PKG_VERSION),
			pkg_get(pkg, PKG_ORIGIN),
			pkg_get(pkg, PKG_COMMENT),
			pkg_get(pkg, PKG_ARCH),
			pkg_get(pkg, PKG_OSVERSION),
			pkg_get(pkg, PKG_WWW),
			pkg_get(pkg, PKG_MAINTAINER) ? pkg_get(pkg, PKG_MAINTAINER) : "UNKNOWN"
			);

	if ((deps = pkg_deps(pkg)) != NULL) {
		for (i = 0; deps[i] != NULL; i++) {
			sbuf_printf(manifest, "@dep %s %s %s\n", 
					pkg_get(deps[i], PKG_NAME),
					pkg_get(deps[i], PKG_ORIGIN),
					pkg_get(deps[i], PKG_VERSION));
		}
	}

	if ((conflicts = pkg_conflicts(pkg)) != NULL) {
		for (i = 0; conflicts[i] != NULL; i++) {
			sbuf_printf(manifest, "@conflict %s\n", pkg_conflict_glob(conflicts[i]));
		}
	}

	if ((execs = pkg_execs(pkg)) != NULL) {
		for (i = 0; execs[i] != NULL; i++) {
			sbuf_printf(manifest, "@%s %s\n",
					pkg_exec_type(execs[i]) == PKG_EXEC ? "exec" : "unexec",
					pkg_exec_cmd(execs[i]));
		}
	}

	if ((options = pkg_options(pkg)) != NULL)  {
		for (i = 0; options[i] != NULL; i++) {
			sbuf_printf(manifest, "@option %s %s\n",
					pkg_option_opt(options[i]),
					pkg_option_value(options[i]));
					
		}
	}

	sbuf_finish(manifest);
	len = sbuf_len(manifest);
	*dest = strdup(sbuf_data(manifest));

	sbuf_free(manifest);

	return (len);
}
