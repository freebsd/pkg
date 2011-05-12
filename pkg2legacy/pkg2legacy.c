#include <err.h>

#include <bzlib.h>
#include <unistd.h>
#include <sys/sbuf.h>
#include <string.h>
#include <sysexits.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <archive.h>
#include <archive_entry.h>

#include <fts.h>

#include <pkg.h>

static int
usage(void)
{
	fprintf(stderr, "usage: %s pkgng_repository legacy_repository\n", getprogname());
	return (EX_USAGE);
}

int
main(int argc, char **argv)
{
	struct stat st;
	FTS *fts;
	FTSENT *p;
	char *dir[2];
	char *category;
	char destdir[MAXPATHLEN];
	char destpath[MAXPATHLEN];
	char relativepath[MAXPATHLEN];
	char linkpath[MAXPATHLEN];
	char *newpath;
	BZFILE *bz;
	int bzError;
	struct pkg **deps;
	struct archive *pkgng;
	struct archive *legacypkg;
	struct archive_entry *ae;
	size_t size;
	int i;
	int r;

	char *tmp;
	char *buf;
	struct pkg *pkg = NULL;

	struct sbuf *sbuf = sbuf_new_auto();
	struct sbuf *indexfile = sbuf_new_auto();

	FILE *indexf;

	if (argc != 3)
		return (usage());

	if (lstat(argv[1], &st) != 0)
		err(EX_USAGE, "Can't find pkgng repository");

	dir[0] = argv[1];
	dir[1] = NULL;

	if (!S_ISDIR(st.st_mode))
		errx(EX_USAGE, "%s is not a pkgng repository", argv[1]);

	if (lstat(argv[2], &st) != 0) {
		if (mkdir(argv[2], 0755) != 0)
			err(EX_CANTCREAT, "Unable to create legacy repository: %s", argv[2]);
	} else {
		errx(EX_USAGE, "legacy repository already exist");
	}

	getcwd(destdir, MAXPATHLEN);

	if (argv[2][0] == '/')
		strlcpy(destdir, argv[2], MAXPATHLEN);
	else
		snprintf(destdir, MAXPATHLEN, "%s/%s", destdir, argv[2]);

	snprintf(destpath, MAXPATHLEN, "%s/All", destdir);
	mkdir(destpath, 0755);

	if ((fts = fts_open(dir, FTS_NOSTAT, NULL)) == NULL)
		err(EX_SOFTWARE, "Problem reading the pkgng repository");

	ae = archive_entry_new();

	while ((p = fts_read(fts)) != NULL) {
		if (!strcmp(p->fts_name, "repo.txz"))
			continue;

		if (p->fts_info != FTS_F)
			continue;

		if (pkg_open(&pkg, p->fts_accpath) != EPKG_OK) {
			pkg_error_warn("Unable to open package %s (ignoring)", p->fts_accpath);
			continue;
		}

		archive_entry_clear(ae);
		category = strdup(pkg_get(pkg, PKG_ORIGIN));
		tmp = strrchr(category, '/');
		tmp[0] = '\0';

		/* prepare the indexfile */
		sbuf_printf(indexfile, "%s-%s|" /* name */
				"/usr/ports/%s|" /* path */
				"%s|" /* prefix */
				"%s|" /* comment */
				"/usr/ports/%s/pkg-descr|" /* origin */
				"%s|" /*maintainer */
				"%s|" /* categories */
				"|", /* build depends */
				pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION),
				pkg_get(pkg, PKG_ORIGIN),
				pkg_get(pkg, PKG_PREFIX),
				pkg_get(pkg, PKG_COMMENT),
				pkg_get(pkg, PKG_ORIGIN),
				pkg_get(pkg, PKG_MAINTAINER),
				category /* FIXME where are the other categories */
			   );


		snprintf(destpath, MAXPATHLEN, "%s/%s", destdir, category);
		bzero(&st, sizeof(struct stat));
		if (lstat(category, &st) != 0)
			mkdir(destpath, 0755);

		snprintf(destpath, MAXPATHLEN, "%s/%s-%s.tbz", destpath, pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));
		snprintf(relativepath, MAXPATHLEN, "../%s/%s-%s.tbz", category, pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));
		snprintf(linkpath, MAXPATHLEN, "%s/All/%s.tbz",destdir, pkg_get(pkg, PKG_NAME));

		pkgng = archive_read_new();
		archive_read_support_format_tar(pkgng);
		archive_read_support_compression_all(pkgng);
		archive_read_open_filename(pkgng, p->fts_accpath, 4096);

		legacypkg = archive_write_new();
		archive_write_set_format_ustar(legacypkg);
		archive_write_set_compression_bzip2(legacypkg);
		archive_write_open_filename(legacypkg, destpath);

		archive_entry_set_pathname(ae, "+COMMENT");
		archive_entry_set_filetype(ae, AE_IFREG);
		archive_entry_set_perm(ae, 0644);
		archive_entry_set_gname(ae, "wheel");
		archive_entry_set_uname(ae, "root");
		archive_entry_set_size(ae, strlen(pkg_get(pkg, PKG_COMMENT)));
		archive_write_header(legacypkg, ae);
		archive_write_data(legacypkg, pkg_get(pkg, PKG_COMMENT), strlen(pkg_get(pkg, PKG_COMMENT)));

		sbuf_clear(sbuf);
		sbuf_printf(sbuf, "@comment PKG_FORMAT_REVISION:1.1\n"
				"@name %s-%s\n"
				"@comment ORIGIN:%s\n"
				"@cwd %s\n",
				pkg_get(pkg, PKG_NAME),
				pkg_get(pkg, PKG_VERSION),
				pkg_get(pkg, PKG_ORIGIN),
				pkg_get(pkg, PKG_PREFIX));

		if ((deps = pkg_deps(pkg)) != NULL) {
			for (i = 0; deps[i] != NULL; i++) {
				sbuf_printf(sbuf, "@pkgdep %s-%s\n"
						"@comment DEPORIGIN:%s\n",
						pkg_get(deps[i], PKG_NAME),
						pkg_get(deps[i], PKG_VERSION),
						pkg_get(deps[i], PKG_ORIGIN));
				sbuf_printf(indexfile, "%s-%s ", pkg_get(deps[i], PKG_NAME), pkg_get(deps[i], PKG_VERSION));
			}
		}

		sbuf_cat(indexfile, "|");
		sbuf_printf(indexfile, "%s|"
				"|" /* extract depends */
				"|" /* patch depends */
				"\n", /* fetch depends */
				pkg_get(pkg, PKG_WWW));
		archive_entry_clear(ae);
		while ((r = archive_read_next_header(pkgng, &ae)) != ARCHIVE_EOF) {
			if (archive_entry_pathname(ae)[0] == '+') {
				if (strcmp(archive_entry_pathname(ae), "+MANIFEST") == 0)
					continue;
				else {
					size = archive_entry_size(ae);
					buf = malloc(size + 1);
					archive_write_header(legacypkg, ae);
					archive_read_data(pkgng, buf, size);
					archive_write_data(legacypkg, buf, size);
					free(buf);
					continue;
				}
			}

			size = archive_entry_size(ae);

			strlcpy(destpath, archive_entry_pathname(ae), MAXPATHLEN);
			if (strncmp(destpath, pkg_get(pkg, PKG_PREFIX), strlen(pkg_get(pkg, PKG_PREFIX))) == 0)
				newpath = destpath + strlen(pkg_get(pkg, PKG_PREFIX));
			else {
				sbuf_cat(sbuf, "@cwd /");
				newpath = destpath;
			}

			if (newpath[0] == '/')
				newpath++;

			sbuf_printf(sbuf, "%s\n", newpath);
			archive_entry_set_pathname(ae, newpath);
			buf = malloc(size + 1);
			archive_write_header(legacypkg, ae);
			archive_read_data(pkgng, buf, size);
			archive_write_data(legacypkg, buf, size);
			free(buf);
		}

		archive_entry_set_pathname(ae, "+CONTENTS");
		archive_entry_set_filetype(ae, AE_IFREG);
		archive_entry_set_perm(ae, 0644);
		archive_entry_set_gname(ae, "wheel");
		archive_entry_set_uname(ae, "root");
		archive_entry_set_size(ae, sbuf_len(sbuf));
		archive_write_header(legacypkg, ae);
		archive_write_data(legacypkg, sbuf_data(sbuf), sbuf_len(sbuf));

		archive_write_finish(legacypkg);
		archive_read_finish(pkgng);

		snprintf(destpath, MAXPATHLEN, "%s/All", destdir);
		symlink(relativepath, linkpath);
	}

	snprintf(destpath, MAXPATHLEN, "%s/INDEX.bz2", destdir);
	indexf = fopen(destpath, "w");
	bz = BZ2_bzWriteOpen(&bzError, indexf, 9, 0, 0);
	BZ2_bzWrite(&bzError, bz, sbuf_data(indexfile), sbuf_len(indexfile));
	BZ2_bzWriteClose(&bzError, bz, 0, NULL, NULL);
	fclose(indexf);

	pkg_free(pkg);
	sbuf_delete(indexfile);
	sbuf_delete(sbuf);

	return (EXIT_SUCCESS);
}
