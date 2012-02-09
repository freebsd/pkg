#include <sys/param.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "pkg.h"
#include "pkg_private.h"
#include "pkg_event.h"

static int rc_stop(const char *);
static int rc_start(const char *);

int
pkg_stop_rc_scripts(struct pkg *pkg)
{
	struct pkg_file *file = NULL;
	char rc_d_path[PATH_MAX + 1];
	const char *rcfile;
	const char *rc;
	size_t len = 0;
	int ret = 0;
	const char *prefix;

	pkg_get(pkg, PKG_PREFIX, &prefix, -1);

	snprintf(rc_d_path, PATH_MAX, "%s/etc/rc.d/", prefix);
	len = strlen(rc_d_path);

	while (pkg_files(pkg, &file) == EPKG_OK) {
		if (strncmp(rc_d_path, pkg_file_get(file, PKG_FILE_PATH), len) == 0) {
			rcfile = pkg_file_get(file, PKG_FILE_PATH);
			rcfile += len;
			rc = strrchr(rcfile, '/');
			rc++;
			ret += rc_stop(rcfile);
		}
	}

	return (ret);
}

int
pkg_start_rc_scripts(struct pkg *pkg)
{
	struct pkg_file *file = NULL;
	char rc_d_path[PATH_MAX + 1];
	const char *rcfile;
	const char *rc;
	size_t len = 0;
	int ret = 0;
	const char *prefix;

	pkg_get(pkg, PKG_PREFIX, &prefix);
	snprintf(rc_d_path, PATH_MAX, "%s/etc/rc.d/", prefix);
	len = strlen(rc_d_path);

	while (pkg_files(pkg, &file) == EPKG_OK) {
		if (strncmp(rc_d_path, pkg_file_get(file, PKG_FILE_PATH), len) == 0) {
			rcfile = pkg_file_get(file, PKG_FILE_PATH);
			rcfile += len;
			rc = strrchr(rcfile, '/');
			rc++;
			ret += rc_start(rcfile);
		}
	}
	return (ret);
}

static int
rc_stop(const char *rc_file)
{
	int pstat;
	int fd;
	pid_t pid;

	if (rc_file == NULL)
		return (0);

	switch ((pid = fork())) {
		case -1:
			return (-1);
		case 0:
			/* child */
			/*
			 * We don't need to see the out put
			 */
			fd = open("/dev/null", O_WRONLY);
			dup2(fd, STDERR_FILENO);
			dup2(fd, STDOUT_FILENO);
			execl("/usr/sbin/service", "service", rc_file, "onestatus", (char *)NULL);
			_exit(1);
			/* NOT REACHED */
		default:
			/* parent */
			break;
	}

	while (waitpid(pid, &pstat, 0) == -1) {
		if (errno != EINTR)
			return (-1);
	}

	if (WEXITSTATUS(pstat) != 0)
		return (0);

	switch ((pid = fork())) {
		case -1:
			return (-1);
		case 0:
			/* child */
			execl("/usr/sbin/service", "service", rc_file, "forcestop", (char *)NULL);
			_exit(1);
			/* NOT REACHED */
		default:
			/* parent */
			break;
	}

	while (waitpid(pid, &pstat, 0) == -1) {
		if (errno != EINTR)
			return (-1);
	}

	return (WEXITSTATUS(pstat));
}

static int
rc_start(const char *rc_file)
{
	int pstat;
	pid_t pid;

	if (rc_file == NULL)
		return (0);

	switch ((pid = fork())) {
		case -1:
			return (-1);
		case 0:
			/* child */
			execl("/usr/sbin/service", "service", rc_file, "quietstart", (char *)NULL);
			_exit(1);
			/* NOT REACHED */
		default:
			/* parent */
			break;
	}

	while (waitpid(pid, &pstat, 0) == -1) {
		if (errno != EINTR)
			return (-1);
	}

	return (WEXITSTATUS(pstat));
}


