#include <fcntl.h>

#include <archive.h>
#include <archive_entry.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <pkg.h>
#include <pkg_error.h>
#include <pkg_private.h>

static const char *packing_set_format(struct archive *a, pkg_formats format);

struct packing {
	struct archive *aread;
	struct archive *awrite;
	struct archive_entry *entry;
};

int
packing_init(struct packing **pack, const char *path, pkg_formats format)
{
	char archive_path[MAXPATHLEN];
	const char *ext;

	if ((*pack = calloc(1, sizeof(struct packing))) == NULL)
		return(pkg_error_set(EPKG_FATAL, "%s", strerror(errno)));

	(*pack)->aread = archive_read_disk_new();
	archive_read_disk_set_standard_lookup((*pack)->aread);

	(*pack)->entry = archive_entry_new();

	if (!is_dir(path)) {
		(*pack)->awrite = archive_write_new();
		archive_write_set_format_pax_restricted((*pack)->awrite);
		if ((ext = packing_set_format((*pack)->awrite, format)) == NULL) {
			archive_read_finish((*pack)->aread);
			archive_write_finish((*pack)->awrite);
			archive_entry_free((*pack)->entry);
			return (pkg_error_set(EPKG_FORMAT, "Unsupported format"));
		}
		snprintf(archive_path, sizeof(archive_path), "%s.%s", path, ext);

		archive_write_open_filename((*pack)->awrite, archive_path);
	} else { /* pass mode directly write to the disk */
		(*pack)->awrite = archive_write_disk_new();
		archive_write_disk_set_options((*pack)->awrite, EXTRACT_ARCHIVE_FLAGS);
	}

	return (EPKG_OK);
}

int
packing_append_buffer(struct packing *pack, const char *buffer, const char *path, int size)
{
	archive_entry_clear(pack->entry);
	archive_entry_set_filetype(pack->entry, AE_IFREG);
	archive_entry_set_perm(pack->entry, 0644);
	archive_entry_set_gname(pack->entry, "wheel");
	archive_entry_set_uname(pack->entry, "root");
	archive_entry_set_pathname(pack->entry, path);
	archive_entry_set_size(pack->entry, size);
	archive_write_header(pack->awrite, pack->entry);
	archive_write_data(pack->awrite, buffer, size);
	archive_entry_clear(pack->entry);

	return (EPKG_OK);
}

int
packing_append_file(struct packing *pack, const char *filepath, const char *newpath)
{
	int fd;
	int len;
	char buf[BUFSIZ];

	archive_entry_clear(pack->entry);
	archive_entry_copy_sourcepath(pack->entry, filepath);

	if (archive_read_disk_entry_from_file(pack->aread, pack->entry, -1, 0) != ARCHIVE_OK)
		pkg_error_warn("archive_read_disk_entry_from_file(%s)", filepath);

	if (newpath != NULL)
		archive_entry_set_pathname(pack->entry, newpath);

	archive_write_header(pack->awrite, pack->entry);
	fd = open(filepath, O_RDONLY);

	if (fd != -1) {
		while ( (len = read(fd, buf, sizeof(buf))) > 0 )
			archive_write_data(pack->awrite, buf, len);

		close(fd);
	}
	archive_entry_clear(pack->entry);

	return (EPKG_OK);
}

int
packing_finish(struct packing *pack)
{
	archive_entry_free(pack->entry);
	archive_read_finish(pack->aread);

	archive_write_close(pack->awrite);
	archive_write_finish(pack->awrite);

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
				pkg_error_warn("xz compression is not supported, trying bzip2");
			}
		case TBZ:
			if (archive_write_set_compression_bzip2(a) == ARCHIVE_OK) {
				return ("tbz");
			} else {
				pkg_error_warn("bzip2 compression is not supported, trying gzip");
			}
		case TGZ:
			if (archive_write_set_compression_gzip(a) == ARCHIVE_OK) {
				return ("tgz");
			} else {
				pkg_error_warn("gzip compression is not supported, trying plain tar");
			}
		case TAR:
			archive_write_set_compression_none(a);
			return ("tar");
	}
	return (NULL);
}
