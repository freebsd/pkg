/*-
 * Copyright (c) 2011-2014 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011 Will Andrews <will@FreeBSD.org>
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


#include <archive.h>
#include <archive_entry.h>
#include <assert.h>
#include <fcntl.h>
#include <fts.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"

static const char *packing_set_format(struct archive *a, pkg_formats format);

struct packing {
	struct archive *aread;
	struct archive *awrite;
	struct archive_entry_linkresolver *resolver;
};

int
packing_init(struct packing **pack, const char *path, pkg_formats format)
{
	char archive_path[MAXPATHLEN];
	const char *ext;

	assert(pack != NULL);

	*pack = xcalloc(1, sizeof(struct packing));

	(*pack)->aread = archive_read_disk_new();
	archive_read_disk_set_standard_lookup((*pack)->aread);
	archive_read_disk_set_symlink_physical((*pack)->aread);

	(*pack)->awrite = archive_write_new();
	archive_write_set_format_pax_restricted((*pack)->awrite);
	ext = packing_set_format((*pack)->awrite, format);
	if (ext == NULL) {
		archive_read_close((*pack)->aread);
		archive_read_free((*pack)->aread);
		archive_write_close((*pack)->awrite);
		archive_write_free((*pack)->awrite);
		*pack = NULL;
		return EPKG_FATAL; /* error set by _set_format() */
	}
	snprintf(archive_path, sizeof(archive_path), "%s.%s", path,
	    ext);

	pkg_debug(1, "Packing to file '%s'", archive_path);
	if (archive_write_open_filename(
	    (*pack)->awrite, archive_path) != ARCHIVE_OK) {
		pkg_emit_errno("archive_write_open_filename",
		    archive_path);
		archive_read_close((*pack)->aread);
		archive_read_free((*pack)->aread);
		archive_write_close((*pack)->awrite);
		archive_write_free((*pack)->awrite);
		*pack = NULL;
		return EPKG_FATAL;
	}

	(*pack)->resolver = archive_entry_linkresolver_new();
	archive_entry_linkresolver_set_strategy((*pack)->resolver,
	    archive_format((*pack)->awrite));

	return (EPKG_OK);
}

int
packing_append_buffer(struct packing *pack, const char *buffer,
    const char *path, int size)
{
	struct archive_entry *entry;
	int ret = EPKG_OK;

	entry = archive_entry_new();
	archive_entry_clear(entry);
	archive_entry_set_filetype(entry, AE_IFREG);
	archive_entry_set_perm(entry, 0644);
	archive_entry_set_gname(entry, "wheel");
	archive_entry_set_uname(entry, "root");
	archive_entry_set_pathname(entry, path);
	archive_entry_set_size(entry, size);
	if (archive_write_header(pack->awrite, entry) == -1) {
		pkg_emit_errno("archive_write_header", path);
		ret = EPKG_FATAL;
		goto cleanup;
	}

	if (archive_write_data(pack->awrite, buffer, size) == -1) {
		pkg_emit_errno("archive_write_data", path);
		ret = EPKG_FATAL;
	}

cleanup:
	archive_entry_free(entry);

	return (ret);
}

