/*-
 * Copyright (c) 2021 Kyle Evans <kevans@FreeBSD.org>
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

#ifdef HAVE_CONFIG_H
#include "pkg_config.h"
#endif

#include <sys/uio.h>

#include <bsd_compat.h>
#include <assert.h>
#include <err.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#ifdef HAVE_READPASSPHRASE_H
#include <readpassphrase.h>
#elif defined(HAVE_BSD_READPASSPHRASE_H)
#include <bsd/readpassphrase.h>
#else
#include "readpassphrase_compat.h"
#endif

#include <unistd.h>

#include <pkg.h>
#include "pkgcli.h"

enum {
	ARG_CREATE = CHAR_MAX + 1,
	ARG_PUBLIC,
	ARG_SIGN,
};

typedef enum {
	MODE_UNSPECIFIED = 0,
	MODE_CREATE,
	MODE_PUBLIC,
	MODE_SIGN,
} key_mode_t;

void
usage_key(void)
{
	fprintf(stderr, "Usage: pkg key [--create | --public | --sign] [-t <type>] "
	    "<key-path>\n");
	fprintf(stderr, "For more information see 'pkg help key'.\n");
}

static int
key_create(struct pkg_key *key, int argc __unused, char *argv[] __unused)
{
	/* No arguments to setup for now. */
	return (pkg_key_create(key, NULL, 0));
}

static int
key_pubout(struct pkg_key *key)
{
	char *keybuf = NULL;
	size_t keylen;
	int ret;

	ret = pkg_key_pubkey(key, &keybuf, &keylen);
	if (ret != EPKG_OK)
		return (ret);

	fwrite(keybuf, keylen, 1, stdout);
	free(keybuf);
	return (0);
}

static int
key_sign_data(struct pkg_key *key, const char *name)
{
	char buf[BUFSIZ];
	xstring *datastr;
	char *data;
	unsigned char *sig;
	size_t datasz, readsz, siglen;
	FILE *datafile;
	int rc;

	datafile = NULL;
	datastr = NULL;
	rc = EPKG_FATAL;
	if (STREQ(name, "-")) {
		datafile = stdin;	/* XXX Make it configurable? */
		name = "stdin";
	} else {
		datafile = fopen(name, "rb");
		if (datafile == NULL)
			err(EXIT_FAILURE, "fopen");
	}

	datastr = xstring_new();
	while (!feof(datafile)) {
		readsz = fread(&buf[0], 1, sizeof(buf), datafile);
		if (readsz == 0 && ferror(datafile)) {
			fprintf(stderr, "%s: I/O error\n", name);
			goto out;
		}

		fwrite(buf, readsz, 1, datastr->fp);
	}

	data = xstring_get_binary(datastr, &datasz);
	datastr = NULL;

	sig = NULL;
	rc = pkg_key_sign_data(key, (unsigned char *)data, datasz, &sig, &siglen);
	free(data);

#if 0
	fprintf(stderr, "SIGNED: %s\n", data);
#endif
/*
+SIGNED: 64628d55add8b281b9868aea00c4829a3ad260cfc4262e9d1244a1ab67584935
+SIGNED: a2eb46d60cd26657b273ec55a0909e642ef522f35074a9c62c3c4b42608e55e1
*/

	if (rc == EPKG_OK) {
		size_t writesz;

		if ((writesz = fwrite(sig, 1, siglen, stdout)) < siglen) {
			fprintf(stderr, "Failed to write signature out [%zu/%zu]\n",
			    writesz, siglen);
			rc = EPKG_FATAL;
		}
	}
	free(sig);

out:
	xstring_free(datastr);
	if (datafile != stdin)
		fclose(datafile);
	return rc;
}

static int
key_info(struct pkg_key *key, const char *file, const char *type)
{
	struct iovec *iov;
	int niov, rc;

	iov = NULL;
	rc = pkg_key_info(key, &iov, &niov);
	if (rc != EPKG_OK)
		return (rc);

	assert((niov % 2) == 0);

	printf("Key file '%s' (type %s)\n", file, type);
	for (int i = 0; i < niov; i += 2) {
		const char *kv_name = iov[i].iov_base;
		const char *kv_val = iov[i + 1].iov_base;
		printf("  - %s: %s\n", kv_name, kv_val);

		free(iov[i + 1].iov_base);
	}

	free(iov);
	return (EPKG_OK);
}

