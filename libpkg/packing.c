/*-
 * Copyright (c) 2011-2021 Baptiste Daroussin <bapt@FreeBSD.org>
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
#include <bsd_compat.h>
#include <fcntl.h>
#include <fts.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/packing.h"

int
packing_init(struct packing **pack, const char *path, pkg_formats format, int clevel,
	time_t timestamp, bool overwrite, bool compat_symlink)
{
	char archive_path[MAXPATHLEN];
	char archive_symlink[MAXPATHLEN];
	char *archive_name;
	const char *ext;
	const char *source_date_epoch;
	char *endptr;
	time_t ts;

	assert(pack != NULL);

	*pack = xcalloc(1, sizeof(struct packing));
	(*pack)->timestamp = timestamp;

	if ((*pack)->timestamp == (time_t)-1 &&
		(source_date_epoch = getenv("SOURCE_DATE_EPOCH")) != NULL) {
			ts = (time_t)strtoimax(source_date_epoch, &endptr, 10);
			if (*endptr != '\0') {
				pkg_emit_error("Ignoring bad environment variable "
				    "SOURCE_DATE_EPOCH: %s", source_date_epoch);
			} else {
				(*pack)->timestamp = ts;
			}
	}

	(*pack)->aread = archive_read_disk_new();
	archive_read_disk_set_standard_lookup((*pack)->aread);
	archive_read_disk_set_symlink_physical((*pack)->aread);

	(*pack)->awrite = archive_write_new();
	archive_write_set_format_pax_restricted((*pack)->awrite);
	ext = packing_set_format((*pack)->awrite, format, clevel);
	if (ext == NULL) {
		archive_read_close((*pack)->aread);
		archive_read_free((*pack)->aread);
		archive_write_close((*pack)->awrite);
		archive_write_free((*pack)->awrite);
		free(*pack);
		*pack = NULL;
		return (EPKG_FATAL); /* error set by _set_format() */
	}
	snprintf(archive_path, sizeof(archive_path), "%s.pkg", path);
	archive_name = strrchr(archive_path, '/');
	if (archive_name == NULL)
		archive_name = archive_path;
	else
		archive_name++;
	snprintf(archive_symlink, sizeof(archive_path), "%s.%s", path,
	    ext);

	if (!overwrite && access(archive_path, F_OK) == 0) {
		archive_read_close((*pack)->aread);
		archive_read_free((*pack)->aread);
		archive_write_close((*pack)->awrite);
		archive_write_free((*pack)->awrite);
		free(*pack);
		*pack = NULL;
		errno = EEXIST;
		return (EPKG_EXIST);
	}
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

	if (compat_symlink || ctx.archive_symlink) {
		unlink(archive_symlink);
		if (symlink(archive_name, archive_symlink) != 0) {
			pkg_emit_errno("symlink", archive_symlink);
		}
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
	struct stat st;
	struct archive_entry *entry, *sparse_entry;
	bool unset_timestamp;
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

	if (pack->timestamp != (time_t) -1) {
		archive_entry_set_atime(entry, pack->timestamp, 0);
		archive_entry_set_ctime(entry, pack->timestamp, 0);
		archive_entry_set_mtime(entry, pack->timestamp, 0);
		archive_entry_set_birthtime(entry, pack->timestamp, 0);
	}

	archive_entry_linkify(pack->resolver, &entry, &sparse_entry);

	if (sparse_entry != NULL && entry == NULL)
		entry = sparse_entry;

	archive_write_header(pack->awrite, entry);

	if (archive_entry_size(entry) <= 0)
		goto cleanup;

	if ((fd = open(filepath, O_RDONLY)) < 0) {
		pkg_emit_errno("open", filepath);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	while ((len = read(fd, buf, sizeof(buf))) > 0) {
		if (archive_write_data(pack->awrite, buf, len) == -1) {
			pkg_emit_errno("archive_write_data", "archive write error");
			retcode = EPKG_FATAL;
			break;
		}
	}

	if (len == -1) {
		pkg_emit_errno("read", "file read error");
		retcode = EPKG_FATAL;
	}
	close(fd);

cleanup:
	archive_entry_free(entry);
	return (retcode);
}

void
packing_finish(struct packing *pack)
{
	if (pack == NULL)
		return;

	archive_read_close(pack->aread);
	archive_read_free(pack->aread);
	archive_entry_linkresolver_free(pack->resolver);

	archive_write_close(pack->awrite);
	archive_write_free(pack->awrite);

	free(pack);
}

const char *
packing_set_format(struct archive *a, pkg_formats format, int clevel)
{
	const char *notsupp_fmt = "%s is not supported, trying %s";
	int error;

	pkg_formats elected_format;

	/*
	 * For several of these formats, ARCHIVE_WARN wil be returned when an
	 * external program will be used to satisfy the request.
	 */
	switch (format) {
	case TZS:
#ifdef HAVE_ARCHIVE_WRITE_ADD_FILTER_ZSTD
		error = archive_write_add_filter_zstd(a);
		if (error == ARCHIVE_OK || error == ARCHIVE_WARN) {
			elected_format = TZS;
			if (clevel == -1)
				clevel = 19;
			goto out;
		}
#endif
		pkg_emit_error(notsupp_fmt, "zstd", "xz");
		/* FALLTHRU */
	case TXZ:
		if (archive_write_add_filter_xz(a) == ARCHIVE_OK) {
			elected_format = TXZ;
			goto out;
		}
		pkg_emit_error(notsupp_fmt, "xz", "bzip2");
		/* FALLTHRU */
	case TBZ:
		error = archive_write_add_filter_bzip2(a);
		if (error == ARCHIVE_OK || error == ARCHIVE_WARN) {
			elected_format = TBZ;
			goto out;
		}
		pkg_emit_error(notsupp_fmt, "bzip2", "gzip");
		/* FALLTHRU */
	case TGZ:
		error = archive_write_add_filter_gzip(a);
		if (error == ARCHIVE_OK || error == ARCHIVE_WARN) {
			elected_format = TGZ;
			goto out;
		}
		pkg_emit_error(notsupp_fmt, "gzip", "plain tar");
		/* FALLTHRU */
	case TAR:
		archive_write_add_filter_none(a);
		elected_format = TAR;
		break;
	default:
		return (NULL);
	}

out:
	if (clevel == -1)
		clevel = 0;
	/*
	 * N.B., we only want to whine about this if the user actually selected
	 * tar and specified a compress level.  If we had to fallback to tar,
	 * that's not the user's fault.
	 */
	if (format == TAR && clevel != 0)
		pkg_emit_error("Plain tar and a compression level does not make sense");

	if (elected_format != TAR && clevel != 0) {
		char buf[16];

		/*
		 * A bit of a kludge but avoids dragging in headers for all of
		 * these libraries.
		 */
		if (clevel == INT_MIN) {
			switch (elected_format) {
			case TZS:
				clevel = -5;
				break;
			case TXZ:
			case TBZ:
			case TGZ:
				clevel = 1;
				break;
			default:
				__unreachable();
			}
		} else if (clevel == INT_MAX) {
			switch (elected_format) {
			case TZS:
				clevel = 19;
				break;
			case TXZ:
			case TBZ:
			case TGZ:
				clevel = 9;
				break;
			default:
				__unreachable();
			}
		}

		snprintf(buf, sizeof(buf), "%d", clevel);
		if (archive_write_set_filter_option(a, NULL, "compression-level", buf) != ARCHIVE_OK)
			pkg_emit_error("bad compression-level %d", clevel);
	}

	return (packing_format_to_string(elected_format));
}

pkg_formats
packing_format_from_string(const char *str)
{
	if (str == NULL)
		return DEFAULT_COMPRESSION;
	if (strcmp(str, "tzst") == 0)
		return TZS;
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

bool
packing_is_valid_format(const char *str)
{
	if (str == NULL)
		return (false);
	if ((strcmp(str, "pkg") == 0) ||
	    (strcmp(str, "tzst") == 0) ||
	    (strcmp(str, "txz") == 0) ||
	    (strcmp(str, "tbz") == 0) ||
	    (strcmp(str, "tgz") == 0) ||
	    (strcmp(str, "tar") == 0))
	    return (true);
	return (false);
}

const char*
packing_format_to_string(pkg_formats format)
{
	const char *res = NULL;

	switch (format) {
	case TZS:
		res = "tzst";
		break;
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
