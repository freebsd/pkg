#include <sys/stat.h>
#include <sys/types.h>

#include <archive.h>
#include <archive_entry.h>
#include <assert.h>
#include <errno.h>
#include <fts.h>
#include <libgen.h>
#include <sqlite3.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>

#include "pkg.h"
#include "pkg_event.h"
#include "pkg_private.h"

static int pkg_repos_is_reserved_name(struct pkg_repos *repos, struct pkg_repos_entry *re);

static int
pkg_repos_is_reserved_name(struct pkg_repos *repos, struct pkg_repos_entry *re)
{
        struct pkg_repos_entry *next = NULL;
	const  char *repo_name = NULL;

        assert(repos != NULL && re != NULL);

        /* 
         * Find if a repository name already exists.
	 *
         * NOTE1: The 'repo' name is always reserved, 
         * as it is being used by default when 
	 * working on a single remote repository,
	 * which means that PACKAGESITE is defined.
	 *
	 * NOTE2: The 'main' and 'temp' names are always
	 * reserved, because they are the names of the 
	 * main and temp databases, 
	 * when working with ATTACH'ed databases in
	 * multi-repos mode.
	 *
	 * NOTE3: The 'local' name is always reserved,
	 * because this is the name of the local database.
         */
	
	repo_name = pkg_repos_get_name(re);

	if ((strcmp(repo_name, "repo") == 0) ||
	    (strcmp(repo_name, "main") == 0) ||
	    (strcmp(repo_name, "temp") == 0) ||
	    (strcmp(repo_name, "local") == 0))
		return (EPKG_FATAL);

        while (pkg_repos_next(repos, &next) == EPKG_OK)
                if ((strcmp(repo_name, pkg_repos_get_name(next)) == 0))
                        return (EPKG_FATAL);

        return (EPKG_OK);
}

int
pkg_repo_fetch(struct pkg *pkg)
{
	char dest[MAXPATHLEN + 1];
	char url[MAXPATHLEN + 1];
	int fetched = 0;
	char cksum[SHA256_DIGEST_LENGTH * 2 +1];
	char *path = NULL;
	const char *packagesite = NULL;
	int retcode = EPKG_OK;

	assert((pkg->type & PKG_REMOTE) == PKG_REMOTE);

	snprintf(dest, sizeof(dest), "%s/%s", pkg_config("PKG_CACHEDIR"),
			 pkg_get(pkg, PKG_REPOPATH));

	/* If it is already in the local cachedir, dont bother to download it */
	if (access(dest, F_OK) == 0)
		goto checksum;

	/* Create the dirs in cachedir */
	if ((path = dirname(dest)) == NULL) {
		pkg_emit_errno("dirname", dest);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	if ((retcode = mkdirs(path)) != 0)
		goto cleanup;

	/* 
	 * In multi-repos the remote URL is stored in pkg[PKG_REPOURL]
	 * For a single attached database the repository URL should be
	 * defined by PACKAGESITE.
	 */
	if (strcasecmp(pkg_config("PKG_MULTIREPOS"), "yes") == 0) {
		packagesite = pkg_get(pkg, PKG_REPOURL);
	} else {
		packagesite = pkg_config("PACKAGESITE");
	}

	if (packagesite == NULL) {
		pkg_emit_error("PACKAGESITE is not defined");
		retcode = 1;
		goto cleanup;
	}

	if (packagesite[strlen(packagesite) - 1] == '/')
		snprintf(url, sizeof(url), "%s%s", packagesite, pkg_get(pkg, PKG_REPOPATH));
	else
		snprintf(url, sizeof(url), "%s/%s", packagesite, pkg_get(pkg, PKG_REPOPATH));

	retcode = pkg_fetch_file(url, dest);
	fetched = 1;

	if (retcode != EPKG_OK)
		goto cleanup;

	checksum:
	retcode = sha256_file(dest, cksum);
	if (retcode == EPKG_OK)
		if (strcmp(cksum, pkg_get(pkg, PKG_CKSUM))) {
			if (fetched == 1) {
				pkg_emit_error("%s-%s failed checksum from repository",
						pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));
				retcode = EPKG_FATAL;
			} else {
				pkg_emit_error("cached package %s-%s: checksum mismatch, fetching from remote",
						pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));
				unlink(dest);
				return (pkg_repo_fetch(pkg));
			}
		}

	cleanup:
	if (retcode != EPKG_OK)
		unlink(dest);

	return (retcode);
}

