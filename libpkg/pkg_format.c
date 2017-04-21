/*-
 * Copyright (c) 2017 Vsevolod Stakhov <vsevolod@FreeBSD.org>
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
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>

#include "external/zstd/zstd.h"

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkg_endian.h"

static const char pkg_v2_magic[6] = {'p', 'k', 'g', '!', 'v', '2'};
static const uint16_t pkg_v2_version = 0x1;

enum pkg_section_type {
	PKG_FORMAT_SECTION_HEADER = 0x1,
	PKG_FORMAT_SECTION_MANIFEST = 0x2,
	PKG_FORMAT_SECTION_FILELIST = 0x3,
	PKG_FORMAT_SECTION_SIGNATURE = 0x4,
	PKG_FORMAT_SECTION_PAYLOAD = 0x5
};

enum pkg_section_flags {
	PKG_FORMAT_FLAGS_DEFAULT = 0x0,
	PKG_FORMAT_FLAGS_ZSTD = 0x1,
	PKG_FORMAT_FLAGS_ZSTD_PARALLEL = 0x2
};
/*
 * We have 16 bits for flags and 16 bits for threads number allowing from 2^1 to 2^16 threads
 */
#define ZSTD_PARALLEL_MASK (((uint32_t)(-1)) << 16)

struct pkg_section_hdr {
	enum pkg_section_type type;
	uint32_t flags;
	uint64_t size;
	uint64_t additional;
};

enum pkg_section_header_elt {
	PKG_FORMAT_HEADER_MANIFEST = 0x1,
	PKG_FORMAT_HEADER_FILES = 0x2,
	PKG_FORMAT_HEADER_SIGNATURE = 0x3,
	PKG_FORMAT_HEADER_PAYLOAD = 0x4
};

struct pkg_v2_header {
	off_t manifest_offset;
	off_t files_offset;
	off_t signature_offset;
	off_t payload_offset;
};

#define READ_32LE_AT(buf, pos, var) do { \
	memcpy(&(var), (buf) + (pos), sizeof(var)); \
	(var) = pkg_le32(var); \
} while(0)

#define READ_64LE_AT(buf, pos, var) do { \
	memcpy(&(var), buf + pos, sizeof(var)); \
	(var) = pkg_le64(var); \
} while(0)


static bool
pkg_is_v2(int fd)
{
	unsigned char buf[8]; /* Buffer for version and magic */
	ssize_t r;
	uint16_t ver;

	r = read(fd, buf, sizeof(buf));

	if (r != sizeof(buf)) {
		return false;
	}

	/* Check magic */
	if (memcmp(buf, pkg_v2_magic, sizeof(pkg_v2_magic)) != 0) {
		return false;
	}

	/* Load version */
	memcpy(&ver, buf + sizeof(pkg_v2_magic), sizeof(uint16_t));
	ver = pkg_le16(ver);

	if (ver == pkg_v2_version) {
		return true;
	}

	return false;
}

static int
pkg_seek_to_offset(FILE *fs, off_t offset, int whence)
{
	unsigned char *skip_buf;
	size_t skip_len, total_read = 0;
	ssize_t r;

	if (fseek(fs, offset, whence) == -1) {
		/* If we cannot seek then it can be just unseekable stream */
		if (errno == EBADF) {
			/* Fallback to reading */
			if (whence != SEEK_CUR || offset < 0) {
				/* Bad type */
				errno = EINVAL;

				return (EPKG_FATAL);
			}

			skip_len = MIN(BUFSIZ, (size_t)offset);
			skip_buf = xmalloc(skip_len);

			for (;;) {
				r = fread(skip_buf, 1, skip_len, fs);

				if (feof(fs) || r <= 0) {
					free(skip_buf);

					return (EPKG_FATAL);
				}

				total_read += r;

				if (total_read == offset) {
					break;
				}
			}

			free(skip_buf);
		}
		else {
			/* Seek error */
			return (EPKG_FATAL);
		}
	}

	return (EPKG_OK);
}

