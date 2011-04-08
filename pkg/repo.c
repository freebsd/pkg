#include <sysexits.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>

#include <pkg.h>

#include <openssl/rsa.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <archive.h>
#include <archive_entry.h>

#include <fcntl.h>

#include "repo.h"

void
usage_repo(void)
{
	fprintf(stderr, "usage: pkg repo <repo-path> <rsa-key>\n\n");
	fprintf(stderr, "For more information see 'pkg help repo'.\n");
}

static void
progress(struct pkg *pkg, void *data)
{
	(void)data;

	if (pkg != NULL)
		printf("%s-%s\n", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));
	else
		pkg_error_warn("");
}

static int
password_cb(char *buf, int size, int rwflag, void *key)
{
	int len;
	char passwd[BUFSIZ];
	(void)rwflag;

	printf("Enter pass phrase for '%s': ", (char *) key);
	scanf("%s", passwd);
	len = strlen(passwd);

	if (len <= 0)  return 0;
	if (len > size) len = size;

	memset(buf, '\0', size);
	memcpy(buf, passwd, len);
	return len;
}

static RSA *
load_rsa_private_key(char *rsa_key_path)
{
	FILE *fp;
	RSA *rsa = NULL;

	fp = fopen(rsa_key_path, "r");
	if (fp == 0) {
		return NULL;
	}

	rsa = RSA_new();
	if (rsa == NULL) {
		fclose(fp);
		return NULL;
	}

	rsa = PEM_read_RSAPrivateKey(fp, 0, password_cb, rsa_key_path);
	if (rsa == NULL) {

		fclose(fp);
		return NULL;
	}

	fclose(fp);
	return rsa;
}

int
exec_repo(int argc, char **argv)
{
	int ret;

	RSA *rsa = NULL;
	char sha256[65];
	char db_path[MAXPATHLEN];
	char repo_data_path[MAXPATHLEN];
	int max_len = 0;
	unsigned char *sigret = NULL;
	int siglen = 0;
	struct archive_entry *entry;
	int fd;
	size_t len;
	char buf[BUFSIZ];
	struct archive *ar, *repo_archive;


	if (argc != 3 ) {
		usage_repo();
		return (EX_USAGE);
	}

	printf("Generating repo.sqlite in %s\n", argv[1]);
	ret = pkg_create_repo(argv[1], progress, NULL, sha256);

	if (ret != EPKG_OK)
		pkg_error_warn("can not create repository");
	else
		printf("Done!\n");

	SSL_load_error_strings();

	OpenSSL_add_all_algorithms();
	OpenSSL_add_all_ciphers();


	rsa = load_rsa_private_key( argv[2] );

	snprintf(db_path, MAXPATHLEN, "%s/repo.sqlite", argv[1]);

	max_len = RSA_size(rsa);
	sigret = malloc(max_len + 1);
	memset(sigret, 0, max_len);

	if (RSA_sign(NID_sha1, sha256, 65, sigret, &siglen, rsa) == 0) {
		return -1;
	}

	snprintf(repo_data_path, MAXPATHLEN, "%s/repo.txz", argv[1]);

	ar = archive_read_disk_new();
	repo_archive = archive_write_new();
	archive_write_set_compression_xz(repo_archive);
	archive_write_set_format_pax(repo_archive);
	archive_write_open_filename(repo_archive, repo_data_path);

	archive_read_disk_set_standard_lookup(ar);

	entry = archive_entry_new();
	archive_entry_clear(entry);
	archive_entry_set_filetype(entry, AE_IFREG);
	archive_entry_set_pathname(entry, "signature");
	archive_entry_set_size(entry, max_len);
	archive_write_header(repo_archive, entry);
	archive_write_data(repo_archive, sigret, max_len);
	archive_entry_clear(entry);

	archive_entry_copy_sourcepath(entry, db_path);
	archive_read_disk_entry_from_file(ar, entry, -1, 0);
	archive_entry_set_pathname(entry, "repo.sqlite");

	archive_write_header(repo_archive, entry);
	fd = open(db_path, O_RDONLY);
	if (fd != -1) {
		while ( (len = read(fd, buf, sizeof(buf))) > 0)
			archive_write_data(repo_archive, buf, len);
		close(fd);
	}

	archive_entry_free(entry);
	archive_read_finish(ar);

	archive_write_close(repo_archive);
	archive_write_finish(repo_archive);

	free(sigret);
	RSA_free( rsa );
	ERR_free_strings();
	return (ret);
}
