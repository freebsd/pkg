#ifndef _SQLITE_CFG_H
#define _SQLITE_CFG_H
#define HAVE_FCHOWN 1
#define HAVE_GMTIME_R 1
#define HAVE_ISNAN 1
#define HAVE_LOCALTIME_R 1
#define HAVE_LSTAT 1
#define HAVE_READLINK 1
#define HAVE_STRERROR_R 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#ifndef __APPLE__
#define HAVE_FDATASYNC 1
#endif
#include "sqlite_generated.h"
#endif