static int
pkg_section_maybe_uncompress(FILE *fs, struct pkg_section_hdr *hdr, void **uncompressed,
		size_t *sz)
{
	ZSTD_DStream *zstream;
	ZSTD_inBuffer zin;
	ZSTD_outBuffer zout;
	unsigned char *out, *in;
	size_t outlen, r;

	if (hdr->flags & PKG_FORMAT_FLAGS_ZSTD) {
		outlen = hdr->additional;

		if (outlen > UINT32_MAX) {
			/* Some garbadge instead of size */
			pkg_emit_error("invalid uncompressed size: %zu", outlen);

			return (EPKG_FATAL);
		}

		/* Read input */
		in = xmalloc(hdr->size);

		if (fread(in, 1, hdr->size, fs) != hdr->size) {
			return (EPKG_FATAL);
		}

		zstream = ZSTD_createDStream();
		ZSTD_initDStream(zstream);

		zin.pos = 0;
		zin.src = in;
		zin.size = hdr->size;

		if (outlen == 0 &&
				(outlen = ZSTD_getDecompressedSize(zin.src, zin.size)) == 0) {
			outlen = ZSTD_DStreamOutSize();
		}

		out = xmalloc(outlen);

		zout.dst = out;
		zout.pos = 0;
		zout.size = outlen;

		while (zin.pos < zin.size) {
			r = ZSTD_decompressStream(zstream, &zout, &zin);

			if (ZSTD_isError(r)) {
				pkg_emit_error("cannot decompress data: %s",
						ZSTD_getErrorName(r));
				ZSTD_freeDStream(zstream);
				free(out);
				free(in);

				return (EPKG_FATAL);
			}

			if (zout.pos == zout.size) {
				/* We need to extend output buffer */
				zout.size = zout.size * 1.5 + 1.0;
				out = xrealloc(zout.dst, zout.size);
				zout.dst = out;
			}
		}

		ZSTD_freeDStream(zstream);
		free(in);

		*uncompressed = out;
		*sz = zout.pos;
	}
	else {
		*sz = hdr->size;
		out = xmalloc(hdr->size);

		if (fread(out, 1, hdr->size, fs) != hdr->size) {
			free(out);

			return (EPKG_FATAL);
		}

		*uncompressed = out;
	}

	return (EPKG_OK);
}

static int
pkg_skip_to_section(FILE *fs, enum pkg_section_type type, struct pkg_section_hdr *hdr)
{
	unsigned char rdbuf[24];
	uint32_t wire_type, wire_flags;
	uint64_t wire_size, wire_additional;

	if (fread(rdbuf, 1, sizeof(rdbuf), fs) != sizeof(rdbuf)) {
		return (EPKG_FATAL);
	}

	/*
	 * Format of section:
	 * type - 32 bit le
	 * size - 64 bit le
	 * flags - 32 bit le
	 * additional 64 bit le
	 */
	READ_32LE_AT(rdbuf, 0, wire_type);
	READ_64LE_AT(rdbuf, 4, wire_size);

	if (wire_type != type) {
		if (wire_type >= PKG_FORMAT_SECTION_PAYLOAD) {
			/* Do not go after payload, that is meaningless */
			return (EPKG_FATAL);
		}

		/* Not something that we were waiting, skip it */
		if (wire_size > 0) {
			/* Skip to the next section */
			if (pkg_seek_to_offset(fs, wire_size, SEEK_CUR) == EPKG_FATAL) {
				return (EPKG_FATAL);
			}
			/* Tail call */
			pkg_skip_to_section(fs, type, hdr);
		}
		else {
			errno = EINVAL;

			return (EPKG_FATAL);
		}
	}

	/* Our section */
	READ_32LE_AT(rdbuf, 12, wire_flags);
	READ_64LE_AT(rdbuf, 16, wire_additional);
	hdr->type = type;
	hdr->size = wire_size;
	hdr->flags = wire_flags;
	hdr->additional = wire_additional;

	return (EPKG_OK);
}

