#include <sys/param.h>
#include <sys/stat.h>

#include <atf-c.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <private/pkg.h>

ATF_TC_WITHOUT_HEAD(pkgs_insert_sorted);
ATF_TC_BODY(pkgs_insert_sorted, tc)
{
	pkgs_t pkgs = vec_init();

	ATF_REQUIRE_EQ_MSG(pkgs.d, NULL, "vec_init failed");
	ATF_REQUIRE_EQ_MSG(pkgs.cap, 0, "vec_init failed");
	ATF_REQUIRE_EQ_MSG(pkgs.len, 0, "vec_init failed");

	struct pkg *p;

	ATF_REQUIRE_EQ(EPKG_OK, pkg_new(&p, PKG_FILE));
	ATF_REQUIRE(p != NULL);
	p->name = xstrdup("name1");
	ATF_REQUIRE_EQ(pkgs_insert_sorted(&pkgs, p), NULL);
	ATF_REQUIRE_EQ_MSG(pkgs.len, 1, "Fail to insert");

	p = NULL;
	ATF_REQUIRE_EQ(EPKG_OK, pkg_new(&p, PKG_FILE));
	p->name = xstrdup("name1");
	ATF_REQUIRE_MSG(pkgs_insert_sorted(&pkgs, p) !=  NULL, "Collision not detected");

	free(p->name);
	p->name = xstrdup("aname1");

	ATF_REQUIRE_EQ(pkgs_insert_sorted(&pkgs, p), NULL);
	ATF_REQUIRE_EQ_MSG(pkgs.len, 2, "Fail to insert");

	ATF_REQUIRE_STREQ(pkgs.d[0]->name, "aname1");
	ATF_REQUIRE_STREQ(pkgs.d[1]->name, "name1");
}

static void
require_cloexec(int fd)
{
	int flags;

	ATF_REQUIRE(fd != -1);
	flags = fcntl(fd, F_GETFD);
	ATF_REQUIRE_MSG(flags != -1, "fcntl(F_GETFD): %s", strerror(errno));
	ATF_REQUIRE_MSG(flags & FD_CLOEXEC,
	    "the root descriptor would leak into executed programs");
}

ATF_TC_WITHOUT_HEAD(open_root_fd_cloexec);
ATF_TC_BODY(open_root_fd_cloexec, tc)
{
	char reloc[MAXPATHLEN];
	struct pkg *p;

	/* inheritable on purpose: what the package gets must not be */
	ctx.rootfd = open("/", O_DIRECTORY);
	ATF_REQUIRE(ctx.rootfd != -1);

	ATF_REQUIRE_EQ(EPKG_OK, pkg_new(&p, PKG_FILE));
	ATF_REQUIRE_EQ(EPKG_OK, pkg_open_root_fd(p));
	require_cloexec(p->rootfd);
	pkg_free(p);

	ATF_REQUIRE(getcwd(reloc, sizeof(reloc)) != NULL);
	strlcat(reloc, "/reloc", sizeof(reloc));
	ATF_REQUIRE(mkdir(reloc, 0755) == 0);
	ATF_REQUIRE_EQ(EPKG_OK, pkg_new(&p, PKG_FILE));
	pkg_kv_add(&p->annotations, "relocated", reloc, "annotation");
	ATF_REQUIRE_EQ(EPKG_OK, pkg_open_root_fd(p));
	require_cloexec(p->rootfd);
	pkg_free(p);

	close(ctx.rootfd);
	ctx.rootfd = -1;
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, pkgs_insert_sorted);
	ATF_TP_ADD_TC(tp, open_root_fd_cloexec);

	return (atf_no_error());
}
