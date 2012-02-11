int gr_copy(int __ffd, int _tfd, const struct group *_gr, struct group *_old_gr);
void gr_fini(void);
int gr_init(const char *_dir, const char *_master);
int gr_lock(void);
int gr_mkdb(void);
int gr_tmp(int _mdf);
