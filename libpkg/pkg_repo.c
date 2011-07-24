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

static int pkg_repos_is_reserved_name(struct pkg_repos *repos, struct pkg_repos_entry *re);
static int pkg_repos_exists_in_pkg(struct pkg *pkg, struct pkg_repos_entry *re);

int
pkg_repo_fetch(struct pkg *pkg)
{
	char dest[MAXPATHLEN];
	char cksum[65];
	char *path = NULL;
	char *url = NULL;
	int retcode = EPKG_OK;
	int fetch_ok = 0;

	struct pkg_repos_entry *re = NULL;

	assert((pkg->type & PKG_REMOTE) == PKG_REMOTE ||
		(pkg->type & PKG_UPGRADE) == PKG_UPGRADE);

	snprintf(dest, sizeof(dest), "%s/%s", pkg_config("PKG_CACHEDIR"),
			 pkg_get(pkg, PKG_REPOPATH));

	/* If it is already in the local cachedir, don't bother to download it */
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

	if (pkg_config("PACKAGESITE") != NULL) {
		asprintf(&url, "%s/%s", pkg_config("PACKAGESITE"),
				 pkg_get(pkg, PKG_REPOPATH));

		retcode = pkg_fetch_file(url, dest);
		free(url);
	} else {
		/* 
		 * Get the repository URL from the package itself 
		 * Working on multiple repos here.
		 *
		 * If a package fetch is not successful go to the next
		 * repository in the list and give it more chance for
		 * successful fetch :)
		 */

		re = NULL; /* starts with the first repository entry */
		fetch_ok = EPKG_FATAL;
		while ((pkg_repos_next_in_pkg(pkg, &re) == EPKG_OK) && fetch_ok != EPKG_OK) {
			printf("Fetching package from repository '%s' [%s]\n",
					pkg_repos_get_name(re),
					pkg_repos_get_url(re));

			asprintf(&url, "%s/%s", pkg_repos_get_url(re),
					 pkg_get(pkg, PKG_REPOPATH));

			fetch_ok = retcode = pkg_fetch_file(url, dest);
			free(url);
		}
	}

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
	struct pkg_repos_entry *re = NULL;

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

	if (pkg_repos_is_reserved_name(repos, re) != EPKG_OK) {
		warnx("Repository name for '%s' is already reserved (ignoring repository at line %d)",
				pkg_repos_get_name(re), pkg_repos_get_line(re));

		if (re->name != NULL)
			free(re->name);
		if (re->url != NULL)
			free(re->url);

		free(re);

		return (EPKG_FATAL);
	}

        STAILQ_INSERT_TAIL(&repos->nodes, re, entries);

        return (EPKG_OK);
}

int
pkg_repos_add_in_pkg(struct pkg *pkg, struct pkg_repos_entry *re)
{
	struct pkg_repos_entry *newre;

	assert(pkg != NULL && re != NULL);

	if (pkg_repos_exists_in_pkg(pkg, re) == EPKG_FATAL)
		return (EPKG_FATAL);

	if ((newre = calloc(1, sizeof(struct pkg_repos_entry))) == NULL) {
		EMIT_ERRNO("calloc", "pkg_repos_entry");
		return (EPKG_FATAL);
	}

	newre->name = strdup(re->name);
	newre->url  = strdup(re->url);
	newre->line = re->line;

	assert (newre->name != NULL && newre->url != NULL);

	STAILQ_INSERT_TAIL(&pkg->repos, newre, entries);

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

int
pkg_repos_next_in_pkg(struct pkg *pkg, struct pkg_repos_entry **re)
{
	assert(pkg != NULL);

	if (*re == NULL)
		*re = STAILQ_FIRST(&pkg->repos);
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

unsigned int
pkg_repos_get_line(struct pkg_repos_entry *re)
{
	assert(re != NULL);

	return(re->line);
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
                
                if (re1->name != NULL)
                        free(re1->name);
                if (re1->url != NULL)
                        free(re1->url);

                free(re1);
                re1 = re2;
        }

	free(repos);
}

void
pkg_repos_free_in_pkg(struct pkg *pkg)
{
	struct pkg_repos_entry *re1, *re2;

	if (pkg == NULL)
		return;

	re1 = STAILQ_FIRST(&pkg->repos);
	while (re1 != NULL) {
		re2 = STAILQ_NEXT(re1, entries);

		if (re1->name != NULL)
			free(re1->name);
		if (re1->url != NULL)
			free(re1->url);

		free(re1);
		re1 = re2;
	}
}

static int
pkg_repos_is_reserved_name(struct pkg_repos *repos, struct pkg_repos_entry *re)
{
	struct pkg_repos_entry *next = NULL;

	/* 
	 * Find if a repository name already exists.
	 * NOTE: The 'repo' name is always reserved, 
	 * as it is being used by default when 
	 * PACKAGESITE is defined.
	 */
	while (pkg_repos_next(repos, &next) == EPKG_OK)
		if ((strcmp(pkg_repos_get_name(re), pkg_repos_get_name(next)) == 0) || \
		    (strcmp(pkg_repos_get_name(re), "repo") == 0))
			return (EPKG_FATAL);

	return (EPKG_OK);
}

static int
pkg_repos_exists_in_pkg(struct pkg *pkg, struct pkg_repos_entry *re)
{
	struct pkg_repos_entry *tmp = NULL;

	while(pkg_repos_next_in_pkg(pkg, &tmp) == EPKG_OK)
		if (strcmp(pkg_repos_get_name(tmp), pkg_repos_get_name(re)) == 0)
			return (EPKG_FATAL);

	return (EPKG_OK);
}
