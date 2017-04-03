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

	cat << EOF > rubygemrubyaugeas.ucl
name: rubygem-ruby-augeas
origin: textproc/rubygem-augeas
version: "1.0"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
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

	cat << EOF > puppet.ucl
name: puppet
origin: sysutils/puppet
version: "1.0"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
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

	cat << EOF > ruby.ucl
name: ruby
origin: lang/ruby20
version: "2.0"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
shlibs_provided [
	"libruby20.so.20",
]
files: {
	${TMPDIR}/ruby.file: "",
}
EOF

	cat << EOF > rubygem-hiera.ucl
name: rubygem-hiera
origin: lang/rubygem-hiera
version: "1.0"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
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

	cat << EOF > ruby-gems20.ucl
name: ruby20-gems
origin: lang/ruby-gems
version: "1.0"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
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

	cat << EOF > repo1.conf
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
	    -e match:".*load error: access repo file.*" \
	    -s exit:0 \
	    pkg -o REPOS_DIR="${TMPDIR}" -o PKG_CACHEDIR="${TMPDIR}" install -y puppet

#### NEW
	rm repo1.conf
	rm -f *.ucl
	rm *.txz

	cat << EOF > rubygemrubyaugeas.new.ucl
name: rubygem-ruby-augeas
origin: textproc/rubygem-augeas
version: "1.0"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
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

	cat << EOF > puppet.new.ucl
name: puppet
origin: sysutils/puppet
version: "1.0"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
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

cat << EOF > ruby.new.ucl
name: ruby
origin: lang/ruby21
version: "2.1"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
shlibs_provided [
	"libruby21.so.21",
]
files: {
	${TMPDIR}/ruby.file: "",
}
EOF

cat << EOF > rubygem-hiera.new.ucl
name: rubygem-hiera
origin: lang/rubygem-hiera
version: "1.0"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
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

cat << EOF > ruby-gems21.new.ucl
name: ruby21-gems
origin: lang/ruby-gems
version: "1.0"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
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

	cat << EOF > repo.conf
local: {
	url: file://${TMPDIR}/,
	enabled: true
}
EOF

	OUTPUT="Updating local repository catalogue...
${JAILED}Fetching meta.txz:  done
${JAILED}Fetching packagesite.txz:  done
Processing entries:  done
local repository update completed. 5 packages processed.
All repositories are up to date.
Checking for upgrades (4 candidates):  done
Processing candidates (4 candidates):  done
Checking integrity... done (1 conflicting)
  - ruby21-gems-1.0 conflicts with ruby20-gems-1.0 on ${TMPDIR}/rubygems.file
Checking integrity... done (0 conflicting)
The following 6 package(s) will be affected (of 0 checked):

Installed packages to be REMOVED:
	ruby20-gems-1.0

New packages to be INSTALLED:
	ruby21-gems: 1.0

Installed packages to be UPGRADED:
	ruby: 2.0 -> 2.1

Installed packages to be REINSTALLED:
	rubygem-ruby-augeas-1.0 (direct dependency changed: ruby)
	rubygem-hiera-1.0 (direct dependency changed: ruby)
	puppet-1.0 (direct dependency changed: ruby)

Number of packages to be removed: 1
Number of packages to be installed: 1
Number of packages to be upgraded: 1
Number of packages to be reinstalled: 3
"

	atf_check \
	    -o inline:"${OUTPUT}" \
	    -e match:".*load error: access repo file.*" \
	    -s exit:1 \
	    pkg -o REPOS_DIR="${TMPDIR}" -o PKG_CACHEDIR="${TMPDIR}" upgrade -yn
}
