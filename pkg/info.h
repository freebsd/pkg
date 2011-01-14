#ifndef _INFO_H
#define _INFO_H

#define INFO_PRINT_DEP (1<<0)
#define INFO_PRINT_RDEP (1<<1)
#define INFO_EXISTS (1<<2)
#define INFO_LIST_FILES (1<<3)
#define INFO_SIZE (1<<4)
#define INFO_QUIET (1<<5)
#define INFO_ORIGIN (1<<6)

int exec_info(int, char **);
void usage_info(void);

#endif
