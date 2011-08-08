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
