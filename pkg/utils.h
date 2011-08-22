#ifndef _UTILS_H
#define _UTILS_H

#define INFO_PRINT_DEP (1<<0)
#define INFO_PRINT_RDEP (1<<1)
#define INFO_EXISTS (1<<2)
#define INFO_LIST_FILES (1<<3)
#define INFO_SIZE (1<<4)
#define INFO_QUIET (1<<5)
#define INFO_ORIGIN (1<<6)
#define INFO_ORIGIN_SEARCH (1<<7)
#define INFO_PREFIX (1<<8)
#define INFO_FULL (1<<9)

int query_yesno(const char *msg);
int print_info(struct pkg * const pkg, unsigned int opt);

#endif
