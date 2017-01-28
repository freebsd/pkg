#ifndef XMALLOC_H
#define XMALLOC_H

static inline void *xmalloc(size_t size)
{
	void *ptr = malloc(size);
	if (ptr == NULL)
		abort();
	return (ptr);
}

static inline void *xcalloc(size_t n, size_t size)
{
	void *ptr = calloc(n, size);
	if (ptr == NULL)
		abort();
	return (ptr);
}

static inline void *xrealloc(void *ptr, size_t size)
{
	ptr = realloc(ptr, size);
	if (ptr == NULL)
		abort();
	return (ptr);
}

static inline char *xstrdup(const char *str)
{
	char *s = strdup(str);
	if (s == NULL)
		abort();
	return (s);
}

static inline char *xstrndup(const char *str, size_t n)
{
	char *s = strndup(str, n);
	if (s == NULL)
		abort();
	return (s);
}

static inline int xasprintf(char **ret, const char *fmt, ...)
{
	va_list ap;
	int i;

	va_start(ap, fmt);
	i = vasprintf(ret, fmt, ap);
	va_end(ap);

	if (i < 0 || *ret == NULL)
		abort();

	return (i);
}
#endif
