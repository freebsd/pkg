/*
 * Copyright (c) 2011-2015 Baptiste Daroussin <bapt@FreeBSD.org>
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
#include <sysexits.h>

#include "pkg.h"
#include "private/pkg.h"
#include "private/event.h"
#include "pkg_repos.h"

#ifndef PORTSDIR
#define PORTSDIR "/usr/ports"
#endif
#ifndef DEFAULT_VULNXML_URL
#define DEFAULT_VULNXML_URL "http://vuxml.freebsd.org/freebsd/vuln.xml.bz2"
#endif

#ifdef	OSMAJOR
#define STRINGIFY(X)	TEXT(X)
#define TEXT(X)		#X
#define INDEXFILE	"INDEX-" STRINGIFY(OSMAJOR)
#else
#define INDEXFILE	"INDEX"
#endif

struct pkg_ctx ctx = {
	.eventpipe = -1,
	.debug_level = 0,
	.developer_mode = false,
	.pkg_rootdir = NULL,
	.dbdir = NULL,
	.cachedir = NULL,
	.rootfd = -1,
	.cachedirfd = -1,
	.pkg_dbdirfd = -1,
	.osversion = 0,
	.backup_libraries = false,
};

struct config_entry {
	uint8_t type;
	const char *key;
	const char *def;
	const char *desc;
};

static char myabi[BUFSIZ], myabi_legacy[BUFSIZ];
#ifdef __FreeBSD__
static char myosversion[BUFSIZ];
#endif
static struct pkg_repo *repos = NULL;
ucl_object_t *config = NULL;

static struct config_entry c[] = {
	{
		PKG_STRING,
		"PKG_DBDIR",
		"/var/db/pkg",
		"Where the package databases are stored",
	},
	{
		PKG_STRING,
		"PKG_CACHEDIR",
		"/var/cache/pkg",
		"Directory containing cache of downloaded packages",
	},
	{
		PKG_STRING,
		"PORTSDIR",
		"/usr/ports",
		"Location of the ports collection",
	},
	{
		PKG_STRING,
		"INDEXDIR",
		NULL,		/* Default to PORTSDIR unless defined */
		"Location of the ports INDEX",
	},
	{
		PKG_STRING,
		"INDEXFILE",
		INDEXFILE,
		"Filename of the ports INDEX",
	},
	{
		PKG_BOOL,
		"HANDLE_RC_SCRIPTS",
		"NO",
		"Automatically handle restarting services",
	},
	{
		PKG_BOOL,
		"DEFAULT_ALWAYS_YES",
		"NO",
		"Default to 'yes' for all pkg(8) questions",
	},
	{
		PKG_BOOL,
		"ASSUME_ALWAYS_YES",
		"NO",
		"Answer 'yes' to all pkg(8) questions",
	},
	{
		PKG_ARRAY,
		"REPOS_DIR",
		"/etc/pkg/,"PREFIX"/etc/pkg/repos/",
		"Location of the repository configuration files"
	},
	{
		PKG_STRING,
		"PLIST_KEYWORDS_DIR",
		NULL,
		"Directory containing definitions of plist keywords",
	},
	{
		PKG_BOOL,
		"SYSLOG",
		"YES",
		"Log pkg(8) operations via syslog(3)",
	},
	{
		PKG_STRING,
		"ABI",
		myabi,
		"Override the automatically detected ABI",
	},
	{
		PKG_STRING,
		"ALTABI",
		myabi_legacy,
		"Override the automatically detected old-form ABI",
	},
	{
		PKG_BOOL,
		"DEVELOPER_MODE",
		"NO",
		"Add extra strict, pedantic warnings as an aid to package maintainers",
	},
	{
		PKG_STRING,
		"VULNXML_SITE",
		DEFAULT_VULNXML_URL,
		"URL giving location of the vulnxml database",
	},
	{
		PKG_INT,
		"FETCH_RETRY",
		"3",
		"How many times to retry fetching files",
	},
	{
		PKG_STRING,
		"PKG_PLUGINS_DIR",
		PREFIX"/lib/pkg/",
		"Directory which pkg(8) will load plugins from",
	},
	{
		PKG_BOOL,
		"PKG_ENABLE_PLUGINS",
		"YES",
		"Activate plugin support",
	},
	{
		PKG_ARRAY,
		"PLUGINS",
		NULL,
		"List of plugins that pkg(8) should load",
	},
	{
		PKG_BOOL,
		"DEBUG_SCRIPTS",
		"NO",
		"Run shell scripts in verbose mode to facilitate debugging",
	},
	{
		PKG_STRING,
		"PLUGINS_CONF_DIR",
		PREFIX"/etc/pkg/",
		"Directory containing plugin configuration data",
	},
	{
		PKG_BOOL,
		"PERMISSIVE",
		"NO",
		"Permit package installation despite presence of conflicting packages",
	},
	{
		PKG_BOOL,
		"REPO_AUTOUPDATE",
		"YES",
		"Automatically update repository catalogues prior to package updates",
	},
	{
		PKG_STRING,
		"NAMESERVER",
		NULL,
		"Use this nameserver when looking up addresses",
	},
	{
		PKG_STRING,
		"HTTP_USER_AGENT",
		"pkg/"PKGVERSION,
		"HTTP User-Agent",
	},
	{
		PKG_STRING,
		"EVENT_PIPE",
		NULL,
		"Send all events to the specified fifo or Unix socket",
	},
	{
		PKG_INT,
		"FETCH_TIMEOUT",
		"30",
		"Number of seconds before fetch(3) times out",
	},
	{
		PKG_BOOL,
		"UNSET_TIMESTAMP",
		"NO",
		"Do not include timestamps in the package",
	},
	{
		PKG_STRING,
		"SSH_RESTRICT_DIR",
		NULL,
		"Directory the ssh subsystem will be restricted to",
	},
	{
		PKG_OBJECT,
		"PKG_ENV",
		NULL,
		"Environment variables pkg will use",
	},
	{
		PKG_STRING,
		"PKG_SSH_ARGS",
		NULL,
		"Extras arguments to pass to ssh(1)",
	},
	{
		PKG_INT,
		"DEBUG_LEVEL",
		"0",
		"Level for debug messages",
	},
	{
		PKG_OBJECT,
		"ALIAS",
		NULL,
		"Command aliases",
	},
	{
		PKG_STRING,
		"CUDF_SOLVER",
		NULL,
		"Experimental: tells pkg to use an external CUDF solver",
	},
	{
		PKG_STRING,
		"SAT_SOLVER",
		NULL,
		"Experimental: tells pkg to use an external SAT solver",
	},
	{
		PKG_BOOL,
		"RUN_SCRIPTS",
		"YES",
		"Run post/pre actions scripts",
	},
	{
		PKG_BOOL,
		"CASE_SENSITIVE_MATCH",
		"NO",
		"Match package names case sensitively",
	},
	{
		PKG_INT,
		"LOCK_WAIT",
		"1",
		"Wait time to regain a lock if it is not available"
	},
	{
		PKG_INT,
		"LOCK_RETRIES",
		"5",
		"Retries performed to obtain a lock"
	},
	{
		PKG_BOOL,
		"SQLITE_PROFILE",
		"NO",
		"Profile sqlite queries"
	},
	{
		PKG_INT,
		"WORKERS_COUNT",
		"0",
		"How many workers are used for pkg-repo (hw.ncpu if 0)"
	},
	{
		PKG_BOOL,
		"READ_LOCK",
		"NO",
		"Use read locking for query database"
	},
	{
		PKG_BOOL,
		"PLIST_ACCEPT_DIRECTORIES",
		"NO",
		"Accept directories listed like plain files in plist"
	},
	{
		PKG_INT,
		"IP_VERSION",
		"0",
		"Restrict network access to IPv4 or IPv6 only"
	},
	{
		PKG_BOOL,
		"AUTOMERGE",
		"YES",
		"Automatically merge configuration files"
	},
	{
		PKG_STRING,
		"VERSION_SOURCE",
		NULL,
		"Version source for pkg-version (I, P, R), default is auto detect"
	},
	{
		PKG_BOOL,
		"CONSERVATIVE_UPGRADE",
		"YES",
		"Prefer repos with higher priority during upgrade"
	},
	{
		PKG_BOOL,
		"PKG_CREATE_VERBOSE",
		"NO",
		"Enable verbose mode for 'pkg create'",
	},
	{
		PKG_BOOL,
		"PKG_REPO_HASH",
		"NO",
		"Rename packages with the short hash of their contents",
	},
	{
		PKG_BOOL,
		"PKG_REPO_SYMLINK",
		"NO",
		"Create symlinks from the hashed filename to the regular filename",
	},
	{
		PKG_BOOL,
		"AUTOCLEAN",
		"NO",
		"Always cleanup the cache directory after install/upgrade",
	},
	{
		PKG_STRING,
		"DOT_FILE",
		NULL,
		"Save SAT problem to the specified dot file"
	},
	{
		PKG_OBJECT,
		"REPOSITORIES",
		NULL,
		"Repository config in pkg.conf"
	},
	{
		PKG_ARRAY,
		"VALID_URL_SCHEME",
		"pkg+http,pkg+https,https,http,file,ssh,ftp,ftps,pkg+ssh,pkg+ftp,pkg+ftps",
	},
	{
		PKG_BOOL,
		"ALLOW_BASE_SHLIBS",
		"NO",
		"Enable base libraries analysis",
	},
	{
		PKG_INT,
		"WARN_SIZE_LIMIT",
		"1048576", /* 1 meg */
		"Ask user when performing changes for more than this limit"
	},
	{
		PKG_STRING,
		"METALOG",
		NULL,
		"Write out the METALOG to the specified file",
	},