static int
pkg_open_header_v2(FILE *fs, struct pkg_v2_header *hdr)
{
	struct pkg_section_hdr raw_hdr;
	unsigned char rdbuf[9], t;
	ssize_t r, total_read = 0;
	uint64_t wire_number;

	if (pkg_skip_to_section(fs, PKG_FORMAT_SECTION_HEADER, &raw_hdr) != EPKG_OK) {
		return (EPKG_FATAL);
	}

	if (raw_hdr.size < sizeof(uint64_t) * 4 + 4 || raw_hdr.size > sizeof(uint64_t) * 100) {
		errno = EINVAL;

		return (EPKG_FATAL);
	}

	while ((r = fread(rdbuf, 1, sizeof(rdbuf), fs)) > 0) {
		total_read += r;

		if (total_read == raw_hdr.size) {
			break;
		}

		t = rdbuf[0];

		switch(t) {
		case PKG_FORMAT_HEADER_MANIFEST:
			READ_64LE_AT(rdbuf, 1, wire_number);
			hdr->manifest_offset = wire_number;
			break;
		case PKG_FORMAT_HEADER_FILES:
			READ_64LE_AT(rdbuf, 1, wire_number);
			hdr->files_offset = wire_number;
			break;
		case PKG_FORMAT_HEADER_SIGNATURE:
			READ_64LE_AT(rdbuf, 1, wire_number);
			hdr->signature_offset = wire_number;
			break;
		case PKG_FORMAT_HEADER_PAYLOAD:
			READ_64LE_AT(rdbuf, 1, wire_number);
			hdr->payload_offset = wire_number;
			break;
		default:
			/* By default we ignore unknown elements */
			break;
		}
	}

	return (EPKG_OK);
}

static int
pkg_read_manifest_v2(FILE *fs, struct pkg **pkg_p, struct pkg_manifest_key *keys)
{
	struct pkg_section_hdr sec;
	struct pkg *pkg;
	void *payload;
	size_t paylen;
	int retcode = EPKG_OK;

	if (pkg_skip_to_section(fs, PKG_FORMAT_SECTION_MANIFEST, &sec) != EPKG_OK) {
		return (EPKG_FATAL);
	}

	if (pkg_section_maybe_uncompress(fs, &sec, &payload, &paylen) != EPKG_OK) {
		return (EPKG_FATAL);
	}

	retcode = pkg_new(pkg_p, PKG_FILE);
	if (retcode != EPKG_OK) {
		goto cleanup;
	}

	pkg = *pkg_p;
	retcode = pkg_parse_manifest(pkg, payload, paylen, keys);

cleanup:
	free(payload);

	return (retcode);
}

static int
pkg_read_files_v2(FILE *fs, struct pkg *pkg, struct pkg_manifest_key *keys)
{
	struct pkg_section_hdr sec;
	void *payload;
	size_t paylen;
	int retcode = EPKG_OK;

	if (pkg_skip_to_section(fs, PKG_FORMAT_SECTION_FILELIST, &sec) != EPKG_OK) {
		return (EPKG_END);
	}

	if (pkg_section_maybe_uncompress(fs, &sec, &payload, &paylen) != EPKG_OK) {
		return (EPKG_FATAL);
	}

	retcode = pkg_parse_manifest(pkg, payload, paylen, keys);
	free(payload);

	return (retcode);
}


static int
pkg_open_v2(struct pkg **pkg_p, const char *path, struct pkg_manifest_key *keys, int flags)
{
	struct pkg_v2_header hdr;
	FILE *fs;
	int fd, ret;

	if (strncmp(path, "-", 2) == 0) {
		fd = STDIN_FILENO;
	}
	else {
		fd = open(path, O_RDONLY);
	}

	memset(&hdr, 0, sizeof(hdr));

	if (fd == -1) {
		if ((flags & PKG_OPEN_TRY) == 0) {
			pkg_emit_error("open(%s): %s", path, strerror(errno));
		}

		return (EPKG_FATAL);
	}

	if (!pkg_is_v2(fd)) {
		return (EPKG_END);
	}

	fs = fdopen(fd, "r");

	if (fs == NULL) {
		if ((flags & PKG_OPEN_TRY) == 0) {
			pkg_emit_error("fdopen(%s): %s", path, strerror(errno));
		}

		return (EPKG_FATAL);
	}

	if ((ret = pkg_open_header_v2(fs, &hdr)) != EPKG_OK) {
		if ((flags & PKG_OPEN_TRY) == 0) {
			pkg_emit_error("bad v2 header in %s: %s", path, strerror(errno));
		}

		return (ret);
	}

	/* If we have signature, we need to check it */
	if (hdr.signature_offset > 0) {
		/* TODO: write signatures check */
	}

	/* Load manifest */
	if ((ret = pkg_read_manifest_v2(fs, pkg_p, keys)) != EPKG_OK) {
		if ((flags & PKG_OPEN_TRY) == 0) {
			pkg_emit_error("bad v2 manifest in %s: %s", path, strerror(errno));
		}

		return (ret);
	}

	if (flags & PKG_OPEN_MANIFEST_COMPACT) {
		return (EPKG_OK);
	}

	/* Read files as well */
	if ((ret = pkg_read_files_v2(fs, *pkg_p, keys)) != EPKG_OK) {
		if (ret != EPKG_END) {
			/* We have some fatal error */
		}
	}
}