int
pkg_repos_new(struct pkg_repos **repos)
{
        if ((*repos = calloc(1, sizeof(struct pkg_repos))) == NULL) {
                pkg_emit_errno("calloc", "pkg_repos");
                return (EPKG_FATAL);
        }

        STAILQ_INIT(&(*repos)->nodes);

        return (EPKG_OK);
}

int
pkg_repos_load(struct pkg_repos *repos)
{
        FILE *fp = NULL;
        char *repo_buf[MAXPATHLEN];
        char buf[MAXPATHLEN];
        char *token = NULL, *tmp = NULL;
        unsigned int count = 0, line = 0;
        struct pkg_repos_entry *re = NULL;

        assert(repos != NULL);

        if ((fp = fopen("/etc/pkg/repositories", "r")) == NULL) {
                pkg_emit_errno("fopen", "/etc/pkg/repositories");
                return (EPKG_FATAL);
        }

        while (fgets(buf, MAXPATHLEN, fp)) {
                line++;

                if (buf[0] == '\n' || buf[0] == '#' || buf[0] == ';')
                        continue;

                count = 0;

                buf[strlen(buf) - 1] = '\0';
                tmp = buf;

                /* get the repository entries */
                while ((token = strsep(&tmp, " \t=")) != NULL)
                        if (*token != '\0')
                                repo_buf[count++] = token;

		/* only name and url are needed for the repository */
                if (count != 2) {
                        pkg_emit_error("Wrong repository format at line %d (ignoring repository)", line);
                        continue;
                }

                if ((re = calloc(1, sizeof(struct pkg_repos_entry))) == NULL) {
                        pkg_emit_errno("calloc", "pkg_repos_entry");
                        return (EPKG_FATAL);
                }

		sbuf_set(&re->name, repo_buf[0]);
		sbuf_set(&re->url, repo_buf[1]);
                re->line = line;

                pkg_repos_add(repos, re);
        }

        fclose(fp);

        return (EPKG_OK);
}

int
pkg_repos_add(struct pkg_repos *repos, struct pkg_repos_entry *re)
{
        assert(repos != NULL && re != NULL);

        if (pkg_repos_is_reserved_name(repos, re) != EPKG_OK) {
                pkg_emit_error("Repository name '%s' is already reserved (ignoring repository at line %d)",
                                pkg_repos_get_name(re), pkg_repos_get_line(re));

                sbuf_free(re->name);
		sbuf_free(re->url);
                free(re);

                return (EPKG_FATAL);
        }

        STAILQ_INSERT_TAIL(&repos->nodes, re, entries);

        return (EPKG_OK);
}

int
pkg_repos_next(struct pkg_repos *repos, struct pkg_repos_entry **re)
{
        assert(repos != NULL);

        if (*re == NULL)
                *re = STAILQ_FIRST(&repos->nodes);
        else
                *re = STAILQ_NEXT(*re, entries);

        if (*re == NULL)
                return (EPKG_END);

	 /* Check if we have switched to a repo */
	if (repos->switchable == 1) {
		if ((*re)->switched == 1)
			return (EPKG_OK);
		else
			return(pkg_repos_next(repos, re));
	}

	return (EPKG_OK);
}

