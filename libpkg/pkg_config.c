/*
 * Copyright (c) 2011-2025 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2014 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2016 Vsevolod Stakhov <vsevolod@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "pkg_config.h"

#include <assert.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <ctype.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#ifdef HAVE_OSRELDATE_H
#include <osreldate.h>
#endif
#include <ucl.h>

#include <curl/curl.h>

#include <archive.h>
#include <sqlite3.h>
#include <openssl/crypto.h>

#include "pkg.h"
#include "private/pkg.h"
#include "private/pkg_abi.h"
#include "private/event.h"
#include "private/fetch.h"
#include "pkg_repos.h"

#ifndef PORTSDIR
#define PORTSDIR "/usr/ports"
#endif
#ifndef DEFAULT_VULNXML_URL
#define DEFAULT_VULNXML_URL "https://vuxml.freebsd.org/freebsd/vuln.xml.xz"
#endif
#ifndef DEFAULT_OSVF_URL
#define DEFAULT_OSVF_URL "https://raw.githubusercontent.com/illuusio/freebsd-osv/refs/heads/main/db/freebsd-osv.json"
#endif

#ifdef	OSMAJOR
#define STRINGIFY(X)	TEXT(X)
#define TEXT(X)		#X
#define INDEXFILE	"INDEX-" STRINGIFY(OSMAJOR)
#else
#define INDEXFILE	"INDEX"
#endif

#define dbg(x, ...) pkg_dbg(PKG_DBG_CONFIG, x, __VA_ARGS__)

struct pkg_ctx ctx = {
	.eventpipe = -1,
	.debug_level = 0,
	.developer_mode = false,
	.pkg_rootdir = NULL,
	.metalog = NULL,
	.dbdir = NULL,
	.cachedir = NULL,
	.rootfd = -1,
	.cachedirfd = -1,
	.pkg_dbdirfd = -1,
	.pkg_reposdirfd = -1,
	.devnullfd = -1,
	.backup_libraries = false,
	.triggers = true,
	.compression_format = NULL,
	.compression_level = -1,
	.compression_threads = -1,
	.defer_triggers = false,
	.no_version_for_deps = false,
};

struct config_entry {
	uint8_t type;
	const char *key;
	const char *def;
};

static struct pkg_repo *repos = NULL;
ucl_object_t *config = NULL;

static struct config_entry c[] = {
	{
		PKG_STRING,
		"PKG_DBDIR",
		"/var/db/pkg",
	},
	{
		PKG_STRING,
		"PKG_CACHEDIR",
		"/var/cache/pkg",
	},
	{
		PKG_STRING,
		"PORTSDIR",
		"/usr/ports",
	},
	{
		PKG_STRING,
		"INDEXDIR",
		NULL,		/* Default to PORTSDIR unless defined */
	},
	{
		PKG_STRING,
		"INDEXFILE",
		INDEXFILE,
	},
	{
		PKG_BOOL,
		"HANDLE_RC_SCRIPTS",
		"NO",
	},
	{
		PKG_BOOL,
		"DEFAULT_ALWAYS_YES",
		"NO",
	},
	{
		PKG_BOOL,
		"ASSUME_ALWAYS_YES",
		"NO",
	},
	{
		PKG_ARRAY,
		"REPOS_DIR",
		"/etc/pkg/,"PREFIX"/etc/pkg/repos/",
	},
	{
		PKG_STRING,
		"PLIST_KEYWORDS_DIR",
		NULL,
	},
	{
		PKG_BOOL,
		"SYSLOG",
		"YES",
	},
	{
		PKG_BOOL,
		"DEVELOPER_MODE",
		"NO",
	},
	{
		PKG_STRING,
		"VULNXML_SITE",
		DEFAULT_OSVF_URL,
	},
	{
		PKG_STRING,
		"OSVF_SITE",
		DEFAULT_OSVF_URL,
	},
	{
		PKG_INT,
		"FETCH_RETRY",
		"3",
	},
	{
		PKG_STRING,
		"PKG_PLUGINS_DIR",
		PREFIX"/lib/pkg/",
	},
	{
		PKG_BOOL,
		"PKG_ENABLE_PLUGINS",
		"YES",
	},
	{
		PKG_ARRAY,
		"PLUGINS",
		NULL,
	},
	{
		PKG_BOOL,
		"DEBUG_SCRIPTS",
		"NO",
	},
	{
		PKG_STRING,
		"PLUGINS_CONF_DIR",
		PREFIX"/etc/pkg/",
	},
	{
		PKG_BOOL,
		"PERMISSIVE",
		"NO",
	},
	{
		PKG_BOOL,
		"REPO_AUTOUPDATE",
		"YES",
	},
	{
		PKG_STRING,
		"NAMESERVER",
		NULL,
	},
	{
		PKG_STRING,
		"HTTP_USER_AGENT",
		"pkg/"PKGVERSION,
	},
	{
		PKG_STRING,
		"EVENT_PIPE",
		NULL,
	},
	{
		PKG_INT,
		"FETCH_TIMEOUT",
		"30",
	},
	{
		PKG_BOOL,
		"UNSET_TIMESTAMP",
		"NO",
	},
	{
		PKG_STRING,
		"SSH_RESTRICT_DIR",
		NULL,
	},
	{
		PKG_OBJECT,
		"PKG_ENV",
		NULL,
	},
	{
		PKG_STRING,
		"PKG_SSH_ARGS",
		NULL,
	},
	{
		PKG_INT,
		"DEBUG_LEVEL",
		"0",
	},
	{
		PKG_OBJECT,
		"ALIAS",
		NULL,
	},
	{
		PKG_STRING,
		"CUDF_SOLVER",
		NULL,
	},
	{
		PKG_STRING,
		"SAT_SOLVER",
		NULL,
	},
	{
		PKG_BOOL,
		"RUN_SCRIPTS",
		"YES",
	},
	{
		PKG_BOOL,
		"CASE_SENSITIVE_MATCH",
		"YES",
	},
	{
		PKG_INT,
		"LOCK_WAIT",
		"1",
	},
	{
		PKG_INT,
		"LOCK_RETRIES",
		"5",
	},
	{
		PKG_BOOL,
		"SQLITE_PROFILE",
		"NO",
	},
	{
		PKG_INT,
		"WORKERS_COUNT",
		"0",
	},
	{
		PKG_BOOL,
		"READ_LOCK",
		"NO",
	},
	{
		PKG_INT,
		"IP_VERSION",
		"0",
	},
	{
		PKG_BOOL,
		"AUTOMERGE",
		"YES",
	},
	{
		PKG_STRING,
		"MERGETOOL",
		NULL,
	},
	{
		PKG_STRING,
		"VERSION_SOURCE",
		NULL,
	},
	{
		PKG_BOOL,
		"CONSERVATIVE_UPGRADE",
		"YES",
	},
	{
		PKG_BOOL,
		"FORCE_CAN_REMOVE_VITAL",
		"YES",
	},
	{
		PKG_BOOL,
		"PKG_CREATE_VERBOSE",
		"NO",
	},
	{
		PKG_BOOL,
		"AUTOCLEAN",
		"NO",
	},
	{
		PKG_STRING,
		"DOT_FILE",
		NULL,
	},
	{
		PKG_STRING,
		"DEBUG_SCHEDULER_DOT_FILE",
		NULL,
	},
	{
		PKG_OBJECT,
		"REPOSITORIES",
		NULL,
	},
	{
		PKG_ARRAY,
		"VALID_URL_SCHEME",
		"pkg+http,pkg+https,https,http,file,ssh,tcp",
	},
	{
		PKG_INT,
		"WARN_SIZE_LIMIT",
		"1048576", /* 1 meg */
	},
	{
		PKG_STRING,
		"METALOG",
		NULL,
	},
	{
		PKG_BOOL,
		"IGNORE_OSVERSION",
		"NO",
	},
	{
		PKG_BOOL,
		"BACKUP_LIBRARIES",
		"NO",
	},
	{
		PKG_STRING,
		"BACKUP_LIBRARY_PATH",
		PREFIX "/lib/compat/pkg",
	},
	{
		PKG_ARRAY,
		"PKG_TRIGGERS_DIR",
		"/usr/share/pkg/triggers/,"PREFIX"/share/pkg/triggers",
	},
	{
		PKG_BOOL,
		"PKG_TRIGGERS_ENABLE",
		"YES",
	},
	{
		PKG_ARRAY,
		"AUDIT_IGNORE_GLOB",
		NULL,
	},
	{
		PKG_ARRAY,
		"AUDIT_IGNORE_REGEX",
		NULL,
	},
	{
		PKG_STRING,
		"COMPRESSION_FORMAT",
		NULL,
	},
	{
		PKG_INT,
		"COMPRESSION_LEVEL",
		"-1",
	},
	{
		PKG_BOOL,
		"REPO_ACCEPT_LEGACY_PKG",
		"FALSE",
	},
	{
		PKG_ARRAY,
		"FILES_IGNORE_GLOB",
		NULL,
	},
	{
		PKG_ARRAY,
		"FILES_IGNORE_REGEX",
		NULL,
	},
	{
		PKG_ARRAY,
		"SHLIB_PROVIDE_PATHS_NATIVE",
		NULL,
	},
	{
		PKG_ARRAY,
		"SHLIB_PROVIDE_PATHS_COMPAT_32",
		NULL,
	},
	{
		PKG_ARRAY,
		"SHLIB_PROVIDE_PATHS_COMPAT_LINUX",
		NULL,
	},
	{
		PKG_ARRAY,
		"SHLIB_PROVIDE_PATHS_COMPAT_LINUX_32",
		NULL,
	},
	{
		PKG_ARRAY,
		"SHLIB_PROVIDE_IGNORE_GLOB",
		NULL,
	},
	{
		PKG_ARRAY,
		"SHLIB_PROVIDE_IGNORE_REGEX",
		NULL,
	},
	{
		PKG_ARRAY,
		"SHLIB_REQUIRE_IGNORE_GLOB",
		NULL,
	},
	{
		PKG_ARRAY,
		"SHLIB_REQUIRE_IGNORE_REGEX",
		NULL,
	},
	{
		PKG_ARRAY,
		"PKG_DEBUG_FLAGS",
		"all",
	},
	{
		PKG_INT,
		"COMPRESSION_THREADS",
		"-1",
	},
	{
		PKG_BOOL,
		"PKG_REINSTALL_ON_OPTIONS_CHANGE",
		"TRUE",
	},
	{
		PKG_BOOL,
		"TRACK_LINUX_COMPAT_SHLIBS",
		"FALSE",
	},
};

