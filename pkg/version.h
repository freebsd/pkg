#ifndef _VERSION_H
#define _VERSION_H

#define VERSION_INDEX (1<<0)
#define VERSION_ORIGIN (1<<1)
#define VERSION_QUIET (1<<2)
#define VERSION_VERBOSE (1<<3)
#define VERSION_STATUS (1<<4)
#define VERSION_NOSTATUS (1<<5)
#define VERSION_EREGEX (1<<6)
#define VERSION_STRING (1<<7)
#define VERSION_WITHORIGIN (1<<8)
#define VERSION_TESTVERSION (1<<9)
#define VERSION_TESTPATTERN (1<<10)

int exec_version(int, char **);
void usage_version(void);

#endif