int
packing_append_file_attr(struct packing *pack, const char *filepath,
    const char *newpath, const char *uname, const char *gname, mode_t perm,
    u_long fflags)
{
	int fd;
	int retcode = EPKG_OK;
	int ret;
	time_t source_time;
	struct stat st;
	struct archive_entry *entry, *sparse_entry;
	bool unset_timestamp;
	const char *source_date_epoch;
	char buf[32768];
	int len;

	entry = archive_entry_new();
	archive_entry_copy_sourcepath(entry, filepath);

	pkg_debug(2, "Packing file '%s'", filepath);

	if (lstat(filepath, &st) != 0) {
		pkg_emit_errno("lstat", filepath);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	ret = archive_read_disk_entry_from_file(pack->aread, entry, -1,
			&st);
	if (ret != ARCHIVE_OK) {
		pkg_emit_error("%s: %s", filepath,
				archive_error_string(pack->aread));
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	if (newpath != NULL)
		archive_entry_set_pathname(entry, newpath);

	if (archive_entry_filetype(entry) != AE_IFREG) {
		archive_entry_set_size(entry, 0);
	}

	if (uname != NULL && uname[0] != '\0') {
		archive_entry_set_uname(entry, uname);
	}

	if (gname != NULL && gname[0] != '\0') {
		archive_entry_set_gname(entry, gname);
	}

	if (fflags > 0)
		archive_entry_set_fflags(entry, fflags, 0);

	if (perm != 0)
		archive_entry_set_perm(entry, perm);

	unset_timestamp = pkg_object_bool(pkg_config_get("UNSET_TIMESTAMP"));

	if (unset_timestamp) {
		archive_entry_unset_atime(entry);
		archive_entry_unset_ctime(entry);
		archive_entry_unset_mtime(entry);
		archive_entry_unset_birthtime(entry);
	}

	if ((source_date_epoch = getenv("SOURCE_DATE_EPOCH")) != NULL) {
		if (source_date_epoch[strspn(source_date_epoch, "0123456789")] != '\0') {
			pkg_emit_error("Bad environment variable "
			    "SOURCE_DATE_EPOCH: %s", source_date_epoch);
			retcode = EPKG_FATAL;
			goto cleanup;
		}
		source_time = strtoll(source_date_epoch, NULL, 10);
		archive_entry_set_atime(entry, source_time, 0);
		archive_entry_set_ctime(entry, source_time, 0);
		archive_entry_set_mtime(entry, source_time, 0);
		archive_entry_set_birthtime(entry, source_time, 0);
	}

	archive_entry_linkify(pack->resolver, &entry, &sparse_entry);

	if (sparse_entry != NULL && entry == NULL)
		entry = sparse_entry;

	archive_write_header(pack->awrite, entry);

	if (archive_entry_size(entry) > 0) {
		if ((fd = open(filepath, O_RDONLY)) < 0) {
			pkg_emit_errno("open", filepath);
			retcode = EPKG_FATAL;
			goto cleanup;
		}

		while ((len = read(fd, buf, sizeof(buf))) > 0)
			if (archive_write_data(pack->awrite, buf, len) == -1) {
				pkg_emit_errno("archive_write_data", "archive write error");
				retcode = EPKG_FATAL;
				break;
			}

		if (len == -1) {
			pkg_emit_errno("read", "file read error");
			retcode = EPKG_FATAL;
		}
		close(fd);
	}

cleanup:
	archive_entry_free(entry);
	return (retcode);
}

int
packing_append_tree(struct packing *pack, const char *treepath,
    const char *newroot)
{
	FTS *fts = NULL;
	FTSENT *fts_e = NULL;
	size_t treelen;
	UT_string *sb;
	char *paths[2] = { __DECONST(char *, treepath), NULL };

	treelen = strlen(treepath);
	fts = fts_open(paths, FTS_PHYSICAL | FTS_XDEV, NULL);
	if (fts == NULL)
		goto cleanup;

	utstring_new(sb);
	while ((fts_e = fts_read(fts)) != NULL) {
		switch(fts_e->fts_info) {
		case FTS_D:
		case FTS_DEFAULT:
		case FTS_F:
		case FTS_SL:
		case FTS_SLNONE:
			 /* Entries not within this tree are irrelevant. */
			 if (fts_e->fts_pathlen <= treelen)
				  break;
			 utstring_clear(sb);
			 /* Strip the prefix to obtain the target path */
			 if (newroot) /* Prepend a root if one is specified */
				  utstring_printf(sb, "%s", newroot);
			 /* +1 = skip trailing slash */
			 utstring_printf(sb, "%s", fts_e->fts_path + treelen + 1);
			 packing_append_file_attr(pack, fts_e->fts_name,
			    utstring_body(sb), NULL, NULL, 0, 0);
			 break;
		case FTS_DC:
		case FTS_DNR:
		case FTS_ERR:
		case FTS_NS:
			 /* XXX error cases, check fts_e->fts_errno and
			  *     bubble up the call chain */
			 break;
		default:
			 break;
		}
	}
	utstring_free(sb);
cleanup:
	fts_close(fts);
	return EPKG_OK;
}

void
packing_finish(struct packing *pack)
{
	if (pack == NULL)
		return;

	archive_read_close(pack->aread);
	archive_read_free(pack->aread);

	archive_write_close(pack->awrite);
	archive_write_free(pack->awrite);

	free(pack);
}

static const char *
packing_set_format(struct archive *a, pkg_formats format)
{
	const char *notsupp_fmt = "%s is not supported, trying %s";

	switch (format) {
	case TXZ:
		if (archive_write_add_filter_xz(a) == ARCHIVE_OK)
			return ("txz");
		else
			pkg_emit_error(notsupp_fmt, "xz", "bzip2");
	case TBZ:
		if (archive_write_add_filter_bzip2(a) == ARCHIVE_OK)
			return ("tbz");
		else
			pkg_emit_error(notsupp_fmt, "bzip2", "gzip");
	case TGZ:
		if (archive_write_add_filter_gzip(a) == ARCHIVE_OK)
			return ("tgz");
		else
			pkg_emit_error(notsupp_fmt, "gzip", "plain tar");
	case TAR:
		archive_write_add_filter_none(a);
		return ("tar");
	}
	return (NULL);
}

pkg_formats
packing_format_from_string(const char *str)
{
	if (str == NULL)
		return TXZ;
	if (strcmp(str, "txz") == 0)
		return TXZ;
	if (strcmp(str, "tbz") == 0)
		return TBZ;
	if (strcmp(str, "tgz") == 0)
		return TGZ;
	if (strcmp(str, "tar") == 0)
		return TAR;
	pkg_emit_error("unknown format %s, using txz", str);
	return TXZ;
}

const char*
packing_format_to_string(pkg_formats format)
{
	const char *res = NULL;

	switch (format) {
	case TXZ:
		res = "txz";
		break;
	case TBZ:
		res = "tbz";
		break;
	case TGZ:
		res = "tgz";
		break;
	case TAR:
		res = "tar";
		break;
	}

	return (res);
}