#ifdef __FreeBSD__
	{
		PKG_INT,
		"OSVERSION",
		myosversion,
		"FreeBSD OS version",
	},
	{
		PKG_BOOL,
		"IGNORE_OSVERSION",
		"NO",
		"Ignore FreeBSD OS version check",
	},
#endif
	{
		PKG_BOOL,
		"BACKUP_LIBRARIES",
		"NO",
		"Backup old versions of libraries during an upgrade",
	},
	{
		PKG_STRING,
		"BACKUP_LIBRARY_PATH",
		PREFIX "/lib/compat/pkg",
		"Path where pkg will backup libraries",
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

int
pkg_initialized(void)
{
	return (parsed);
}

const pkg_object *
pkg_config_get(const char *key) {
	return (ucl_object_find_key(config, key));
}

const char *
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

	pkg_debug(1, "PkgConfig: parsing repository object %s", rname);

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
			pkg_debug(1, "PkgConfig: disabling repo %s", rname);
			HASH_DEL(repos, r);
			pkg_repo_free(r);
			return;
		}
	}

	while ((cur = ucl_iterate_object(obj, &it, true))) {
		key = ucl_object_key(cur);
		if (key == NULL)
			continue;

		if (strcasecmp(key, "url") == 0) {
			if (cur->type != UCL_STRING) {
				pkg_emit_error("Expecting a string for the "
				    "'%s' key of the '%s' repo",
				    key, rname);
				return;
			}
			url = ucl_object_tostring(cur);
		} else if (strcasecmp(key, "pubkey") == 0) {
			if (cur->type != UCL_STRING) {
				pkg_emit_error("Expecting a string for the "
				    "'%s' key of the '%s' repo",
				    key, rname);
				return;
			}
			pubkey = ucl_object_tostring(cur);
		} else if (strcasecmp(key, "mirror_type") == 0) {
			if (cur->type != UCL_STRING) {
				pkg_emit_error("Expecting a string for the "
				    "'%s' key of the '%s' repo",
				    key, rname);
				return;
			}
			mirror_type = ucl_object_tostring(cur);
		} else if (strcasecmp(key, "signature_type") == 0) {
			if (cur->type != UCL_STRING) {
				pkg_emit_error("Expecting a string for the "
				    "'%s' key of the '%s' repo",
				    key, rname);
				return;
			}
			signature_type = ucl_object_tostring(cur);
		} else if (strcasecmp(key, "fingerprints") == 0) {
			if (cur->type != UCL_STRING) {
				pkg_emit_error("Expecting a string for the "
				    "'%s' key of the '%s' repo",
				    key, rname);
				return;
			}
			fingerprints = ucl_object_tostring(cur);
		} else if (strcasecmp(key, "type") == 0) {
			if (cur->type != UCL_STRING) {
				pkg_emit_error("Expecting a string for the "
					"'%s' key of the '%s' repo",
					key, rname);
				return;
			}
			type = ucl_object_tostring(cur);
		} else if (strcasecmp(key, "ip_version") == 0) {
			if (cur->type != UCL_INT) {
				pkg_emit_error("Expecting a integer for the "
					"'%s' key of the '%s' repo",
					key, rname);
				return;
			}
			use_ipvx = ucl_object_toint(cur);
			if (use_ipvx != 4 && use_ipvx != 6)
				use_ipvx = 0;
		} else if (strcasecmp(key, "priority") == 0) {
			if (cur->type != UCL_INT) {
				pkg_emit_error("Expecting a integer for the "
					"'%s' key of the '%s' repo",
					key, rname);
				return;
			}
			priority = ucl_object_toint(cur);
		} else if (strcasecmp(key, "env") == 0) {
			if (cur->type != UCL_OBJECT) {
				pkg_emit_error("Expecting an object for the "
					"'%s' key of the '%s' repo",
					key, rname);
			}
			env = cur;
		}
	}

	if (r == NULL && url == NULL) {
		pkg_debug(1, "No repo and no url for %s", rname);
		return;
	}

	if (r == NULL)
		r = pkg_repo_new(rname, url, type);
	else
		pkg_repo_overwrite(r, rname, url, type);

	if (signature_type != NULL) {
		if (strcasecmp(signature_type, "pubkey") == 0)
			r->signature_type = SIG_PUBKEY;
		else if (strcasecmp(signature_type, "fingerprints") == 0)
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
		if (strcasecmp(mirror_type, "srv") == 0)
			r->mirror_type = SRV;
		else if (strcasecmp(mirror_type, "http") == 0)
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
		r->flags = REPO_FLAGS_USE_IPV4;
	else if (use_ipvx == 6)
		r->flags = REPO_FLAGS_USE_IPV6;

	if (env != NULL) {
		it = NULL;
		while ((cur = ucl_iterate_object(env, &it, true))) {
			kv = pkg_kv_new(ucl_object_key(cur),
			    ucl_object_tostring_forced(cur));
			DL_APPEND(r->env, kv);
		}
	}
}

