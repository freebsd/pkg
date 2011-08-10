#ifndef _PKG_EVENT
#define _PKG_EVENT

void pkg_emit_error(const char *fmt, ...);
void pkg_emit_errno(const char *func, const char *arg);
void pkg_emit_already_installed(struct pkg *p);
void pkg_emit_fetching(const char *url, off_t total, off_t done, time_t elapsed);
void pkg_emit_install_begin(struct pkg *p);
void pkg_emit_install_finished(struct pkg *p);
void pkg_emit_deinstall_begin(struct pkg *p);
void pkg_emit_deinstall_finished(struct pkg *p);
void pkg_emit_upgrade_begin(struct pkg *p);
void pkg_emit_upgrade_finished(struct pkg *p);
void pkg_emit_missing_dep(struct pkg *p, struct pkg_dep *d);
void pkg_emit_required(struct pkg *p, int force);

#endif
