#include <sys/types.h>
#include <sys/sbuf.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "pkg.h"
#include "pkg_error.h"
#include "pkg_util.h"
#include "pkg_private.h"

static int m_parse_name(struct pkg *pkg, char *buf);
static int m_parse_origin(struct pkg *pkg, char *buf);
static int m_parse_version(struct pkg *pkg, char *buf);
static int m_parse_arch(struct pkg *pkg, char *buf);
static int m_parse_osversion(struct pkg *pkg, char *buf);
static int m_parse_www(struct pkg *pkg, char *buf);
static int m_parse_comment(struct pkg *pkg, char *buf);
static int m_parse_flatsize(struct pkg *pkg, char *buf);
static int m_parse_option(struct pkg *pkg, char *buf);
static int m_parse_dep(struct pkg *pkg, char *buf);
static int m_parse_conflict(struct pkg *pkg, char *buf);
static int m_parse_maintainer(struct pkg *pkg, char *buf);
static int m_parse_prefix(struct pkg *pkg, char *buf);
static int m_parse_file(struct pkg *pkg, char *buf);
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
	{ "@flatsize", m_parse_flatsize},
	{ "@option", m_parse_option},
	{ "@dep", m_parse_dep},
	{ "@conflict", m_parse_conflict},
	{ "@maintainer", m_parse_maintainer},
	{ "@prefix", m_parse_prefix},
	{ "@file", m_parse_file},
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
m_parse_prefix(struct pkg *pkg, char *buf) {
	return (m_parse_set_string(pkg, buf, PKG_PREFIX));
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
m_parse_flatsize(struct pkg *pkg, char *buf)
{
	int64_t size;

	/*
	 * Set errno to 0 to make sure that the error we will eventually catch
	 * later was set by strtoimax()
	 */
	errno = 0;
	size = strtoimax(buf, NULL, 10);

	if (errno == EINVAL || errno == ERANGE)
		return (pkg_error_set(EPKG_FATAL, "m_parse_flatsize(): %s",
				strerror(errno)));

	pkg_setflatsize(pkg, size);
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

static int
m_parse_file(struct pkg *pkg, char *buf)
{
	const char *path;
	const char *sha256;

	while (isspace(*buf))
		buf++;

	if (split_chr(buf, ' ') != 1)
		return (EPKG_FATAL);

	path = buf;

	buf += strlen(path) + 1;
	sha256 = buf;

	pkg_addfile(pkg, path, sha256);

	return (EPKG_OK);
}

int
pkg_parse_manifest(struct pkg *pkg, char *buf)
{
	int nbel;
	int i, j;
	char *buf_ptr;
	size_t next;
	int found;

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
		found = 0;
		for (j = 0; j < manifest_key_len; j++) {
			if (STARTS_WITH(buf_ptr, manifest_key[j].key)) {
				found = 1;
				if (manifest_key[j].parse(pkg, buf_ptr +
					strlen(manifest_key[j].key)) != EPKG_OK) {

					warnx("Error while parsing %s at line %d",
						  manifest_key[j].key, i + 1);
					return (EPKG_FATAL);
				}
				break;
			}
		}
		if (found == 0 && buf_ptr[0] != '\0') {
			warnx("Unknown line #%d: %s (ignored)", i + 1,  buf_ptr);
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
	struct pkg_option **options;
	struct pkg_file **files;
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
			"@maintainer %s\n"
			"@prefix %s\n"
			"@flatsize %" PRId64 "\n",
			pkg_get(pkg, PKG_NAME),
			pkg_get(pkg, PKG_VERSION),
			pkg_get(pkg, PKG_ORIGIN),
			pkg_get(pkg, PKG_COMMENT),
			pkg_get(pkg, PKG_ARCH),
			pkg_get(pkg, PKG_OSVERSION),
			pkg_get(pkg, PKG_WWW),
			pkg_get(pkg, PKG_MAINTAINER),
			pkg_get(pkg, PKG_PREFIX),
			pkg_flatsize(pkg)
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

	if ((options = pkg_options(pkg)) != NULL)  {
		for (i = 0; options[i] != NULL; i++) {
			sbuf_printf(manifest, "@option %s %s\n",
					pkg_option_opt(options[i]),
					pkg_option_value(options[i]));
					
		}
	}

	if ((files = pkg_files(pkg)) != NULL) {
		for (i = 0; files[i] != NULL; i++) {
			sbuf_printf(manifest, "@file %s %s\n", pkg_file_path(files[i]),
						pkg_file_sha256(files[i]));
		}
	}

	sbuf_finish(manifest);
	len = sbuf_len(manifest);
	*dest = strdup(sbuf_data(manifest));

	sbuf_free(manifest);

	return (len);
}