static void
add_repo_obj(const ucl_object_t *obj, const char *file, pkg_init_flags flags)
{
	struct pkg_repo *r;
	const char *key;

	key = ucl_object_key(obj);
	pkg_debug(1, "PkgConfig: parsing repo key '%s' in file '%s'", key, file);
	r = pkg_repo_find(key);
	if (r != NULL)
		pkg_debug(1, "PkgConfig: overwriting repository %s", key);
       add_repo(obj, r, key, flags);
}

static void
walk_repo_obj(const ucl_object_t *obj, const char *file, pkg_init_flags flags)
{
	const ucl_object_t *cur;
	ucl_object_iter_t it = NULL;
	struct pkg_repo *r;
	const char *key;

	while ((cur = ucl_iterate_object(obj, &it, true))) {
		key = ucl_object_key(cur);
		pkg_debug(1, "PkgConfig: parsing key '%s'", key);
		r = pkg_repo_find(key);
		if (r != NULL)
			pkg_debug(1, "PkgConfig: overwriting repository %s", key);
		if (cur->type == UCL_OBJECT)
			add_repo(cur, r, key, flags);
		else
			pkg_emit_error("Ignoring bad configuration entry in %s: %s",
			    file, ucl_object_emit(cur, UCL_EMIT_YAML));
	}
}

