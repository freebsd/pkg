#ifndef __XSTRING_H_
#define __XSTRING_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct xstring {
	char* buf;
	size_t size;
	FILE* fp;
};

typedef struct xstring xstring;

static inline xstring *
xstring_new(void)
{
	xstring *str;

	str = calloc(1, sizeof(*str));
	if (str == NULL)
		abort();
	str->fp = open_memstream(&str->buf, &str->size);
	if (str->fp == NULL)
		abort();

	return (str);
}

static inline void
xstring_reset(xstring *str)
{
	if (str->buf)
		memset(str->buf, 0, str->size);
	rewind(str->fp);

}

static inline void
xstring_free(xstring *str)
{
	if (str == NULL)
		return;
	fclose(str->fp);
	free(str->buf);
	free(str);
}

#define xstring_renew(s)      \
do {                          \
   if (s) {                   \
     xstring_reset(s);        \
   } else {                   \
     s = xstring_new();       \
   }                          \
} while(0)

#define xstring_printf(s, fmt, ...)        fprintf((s)->fp, fmt, ##__VA_ARGS__)
#define xstring_puts(s, str)               fputs(str, (s)->fp)
#define xstring_flush(s)                   fflush((s)->fp)
#define xstring_putc(s, c)                 fputc(c, (s)->fp)
#define xstring_write(s, buf, size, nmemb) fwrite(buf, size, nmemb, (s)->fp)

static inline char *
xstring_get(xstring *str)
{
	fclose(str->fp);
	char *ret = str->buf;
	free(str);
	return (ret);
}

static inline char *
xstring_get_binary(xstring *str, size_t *size)
{
	fclose(str->fp);
	char *ret = str->buf;
	*size = str->size;
	free(str);
	return (ret);
}

#endif