int
pkg_repos_switch(struct pkg_repos *repos, const char *reponame)
{
	struct pkg_repos_entry *re = NULL;

	pkg_repos_switch_reset(repos);

	while (pkg_repos_next(repos, &re) == EPKG_OK)
		if (strcmp(reponame, pkg_repos_get_name(re)) == 0) {
			repos->switchable = 1;
			re->switched = 1;
			return (EPKG_OK);
		}

	return (EPKG_FATAL);
}

int
pkg_repos_switch_reset(struct pkg_repos *repos)
{
	struct pkg_repos_entry *re = NULL;

	repos->switchable = 0;

	while (pkg_repos_next(repos, &re) == EPKG_OK)
		re->switched = 0;

	return (EPKG_OK);
}

const char *
pkg_repos_get_name(struct pkg_repos_entry *re)
{
        assert(re != NULL);

        return (sbuf_get(re->name));
}

const char *
pkg_repos_get_url(struct pkg_repos_entry *re)
{
        assert(re != NULL);

        return (sbuf_get(re->url));
}

unsigned int
pkg_repos_get_line(struct pkg_repos_entry *re)
{
        assert(re != NULL);

        return(re->line);
}

int
pkg_repos_exists(struct pkg_repos *repos, const char *reponame)
{
	struct pkg_repos_entry *re = NULL;
	bool exists = false;

	while (pkg_repos_next(repos, &re) == EPKG_OK) {
		if (strcmp(pkg_repos_get_name(re), reponame) == 0) {
			exists = true;
			break;
		}
	}

	return (exists == true ? EPKG_OK : EPKG_FATAL);
}

void
pkg_repos_free(struct pkg_repos *repos)
{
        struct pkg_repos_entry *re1, *re2;

        if (repos == NULL)
                return;

        re1 = STAILQ_FIRST(&repos->nodes);
        while (re1 != NULL) {
                re2 = STAILQ_NEXT(re1, entries);
                
		sbuf_free(re1->name);
		sbuf_free(re1->url);
                free(re1);

                re1 = re2;
        }

        free(repos);
}

static RSA *
load_rsa_private_key(char *rsa_key_path, pem_password_cb *password_cb)
{
	FILE *fp;
	RSA *rsa = NULL;

	if ((fp = fopen(rsa_key_path, "r")) == 0)
		return (NULL);

	if ((rsa = RSA_new()) == NULL) {
		fclose(fp);
		return (NULL);
	}

	if ((rsa = PEM_read_RSAPrivateKey(fp, 0, password_cb, rsa_key_path)) == NULL) {
		fclose(fp);
		return (NULL);
	}

	fclose(fp);
	return (rsa);
}

static RSA *
load_rsa_public_key(const char *rsa_key_path)
{
	FILE *fp;
	RSA *rsa = NULL;
	char errbuf[1024];

	if ((fp = fopen(rsa_key_path, "rb")) == 0) {
		pkg_emit_errno("fopen", rsa_key_path);
		return (NULL);
	}

	if (!PEM_read_RSA_PUBKEY( fp, &rsa, NULL, NULL )) {
		pkg_emit_error("error reading public key(%s): %s", rsa_key_path,
					   ERR_error_string(ERR_get_error(), errbuf));
		fclose(fp);
		return (NULL);
	}

	fclose(fp);
	return (rsa);
}

int
pkg_repo_verify(const char *path, unsigned char *sig, unsigned int sig_len)
{
	char sha256[SHA256_DIGEST_LENGTH *2 +1];
	char errbuf[1024];
	RSA *rsa = NULL;

	sha256_file(path, sha256);

	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();
	OpenSSL_add_all_ciphers();

	rsa = load_rsa_public_key(pkg_config("PUBKEY"));
	if (rsa == NULL)
		return(EPKG_FATAL);

	if (RSA_verify(NID_sha1, sha256, sizeof(sha256), sig, sig_len, rsa) == 0) {
		pkg_emit_error("%s: %s", pkg_config("PUBKEY"),
					   ERR_error_string(ERR_get_error(), errbuf));
		return (EPKG_FATAL);
	}

	RSA_free(rsa);
	ERR_free_strings();

	return (EPKG_OK);
}

