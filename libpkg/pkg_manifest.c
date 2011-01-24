#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/sbuf.h>

#include "pkg_util.h"
#include "pkg.h"
#include "pkg_private.h"

static int m_parse_name(struct pkg *pkg, char *buf);
static int m_parse_origin(struct pkg *pkg, char *buf);
static int m_parse_version(struct pkg *pkg, char *buf);
static int m_parse_arch(struct pkg *pkg, char *buf);
static int m_parse_osversion(struct pkg *pkg, char *buf);
static int m_parse_www(struct pkg *pkg, char *buf);
static int m_parse_comment(struct pkg *pkg, char *buf);
static int m_parse_option(struct pkg *pkg, char *buf);
static int m_parse_dep(struct pkg *pkg, char *buf);
static int m_parse_conflict(struct pkg *pkg, char *buf);
static int m_parse_maintainer(struct pkg *pkg, char *buf);
static int m_parse_exec(struct pkg *pkg, char *buf);
static int m_parse_set_string(struct pkg *pkg, char *buf, pkg_attr attr);

#define MANIFEST_FORMAT_KEY "@pkg_format_version"

static struct manifest_key {
	const char *key;
	int (*parse)(struct pkg *pkg, char *buf);
} manifest_key[] = {
	{ "@name", m_parse_name},
	{ "@origin", m_parse_origin},
	{ "@version", m_parse_version},
	{ "@arch", m_parse_arch},
	{ "@osversion", m_parse_osversion},
	{ "@www", m_parse_www},
	{ "@comment", m_parse_comment},
	{ "@option", m_parse_option},
	{ "@dep", m_parse_dep},
	{ "@conflict", m_parse_conflict},
	{ "@maintainer", m_parse_maintainer},
	{ "@exec", m_parse_exec},
};

#define manifest_key_len (int)(sizeof(manifest_key)/sizeof(manifest_key[0]))

static int
m_parse_set_string(struct pkg *pkg, char *buf, pkg_attr attr) {
	while (isspace(*buf))
		buf++;

	if (*buf == '\0')
		return (EPKG_FATAL);

	pkg_set(pkg, attr, buf);

	return (EPKG_OK);
}

static int
m_parse_www(struct pkg *pkg, char *buf) {
	return (m_parse_set_string(pkg, buf, PKG_WWW));
}

static int
m_parse_maintainer(struct pkg *pkg, char *buf) {
	return (m_parse_set_string(pkg, buf, PKG_MAINTAINER));
}

static int
m_parse_name(struct pkg *pkg, char *buf)
{
	return (m_parse_set_string(pkg, buf, PKG_NAME));
}

static int
m_parse_origin(struct pkg *pkg, char *buf)
{
	return (m_parse_set_string(pkg, buf, PKG_ORIGIN));
}

static int
m_parse_version(struct pkg *pkg, char *buf)
{
	return (m_parse_set_string(pkg, buf, PKG_VERSION));
}

static int
m_parse_arch(struct pkg *pkg, char *buf)
{
	return (m_parse_set_string(pkg, buf, PKG_ARCH));
}

static int
m_parse_osversion(struct pkg *pkg, char *buf)
{
	return (m_parse_set_string(pkg, buf, PKG_OSVERSION));
}

static int
m_parse_comment(struct pkg *pkg, char *buf)
{
	return (m_parse_set_string(pkg, buf, PKG_COMMENT));
}

static int
m_parse_exec(struct pkg *pkg, char *buf)
{
	while (isspace(*buf))
		buf++;

	if (*buf == '\0')
		return (EPKG_FATAL);

	pkg_addexec(pkg, buf, PKG_EXEC);

	return (EPKG_OK);
}

static int
m_parse_option(struct pkg *pkg, char *buf)
{
	char *value;

	while (isspace(*buf))
		buf++;

	if (*buf == '\0')
		return (EPKG_FATAL);

	value = strrchr(buf, ' ');
	if (value == NULL)
		return (EPKG_FATAL);

	value[0] = '\0';
	value++;

	pkg_addoption(pkg, buf, value);

	return (EPKG_OK);
}

static int
m_parse_dep(struct pkg *pkg, char *buf)
{
	char *buf_ptr;
	size_t next;
	const char *name, *origin, *version;

	while (isspace(*buf))
		buf++;

	buf_ptr = buf;

	if (split_chr(buf_ptr, ' ') != 2)
		return (EPKG_FATAL);

	next = strlen(buf_ptr);
	name = buf_ptr;

	buf_ptr += next + 1;
	next = strlen(buf_ptr);

	origin = buf_ptr;

	buf_ptr += next + 1;
	next = strlen(buf_ptr);

	version = buf_ptr;

	pkg_adddep(pkg, name, origin, version);

	return (EPKG_OK);
}

static int
m_parse_conflict(struct pkg *pkg, char *buf)
{
	while (isspace(*buf))
		buf++;

	if (*buf == '\0')
		return (EPKG_FATAL);

	pkg_addconflict(pkg, buf);

	return (EPKG_OK);
}

int
pkg_parse_manifest(struct pkg *pkg, char *buf)
{
	int nbel;
	int i, j;
	char *buf_ptr;
	size_t next;


	nbel = split_chr(buf, '\n');

	buf_ptr = buf;
	if (!STARTS_WITH(buf, MANIFEST_FORMAT_KEY)) {
		warn("Not a package manifest");
		return (-1);
	}

	next = strlen(buf_ptr);
	buf_ptr += next + 1;
	next = strlen(buf_ptr);
	for (i = 1; i <= nbel; i++) {
		for (j = 0; j < manifest_key_len; j++) {
			if (STARTS_WITH(buf_ptr, manifest_key[j].key)) {
				if (manifest_key[j].parse(pkg, buf_ptr + strlen(manifest_key[j].key)) != EPKG_OK)
					return (EPKG_FATAL);
				break;
			}
		}
		/* We need to compute `next' only if we are not at the end of the manifest */
		if (i != nbel) {
			buf_ptr += next + 1;
			next = strlen(buf_ptr);
		}
	}

	return (EPKG_OK);
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