int
pkg_open_format(struct pkg **pkg_p, struct archive **a, struct archive_entry **ae,
		const char *path, struct pkg_manifest_key *keys, int flags, int fd)
{
	struct pkg	*pkg = NULL;
	pkg_error_t	 retcode = EPKG_OK;
	int		 ret;
	const char	*fpath;
	bool		 manifest = false;
	bool		 read_from_stdin = 0;

	*a = archive_read_new();
	archive_read_support_filter_all(*a);
	archive_read_support_format_tar(*a);

	/* archive_read_open_filename() treats a path of NULL as
	 * meaning "read from stdin," but we want this behaviour if
	 * path is exactly "-". In the unlikely event of wanting to
	 * read an on-disk file called "-", just say "./-" or some
	 * other leading path. */

	if (fd == -1) {
		read_from_stdin = (strncmp(path, "-", 2) == 0);

		if (archive_read_open_filename(*a,
				read_from_stdin ? NULL : path, 4096) != ARCHIVE_OK) {
			if ((flags & PKG_OPEN_TRY) == 0)
				pkg_emit_error("archive_read_open_filename(%s): %s", path,
						archive_error_string(*a));

			retcode = EPKG_FATAL;
			goto cleanup;
		}
	} else {
		if (archive_read_open_fd(*a, fd, 4096) != ARCHIVE_OK) {
			if ((flags & PKG_OPEN_TRY) == 0)
				pkg_emit_error("archive_read_open_fd: %s",
						archive_error_string(*a));

			retcode = EPKG_FATAL;
			goto cleanup;
		}
	}

	retcode = pkg_new(pkg_p, PKG_FILE);
	if (retcode != EPKG_OK)
		goto cleanup;

	pkg = *pkg_p;

	while ((ret = archive_read_next_header(*a, ae)) == ARCHIVE_OK) {
		fpath = archive_entry_pathname(*ae);
		if (fpath[0] != '+')
			break;

		if (!manifest &&
				(flags & PKG_OPEN_MANIFEST_COMPACT) &&
				strcmp(fpath, "+COMPACT_MANIFEST") == 0) {
			char *buffer;
			manifest = true;

			size_t len = archive_entry_size(*ae);
			buffer = xmalloc(len);
			archive_read_data(*a, buffer, archive_entry_size(*ae));
			ret = pkg_parse_manifest(pkg, buffer, len, keys);
			free(buffer);
			if (ret != EPKG_OK) {
				retcode = EPKG_FATAL;
				goto cleanup;
			}
			/* Do not read anything more */
			break;
		}
		if (!manifest && strcmp(fpath, "+MANIFEST") == 0) {
			manifest = true;
			char *buffer;

			size_t len = archive_entry_size(*ae);
			buffer = xmalloc(len);
			archive_read_data(*a, buffer, archive_entry_size(*ae));
			ret = pkg_parse_manifest(pkg, buffer, len, keys);
			free(buffer);
			if (ret != EPKG_OK) {
				if ((flags & PKG_OPEN_TRY) == 0)
					pkg_emit_error("%s is not a valid package: "
							"Invalid manifest", path);

				retcode = EPKG_FATAL;
				goto cleanup;
			}

			if (flags & PKG_OPEN_MANIFEST_ONLY)
				break;
		}
	}

	if (ret != ARCHIVE_OK && ret != ARCHIVE_EOF) {
		if ((flags & PKG_OPEN_TRY) == 0)
			pkg_emit_error("archive_read_next_header(): %s",
					archive_error_string(*a));

		retcode = EPKG_FATAL;
	}

	if (ret == ARCHIVE_EOF)
		retcode = EPKG_END;

	if (!manifest) {
		retcode = EPKG_FATAL;
		if ((flags & PKG_OPEN_TRY) == 0)
			pkg_emit_error("%s is not a valid package: no manifest found", path);
	}

	cleanup:
	if (retcode != EPKG_OK && retcode != EPKG_END) {
		if (*a != NULL) {
			archive_read_close(*a);
			archive_read_free(*a);
		}
		free(pkg);
		*pkg_p = NULL;
		*a = NULL;
		*ae = NULL;
	}

	return (retcode);
}
