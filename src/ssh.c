/*-
 * Copyright (c) 2011-2013 Baptiste Daroussin <bapt@FreeBSD.org>
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

#ifdef HAVE_CONFIG_H
#include "pkg_config.h"
#endif

#ifdef HAVE_CAPSICUM
#include <sys/capability.h>
#endif

#include <sysexits.h>
#include <stdio.h>
#include <fcntl.h>
#include <err.h>
#include <errno.h>

#include <pkg.h>

#include "pkgcli.h"

void
usage_ssh(void)
{
	fprintf(stderr, "Usage: pkg ssh\n\n");
	fprintf(stderr, "For more information see 'pkg help ssh'.\n");
}

int
exec_ssh(int argc, char **argv __unused)
{
	int fd = -1;
	const char *restricted = NULL;

#ifdef HAVE_CAPSICUM
	cap_rights_t rights;
#endif

	if (argc > 1) {
		usage_ssh();
		return (EX_USAGE);
	}

	restricted = pkg_object_string(pkg_config_get("SSH_RESTRICT_DIR"));
	if (restricted == NULL)
		restricted = "/";

	if ((fd = open(restricted, O_DIRECTORY|O_RDONLY)) < 0) {
		warn("Impossible to open the restricted directory");
		return (EX_SOFTWARE);
	}

#ifdef HAVE_CAPSICUM
	cap_rights_init(&rights, CAP_READ, CAP_FSTATAT, CAP_FCNTL);
	if (cap_rights_limit(fd, &rights) < 0 && errno != ENOSYS ) {
		warn("cap_rights_limit() failed");
		return (EX_SOFTWARE);
	}

	if (cap_enter() < 0 && errno != ENOSYS) {
		warn("cap_enter() failed");
		return (EX_SOFTWARE);
	}

#endif
	if (pkg_sshserve(fd) != EPKG_OK)
		return (EX_SOFTWARE);

	return (EX_OK);
}
