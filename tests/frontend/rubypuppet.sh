#! /usr/bin/env atf-sh

# If you remove rubygem-ruby-augeas from puppet as deps pkg will not remove puppet
# but also don't reinstall it which is also wrong.

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	pkg_puppet

pkg_puppet_body() {
	touch puppet.file
	touch ruby.file
	touch rubygemrubyaugeas.file
	touch rubygemhiera.file
	touch rubygems.file

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkgf \
		rubygemrubyaugeas \
		rubygem-ruby-augeas \
		textproc/rubygem-augeas \
		1.0
	cat << EOF >> rubygemrubyaugeas.ucl
deps: {
	ruby: {
		origin: "lang/ruby20",
		version: "2.0"
	},
	ruby20-gems: {
		origin: "lang/ruby-gems",
		version: "1.0"
	}
}
files: {
	${TMPDIR}/rubygemrubyaugeas.file: "",
}
EOF

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkgf \
		puppet \
		puppet \
		sysutils/puppet \
		1.0
	cat << EOF >> puppet.ucl
deps: {
	ruby: {
		origin: "lang/ruby20",
		version: "2.0"
	},
	rubygem-hiera: {
		origin: "lang/rubygem-hiera",
		version: "1.0"
	},
	rubygem-ruby-augeas: {
		origin: "textproc/rubygem-augeas",
		version: "1.0"
	}
}
files: {
	${TMPDIR}/puppet.file: "",
}
EOF

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkgf \
		ruby \
		ruby \
		lang/ruby20 \
		2.0
	cat << EOF >> ruby.ucl
shlibs_provided [
	"libruby20.so.20",
]
files: {
	${TMPDIR}/ruby.file: "",
}
EOF

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkgf \
		rubygem-hiera \
		rubygem-hiera \
		lang/rubygem-hiera \
		1.0
	cat << EOF >> rubygem-hiera.ucl
deps: {
	ruby: {
		origin: "lang/ruby20",
		version: "2.0"
	},
	ruby20-gems: {
		origin: "lang/ruby-gems",
		version: "1.0"
	},
}
files: {
	${TMPDIR}/rubygemhiera.file: "",
}
EOF

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkgf \
		ruby-gems20  \
		ruby20-gems \
		lang/ruby-gems \
		1.0
	cat << EOF >> ruby-gems20.ucl
deps: {
	ruby {
		origin: lang/ruby20,
		version: "2.0"
	}
}
files: {
	${TMPDIR}/rubygems.file: "",
}
EOF

	mkdir reposconf
	cat << EOF > reposconf/repo1.conf
local1: {
	url: file://${TMPDIR},
	enabled: true
}
EOF

	for p in ruby ruby-gems20 rubygem-hiera rubygemrubyaugeas puppet; do
		atf_check \
		    -o ignore \
		    -e empty \
		    -s exit:0 \
		    pkg create -M ./${p}.ucl
	done

	atf_check \
	    -o inline:"Creating repository in .:  done\nPacking files for repository:  done\n" \
	    -e empty \
	    -s exit:0 \
	    pkg repo .

	atf_check \
	    -o ignore \
	    -s exit:0 \
	    pkg -o REPOS_DIR="${TMPDIR}"/reposconf -o PKG_CACHEDIR="${TMPDIR}" install -y puppet

#### NEW
	rm reposconf/repo1.conf
	rm -f *.ucl
	rm *.txz
	rm *.pkg

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkgf \
		rubygemrubyaugeas.new \
		rubygem-ruby-augeas \
		textproc/rubygem-augeas \
		1.0
	cat << EOF >> rubygemrubyaugeas.new.ucl
deps: {
	ruby: {
		origin: "lang/ruby21",
		version: "2.1"
	},
	ruby21-gems: {
		origin: "lang/ruby-gems",
		version: "1.0"
	}
}
files: {
	${TMPDIR}/rubygemrubyaugeas.file: "",
}
EOF

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkgf \
		puppet.new \
		puppet \
		lang/ruby21 \
		1.0
	cat << EOF >> puppet.new.ucl
deps: {
	ruby: {
		origin: "lang/ruby21",
		version: "2.1"
	},
	rubygem-hiera: {
		origin: "lang/rubygem-hiera",
		version: "1.0"
	},
	rubygem-ruby-augeas: {
		origin: "textproc/rubygem-augeas",
		version: "1.0"
	}
}
files: {
	${TMPDIR}/puppet.file: "",
}
EOF

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg ruby.new ruby 2.1
	cat << EOF >> ruby.new.ucl
shlibs_provided [
	"libruby21.so.21",
]
files: {
	${TMPDIR}/ruby.file: "",
}
EOF

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg rubygem-hiera.new rubygem-hiera 1.0
	cat << EOF >> rubygem-hiera.new.ucl
deps: {
	ruby: {
		origin: "lang/ruby21",
		version: "2.1"
	},
	ruby21-gems: {
		origin: "lang/ruby-gems",
		version: "1.0"
	},
}
files: {
	${TMPDIR}/rubygemhiera.file: "",
	}
EOF

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg ruby-gems21.new ruby21-gems 1.0
	cat << EOF >> ruby-gems21.new.ucl
deps: {
	ruby {
		origin: lang/ruby21,
		version: "2.1"
	}
}
files: {
	${TMPDIR}/rubygems.file: "",
}
EOF

	for p in ruby ruby-gems21 rubygem-hiera rubygemrubyaugeas puppet; do
		atf_check \
		    -o ignore \
		    -e empty \
		    -s exit:0 \
		    pkg create -M ./${p}.new.ucl
	done

	atf_check \
	    -o inline:"Creating repository in .:  done\nPacking files for repository:  done\n" \
	    -e empty \
	    -s exit:0 \
	    pkg repo .

	mkdir reposconf
	cat << EOF > reposconf/repo.conf
local: {
	url: file://${TMPDIR}/,
	enabled: true
}
EOF

	OUTPUT="Updating local repository catalogue...
${JAILED}Fetching meta.conf:  done
${JAILED}Fetching data.pkg:  done
Processing entries:  done
local repository update completed. 5 packages processed.
All repositories are up to date.
Checking for upgrades (4 candidates):  done
Processing candidates (4 candidates):  done
Checking integrity... done (1 conflicting)
  - ruby21-gems-1.0 conflicts with ruby20-gems-1.0 on ${TMPDIR}/rubygems.file
Checking integrity... done (0 conflicting)
The following 6 package(s) will be affected (of 0 checked):

New packages to be INSTALLED:
	ruby21-gems: 1.0

Installed packages to be UPGRADED:
	ruby: 2.0 -> 2.1

Installed packages to be REINSTALLED:
	puppet-1.0 (direct dependency changed: ruby)
	rubygem-hiera-1.0 (direct dependency changed: ruby)
	rubygem-ruby-augeas-1.0 (direct dependency changed: ruby)

Installed packages to be REMOVED:
	ruby20-gems: 1.0

Number of packages to be removed: 1
Number of packages to be installed: 1
Number of packages to be upgraded: 1
Number of packages to be reinstalled: 3
"

	atf_check \
	    -o inline:"${OUTPUT}" \
	    -s exit:1 \
	    pkg -o REPOS_DIR="${TMPDIR}/reposconf" -o PKG_CACHEDIR="${TMPDIR}" upgrade -yn
}
