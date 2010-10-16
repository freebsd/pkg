#ifndef _INFO_H
#define _INFO_H

#define INFO_PRINT_DEP (1<<0)
#define INFO_PRINT_RDEP (1<<1)
#define INFO_EXISTS (1<<2)

int cmd_info(int argc, char **argv);
#endif