int
pkg_create_repo(char *path, void (progress)(struct pkg *pkg, void *data), void *data)
{
	FTS *fts = NULL;
	FTSENT *ent = NULL;

	struct stat st;
	struct pkg *pkg = NULL;
	struct pkg_dep *dep = NULL;
	struct pkg_category *category = NULL;
	struct pkg_license *license = NULL;
	struct pkg_option *option = NULL;
	struct sbuf *manifest = sbuf_new_auto();
	char *ext = NULL;

	sqlite3 *sqlite = NULL;
	sqlite3_stmt *stmt_deps = NULL;
	sqlite3_stmt *stmt_pkg = NULL;
	sqlite3_stmt *stmt_lic1 = NULL;
	sqlite3_stmt *stmt_lic2 = NULL;
	sqlite3_stmt *stmt_cat1 = NULL;
	sqlite3_stmt *stmt_cat2 = NULL;
	sqlite3_stmt *stmt_opts = NULL;

	int64_t package_id;
	char *errmsg = NULL;
	int retcode = EPKG_OK;
	char *pkg_path;
	char cksum[SHA256_DIGEST_LENGTH * 2 +1];

	char *repopath[2];
	char repodb[MAXPATHLEN + 1];

	const char initsql[] = ""
		"CREATE TABLE packages ("
			"id INTEGER PRIMARY KEY,"
			"origin TEXT UNIQUE,"
			"name TEXT NOT NULL,"
			"version TEXT NOT NULL,"
			"comment TEXT NOT NULL,"
			"desc TEXT NOT NULL,"
			"arch TEXT NOT NULL,"
			"osversion TEXT NOT NULL,"
			"maintainer TEXT NOT NULL,"
			"www TEXT,"
			"prefix TEXT NOT NULL,"
			"pkgsize INTEGER NOT NULL,"
			"flatsize INTEGER NOT NULL,"
			"licenselogic INTEGER NOT NULL,"
			"cksum TEXT NOT NULL,"
			"path TEXT NOT NULL," /* relative path to the package in the repository */
			"pkg_format_version INTEGER"
		");"
		"CREATE TABLE deps ("
			"origin TEXT,"
			"name TEXT,"
			"version TEXT,"
			"package_id INTEGER REFERENCES packages(id),"
			"UNIQUE(package_id, origin)"
		");"
		"CREATE TABLE categories ("
			"id INTEGER PRIMARY KEY, "
			"name TEXT NOT NULL UNIQUE "
		");"
		"CREATE TABLE pkg_categories ("
			"package_id INTEGER REFERENCES packages(id), "
			"category_id INTEGER REFERENCES categories(id), "
			"UNIQUE(package_id, category_id)"
		");"
		"CREATE TABLE licenses ("
			"id INTEGER PRIMARY KEY,"
			"name TEXT NOT NULL UNIQUE"
		");"
		"CREATE TABLE pkg_licenses ("
			"package_id INTEGER REFERENCES packages(id), "
			"license_id INTEGER REFERENCES licenses(id), "
			"UNIQUE(package_id, license_id)"
		");"
		"CREATE TABLE options ("
			"package_id INTEGER REFERENCES packages(id), "
			"option TEXT,"
			"value TEXT,"
			"UNIQUE (package_id, option)"
		");"
		"PRAGMA user_version=2;"
		;
	const char pkgsql[] = ""
		"INSERT INTO packages ("
				"origin, name, version, comment, desc, arch, osversion, "
				"maintainer, www, prefix, pkgsize, flatsize, licenselogic, cksum, path"
		")"
		"VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15);";
	const char depssql[] = ""
		"INSERT INTO deps (origin, name, version, package_id) "
		"VALUES (?1, ?2, ?3, ?4);";
	const char licsql[] = "INSERT OR IGNORE INTO licenses(name) VALUES(?1);";
	const char addlicsql[] = "INSERT OR ROLLBACK INTO pkg_licenses(package_id, license_id) "
		"VALUES (?1, (SELECT id FROM licenses WHERE name = ?2));";
	const char catsql[] = "INSERT OR IGNORE INTO categories(name) VALUES(?1);";
	const char addcatsql[] = "INSERT OR ROLLBACK INTO pkg_categories(package_id, category_id) "
		"VALUES (?1, (SELECT id FROM categories WHERE name = ?2));";
	const char addoption[] = "INSERT OR ROLLBACK INTO options (option, value, package_id) "
		"VALUES (?1, ?2, ?3);";

	if (!is_dir(path)) {
		pkg_emit_error("%s is not a directory", path);
		return EPKG_FATAL;
	}

	repopath[0] = path;
	repopath[1] = NULL;

	snprintf(repodb, sizeof(repodb), "%s/repo.sqlite", path);

	if (stat(repodb, &st) != -1)
		if (unlink(repodb) != 0) {
			pkg_emit_errno("unlink", path);
			return EPKG_FATAL;
		}

	sqlite3_initialize();
	if (sqlite3_open(repodb, &sqlite) != SQLITE_OK) {
		sqlite3_shutdown();
		return (EPKG_FATAL);
	}
	
	if ((retcode = sql_exec(sqlite, initsql)) != EPKG_OK)
		goto cleanup;

	if ((retcode = sql_exec(sqlite, "BEGIN TRANSACTION;")) != EPKG_OK)
		goto cleanup;

	if (sqlite3_prepare_v2(sqlite, pkgsql, -1, &stmt_pkg, NULL) != SQLITE_OK) {
		ERROR_SQLITE(sqlite);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	if (sqlite3_prepare_v2(sqlite, depssql, -1, &stmt_deps, NULL) != SQLITE_OK) {
		ERROR_SQLITE(sqlite);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	if (sqlite3_prepare_v2(sqlite, licsql, -1, &stmt_lic1, NULL) != SQLITE_OK) {
		ERROR_SQLITE(sqlite);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	if (sqlite3_prepare_v2(sqlite, addlicsql, -1, &stmt_lic2, NULL) != SQLITE_OK) {
		ERROR_SQLITE(sqlite);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	if (sqlite3_prepare_v2(sqlite, addoption, -1, &stmt_opts, NULL) != SQLITE_OK) {
		ERROR_SQLITE(sqlite);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	if ((fts = fts_open(repopath, FTS_PHYSICAL, NULL)) == NULL) {
		pkg_emit_errno("fts_open", path);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	if (sqlite3_prepare_v2(sqlite, catsql, -1, &stmt_cat1, NULL) != SQLITE_OK) {
		ERROR_SQLITE(sqlite);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	if (sqlite3_prepare_v2(sqlite, addcatsql, -1, &stmt_cat2, NULL) != SQLITE_OK) {
		ERROR_SQLITE(sqlite);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	while ((ent = fts_read(fts)) != NULL) {
		cksum[0] = '\0';
		/* skip everything that is not a file */
		if (ent->fts_info != FTS_F)
			continue;

		ext = strrchr(ent->fts_name, '.');

		if (ext == NULL)
			continue;

		if (strcmp(ext, ".tgz") != 0 &&
				strcmp(ext, ".tbz") != 0 &&
				strcmp(ext, ".txz") != 0 &&
				strcmp(ext, ".tar") != 0)
			continue;

		if (strcmp(ent->fts_name, "repo.txz") == 0)
			continue;

		pkg_path = ent->fts_path;
		pkg_path += strlen(path);
		while (pkg_path[0] == '/' )
			pkg_path++;

		if (pkg_open(&pkg, ent->fts_accpath, manifest) != EPKG_OK) {
			retcode = EPKG_WARN;
			continue;
		}

		if (progress != NULL)
			progress(pkg, data);

		sqlite3_bind_text(stmt_pkg, 1, pkg_get(pkg, PKG_ORIGIN), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_pkg, 2, pkg_get(pkg, PKG_NAME), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_pkg, 3, pkg_get(pkg, PKG_VERSION), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_pkg, 4, pkg_get(pkg, PKG_COMMENT), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_pkg, 5, pkg_get(pkg, PKG_DESC), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_pkg, 6, pkg_get(pkg, PKG_ARCH), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_pkg, 7, pkg_get(pkg, PKG_OSVERSION), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_pkg, 8, pkg_get(pkg, PKG_MAINTAINER), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_pkg, 9, pkg_get(pkg, PKG_WWW), -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_pkg, 10, pkg_get(pkg, PKG_PREFIX), -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt_pkg, 11, ent->fts_statp->st_size);
		sqlite3_bind_int64(stmt_pkg, 12, pkg_flatsize(pkg));
		sha256_file(ent->fts_accpath, cksum);
		sqlite3_bind_int64(stmt_pkg, 13, pkg_licenselogic(pkg));
		sqlite3_bind_text(stmt_pkg, 14, cksum, -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt_pkg, 15, pkg_path, -1, SQLITE_STATIC);

		if (sqlite3_step(stmt_pkg) != SQLITE_DONE) {
			ERROR_SQLITE(sqlite);
			retcode = EPKG_FATAL;
			goto cleanup;
		}
		sqlite3_reset(stmt_pkg);

		package_id = sqlite3_last_insert_rowid(sqlite);

		dep = NULL;
		while (pkg_deps(pkg, &dep) == EPKG_OK) {
			sqlite3_bind_text(stmt_deps, 1, pkg_dep_origin(dep), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt_deps, 2, pkg_dep_name(dep), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt_deps, 3, pkg_dep_version(dep), -1, SQLITE_STATIC);
			sqlite3_bind_int64(stmt_deps, 4, package_id);

			if (sqlite3_step(stmt_deps) != SQLITE_DONE) {
				ERROR_SQLITE(sqlite);
				retcode = EPKG_FATAL;
				goto cleanup;
			}
			sqlite3_reset(stmt_deps);
		}

		category = NULL;
		while (pkg_categories(pkg, &category) == EPKG_OK) {
			sqlite3_bind_text(stmt_cat1, 1, pkg_category_name(category), -1, SQLITE_STATIC);
			sqlite3_bind_int64(stmt_cat2, 1, package_id);
			sqlite3_bind_text(stmt_cat2, 2, pkg_category_name(category), -1, SQLITE_STATIC);

			if (sqlite3_step(stmt_cat1) != SQLITE_DONE) {
				ERROR_SQLITE(sqlite);
				retcode = EPKG_FATAL;
				goto cleanup;
			}

			if (sqlite3_step(stmt_cat2) != SQLITE_DONE) {
				ERROR_SQLITE(sqlite);
				retcode = EPKG_FATAL;
				goto cleanup;
			}
			sqlite3_reset(stmt_cat1);
			sqlite3_reset(stmt_cat2);
		}

		license = NULL;
		while (pkg_licenses(pkg, &license) == EPKG_OK) {
			sqlite3_bind_text(stmt_lic1, 1, pkg_license_name(license), -1, SQLITE_STATIC);
			sqlite3_bind_int64(stmt_lic2, 1, package_id);
			sqlite3_bind_text(stmt_lic2, 2, pkg_license_name(license), -1, SQLITE_STATIC);

			if (sqlite3_step(stmt_lic1) != SQLITE_DONE) {
				ERROR_SQLITE(sqlite);
				retcode = EPKG_FATAL;
				goto cleanup;
			}

			if (sqlite3_step(stmt_lic2) != SQLITE_DONE) {
				ERROR_SQLITE(sqlite);
				retcode = EPKG_FATAL;
				goto cleanup;
			}
			sqlite3_reset(stmt_lic1);
			sqlite3_reset(stmt_lic2);
		}
		option = NULL;
		while (pkg_options(pkg, &option) == EPKG_OK) {
			sqlite3_bind_text(stmt_opts, 1, pkg_option_opt(option), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt_opts, 2, pkg_option_value(option), -1, SQLITE_STATIC);
			sqlite3_bind_int64(stmt_opts, 3, package_id);

			if (sqlite3_step(stmt_opts) != SQLITE_DONE) {
				ERROR_SQLITE(sqlite);
				retcode = EPKG_FATAL;
				goto cleanup;
			}
			sqlite3_reset(stmt_opts);
		}
	}

	if (sqlite3_exec(sqlite, "COMMIT;", NULL, NULL, &errmsg) != SQLITE_OK) {
		pkg_emit_error("sqlite: %s", errmsg);
		retcode = EPKG_FATAL;
	}

	cleanup:
	if (fts != NULL)
		fts_close(fts);

	if (pkg != NULL)
		pkg_free(pkg);

	if (stmt_pkg != NULL)
		sqlite3_finalize(stmt_pkg);

	if (stmt_deps != NULL)
		sqlite3_finalize(stmt_deps);

	if (stmt_cat1 != NULL)
		sqlite3_finalize(stmt_cat1);

	if (stmt_cat2 != NULL)
		sqlite3_finalize(stmt_cat2);

	if (stmt_lic1 != NULL)
		sqlite3_finalize(stmt_lic1);

	if (stmt_lic2 != NULL)
		sqlite3_finalize(stmt_lic2);

	if (stmt_opts != NULL)
		sqlite3_finalize(stmt_opts);

	if (sqlite != NULL)
		sqlite3_close(sqlite);

	if (errmsg != NULL)
		sqlite3_free(errmsg);

	sbuf_delete(manifest);

	sqlite3_shutdown();

	return (retcode);
}

int
pkg_finish_repo(char *path, pem_password_cb *password_cb, char *rsa_key_path)
{
	char repo_path[MAXPATHLEN + 1];
	char repo_archive[MAXPATHLEN + 1];
	struct packing *pack;
	int max_len = 0;
	unsigned char *sigret = NULL;
	int siglen = 0;
	RSA *rsa = NULL;
	char sha256[SHA256_DIGEST_LENGTH * 2 +1];

	snprintf(repo_path, sizeof(repo_path), "%s/repo.sqlite", path);
	snprintf(repo_archive, sizeof(repo_archive), "%s/repo", path);

	packing_init(&pack, repo_archive, TXZ);
	if (rsa_key_path != NULL) {
		if (access(rsa_key_path, R_OK) == -1) {
			pkg_emit_errno("access", rsa_key_path);
			return EPKG_FATAL;
		}

		SSL_load_error_strings();

		OpenSSL_add_all_algorithms();
		OpenSSL_add_all_ciphers();

		rsa = load_rsa_private_key(rsa_key_path, password_cb);
		max_len = RSA_size(rsa);
		sigret = malloc(max_len + 1);
		memset(sigret, 0, max_len);

		sha256_file(repo_path, sha256);

		if (RSA_sign(NID_sha1, sha256, sizeof(sha256), sigret, &siglen, rsa) == 0) {
			/* XXX pass back RSA errors correctly */
			pkg_emit_error("%s: %lu", rsa_key_path, ERR_get_error());
			return EPKG_FATAL;
		}

		packing_append_buffer(pack, sigret, "signature", siglen + 1);

		free(sigret);
		RSA_free(rsa);
		ERR_free_strings();
	}
	packing_append_file(pack, repo_path, "repo.sqlite");
	unlink(repo_path);
	packing_finish(pack);

	return (EPKG_OK);
}
