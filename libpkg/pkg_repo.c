#include <err.h>
#include <ctype.h>
#include <assert.h>
#include <errno.h>
#include <libgen.h>
#include <string.h>
#include <unistd.h>

#include "pkg.h"
#include "pkg_event.h"
#include "pkg_private.h"

int
pkg_repo_fetch(struct pkg *pkg)
{
	char dest[MAXPATHLEN];
	char cksum[65];
	char *path;
	char *url;
	int retcode = EPKG_OK;

	assert((pkg->type & PKG_REMOTE) == PKG_REMOTE ||
		(pkg->type & PKG_UPGRADE) == PKG_UPGRADE);

	snprintf(dest, sizeof(dest), "%s/%s", pkg_config("PKG_CACHEDIR"),
			 pkg_get(pkg, PKG_REPOPATH));

	/* If it is already in the local cachedir, dont bother to download it */
	if (access(dest, F_OK) == 0)
		goto checksum;

	/* Create the dirs in cachedir */
	if ((path = dirname(dest)) == NULL) {
		EMIT_ERRNO("dirname", dest);
		retcode = EPKG_FATAL;
		goto cleanup;
	}
	if ((retcode = mkdirs(path)) != 0)
		goto cleanup;

	asprintf(&url, "%s/%s", pkg_config("PACKAGESITE"),
			 pkg_get(pkg, PKG_REPOPATH));

	retcode = pkg_fetch_file(url, dest);
	free(url);
	if (retcode != EPKG_OK)
		goto cleanup;

	checksum:
	retcode = sha256_file(dest, cksum);
	if (retcode == EPKG_OK)
		if (strcmp(cksum, pkg_get(pkg, PKG_CKSUM))) {
			EMIT_FAILED_CKSUM(pkg);
			retcode = EPKG_FATAL;
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
		EMIT_ERRNO("calloc", "pkg_repos");
		return (EPKG_FATAL);
	}

	STAILQ_INIT(&(*repos)->nodes);

	return (EPKG_OK);
}

int
pkg_repos_load(struct pkg_repos *repos)
{
        FILE *fp;
	char *repo_buf[MAXPATHLEN];
        char buf[MAXPATHLEN];
        char *token = NULL, *tmp = NULL;
        unsigned int count = 0, line = 0;
	struct pkg_repos_entry *re;

	assert(repos != NULL);

        if ((fp = fopen("/etc/pkg/repositories", "r")) == NULL) {
		EMIT_ERRNO("fopen", "/etc/pkg/repositories");
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

		if (count != 2) {
                        warnx("Wrong repository format at line %d (ignoring repository)", line);
                        continue;
                }

		if ((re = calloc(1, sizeof(struct pkg_repos_entry))) == NULL) {
			EMIT_ERRNO("calloc", "pkg_repos_entry");
			return (EPKG_FATAL);
		}

		re->name = strdup(repo_buf[0]);
		re->url  = strdup(repo_buf[1]);
		re->line = line;

		assert(re->name != NULL && re->url != NULL);
                
                pkg_repos_add(repos, re);
        }

        fclose(fp);

        return (EPKG_OK);
}

int
pkg_repos_add(struct pkg_repos *repos, struct pkg_repos_entry *re)
{
	assert(repos != NULL && re != NULL);

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
	else
		return (EPKG_OK);
}

const char *
pkg_repos_get_name(struct pkg_repos_entry *re)
{
	assert(re != NULL);

	return (re->name);
}

const char *
pkg_repos_get_url(struct pkg_repos_entry *re)
{
	assert(re != NULL);

	return (re->url);
}

void
pkg_repos_free(struct pkg_repos *repos)
{
	struct pkg_repos_entry *re1, *re2;

        re1 = STAILQ_FIRST(&repos->nodes);
        while (re1 != NULL) {
                re2 = STAILQ_NEXT(re1, entries);
                
                if (re1->name != NULL)
                        free(re1->name);
                if (re1->url != NULL)
                        free(re1->url);

                free(re1);
                re1 = re2;
        }

	free(repos);
}