static void
load_repo_file(int dfd, const char *repodir, const char *repofile,
    pkg_init_flags flags)
{
	struct ucl_parser *p;
	ucl_object_t *obj = NULL;
	const char *myarch = NULL;
	const char *myarch_legacy = NULL;
	int fd;

	p = ucl_parser_new(0);

	myarch = pkg_object_string(pkg_config_get("ABI"));
	ucl_parser_register_variable (p, "ABI", myarch);

	myarch_legacy = pkg_object_string(pkg_config_get("ALTABI"));
	ucl_parser_register_variable (p, "ALTABI", myarch_legacy);

	pkg_debug(1, "PKgConfig: loading %s/%s", repodir, repofile);
	fd = openat(dfd, repofile, O_RDONLY);
	if (fd == -1) {
		pkg_errno("Unable to open '%s/%s'", repodir, repofile);
		return;
	}
	if (!ucl_parser_add_fd(p, fd)) {
		pkg_emit_error("Error parsing: '%s/%s': %s", repodir,
		    repofile, ucl_parser_get_error(p));
		ucl_parser_free(p);
		close(fd);
		return;
	}
	close(fd);

	obj = ucl_parser_get_object(p);
	if (obj == NULL)
		return;

	if (obj->type == UCL_OBJECT)
		walk_repo_obj(obj, repofile, flags);

	ucl_object_unref(obj);
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
	if (strcmp(p, ".conf") != 0)
		return (0);
	return (1);
}