int
password_cb(char *buf, int size, int rwflag, void *key)
{
	int len = 0;
	char pass[BUFSIZ];
	sigset_t sig, oldsig;

	(void)rwflag;
	(void)key;

	/* Block sigalarm temporary */
	sigemptyset(&sig);
	sigaddset(&sig, SIGALRM);
	sigprocmask(SIG_BLOCK, &sig, &oldsig);

	if (readpassphrase("\nEnter passphrase: ", pass, BUFSIZ, RPP_ECHO_OFF) == NULL)
		return 0;

	len = strlen(pass);

	if (len <= 0)  return 0;
	if (len > size) len = size;

	memset(buf, '\0', size);
	memcpy(buf, pass, len);
	memset(pass, 0, BUFSIZ);

	sigprocmask(SIG_SETMASK, &oldsig, NULL);

	return (len);
}

int
exec_key(int argc, char **argv)
{
	int	 ret;
	int	 ch;
	struct pkg_key *key = NULL;
	const char *keypath, *keytype = NULL;
	key_mode_t keymode;

	struct option longopts[] = {
		{ "create",	no_argument,		NULL,	ARG_CREATE },
		{ "public", no_argument,		NULL,	ARG_PUBLIC },
		{ "sign", no_argument,		NULL,		ARG_SIGN },
		{ NULL,		0,			NULL,	0 },
	};

	keymode = MODE_UNSPECIFIED;

	/* XXX maybe eventually we can just derive the key type. */
	while ((ch = getopt_long(argc, argv, "t:", longopts, NULL)) != -1) {
		switch (ch) {
		case ARG_CREATE:
			if (keymode != MODE_UNSPECIFIED) {
				usage_key();
				return (EXIT_FAILURE);
			}
			keymode = MODE_CREATE;
			break;
		case ARG_PUBLIC:
			if (keymode != MODE_UNSPECIFIED) {
				usage_key();
				return (EXIT_FAILURE);
			}
			keymode = MODE_PUBLIC;
			break;
		case ARG_SIGN:
			if (keymode != MODE_UNSPECIFIED) {
				usage_key();
				return (EXIT_FAILURE);
			}
			keymode = MODE_SIGN;
			break;
		case 't':
			keytype = optarg;
			break;
		default:
			usage_key();
			return (EXIT_FAILURE);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1) {
		usage_key();
		return (EXIT_FAILURE);
	}

	if (keytype == NULL)
		keytype = "rsa";

	keypath = argv[0];
	if (*keypath == '\0') {
		fprintf(stderr, "keypath must not be empty.\n");
		usage_key();
		return (EXIT_FAILURE);
	}

	ret = pkg_key_new(&key, keytype, keypath, password_cb);
	if (ret != EPKG_OK) {
		fprintf(stderr, "Failed to create key context.\n");
		return (EXIT_FAILURE);
	}

	switch (keymode) {
	case MODE_CREATE:
		ret = key_create(key, argc, argv);
		if (ret != EPKG_OK) {
			switch (ret) {
			case EPKG_OPNOTSUPP:
				fprintf(stderr, "Type '%s' does not support generation.\n",
				    keytype);
				break;
			default:
				fprintf(stderr, "Failed to generate the key.\n");
				break;
			}

			goto out;
		}

		fprintf(stderr, "Created '%s' private key at %s\n", keytype, keypath);
		/* FALLTHROUGH */
	case MODE_PUBLIC:
		ret = key_pubout(key);
		if (ret != EPKG_OK) {
			switch (ret) {
			case EPKG_OPNOTSUPP:
				fprintf(stderr, "Type '%s' does not support pubout.\n",
				    keytype);
				break;
			default:
				fprintf(stderr, "Failed to get keyinfo.\n");
				break;
			}

			goto out;
		}

		break;
	case MODE_SIGN:
		ret = key_sign_data(key, "-");
		if (ret != EPKG_OK) {
			switch (ret) {
			case EPKG_OPNOTSUPP:
				fprintf(stderr, "Type '%s' does not support signing.\n",
				    keytype);
				break;
			default:
				fprintf(stderr, "Failed to sign.\n");
				break;
			}

			goto out;
		}
		break;
	case MODE_UNSPECIFIED:
		ret = key_info(key, keypath, keytype);
		if (ret != EPKG_OK) {
			switch (ret) {
			case EPKG_OPNOTSUPP:
				printf("Type '%s' does not support keyinfo.\n",
				    keytype);
				break;
			default:
				printf("Failed to get keyinfo.\n");
				break;
			}

			goto out;
		}

		break;
	}

out:
	pkg_key_free(key);
	return (ret == EPKG_OK ? EXIT_SUCCESS : EXIT_FAILURE);
}
