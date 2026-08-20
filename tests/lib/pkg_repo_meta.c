/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 */

#include <atf-c.h>
#include <string.h>
#include <ucl.h>

#include <pkg.h>
#include <private/pkg.h>

/*
 * Unit tests for pkg_repo_meta_extra().
 *
 * The function returns the "extra" field of a repository's meta as a
 * UCL-emitted string, or NULL when the meta has no "extra" field.
 */

static void
make_repo(struct pkg_repo *repo)
{
	memset(repo, 0, sizeof(*repo));
	repo->meta = pkg_repo_meta_default();
}

ATF_TC_WITHOUT_HEAD(extra_emitted);
ATF_TC_BODY(extra_emitted, tc)
{
	struct pkg_repo repo;
	ucl_object_t *extra;
	char *out;

	make_repo(&repo);

	extra = ucl_object_typed_new(UCL_OBJECT);
	ATF_REQUIRE(extra != NULL);
	ucl_object_insert_key(extra, ucl_object_fromstring("bar"),
	    "foo", 3, false);
	repo.meta->extra_fields = extra;

	out = pkg_repo_meta_extra(&repo);
	ATF_REQUIRE(out != NULL);
	ATF_REQUIRE(strstr(out, "foo") != NULL);
	ATF_REQUIRE(strstr(out, "bar") != NULL);
	free(out);

	pkg_repo_meta_free(repo.meta);
}

ATF_TC_WITHOUT_HEAD(extra_missing);
ATF_TC_BODY(extra_missing, tc)
{
	struct pkg_repo repo;

	make_repo(&repo);

	/* No "extra" field: the function must return NULL, not crash. */
	ATF_REQUIRE(pkg_repo_meta_extra(&repo) == NULL);

	pkg_repo_meta_free(repo.meta);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, extra_emitted);
	ATF_TP_ADD_TC(tp, extra_missing);

	return (atf_no_error());
}