static void
load_repo_files(const char *repodir, pkg_init_flags flags)
{
	struct dirent **ent;
	int nents, i, fd;

	pkg_debug(1, "PkgConfig: loading repositories in %s", repodir);
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
	const char	*myabi;
	int		 osmajor;

	if (getenv("IGNORE_OSMAJOR") != NULL)
		return (true);

	myabi = pkg_object_string(pkg_config_get("ABI"));
	myabi = strchr(myabi,':');
	if (myabi == NULL) {
		pkg_emit_error("Invalid ABI");
		return (false);
	}
	myabi++;

	osmajor = (int) strtol(myabi, NULL, 10);

	return (osmajor == OSMAJOR);
#else
	return (true);		/* Can't tell, so assume yes  */
#endif
}


int
pkg_init(const char *path, const char *reposdir)
{
	return (pkg_ini(path, reposdir, 0));
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
	const char *metalog = NULL;
	const char *useragent = NULL;
	const char *evpipe = NULL;
	const char *url;
	struct pkg_repo *repo = NULL;
	const ucl_object_t *cur, *object;
	ucl_object_t *obj = NULL, *o, *ncfg;
	ucl_object_iter_t it = NULL;
	UT_string *ukey = NULL;
	bool fatal_errors = false;
	int conffd = -1;
	char *tmp = NULL;
	struct os_info oi;

	k = NULL;
	o = NULL;
	if (ctx.rootfd == -1 && (ctx.rootfd = open("/", O_DIRECTORY|O_RDONLY|O_CLOEXEC)) < 0) {
		pkg_emit_error("Impossible to open /");
		return (EPKG_FATAL);
	}

	memset(&oi, 0, sizeof(oi));
	pkg_get_myarch(myabi, BUFSIZ, &oi);
	pkg_get_myarch_legacy(myabi_legacy, BUFSIZ);
#ifdef __FreeBSD__
	ctx.osversion = oi.osversion;
	snprintf(myosversion, sizeof(myosversion), "%d", ctx.osversion);
#endif
	if (parsed != false) {
		pkg_emit_error("pkg_init() must only be called once");
		return (EPKG_FATAL);
	}

	if (((flags & PKG_INIT_FLAG_USE_IPV4) == PKG_INIT_FLAG_USE_IPV4) &&
	    ((flags & PKG_INIT_FLAG_USE_IPV6) == PKG_INIT_FLAG_USE_IPV6)) {
		pkg_emit_error("Invalid flags for pkg_init()");
		return (EPKG_FATAL);
	}

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

	if (path == NULL)
		conffd = openat(ctx.rootfd, &PREFIX"/etc/pkg.conf"[1], 0);
	else
		conffd = open(path, O_RDONLY);
	if (conffd == -1 && errno != ENOENT) {
		pkg_errno("Cannot open %s/%s",
		    ctx.pkg_rootdir != NULL ? ctx.pkg_rootdir : "",
		    path);
	}

	p = ucl_parser_new(0);
	ucl_parser_register_variable (p, "ABI", myabi);
	ucl_parser_register_variable (p, "ALTABI", myabi_legacy);
#ifdef __FreeBSD__
	ucl_parser_register_variable(p, "OSVERSION", myosversion);
#endif
	if (oi.name != NULL) {
		ucl_parser_register_variable(p, "OSNAME", oi.name);
	}
	if (oi.version != NULL) {
		ucl_parser_register_variable(p, "RELEASE", oi.version);
	}
	if (oi.version_major != NULL) {
		ucl_parser_register_variable(p, "VERSION_MAJOR", oi.version_major);
	}
	if (oi.version_minor != NULL) {
		ucl_parser_register_variable(p, "VERSION_MINOR", oi.version_minor);
	}
	if (oi.arch != NULL) {
		ucl_parser_register_variable(p, "ARCH", oi.arch);
	}
	free(oi.name);
	free(oi.version);
	free(oi.version_major);
	free(oi.version_minor);
	free(oi.arch);

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
	utstring_new(ukey);
	while (obj != NULL && (cur = ucl_iterate_object(obj, &it, true))) {
		utstring_clear(ukey);
		key = ucl_object_key(cur);
		for (i = 0; key[i] != '\0'; i++)
			utstring_printf(ukey, "%c", toupper(key[i]));
		object = ucl_object_find_keyl(config, utstring_body(ukey), utstring_len(ukey));

		if (strncasecmp(utstring_body(ukey), "PACKAGESITE", utstring_len(ukey))
		    == 0 || strncasecmp(utstring_body(ukey), "PUBKEY",
		    utstring_len(ukey)) == 0 || strncasecmp(utstring_body(ukey),
		    "MIRROR_TYPE", utstring_len(ukey)) == 0) {
			pkg_emit_error("%s in pkg.conf is no longer "
			    "supported.  Convert to the new repository style."
			    "  See pkg.conf(5)", utstring_body(ukey));
			fatal_errors = true;
			continue;
		}

		/* ignore unknown keys */
		if (object == NULL)
			continue;

		if (object->type != cur->type) {
			pkg_emit_error("Malformed key %s, ignoring", key);
			continue;
		}

		if (ncfg == NULL)
			ncfg = ucl_object_typed_new(UCL_OBJECT);
		ucl_object_insert_key(ncfg, ucl_object_copy(cur), utstring_body(ukey), utstring_len(ukey), true);
	}
	utstring_free(ukey);

	if (fatal_errors) {
		ucl_object_unref(ncfg);
		ucl_parser_free(p);
		return (EPKG_FATAL);
	}

	if (ncfg != NULL) {
		it = NULL;
		while (( cur = ucl_iterate_object(ncfg, &it, true))) {
			key = ucl_object_key(cur);
			ucl_object_replace_key(config, ucl_object_ref(cur), key, strlen(key), true);
		}
		ucl_object_unref(ncfg);
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

	if (pkg_object_string(pkg_config_get("ABI")) == NULL ||
	    strcmp(pkg_object_string(pkg_config_get("ABI")), "unknown") == 0) {
		pkg_emit_error("Unable to determine ABI");
		return (EPKG_FATAL);
	}

	pkg_debug(1, "%s", "pkg initialized");

#ifdef __FreeBSD__
	ctx.osversion = pkg_object_int(pkg_config_get("OSVERSION"));
#endif
	/* Start the event pipe */
	evpipe = pkg_object_string(pkg_config_get("EVENT_PIPE"));
	if (evpipe != NULL)
		connect_evpipe(evpipe);

	ctx.debug_level = pkg_object_int(pkg_config_get("DEBUG_LEVEL"));
	ctx.developer_mode = pkg_object_bool(pkg_config_get("DEVELOPER_MODE"));
	ctx.dbdir = pkg_object_string(pkg_config_get("PKG_DBDIR"));
	ctx.cachedir = pkg_object_string(pkg_config_get("PKG_CACHEDIR"));
	ctx.backup_libraries = pkg_object_bool(pkg_config_get("BACKUP_LIBRARIES"));
	ctx.backup_library_path = pkg_object_string(pkg_config_get("BACKUP_LIBRARY_PATH"));

	it = NULL;
	object = ucl_object_find_key(config, "PKG_ENV");
	while ((cur = ucl_iterate_object(object, &it, true))) {
		evkey = ucl_object_key(cur);
		pkg_debug(1, "Setting env var: %s", evkey);
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
			return (EPKG_FATAL);
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
			return (EPKG_FATAL);
		}
	}

	/* bypass resolv.conf with specified NAMESERVER if any */
	nsname = pkg_object_string(pkg_config_get("NAMESERVER"));
	if (nsname != NULL && set_nameserver(nsname) != 0)
			pkg_emit_error("Unable to set nameserver, ignoring");

	/* Open metalog */
	metalog = pkg_object_string(pkg_config_get("METALOG"));
	if (metalog != NULL) {
		if(metalog_open(metalog) != EPKG_OK) {
			return (EPKG_FATAL);
		}
	}

	return (EPKG_OK);
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
		if (strcasecmp(type, (*cur)->type) == 0) {
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
	r->ops = pkg_repo_find_type(type);
	r->url = xstrdup(url);
	r->signature_type = SIG_NONE;
	r->mirror_type = NOMIRROR;
	r->enable = true;
	r->meta = pkg_repo_meta_default();
	r->name = xstrdup(name);
	HASH_ADD_KEYPTR(hh, repos, r->name, strlen(r->name), r);

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
	HASH_DEL(repos, r);
	HASH_ADD_KEYPTR(hh, repos, r->name, strlen(r->name), r);
}

static void
pkg_repo_free(struct pkg_repo *r)
{
	struct pkg_kv *kv, *tmp;

	free(r->url);
	free(r->name);
	free(r->pubkey);
	free(r->meta);
	if (r->ssh != NULL) {
		fprintf(r->ssh, "quit\n");
		pclose(r->ssh);
	}
	LL_FOREACH_SAFE(r->env, kv, tmp) {
		LL_DELETE(r->env, kv);
		pkg_kv_free(kv);
	}
	free(r);
}

void
pkg_shutdown(void)
{
	if (!parsed) {
		pkg_emit_error("pkg_shutdown() must be called after pkg_init()");
		_exit(EX_SOFTWARE);
		/* NOTREACHED */
	}

	ucl_object_unref(config);
	HASH_FREE(repos, pkg_repo_free);

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

	return (HASH_COUNT(repos));
}

int
pkg_repos_activated_count(void)
{
	struct pkg_repo *r = NULL;
	int count = 0;

	for (r = repos; r != NULL; r = r->hh.next) {
		if (r->enable)
			count++;
	}

	return (count);
}

int
pkg_repos(struct pkg_repo **r)
{
	HASH_NEXT(repos, (*r));
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

unsigned int
pkg_repo_priority(struct pkg_repo *r)
{
	return (r->priority);
}

unsigned int
pkg_repo_ip_version(struct pkg_repo *r)
{
	if ((r->flags & PKG_INIT_FLAG_USE_IPV4) == PKG_INIT_FLAG_USE_IPV4)
		return 4;
	else if ((r->flags & PKG_INIT_FLAG_USE_IPV6) == PKG_INIT_FLAG_USE_IPV6)
		return 6;
	else
		return 0;
}

/* Locate the repo by the file basename / database name */
struct pkg_repo *
pkg_repo_find(const char *reponame)
{
	struct pkg_repo *r;

	HASH_FIND_STR(repos, reponame, r);
	return (r);
}

int64_t
pkg_set_debug_level(int64_t new_debug_level) {
	int64_t old_debug_level = ctx.debug_level;

	ctx.debug_level = new_debug_level;
	return old_debug_level;
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
