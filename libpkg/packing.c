/*
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
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

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <archive.h>
#include <archive_entry.h>
#include <assert.h>
#include <fcntl.h>
#include <fts.h>
#include <string.h>

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

	if ((*pack = calloc(1, sizeof(struct packing))) == NULL) {
		pkg_emit_errno("malloc", "packing");
		return (EPKG_FATAL);
	}

	(*pack)->aread = archive_read_disk_new();
	archive_read_disk_set_standard_lookup((*pack)->aread);
	archive_read_disk_set_symlink_physical((*pack)->aread);

	if (!is_dir(path)) {
		(*pack)->awrite = archive_write_new();
		archive_write_set_format_pax_restricted((*pack)->awrite);
		if ((ext = packing_set_format((*pack)->awrite, format)) == NULL) {
			archive_read_finish((*pack)->aread);
			archive_write_finish((*pack)->awrite);
			*pack = NULL;
			return EPKG_FATAL; /* error set by _set_format() */
		}
		snprintf(archive_path, sizeof(archive_path), "%s.%s", path, ext);

		if (archive_write_open_filename(
		    (*pack)->awrite, archive_path) != ARCHIVE_OK ) {
			pkg_emit_errno("archive_write_open_filename",
			    archive_path);
			archive_read_finish((*pack)->aread);
			archive_write_finish((*pack)->awrite);
			*pack = NULL;
			return EPKG_FATAL;
		}
	} else { /* pass mode directly write to the disk */
		(*pack)->awrite = archive_write_disk_new();
		archive_write_disk_set_options((*pack)->awrite, EXTRACT_ARCHIVE_FLAGS);
	}

	(*pack)->resolver = archive_entry_linkresolver_new();
	archive_entry_linkresolver_set_strategy((*pack)->resolver, archive_format((*pack)->awrite));
	return (EPKG_OK);
}

int
packing_append_buffer(struct packing *pack, const char *buffer, const char *path, int size)
{
	struct archive_entry *entry;

	entry = archive_entry_new();
	archive_entry_clear(entry);
	archive_entry_set_filetype(entry, AE_IFREG);
	archive_entry_set_perm(entry, 0644);
	archive_entry_set_gname(entry, "wheel");
	archive_entry_set_uname(entry, "root");
	archive_entry_set_pathname(entry, path);
	archive_entry_set_size(entry, size);
	archive_write_header(pack->awrite, entry);
	archive_write_data(pack->awrite, buffer, size);

	archive_entry_free(entry);

	return (EPKG_OK);
}

int
packing_append_file(struct packing *pack, const char *filepath, const char *newpath)
{
	return (packing_append_file_attr(pack, filepath, newpath, NULL, NULL, 0));
}

int
packing_append_file_attr(struct packing *pack, const char *filepath, const char *newpath,
		const char *uname, const char *gname, mode_t perm)
{
	int fd;
	int len;
	char buf[BUFSIZ];
	int retcode = EPKG_OK;
	int ret;
	struct stat st;
	struct archive_entry *entry, *sparse_entry;

	entry = archive_entry_new();
	archive_entry_copy_sourcepath(entry, filepath);

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

	if (uname != NULL)
		archive_entry_set_uname(entry, uname);

	if (gname != NULL)
		archive_entry_set_gname(entry, gname);

	if (perm != 0)
		archive_entry_set_perm(entry, perm);

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

		while ((len = read(fd, buf, sizeof(buf))) > 0 )
			archive_write_data(pack->awrite, buf, len);

		close(fd);
	}

	cleanup:
	archive_entry_free(entry);
	return (retcode);
}

int
packing_append_tree(struct packing *pack, const char *treepath, const char *newroot)
{
	FTS *fts = NULL;
	FTSENT *fts_e = NULL;
	size_t treelen;
	struct sbuf *sb;
	char *paths[2] = { __DECONST(char *, treepath), NULL };
 
	treelen = strlen(treepath);
	fts = fts_open(paths, FTS_PHYSICAL | FTS_XDEV, NULL);
	if (fts == NULL)
		goto cleanup;

	sb = sbuf_new_auto();
	while ((fts_e = fts_read(fts)) != NULL) {
		switch(fts_e->fts_info) {
		case FTS_D:
		case FTS_DEFAULT:
		case FTS_F:
		case FTS_SL:
		case FTS_SLNONE:
			 /* Skip entries that are shorter than the tree itself */
			 if (fts_e->fts_pathlen <= treelen)
				  break;
			 sbuf_clear(sb);
			 /* Strip the prefix to obtain the target path */
			 if (newroot) /* Prepend a root if one is specified */
				  sbuf_cat(sb, newroot);
			 sbuf_cat(sb, fts_e->fts_path + treelen + 1 /* skip trailing slash */);
			 sbuf_finish(sb);
			 packing_append_file(pack, fts_e->fts_name, sbuf_get(sb));
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
	sbuf_free(sb);
cleanup:
	fts_close(fts);
	return EPKG_OK;
}

int
packing_finish(struct packing *pack)
{
	assert(pack != NULL);

	archive_read_finish(pack->aread);

	archive_write_close(pack->awrite);
	archive_write_finish(pack->awrite);

	free(pack);

	return (EPKG_OK);
}

static const char *
packing_set_format(struct archive *a, pkg_formats format)
{
	switch (format) {
		case TXZ:
			if (archive_write_set_compression_xz(a) == ARCHIVE_OK) {
				return ("txz");
			} else {
				pkg_emit_error("%s", "xz is not supported, trying bzip2");
			}
		case TBZ:
			if (archive_write_set_compression_bzip2(a) == ARCHIVE_OK) {
				return ("tbz");
			} else {
				pkg_emit_error("%s", "bzip2 is not supported, trying gzip");
			}
		case TGZ:
			if (archive_write_set_compression_gzip(a) == ARCHIVE_OK) {
				return ("tgz");
			} else {
				pkg_emit_error("%s", "gzip is not supported, trying plain tar");
			}
		case TAR:
			archive_write_set_compression_none(a);
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