static bool parsed = false;
static size_t c_size = NELEM(c);

static struct pkg_repo* pkg_repo_new(const char *name,
	const char *url, const char *type);
static void pkg_repo_overwrite(struct pkg_repo*, const char *name,
	const char *url, const char *type);
static void pkg_repo_free(struct pkg_repo *r);

static void
connect_evpipe(const char *evpipe) {
	struct stat st;
	struct sockaddr_un sock;
	int flag = O_WRONLY;

	if (stat(evpipe, &st) != 0) {
		pkg_emit_error("No such event pipe: %s", evpipe);
		return;
	}

	if (!S_ISFIFO(st.st_mode) && !S_ISSOCK(st.st_mode)) {
		pkg_emit_error("%s is not a fifo or socket", evpipe);
		return;
	}

	if (S_ISFIFO(st.st_mode)) {
		flag |= O_NONBLOCK;
		if ((ctx.eventpipe = open(evpipe, flag)) == -1)
			pkg_emit_errno("open event pipe", evpipe);
		return;
	}

	if (S_ISSOCK(st.st_mode)) {
		if ((ctx.eventpipe = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
			pkg_emit_errno("Open event pipe", evpipe);
			return;
		}
		memset(&sock, 0, sizeof(struct sockaddr_un));
		sock.sun_family = AF_UNIX;
		if (strlcpy(sock.sun_path, evpipe, sizeof(sock.sun_path)) >=
		    sizeof(sock.sun_path)) {
			pkg_emit_error("Socket path too long: %s", evpipe);
			close(ctx.eventpipe);
			ctx.eventpipe = -1;
			return;
		}

		if (connect(ctx.eventpipe, (struct sockaddr *)&sock, SUN_LEN(&sock)) == -1) {
			pkg_emit_errno("Connect event pipe", evpipe);
			close(ctx.eventpipe);
			ctx.eventpipe = -1;
			return;
		}
	}

}

const char *
pkg_libversion(void)
{
	return PKGVERSION;
}

pkg_kvl_t *
pkg_external_libs_version(void)
{
	pkg_kvl_t *kvl = xcalloc(1, sizeof(*kvl));

	vec_push(kvl, pkg_kv_new("libcurl", curl_version()));
	vec_push(kvl, pkg_kv_new("libarchive", archive_version_string()));
	vec_push(kvl, pkg_kv_new("sqlite", sqlite3_libversion()));
	vec_push(kvl, pkg_kv_new("openssl", OpenSSL_version(OPENSSL_VERSION)));

	return (kvl);
}


int
pkg_initialized(void)
{
	return (parsed);
}

const pkg_object *
pkg_config_get(const char *key) {
	return (ucl_object_find_key(config, key));
}

char *
pkg_config_dump(void)
{
	return (pkg_object_dump(config));
}

static void
disable_plugins_if_static(void)
{
	void *dlh;

	dlh = dlopen(0, RTLD_NOW);

	/* if dlh is NULL then we are in static binary */
	if (dlh == NULL)
		ucl_object_replace_key(config, ucl_object_frombool(false), "PKG_ENABLE_PLUGINS", 18, false);
	else
		dlclose(dlh);

	return;
}

static void
add_repo(const ucl_object_t *obj, struct pkg_repo *r, const char *rname, pkg_init_flags flags)
{
	const ucl_object_t *cur, *enabled, *env;
	ucl_object_iter_t it = NULL;
	struct pkg_kv *kv;
	bool enable = true;
	const char *url = NULL, *pubkey = NULL, *mirror_type = NULL;
	const char *signature_type = NULL, *fingerprints = NULL;
	const char *key;
	const char *type = NULL;
	int use_ipvx = 0;
	int priority = 0;

	dbg(1, "parsing repository object %s", rname);

	env = NULL;
	enabled = ucl_object_find_key(obj, "enabled");
	if (enabled == NULL)
		enabled = ucl_object_find_key(obj, "ENABLED");
	if (enabled != NULL) {
		enable = ucl_object_toboolean(enabled);
		if (!enable && r != NULL) {
			/*
			 * We basically want to remove the existing repo r and
			 * forget all stuff parsed
			 */
			dbg(1, "disabling repo %s", rname);
			DL_DELETE(repos, r);
			pkg_repo_free(r);
			return;
		}
	}

	while ((cur = ucl_iterate_object(obj, &it, true))) {
		key = ucl_object_key(cur);
		if (key == NULL)
			continue;

		if (STRIEQ(key, "url")) {
			if (cur->type != UCL_STRING) {
				pkg_emit_error("Expecting a string for the "
				    "'%s' key of the '%s' repo",
				    key, rname);
				return;
			}
			url = ucl_object_tostring(cur);
		} else if (STRIEQ(key, "pubkey")) {
			if (cur->type != UCL_STRING) {
				pkg_emit_error("Expecting a string for the "
				    "'%s' key of the '%s' repo",
				    key, rname);
				return;
			}
			pubkey = ucl_object_tostring(cur);
		} else if (STRIEQ(key, "mirror_type")) {
			if (cur->type != UCL_STRING) {
				pkg_emit_error("Expecting a string for the "
				    "'%s' key of the '%s' repo",
				    key, rname);
				return;
			}
			mirror_type = ucl_object_tostring(cur);
		} else if (STRIEQ(key, "signature_type")) {
			if (cur->type != UCL_STRING) {
				pkg_emit_error("Expecting a string for the "
				    "'%s' key of the '%s' repo",
				    key, rname);
				return;
			}
			signature_type = ucl_object_tostring(cur);
		} else if (STRIEQ(key, "fingerprints")) {
			if (cur->type != UCL_STRING) {
				pkg_emit_error("Expecting a string for the "
				    "'%s' key of the '%s' repo",
				    key, rname);
				return;
			}
			fingerprints = ucl_object_tostring(cur);
		} else if (STRIEQ(key, "type")) {
			if (cur->type != UCL_STRING) {
				pkg_emit_error("Expecting a string for the "
					"'%s' key of the '%s' repo",
					key, rname);
				return;
			}
			type = ucl_object_tostring(cur);
		} else if (STRIEQ(key, "ip_version")) {
			if (cur->type != UCL_INT) {
				pkg_emit_error("Expecting a integer for the "
					"'%s' key of the '%s' repo",
					key, rname);
				return;
			}
			use_ipvx = ucl_object_toint(cur);
			if (use_ipvx != 4 && use_ipvx != 6)
				use_ipvx = 0;
		} else if (STRIEQ(key, "priority")) {
			if (cur->type != UCL_INT) {
				pkg_emit_error("Expecting a integer for the "
					"'%s' key of the '%s' repo",
					key, rname);
				return;
			}
			priority = ucl_object_toint(cur);
		} else if (STRIEQ(key, "env")) {
			if (cur->type != UCL_OBJECT) {
				pkg_emit_error("Expecting an object for the "
					"'%s' key of the '%s' repo",
					key, rname);
			}
			env = cur;
		}
	}

	if (r == NULL && url == NULL) {
		dbg(1, "No repo and no url for %s", rname);
		return;
	}

	if (r == NULL)
		r = pkg_repo_new(rname, url, type);
	else
		pkg_repo_overwrite(r, rname, url, type);

	if (signature_type != NULL) {
		if (STRIEQ(signature_type, "pubkey"))
			r->signature_type = SIG_PUBKEY;
		else if (STRIEQ(signature_type, "fingerprints"))
			r->signature_type = SIG_FINGERPRINT;
		else
			r->signature_type = SIG_NONE;
	}


	if (fingerprints != NULL) {
		free(r->fingerprints);
		r->fingerprints = xstrdup(fingerprints);
	}

	if (pubkey != NULL) {
		free(r->pubkey);
		r->pubkey = xstrdup(pubkey);
	}

	r->enable = enable;
	r->priority = priority;

	if (mirror_type != NULL) {
		if (STRIEQ(mirror_type, "srv"))
			r->mirror_type = SRV;
		else if (STRIEQ(mirror_type, "http"))
			r->mirror_type = HTTP;
		else
			r->mirror_type = NOMIRROR;
	}

	if ((flags & PKG_INIT_FLAG_USE_IPV4) == PKG_INIT_FLAG_USE_IPV4)
		use_ipvx = 4;
	else if ((flags & PKG_INIT_FLAG_USE_IPV6) == PKG_INIT_FLAG_USE_IPV6)
		use_ipvx = 6;

	if (use_ipvx != 4 && use_ipvx != 6)
		use_ipvx = pkg_object_int(pkg_config_get("IP_VERSION"));

	if (use_ipvx == 4)
		r->ip = IPV4;
	else if (use_ipvx == 6)
		r->ip = IPV6;

	if (env != NULL) {
		it = NULL;
		while ((cur = ucl_iterate_object(env, &it, true))) {
			kv = pkg_kv_new(ucl_object_key(cur),
			    ucl_object_tostring_forced(cur));
			vec_push(&r->env, kv);
		}
	}
}

static void
add_repo_obj(const ucl_object_t *obj, const char *file, pkg_init_flags flags)
{
	struct pkg_repo *r;
	const char *key;

	key = ucl_object_key(obj);
	dbg(1, "parsing repo key '%s' in file '%s'", key, file);
	r = pkg_repo_find(key);
	if (r != NULL)
		dbg(1, "overwriting repository %s", key);
       add_repo(obj, r, key, flags);
}

static void
walk_repo_obj(const ucl_object_t *obj, const char *file, pkg_init_flags flags)
{
	const ucl_object_t *cur;
	ucl_object_iter_t it = NULL;
	struct pkg_repo *r;
	const char *key;
	char *yaml;

	while ((cur = ucl_iterate_object(obj, &it, true))) {
		key = ucl_object_key(cur);
		dbg(1, "parsing key '%s'", key);
		r = pkg_repo_find(key);
		if (r != NULL)
			dbg(1, "overwriting repository %s", key);
		if (cur->type == UCL_OBJECT)
			add_repo(cur, r, key, flags);
		else {
			yaml = ucl_object_emit(cur, UCL_EMIT_YAML);
			pkg_emit_error("Ignoring bad configuration entry in %s: %s",
			    file, yaml);
			free(yaml);
		}
	}
}

struct config_parser_vars {
	char *abi;
	char *altabi;
	char *osversion;
	char *release;
	char *version_major;
	char *version_minor;
};

/* Register parser variables based on ctx.abi.
 * The returned struct must be free'd with config_parser_variables_free()
 * after parsing is complete. */
static struct config_parser_vars *
config_parser_vars_register(struct ucl_parser *p)
{
	struct config_parser_vars *vars = xcalloc(1, sizeof(struct config_parser_vars));

	vars->abi = pkg_abi_to_string(&ctx.abi);
	ucl_parser_register_variable(p, "ABI", vars->abi);

	char altabi_buffer[BUFSIZ];
	pkg_arch_to_legacy(vars->abi, altabi_buffer, sizeof(altabi_buffer));
	vars->altabi = xstrdup(altabi_buffer);
	ucl_parser_register_variable(p, "ALTABI", vars->altabi);

	if (ctx.abi.os == PKG_OS_FREEBSD) {
		xasprintf(&vars->osversion, "%d",
		    pkg_abi_get_freebsd_osversion(&ctx.abi));
		ucl_parser_register_variable(p, "OSVERSION", vars->osversion);
	}
	ucl_parser_register_variable(p, "OSNAME", pkg_os_to_string(ctx.abi.os));

	if (pkg_abi_string_only_major_version(ctx.abi.os)) {
		xasprintf(&vars->release, "%d", ctx.abi.major);
	} else {
		xasprintf(&vars->release, "%d.%d", ctx.abi.major, ctx.abi.minor);
	}
	ucl_parser_register_variable(p, "RELEASE", vars->release);

	xasprintf(&vars->version_major, "%d", ctx.abi.major);
	ucl_parser_register_variable(p, "VERSION_MAJOR", vars->version_major);

	xasprintf(&vars->version_minor, "%d", ctx.abi.minor);
	ucl_parser_register_variable(p, "VERSION_MINOR", vars->version_minor);

	ucl_parser_register_variable(p, "ARCH",
	    pkg_arch_to_string(ctx.abi.os, ctx.abi.arch));

	return vars;
}

static void
config_parser_vars_free(struct config_parser_vars *vars)
{
	free(vars->abi);
	free(vars->altabi);
	free(vars->osversion);
	free(vars->release);
	free(vars->version_major);
	free(vars->version_minor);
	free(vars);
}
static void
load_repo_file(int dfd, const char *repodir, const char *repofile,
    pkg_init_flags flags)
{
	struct ucl_parser *p;
	ucl_object_t *obj = NULL;
	int fd;

	p = ucl_parser_new(0);

	struct config_parser_vars *parser_vars = config_parser_vars_register(p);

	errno = 0;
	obj = NULL;

	dbg(1, "loading %s/%s", repodir, repofile);
	fd = openat(dfd, repofile, O_RDONLY);
	if (fd == -1) {
		pkg_errno("Unable to open '%s/%s'", repodir, repofile);
		goto out_parser_vars;
	}
	if (!ucl_parser_add_fd(p, fd)) {
		pkg_emit_error("Error parsing: '%s/%s': %s", repodir,
		    repofile, ucl_parser_get_error(p));
		goto out_fd;
	}

	obj = ucl_parser_get_object(p);
	if (obj == NULL) {
		goto out_fd;
	}

	if (obj->type == UCL_OBJECT)
		walk_repo_obj(obj, repofile, flags);

	ucl_object_unref(obj);
out_fd:
	close(fd);
out_parser_vars:
	ucl_parser_free(p);
	config_parser_vars_free(parser_vars);
}

static int
configfile(const struct dirent *dp)
{
	const char *p;
	size_t n;

	if (dp->d_name[0] == '.')
		return (0);

	n = strlen(dp->d_name);
	if (n <= 5)
		return (0);

	p = &dp->d_name[n - 5];
	if (!STREQ(p, ".conf"))
		return (0);
	return (1);
}

static void
load_repo_files(const char *repodir, pkg_init_flags flags)
{
	struct dirent **ent;
	int nents, i, fd;

	dbg(1, "loading repositories in %s", repodir);
	if ((fd = open(repodir, O_DIRECTORY|O_CLOEXEC)) == -1)
		return;

	nents = scandir(repodir, &ent, configfile, alphasort);
	for (i = 0; i < nents; i++) {
		load_repo_file(fd, repodir, ent[i]->d_name, flags);
		free(ent[i]);
	}
	if (nents >= 0)
		free(ent);
	close(fd);
}

static void
load_repositories(const char *repodir, pkg_init_flags flags)
{
	const pkg_object *reposlist, *cur;
	pkg_iter it = NULL;

	if (repodir != NULL) {
		load_repo_files(repodir, flags);
		return;
	}

	reposlist = pkg_config_get("REPOS_DIR");
	while ((cur = pkg_object_iterate(reposlist, &it)))
		load_repo_files(pkg_object_string(cur), flags);
}

bool
pkg_compiled_for_same_os_major(void)
{
#ifdef OSMAJOR
	if (getenv("IGNORE_OSMAJOR") != NULL)
		return (true);

	return (ctx.abi.major == OSMAJOR);
#else
	return (true);		/* Can't tell, so assume yes  */
#endif
}


int
pkg_init(const char *path, const char *reposdir)
{
	return (pkg_ini(path, reposdir, 0));
}

static const char *
type_to_string(int type)
{
	if (type == UCL_ARRAY)
		return ("array");
	if (type == UCL_OBJECT)
		return ("object");
	if (type == UCL_STRING)
		return ("string");
	if (type == UCL_INT)
		return ("integer");
	if (type == UCL_BOOLEAN)
		return ("boolean");
	return ("unknown");
}

const struct pkg_dbg_flags *
_find_flag(const char *str)
{
	for (size_t i = 0; i < NELEM(debug_flags); i++) {
		if (STRIEQ(debug_flags[i].name, str))
			return (&debug_flags[i]);
	}
	return (NULL);
}
static uint64_t
config_validate_debug_flags(const ucl_object_t *o)
{
	ucl_object_iter_t it = NULL;
	const ucl_object_t *cur;
	int ret = EPKG_OK;
	const struct pkg_dbg_flags *f;

	if (o == NULL)
		return (ret);

	while ((cur = ucl_iterate_object(o, &it, true))) {
		const char *str = ucl_object_tostring(cur);
		f = _find_flag(str);
		if (f == NULL) {
			pkg_emit_error("Invalid debug flag %s",
			    ucl_object_tostring(cur));
			ret = EPKG_FATAL;
			continue;
		}
		ctx.debug_flags |= f->flag;
	}
	return (ret);
}

static bool
config_validate_shlib_provide_paths() {
	const char *config_options[] = {
		"SHLIB_PROVIDE_PATHS_NATIVE",
		"SHLIB_PROVIDE_PATHS_COMPAT_32",
		"SHLIB_PROVIDE_PATHS_COMPAT_LINUX",
		"SHLIB_PROVIDE_PATHS_COMPAT_LINUX_32",
		NULL,
	};
	bool valid = true;
	for (const char **option = config_options; *option != NULL; option++) {
		const ucl_object_t *paths = pkg_config_get(*option);
		const ucl_object_t *cur;
		ucl_object_iter_t it = NULL;
		while ((cur = ucl_object_iterate(paths, &it, true))) {
			const char *path = ucl_object_tostring(cur);
			if (path[0] != '/') {
				pkg_emit_error("Invalid value for config option %s, "
				    "'%s' is not an absolute path.",
				    *option, path);
				valid = false;
			}
		}
	}
	return valid;
}

/* Parses ABI_FILE, ABI, ALTABI, and OSVERSION from the given ucl file and sets
 * the values in the environment. These values must be parsed separately from
 * the rest of the config because they are made available as variable expansions
 * when parsing the rest of the config (See config_parser_vars_register()). */
static void
config_parse_abi_options(int conffd)
{
	if (conffd < 0) {
		return;
	}

	struct ucl_parser *p = ucl_parser_new(0);

	if (!ucl_parser_add_fd(p, conffd)) {
		pkg_emit_error("Invalid configuration file: %s", ucl_parser_get_error(p));
	}

	ucl_object_t *obj = ucl_parser_get_object(p);

	ucl_object_iter_t it = NULL;
	const ucl_object_t *cur;
	xstring *ukey = NULL;
	while (obj != NULL && (cur = ucl_iterate_object(obj, &it, true))) {
		xstring_renew(ukey);
		const char *key = ucl_object_key(cur);
		for (size_t i = 0; key[i] != '\0'; i++)
			fputc(toupper(key[i]), ukey->fp);
		fflush(ukey->fp);

		if (STREQ(ukey->buf, "ABI_FILE") ||
		    STREQ(ukey->buf, "ABI") ||
		    STREQ(ukey->buf, "ALTABI")) {
			if (cur->type == UCL_STRING) {
				/* Don't overwrite the value already set on the
				   command line or in the environment */
				setenv(ukey->buf, ucl_object_tostring(cur), 0);
			} else {
				pkg_emit_error("Malformed key %s, got '%s' expecting "
				    "'string', ignoring", key,
				    type_to_string(cur->type));
			}
		} else if (STREQ(ukey->buf, "OSVERSION")) {
			if (cur->type == UCL_INT) {
				int64_t osversion = ucl_object_toint(cur);
				char *str_osversion;
				xasprintf(&str_osversion, "%" PRIi64, osversion);
				/* Don't overwrite the value already set on the
				   command line or in the environment */
				setenv(ukey->buf, str_osversion, 0);
				free(str_osversion);
			} else {
				pkg_emit_error("Malformed key %s, got '%s' expecting "
				    "'integer', ignoring", key,
				    type_to_string(cur->type));
			}


		}
	}

	ucl_object_unref(obj);
	ucl_parser_free(p);
}

static bool
config_init_abi(struct pkg_abi *abi)
{
	if (getenv("ALTABI") != NULL) {
		pkg_emit_error("Setting ALTABI manually is no longer supported, "
		    "set ABI and OSVERSION or ABI_FILE instead.");
	}

	const char *env_abi_file = getenv("ABI_FILE");
	const char *env_abi_string = getenv("ABI");
	const char *env_osversion_string = getenv("OSVERSION");

	if (env_abi_file != NULL && env_abi_string != NULL) {
		pkg_emit_error("Both ABI_FILE and ABI are set, ABI_FILE overrides ABI");
	}

	if (env_abi_file != NULL && env_osversion_string != NULL) {
		pkg_emit_error("Both ABI_FILE and OSVERSION are set, ABI_FILE overrides OSVERSION");
	}

	if (env_abi_string != NULL) {
		if (!pkg_abi_from_string(abi, env_abi_string)) {
			return (false);
		}

		if (abi->os == PKG_OS_FREEBSD) {
			if (env_osversion_string == NULL) {
				pkg_emit_error("Setting ABI requires setting OSVERSION, guessing the OSVERSION as: %d",
				    pkg_abi_get_freebsd_osversion(abi));
				return (true);
			}

			const char *errstr = NULL;
			int env_osversion = strtonum(env_osversion_string, 1, INT_MAX, &errstr);
			if (errstr != NULL) {
				pkg_emit_error("Invalid OSVERSION %s, %s", env_osversion_string, errstr);
				return (false);
			}

			pkg_abi_set_freebsd_osversion(abi, env_osversion);
		} else {
			if (env_osversion_string != NULL) {
				pkg_emit_notice("OSVERSION is ignored on %s",
				    pkg_os_to_string(abi->os));
			}
		}
	} else {
		if (env_osversion_string != NULL) {
			dbg(1, "Setting OSVERSION requires setting ABI as well (ignoring)");
			unsetenv("OSVERSION");
		}
		if (pkg_abi_from_file(abi) != EPKG_OK) {
			return (false);
		}
	}

	return (true);
}

int
pkg_ini(const char *path, const char *reposdir, pkg_init_flags flags)
{
	struct ucl_parser *p = NULL;
	size_t i;
	const char *val = NULL;
	const char *buf, *walk, *value, *key, *k;
	const char *evkey = NULL;
	const char *nsname = NULL;
	const char *useragent = NULL;
	const char *evpipe = NULL;
	const char *url;
	struct pkg_repo *repo = NULL;
	const ucl_object_t *cur, *object;
	ucl_object_t *obj = NULL, *o, *ncfg;
	ucl_object_iter_t it = NULL;
	xstring *ukey = NULL;
	bool fatal_errors = false;
	int conffd = -1;
	char *tmp = NULL;
	size_t ukeylen;
	int err = EPKG_OK;

	k = NULL;
	o = NULL;
	if (ctx.rootfd == -1 && (ctx.rootfd = open("/", O_DIRECTORY|O_RDONLY|O_CLOEXEC)) < 0) {
		pkg_emit_error("Impossible to open /");
		return (EPKG_FATAL);
	}

	if (parsed != false) {
		pkg_emit_error("pkg_init() must only be called once");
		return (EPKG_FATAL);
	}

	if (path == NULL)
		conffd = openat(ctx.rootfd, &PREFIX"/etc/pkg.conf"[1], 0);
	else
		conffd = open(path, O_RDONLY);
	if (conffd == -1 && errno != ENOENT) {
		pkg_errno("Cannot open %s/%s",
		    ctx.pkg_rootdir != NULL ? ctx.pkg_rootdir : "",
		    path);
	}

	config_parse_abi_options(conffd);
	if (!config_init_abi(&ctx.abi)) {
		return (EPKG_FATAL);
	}

	if (((flags & PKG_INIT_FLAG_USE_IPV4) == PKG_INIT_FLAG_USE_IPV4) &&
	    ((flags & PKG_INIT_FLAG_USE_IPV6) == PKG_INIT_FLAG_USE_IPV6)) {
		pkg_emit_error("Invalid flags for pkg_init()");
		return (EPKG_FATAL);
	}
	if ((flags & PKG_INIT_FLAG_USE_IPV4) == PKG_INIT_FLAG_USE_IPV4)
		ctx.ip = IPV4;
	if ((flags & PKG_INIT_FLAG_USE_IPV4) == PKG_INIT_FLAG_USE_IPV4)
		ctx.ip = IPV6;

	config = ucl_object_typed_new(UCL_OBJECT);

	for (i = 0; i < c_size; i++) {
		switch (c[i].type) {
		case PKG_STRING:
			tmp = NULL;
			if (c[i].def != NULL && c[i].def[0] == '/' &&
			    ctx.pkg_rootdir != NULL) {
				xasprintf(&tmp, "%s%s", ctx.pkg_rootdir, c[i].def);
			}
			obj = ucl_object_fromstring_common(
			    c[i].def != NULL ? tmp != NULL ? tmp : c[i].def : "", 0, UCL_STRING_TRIM);
			free(tmp);
			ucl_object_insert_key(config, obj,
			    c[i].key, strlen(c[i].key), false);
			break;
		case PKG_INT:
			ucl_object_insert_key(config,
			    ucl_object_fromstring_common(c[i].def, 0, UCL_STRING_PARSE_INT),
			    c[i].key, strlen(c[i].key), false);
			break;
		case PKG_BOOL:
			ucl_object_insert_key(config,
			    ucl_object_fromstring_common(c[i].def, 0, UCL_STRING_PARSE_BOOLEAN),
			    c[i].key, strlen(c[i].key), false);
			break;
		case PKG_OBJECT:
			obj = ucl_object_typed_new(UCL_OBJECT);
			if (c[i].def != NULL) {
				walk = buf = c[i].def;
				while ((buf = strchr(buf, ',')) != NULL) {
					key = walk;
					value = walk;
					while (*value != ',') {
						if (*value == '=')
							break;
						value++;
					}
					ucl_object_insert_key(obj,
					    ucl_object_fromstring_common(value + 1, buf - value - 1, UCL_STRING_TRIM),
					    key, value - key, false);
					buf++;
					walk = buf;
				}
				key = walk;
				value = walk;
				while (*value != ',') {
					if (*value == '=')
						break;
					value++;
				}
				if (o == NULL)
					o = ucl_object_typed_new(UCL_OBJECT);
				ucl_object_insert_key(o,
				    ucl_object_fromstring_common(value + 1, strlen(value + 1), UCL_STRING_TRIM),
				    key, value - key, false);
			}
			ucl_object_insert_key(config, obj,
			    c[i].key, strlen(c[i].key), false);
			break;
		case PKG_ARRAY:
			obj = ucl_object_typed_new(UCL_ARRAY);
			if (c[i].def != NULL) {
				walk = buf = c[i].def;
				while ((buf = strchr(buf, ',')) != NULL) {
					ucl_array_append(obj,
					    ucl_object_fromstring_common(walk, buf - walk, UCL_STRING_TRIM));
					buf++;
					walk = buf;
				}
				ucl_array_append(obj,
				    ucl_object_fromstring_common(walk, strlen(walk), UCL_STRING_TRIM));
			}
			ucl_object_insert_key(config, obj,
			    c[i].key, strlen(c[i].key), false);
			break;
		}
	}

	p = ucl_parser_new(0);

	struct config_parser_vars *parser_vars = config_parser_vars_register(p);

	errno = 0;
	obj = NULL;
	if (conffd != -1) {
		if (!ucl_parser_add_fd(p, conffd)) {
			pkg_emit_error("Invalid configuration file: %s", ucl_parser_get_error(p));
		}
		close(conffd);
	}

	obj = ucl_parser_get_object(p);
	ncfg = NULL;
	ukey = NULL;
	while (obj != NULL && (cur = ucl_iterate_object(obj, &it, true))) {
		xstring_renew(ukey);
		key = ucl_object_key(cur);
		for (i = 0; key[i] != '\0'; i++)
			fputc(toupper(key[i]), ukey->fp);
		fflush(ukey->fp);
		ukeylen = strlen(ukey->buf);
		object = ucl_object_find_keyl(config, ukey->buf, ukeylen);

		if (STREQ(ukey->buf, "PACKAGESITE") ||
		    STREQ(ukey->buf, "PUBKEY") ||
		    STREQ(ukey->buf, "MIRROR_TYPE")) {
			pkg_emit_error("%s in pkg.conf is no longer "
			    "supported.  Convert to the new repository style."
			    "  See pkg.conf(5)", ukey->buf);
			fatal_errors = true;
			continue;
		}

		if (STREQ(ukey->buf, "ABI_FILE") ||
		    STREQ(ukey->buf, "ABI") ||
		    STREQ(ukey->buf, "ALTABI") ||
		    STREQ(ukey->buf, "OSVERSION")) {
			continue; /* Already parsed in config_parse_abi_options() */
		}

		/* ignore unknown keys */
		if (object == NULL)
			continue;

		if (object->type != cur->type) {
			pkg_emit_error("Malformed key %s, got '%s' expecting "
			    "'%s', ignoring", key,
			    type_to_string(cur->type),
			    type_to_string(object->type));
			continue;
		}

		if (ncfg == NULL)
			ncfg = ucl_object_typed_new(UCL_OBJECT);
		ucl_object_insert_key(ncfg, ucl_object_copy(cur), ukey->buf,
		    ukeylen, true);
	}
	xstring_free(ukey);

	if (fatal_errors) {
		ucl_object_unref(ncfg);
		ucl_parser_free(p);
		err = EPKG_FATAL;
		goto out;
	}

	if (ncfg != NULL) {
		it = NULL;
		while (( cur = ucl_iterate_object(ncfg, &it, true))) {
			key = ucl_object_key(cur);
			ucl_object_replace_key(config, ucl_object_ref(cur), key, strlen(key), true);
		}
	}
	ncfg = NULL;
	it = NULL;
	while ((cur = ucl_iterate_object(config, &it, true))) {
		o = NULL;
		key = ucl_object_key(cur);
		val = getenv(key);
		if (val == NULL)
			continue;
		switch (cur->type) {
		case UCL_STRING:
			o = ucl_object_fromstring_common(val, 0, UCL_STRING_TRIM);
			break;
		case UCL_INT:
			o = ucl_object_fromstring_common(val, 0, UCL_STRING_PARSE_INT);
			if (o->type != UCL_INT) {
				pkg_emit_error("Invalid type for environment "
				    "variable %s, got %s, while expecting an integer",
				    key, val);
				ucl_object_unref(o);
				continue;
			}
			break;
		case UCL_BOOLEAN:
			o = ucl_object_fromstring_common(val, 0, UCL_STRING_PARSE_BOOLEAN);
			if (o->type != UCL_BOOLEAN) {
				pkg_emit_error("Invalid type for environment "
				    "variable %s, got %s, while expecting a boolean",
				    key, val);
				ucl_object_unref(o);
				continue;
			}
			break;
		case UCL_OBJECT:
			o = ucl_object_typed_new(UCL_OBJECT);
			walk = buf = val;
			while ((buf = strchr(buf, ',')) != NULL) {
				k = walk;
				value = walk;
				while (*value != ',') {
					if (*value == '=')
						break;
					value++;
				}
				ucl_object_insert_key(o,
				    ucl_object_fromstring_common(value + 1, buf - value - 1, UCL_STRING_TRIM),
				    k, value - k, false);
				buf++;
				walk = buf;
			}
			k = walk;
			value = walk;
			while (*value != '\0') {
				if (*value == '=')
					break;
				value++;
			}
			ucl_object_insert_key(o,
			    ucl_object_fromstring_common(value + 1, strlen(value + 1), UCL_STRING_TRIM),
			    k, value - k, false);
			break;
		case UCL_ARRAY:
			o = ucl_object_typed_new(UCL_ARRAY);
			walk = buf = val;
			while ((buf = strchr(buf, ',')) != NULL) {
				ucl_array_append(o,
				    ucl_object_fromstring_common(walk, buf - walk, UCL_STRING_TRIM));
				buf++;
				walk = buf;
			}
			ucl_array_append(o,
			    ucl_object_fromstring_common(walk, strlen(walk), UCL_STRING_TRIM));
			break;
		default:
			/* ignore other types */
			break;
		}
		if (o != NULL) {
			if (ncfg == NULL)
				ncfg = ucl_object_typed_new(UCL_OBJECT);
			ucl_object_insert_key(ncfg, o, key, strlen(key), true);
		}
	}

	if (ncfg != NULL) {
		it = NULL;
		while (( cur = ucl_iterate_object(ncfg, &it, true))) {
			key = ucl_object_key(cur);
			ucl_object_replace_key(config, ucl_object_ref(cur), key, strlen(key), true);
		}
		ucl_object_unref(ncfg);
	}

	disable_plugins_if_static();

	parsed = true;
	ucl_object_unref(obj);
	ucl_parser_free(p);

	if (!config_validate_shlib_provide_paths()) {
		err = EPKG_FATAL;
		goto out;
	}

	{
		/* Even though we no longer support setting ABI/ALTABI/OSVERSION
		   in the pkg.conf config file, we still need to expose these
		   values through e.g. `pkg config ABI`. */
		char *abi_string = pkg_abi_to_string(&ctx.abi);
		char altabi_string[BUFSIZ];
		pkg_arch_to_legacy(abi_string, altabi_string, sizeof(altabi_string));

		ucl_object_insert_key(config,
		    ucl_object_fromstring(abi_string), "ABI", 0, true);
		ucl_object_insert_key(config,
		    ucl_object_fromstring(altabi_string), "ALTABI", 0, true);

		free(abi_string);

		if (ctx.abi.os == PKG_OS_FREEBSD) {
			char *osversion;
			xasprintf(&osversion, "%d", pkg_abi_get_freebsd_osversion(&ctx.abi));
			ucl_object_insert_key(config,
			    ucl_object_fromstring(osversion), "OSVERSION", 0, true);
			free(osversion);
		}
	}

	dbg(1, "pkg initialized");

	/* Start the event pipe */
	evpipe = pkg_object_string(pkg_config_get("EVENT_PIPE"));
	if (evpipe != NULL)
		connect_evpipe(evpipe);

	ctx.debug_level = pkg_object_int(pkg_config_get("DEBUG_LEVEL"));
	err = config_validate_debug_flags(ucl_object_find_key(config, "PKG_DEBUG_FLAGS"));
	if (err != EPKG_OK)
		goto out;
	ctx.developer_mode = pkg_object_bool(pkg_config_get("DEVELOPER_MODE"));
	ctx.metalog = pkg_object_string(pkg_config_get("METALOG"));
	ctx.dbdir = pkg_object_string(pkg_config_get("PKG_DBDIR"));
	ctx.cachedir = pkg_object_string(pkg_config_get("PKG_CACHEDIR"));
	ctx.backup_libraries = pkg_object_bool(pkg_config_get("BACKUP_LIBRARIES"));
	ctx.backup_library_path = pkg_object_string(pkg_config_get("BACKUP_LIBRARY_PATH"));
	ctx.triggers = pkg_object_bool(pkg_config_get("PKG_TRIGGERS_ENABLE"));
	ctx.compression_format = pkg_object_string(pkg_config_get("COMPRESSION_FORMAT"));
	ctx.compression_level = pkg_object_int(pkg_config_get("COMPRESSION_LEVEL"));
	ctx.compression_threads = pkg_object_int(pkg_config_get("COMPRESSION_THREADS"));
	ctx.repo_accept_legacy_pkg = pkg_object_bool(pkg_config_get("REPO_ACCEPT_LEGACY_PKG"));
	ctx.no_version_for_deps = (getenv("PKG_NO_VERSION_FOR_DEPS") != NULL);
	ctx.track_linux_compat_shlibs = pkg_object_bool(pkg_config_get("TRACK_LINUX_COMPAT_SHLIBS"));
        ctx.case_sensitive = pkg_object_bool(pkg_config_get("CASE_SENSITIVE_MATCH"));

	it = NULL;
	object = ucl_object_find_key(config, "PKG_ENV");
	while ((cur = ucl_iterate_object(object, &it, true))) {
		evkey = ucl_object_key(cur);
		dbg(1, "Setting env var: %s", evkey);
		if (evkey != NULL && evkey[0] != '\0')
			setenv(evkey, ucl_object_tostring_forced(cur), 1);
	}

	/* Set user-agent */
	useragent = pkg_object_string(pkg_config_get("HTTP_USER_AGENT"));
	if (useragent != NULL)
		setenv("HTTP_USER_AGENT", useragent, 1);
	else
		setenv("HTTP_USER_AGENT", "pkg/"PKGVERSION, 1);

	/* load the repositories */
	load_repositories(reposdir, flags);

	object = ucl_object_find_key(config, "REPOSITORIES");
	while ((cur = ucl_iterate_object(object, &it, true))) {
		add_repo_obj(cur, path, flags);
	}

	/* validate the different scheme */
	while (pkg_repos(&repo) == EPKG_OK) {
		object = ucl_object_find_key(config, "VALID_URL_SCHEME");
		url = pkg_repo_url(repo);
		buf = strstr(url, ":/");
		if (buf == NULL) {
			pkg_emit_error("invalid url: %s", url);
			err = EPKG_FATAL;
			goto out;
		}
		fatal_errors = true;
		it = NULL;
		while ((cur = ucl_iterate_object(object, &it, true))) {
			if (strncmp(url, ucl_object_tostring_forced(cur),
			    buf - url) == 0) {
				fatal_errors = false;
				break;
			}
		}

		if (fatal_errors) {
			pkg_emit_error("invalid scheme %.*s", (int)(buf - url), url);
			err = EPKG_FATAL;
			goto out;
		}
	}

	/* bypass resolv.conf with specified NAMESERVER if any */
	nsname = pkg_object_string(pkg_config_get("NAMESERVER"));
	if (nsname != NULL && set_nameserver(nsname) != 0)
			pkg_emit_error("Unable to set nameserver, ignoring");

	/* Open metalog */
	if (ctx.metalog != NULL && metalog_open(ctx.metalog) != EPKG_OK) {
		err = EPKG_FATAL;
		goto out;
	}

out:
	config_parser_vars_free(parser_vars);

	return err;
}

static struct pkg_repo_ops*
pkg_repo_find_type(const char *type)
{
	struct pkg_repo_ops *found = NULL, **cur;

	/* Default repo type */
	if (type == NULL)
		return (pkg_repo_find_type("binary"));

	cur = &repos_ops[0];
	while (*cur != NULL) {
		if (STRIEQ(type, (*cur)->type)) {
			found = *cur;
		}
		cur ++;
	}

	if (found == NULL)
		return (pkg_repo_find_type("binary"));

	return (found);
}

static struct pkg_repo *
pkg_repo_new(const char *name, const char *url, const char *type)
{
	struct pkg_repo *r;

	r = xcalloc(1, sizeof(struct pkg_repo));
	r->dfd = -1;
	r->ops = pkg_repo_find_type(type);
	r->url = xstrdup(url);
	r->signature_type = SIG_NONE;
	r->mirror_type = NOMIRROR;
	r->enable = true;
	r->meta = pkg_repo_meta_default();
	r->name = xstrdup(name);
	DL_APPEND(repos, r);

	return (r);
}

static void
pkg_repo_overwrite(struct pkg_repo *r, const char *name, const char *url,
    const char *type)
{

	free(r->name);
	r->name = xstrdup(name);
	if (url != NULL) {
		free(r->url);
		r->url = xstrdup(url);
	}
	r->ops = pkg_repo_find_type(type);
}

static void
pkg_repo_free(struct pkg_repo *r)
{
	free(r->url);
	free(r->name);
	free(r->pubkey);
	free(r->fingerprints);
	pkg_repo_meta_free(r->meta);
	if (r->fetcher != NULL && r->fetcher->cleanup != NULL)
		r->fetcher->cleanup(r);
	vec_free_and_free(&r->env, pkg_kv_free);
	free(r->dbpath);
	free(r);
}

void
pkg_shutdown(void)
{
	if (!parsed) {
		pkg_emit_error("pkg_shutdown() must be called after pkg_init()");
		_exit(EXIT_FAILURE);
		/* NOTREACHED */
	}

	metalog_close();
	ucl_object_unref(config);
	LL_FREE(repos, pkg_repo_free);

	if (ctx.rootfd != -1) {
		close(ctx.rootfd);
		ctx.rootfd = -1;
	}
	if (ctx.cachedirfd != -1) {
		close(ctx.cachedirfd);
		ctx.cachedirfd = -1;
	}
	if (ctx.pkg_dbdirfd != -1) {
		close(ctx.pkg_dbdirfd);
		ctx.pkg_dbdirfd = -1;
	}

	parsed = false;

	return;
}

int
pkg_repos_total_count(void)
{
	int cnt = 0;
	struct pkg_repo *r;

	LL_COUNT(repos, r, cnt);
	return (cnt);
}

int
pkg_repos_activated_count(void)
{
	struct pkg_repo *r = NULL;
	int count = 0;

	LL_FOREACH(repos, r) {
		if (r->enable)
			count++;
	}

	return (count);
}

int
pkg_repos(struct pkg_repo **r)
{
	if (*r == NULL)
		*r = repos;
	else
		*r = (*r)->next;
	if (*r == NULL)
		return (EPKG_END);
	return (EPKG_OK);
}

const char *
pkg_repo_url(struct pkg_repo *r)
{
	return (r->url);
}

/* The basename of the sqlite DB file and the database name */
const char *
pkg_repo_name(struct pkg_repo *r)
{
	return (r->name);
}

const char *
pkg_repo_key(struct pkg_repo *r)
{
	return (r->pubkey);
}

const char *
pkg_repo_fingerprints(struct pkg_repo *r)
{
	return (r->fingerprints);
}

signature_t
pkg_repo_signature_type(struct pkg_repo *r)
{
	return (r->signature_type);
}

bool
pkg_repo_enabled(struct pkg_repo *r)
{
	return (r->enable);
}

mirror_t
pkg_repo_mirror_type(struct pkg_repo *r)
{
	return (r->mirror_type);
}

int
pkg_repo_priority(struct pkg_repo *r)
{
	return (r->priority);
}

unsigned int
pkg_repo_ip_version(struct pkg_repo *r)
{
	if (r->ip == IPV4)
		return 4;
	if (r->ip == IPV6)
		return 6;
	return 0;
}

/* Locate the repo by the file basename / database name */
struct pkg_repo *
pkg_repo_find(const char *reponame)
{
	struct pkg_repo *r;

	LL_FOREACH(repos, r) {
		if (STREQ(r->name, reponame))
			return (r);
	}
	return (NULL);
}

int64_t
pkg_set_debug_level(int64_t new_debug_level) {
	int64_t old_debug_level = ctx.debug_level;

	ctx.debug_level = new_debug_level;
	return old_debug_level;
}

int
pkg_set_ignore_osversion(bool ignore) {
	if (pkg_initialized())
		return (EPKG_FATAL);

	ucl_object_insert_key(config,
		ucl_object_frombool(ignore),
		"IGNORE_OSVERSION", sizeof("IGNORE_OSVERSION"), false);

	return (EPKG_OK);
}

int
pkg_set_rootdir(const char *rootdir) {
	if (pkg_initialized())
		return (EPKG_FATAL);

	if (ctx.rootfd != -1)
		close(ctx.rootfd);

	if ((ctx.rootfd = open(rootdir, O_DIRECTORY|O_RDONLY|O_CLOEXEC)) < 0) {
		pkg_emit_error("Impossible to open %s", rootdir);
		return (EPKG_FATAL);
	}
	ctx.pkg_rootdir = rootdir;
	ctx.defer_triggers = true;

	return (EPKG_OK);
}

int
pkg_set_ischrooted(bool ischrooted) {
	if (pkg_initialized())
		return (EPKG_FATAL);

	ctx.ischrooted = ischrooted;

	return (EPKG_OK);
}

const char *
pkg_get_cachedir(void)
{

	return (ctx.cachedir);
}

int
pkg_get_cachedirfd(void)
{

	if (ctx.cachedirfd == -1) {
		/*
		 * do not check the value as if we cannot open it means
		 * it has not been created yet
		 */
		ctx.cachedirfd = open(ctx.cachedir, O_DIRECTORY|O_CLOEXEC);
	}

	return (ctx.cachedirfd);
}

int
pkg_get_dbdirfd(void)
{

	if (ctx.pkg_dbdirfd == -1) {
		/*
		 * do not check the value as if we cannot open it means
		 * it has not been created yet
		 */
		ctx.pkg_dbdirfd = open(ctx.dbdir, O_DIRECTORY|O_CLOEXEC);
	}

	return (ctx.pkg_dbdirfd);
}

int
pkg_get_reposdirfd(void)
{
	int dbfd = pkg_get_dbdirfd();
	if (dbfd == -1)
		return (-1);
	if (ctx.pkg_reposdirfd == -1) {
		ctx.pkg_reposdirfd = openat(dbfd, "repos", O_DIRECTORY|O_CLOEXEC);
		if (ctx.pkg_reposdirfd == -1) {
			if (mkdirat(dbfd, "repos", 0755) == -1) {
				return (-1);
			}
			ctx.pkg_reposdirfd = openat(dbfd, "repos", O_DIRECTORY|O_CLOEXEC);
		}
	}
	return (ctx.pkg_reposdirfd);
}

int
pkg_open_devnull(void) {
	pkg_close_devnull();

	if ((ctx.devnullfd = open("/dev/null", O_RDWR)) < 0) {
		pkg_emit_error("Cannot open /dev/null");
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

void
pkg_close_devnull(void) {
	if (ctx.devnullfd != 1) {
		close(ctx.devnullfd);
	}

	return;
}
