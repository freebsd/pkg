#include <err.h>

#include <bzlib.h>
#include <unistd.h>
#include <sys/sbuf.h>
#include <string.h>
#include <sysexits.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <openssl/md5.h>

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

static void
md5_hash(char *string, char *md5)
{
	unsigned char hash[MD5_DIGEST_LENGTH];
	int i;

	MD5((const unsigned char *)string, strlen(string), hash);

	for (i = 0;  i < MD5_DIGEST_LENGTH; i++)
		sprintf(md5 + (i * 2), "%02x", hash[i]);
}

int
main(int argc, char **argv)
{
	struct stat st;
	FTS *fts;
	FTSENT *p;
	char *dir[2];
	char destdir[MAXPATHLEN + 1];
	char destpath[MAXPATHLEN + 1];
	char relativepath[MAXPATHLEN + 1];
	char linkpath[MAXPATHLEN + 1];
	const char *newpath;
	BZFILE *bz;
	int bzError;
	struct pkg_dep *dep = NULL;
	struct pkg_category *cat = NULL;
	struct pkg_script *script = NULL;
	struct archive *pkgng;
	struct archive *legacypkg;
	struct archive_entry *ae;
	char *tmpbuf, *next, *tofree;
	size_t size;
	int r;
	int ok;
	char md5[2*MD5_DIGEST_LENGTH+1] = "";

	char *buf;
	struct pkg *pkg = NULL;

	struct sbuf *sbuf = sbuf_new_auto();
	struct sbuf *late_sbuf = sbuf_new_auto();
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

	getcwd(destdir, sizeof(destdir));

	if (argv[2][0] == '/')
		strlcpy(destdir, argv[2], sizeof(destdir));
	else
		snprintf(destdir, sizeof(destdir), "%s/%s", destdir, argv[2]);

	snprintf(destpath, sizeof(destpath), "%s/Latest", destdir);
	mkdir(destpath, 0755);
	snprintf(destpath, sizeof(destpath), "%s/All", destdir);
	mkdir(destpath, 0755);

	if ((fts = fts_open(dir, FTS_NOSTAT, NULL)) == NULL)
		err(EX_SOFTWARE, "Problem reading the pkgng repository");

	ae = archive_entry_new();

	while ((p = fts_read(fts)) != NULL) {
		if (!strcmp(p->fts_name, "repo.txz"))
			continue;

		if (p->fts_info != FTS_F)
			continue;

		if (pkg_open(&pkg, p->fts_accpath, NULL) != EPKG_OK) {
			continue;
		}

		printf("Generating %s-%s.tbz...", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));
		fflush(stdout);

		archive_entry_clear(ae);

		/* prepare the indexfile */
		sbuf_printf(indexfile, "%s-%s|" /* name */
				"/usr/ports/%s|" /* path */
				"%s|" /* prefix */
				"%s|" /* comment */
				"/usr/ports/%s/pkg-descr|" /* origin */
				"%s|", /*maintainer */
				pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION),
				pkg_get(pkg, PKG_ORIGIN),
				pkg_get(pkg, PKG_PREFIX),
				pkg_get(pkg, PKG_COMMENT),
				pkg_get(pkg, PKG_ORIGIN),
				pkg_get(pkg, PKG_MAINTAINER)
			   );
		while (pkg_categories(pkg, &cat) == EPKG_OK)
			sbuf_printf(indexfile, "%s ", pkg_category_name(cat));

		sbuf_cat(indexfile, "||");

		snprintf(destpath, sizeof(destpath), "%s/All/%s-%s.tbz", destdir, pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));
		snprintf(relativepath, sizeof(relativepath), "../All/%s-%s.tbz", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));
		snprintf(linkpath, sizeof(linkpath), "%s/Latest/%s.tbz",destdir, pkg_get(pkg, PKG_NAME));

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
		sbuf_clear(late_sbuf);
		sbuf_printf(sbuf, "@comment PKG_FORMAT_REVISION:1.1\n"
				"@name %s-%s\n"
				"@comment ORIGIN:%s\n"
				"@cwd %s\n"
				"@cwd /\n",
				pkg_get(pkg, PKG_NAME),
				pkg_get(pkg, PKG_VERSION),
				pkg_get(pkg, PKG_ORIGIN),
				pkg_get(pkg, PKG_PREFIX));

		while (pkg_deps(pkg, &dep) == EPKG_OK) {
			sbuf_printf(sbuf, "@pkgdep %s-%s\n"
						"@comment DEPORIGIN:%s\n",
						pkg_dep_name(dep),
						pkg_dep_version(dep),
						pkg_dep_origin(dep));
			sbuf_printf(indexfile, "%s-%s ", pkg_dep_name(dep), pkg_dep_version(dep));
		}

		sbuf_cat(indexfile, "|");
		sbuf_printf(indexfile, "%s|"
				"|" /* extract depends */
				"|" /* patch depends */
				"\n", /* fetch depends */
				pkg_get(pkg, PKG_WWW));
		while (pkg_scripts(pkg, &script) == EPKG_OK) {
			archive_entry_clear(ae);
			switch (pkg_script_type(script)) {
				case PKG_SCRIPT_INSTALL:
					archive_entry_set_pathname(ae, "+INSTALL");
					archive_entry_set_filetype(ae, AE_IFREG);
					archive_entry_set_perm(ae, 0755);
					archive_entry_set_gname(ae, "wheel");
					archive_entry_set_uname(ae, "root");
					archive_entry_set_size(ae, strlen(pkg_script_data(script)));
					archive_write_header(legacypkg, ae);
					archive_write_data(legacypkg, pkg_script_data(script), strlen(pkg_script_data(script)));
					break;
				case PKG_SCRIPT_DEINSTALL:
					archive_entry_set_pathname(ae, "+DEINSTALL");
					archive_entry_set_filetype(ae, AE_IFREG);
					archive_entry_set_perm(ae, 0755);
					archive_entry_set_gname(ae, "wheel");
					archive_entry_set_uname(ae, "root");
					archive_entry_set_size(ae, strlen(pkg_script_data(script)));
					archive_write_header(legacypkg, ae);
					archive_write_data(legacypkg, pkg_script_data(script), strlen(pkg_script_data(script)));
					break;
				case PKG_SCRIPT_POST_INSTALL:
					tmpbuf = strdup(pkg_script_data(script));
					tofree = tmpbuf;
					ok = 0;
					while ((next = strchr(tmpbuf, '\n')) != NULL) {
						next[0] = '\0';
						if (!ok && strncmp(tmpbuf, "#@exec", 6) != 0) {
							tmpbuf = next;
							tmpbuf++;
							continue;
						}
						if (!ok) {
							ok = 1;
							tmpbuf = next;
							tmpbuf++;
							continue;
						}
						if (tmpbuf[0] == '#')
							tmpbuf++;
						if (tmpbuf[0] != '@')
							sbuf_printf(late_sbuf, "@exec %s\n", tmpbuf);
						else
							sbuf_printf(late_sbuf, "%s\n", tmpbuf);
						tmpbuf = next;
						tmpbuf++;
					}
					free(tofree);
					break;
				case PKG_SCRIPT_POST_DEINSTALL:
					tmpbuf = strdup(pkg_script_data(script));
					tofree = tmpbuf;
					ok = 0;
					while ((next = strchr(tmpbuf, '\n')) != NULL) {
						next[0] = '\0';
						if (!ok && strncmp(tmpbuf, "#@unexec", 6) != 0) {
							tmpbuf = next;
							tmpbuf++;
							continue;
						}
						if (!ok) {
							ok = 1;
							tmpbuf = next;
							tmpbuf++;
							continue;
						}
						if (tmpbuf[0] == '#')
							tmpbuf++;
						if (tmpbuf[0] != '@')
							sbuf_printf(late_sbuf, "@unexec %s\n", tmpbuf);
						else
							sbuf_printf(late_sbuf, "%s\n", tmpbuf);
						tmpbuf = next;
						tmpbuf++;
					}
					free(tofree);
					break;
				case PKG_SCRIPT_PRE_DEINSTALL:
					tmpbuf = strdup(pkg_script_data(script));
					tofree = tmpbuf;
					ok = 0;
					while ((next = strchr(tmpbuf, '\n')) != NULL) {
						next[0] = '\0';
						if (!ok && strncmp(tmpbuf, "#@unexec", 6) != 0) {
							tmpbuf = next;
							tmpbuf++;
							continue;
						}
						if (!ok) {
							ok = 1;
							tmpbuf = next;
							tmpbuf++;
							continue;
						}
						if (tmpbuf[0] == '#')
							tmpbuf++;
						sbuf_printf(sbuf, "@unexec %s\n", tmpbuf);
						tmpbuf = next;
						tmpbuf++;
					}
					free(tofree);
					break;
				default:
					/* Just ignore */
					break;
			};
		}
		archive_entry_clear(ae);
		while ((r = archive_read_next_header(pkgng, &ae)) != ARCHIVE_EOF) {
			if (archive_entry_pathname(ae)[0] == '+') {
				if (strcmp(archive_entry_pathname(ae), "+MANIFEST") == 0)
					continue;
				else {
					size = archive_entry_size(ae);
					buf = malloc(size + 1);
					md5[0] = '\0';
					md5_hash(buf, md5);
					archive_write_header(legacypkg, ae);
					archive_read_data(pkgng, buf, size);
					archive_write_data(legacypkg, buf, size);
					free(buf);
					continue;
				}
			}

			size = archive_entry_size(ae);

			newpath = archive_entry_pathname(ae);

			if (newpath[0] == '/')
				newpath++;

			if (archive_entry_filetype(ae) == AE_IFDIR)
				continue;

			sbuf_printf(sbuf, "%s\n", newpath);
			archive_entry_set_pathname(ae, newpath);
			buf = malloc(size + 1);
			md5[0] = '\0';
			md5_hash(buf, md5);
			sbuf_printf(sbuf, "@comment MD5:%s\n", md5);
			archive_write_header(legacypkg, ae);
			archive_read_data(pkgng, buf, size);
			archive_write_data(legacypkg, buf, size);
			free(buf);
		}

		sbuf_finish(late_sbuf);
		sbuf_cat(sbuf, sbuf_data(late_sbuf));
		sbuf_finish(sbuf);
		archive_entry_clear(ae);
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

		symlink(relativepath, linkpath);
		while (pkg_categories(pkg, &cat) == EPKG_OK) {
			snprintf(destpath, sizeof(destpath), "%s/%s", destdir, pkg_category_name(cat));
			if (lstat(destpath, &st) != 0)
				mkdir(destpath, 0755);
			snprintf(linkpath, sizeof(linkpath), "%s/%s/%s-%s.tbz", destdir, pkg_category_name(cat), pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));
			symlink(relativepath, linkpath);
		}

		printf("done\n");
	}

	snprintf(destpath, sizeof(destpath), "%s/INDEX.bz2", destdir);
	indexf = fopen(destpath, "w");
	bz = BZ2_bzWriteOpen(&bzError, indexf, 9, 0, 0);
	BZ2_bzWrite(&bzError, bz, sbuf_data(indexfile), sbuf_len(indexfile));
	BZ2_bzWriteClose(&bzError, bz, 0, NULL, NULL);
	fclose(indexf);

	pkg_free(pkg);
	sbuf_delete(indexfile);
	sbuf_delete(sbuf);
	sbuf_delete(late_sbuf);

	return (EXIT_SUCCESS);
}
