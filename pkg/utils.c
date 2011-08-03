#include <sys/param.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <string.h>
#include <pkg.h>

#include "utils.h"

int
query_yesno(const char *msg)
{
        int c, r = 0;

        printf(msg);

        c = getchar();
        if (c == 'y' || c == 'Y')
                r = 1;
        else if (c == '\n' || c == EOF)
                return 0;

        while((c = getchar()) != '\n' && c != EOF)
                continue;

        return r;
}

int
mkdirs(const char *_path)
{
	char path[MAXPATHLEN];
	char *p;

	strlcpy(path, _path, sizeof(path));
	p = path;
	if (*p == '/')
		p++;

	for (;;) {
		if ((p = strchr(p, '/')) != NULL)
			*p = '\0';

		if (mkdir(path, S_IRWXU | S_IRWXG | S_IRWXO) < 0)
			if (errno != EEXIST && errno != EISDIR) {
				warn("mkdir(%s)", path);
				return (EPKG_FATAL);
			}

		/* that was the last element of the path */
		if (p == NULL)
			break;

		*p = '/';
		p++;
	}

	return (EPKG_OK);
}
