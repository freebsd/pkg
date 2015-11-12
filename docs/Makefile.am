dist_man_MANS=	pkg-add.8 \
		pkg-alias.8 \
		pkg-annotate.8 \
		pkg-audit.8 \
		pkg-autoremove.8 \
		pkg-backup.8 \
		pkg-check.8 \
		pkg-clean.8 \
		pkg-config.8 \
		pkg-convert.8 \
		pkg-create.8 \
		pkg-delete.8 \
		pkg-fetch.8 \
		pkg-info.8 \
		pkg-install.8 \
		pkg-lock.8 \
		pkg-query.8 \
		pkg-register.8 \
		pkg-repo.8 \
		pkg-repository.5 \
		pkg-rquery.8 \
		pkg-search.8 \
		pkg-set.8 \
		pkg-shell.8 \
		pkg-shlib.8 \
		pkg-ssh.8 \
		pkg-stats.8 \
		pkg-update.8 \
		pkg-updating.8 \
		pkg-upgrade.8 \
		pkg-version.8 \
		pkg-which.8 \
		pkg.8 \
		pkg.conf.5 \
		pkg_printf.3 \
		pkg_repos.3

mandir=	$(prefix)/man

EXTRA_DIST=	fix-xrefs
.PHONY: run-fix-xrefs

run-fix-xrefs:
	@$(srcdir)/fix-xrefs ${dist_man_MANS}

install-data-hook:
	$(LN_S) -f pkg-delete.8 \
	$(DESTDIR)$(man8dir)/pkg-remove.8
	$(LN_S) -f pkg-lock.8 \
	$(DESTDIR)$(man8dir)/pkg-unlock.8
	$(LN_S) -f pkg.8 \
	$(DESTDIR)$(man8dir)/pkg-static.8

