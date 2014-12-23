pkg - a binary package manager for FreeBSD
============================================

Table of Contents:
------------------

* [libpkg](#libpkg)
* [pkg package format](#pkgfmt)
* [Local Database](#localdb)
* [Installing packages](#pkginst)
* [Upgrading packages](#pkgupg)
* [Deleting packages](#pkgdel)
* [Installing pkg](#installpkg)
* [pkg bootstrap](#pkgbootstrap)
* [pkg in Ports](#pkgports)
* [Building pkg using sources from Git](#pkggit)
* [Converting an old-style pkg database](#pkg2ng)
* [A quick usage introduction to pkg](#usageintro)
* [Getting help on the commands usage](#pkghelp)
* [Querying the local package database](#pkginfo)
* [Installing packages](#pkginstalling)
* [Adding pkg tarballs directly](#pkgadd)
* [Working with a remote package repository](#pkgrepos)
* [Working with multiple remote package repositories](#multirepos)
* [Updating remote repositories](#pkgupdate)
* [Searching in remote package repositories](#pkgsearch)
* [Installing from remote repositories](#pkginstall)
* [Backing up your package database](#pkgbackup)
* [Creating a package repository](#pkgcreate)
* [Additional resources](#resources)

<a name="libpkg"></a>
### libpkg

pkg is built on top of libpkg, a new library to interface with package
registration backends.
It abstracts package management details such as registration, remote
repositories, package creation, updating, etc.

<a name="pkgfmt"></a>
### pkg package format

pkg package format is a tar archive which can be raw, or use the following
compression: gz, bzip2 and xz, defaulting in xz format.

The tar itself is composed in two types of elements:

* the special files at the beginning of the archive, starting with a "+"
* the data.

#### The metadata

pkg uses several files for metadata:

* +COMPACT\_MANIFEST
* +MANIFEST
* +MTREE\_DIRS (optional)

##### COMPACT\_MANIFEST

This is a subset of the information included in the main MANIFEST,
omitting the lists of files, checksums, directories and scripts.
It contains the information used to build the repository catalogue.

##### MANIFEST

The manifest is in [YAML](http://yaml.org) format, it contains all the
information about the package:

	name: foo
	version: 1.0
	origin: category/foo
	comment: this is foo package
	arch: i386
	www: http://www.foo.org
	maintainer: foo@bar.org
	prefix: /usr/local
	licenselogic: or
	licenses: [MIT, MPL]
	flatsize: 482120
	users: [USER1, USER2]
	groups: [GROUP1, GROUP2]
	options: { OPT1: off, OPT2: on }
	desc: <<EOD
	  This is the description
	  Of foo
	  
	  A component of bar
	EOD
	categories: [bar, plop]
	deps: {
	  libiconv: {origin: converters/libiconv, version: 1.13.1_2};
	  perl: {origin: lang/perl5.12, version: 5.12.4 };
	}
	files: {
	  /usr/local/bin/foo: 'sha256sum',
	  /usr/local/bin/i_am_a_link: '-';
	  /usr/local/share/foo-1.0/foo.txt: 'sha256sum;
	}
	directories: {
	  /usr/local/share/foo-1.0 : 'y';
	}
	scripts: {
	  post-install: <<EOD
	    #!/bin/sh
	    echo post-install
	EOD
	  pre-install: <<EOD
	    #!/bin/sh
	    echo pre-install
	EOD
	}

Valid scripts are:

* pre-install
* post-install
* install
* pre-deinstall
* post-deinstall
* deinstall
* pre-upgrade
* post-upgrade
* upgrade

Script *MUST* be in sh format.
Nothing else will work.
The shebang is not required.

When the manifest is read by pkg\_create files and dirs can use an
alternate format:

	files:
	  /usr/local/bin/foo, 'sha256sum'
	  /usr/local/bin/bar: {sum: 'sha256sum', uname: baruser, gname: foogroup, perm: 0644 }
	dirs:
	- /usr/local/share/foo-1.0
	- /path/to/directory: {uname: foouser, gname: foogroup, perm: 0755}


This allows overriding the users, groups and mode of files and
directories during package creation.
So, for example, this allows to creation of a package containing
root-owned files without being packaged by the root user.

##### MTREE\_DIRS

This is optional.  It is used by the package the same way as done by
the legacy tools. The MTREE is extracted in prefix before each
installation.

In the future we hope that mtree will be deprecated in favour of a
hier package or a single MTREE that won't be customisable in per
package basis. Since pkg supports packing of empty directories, per
package MTREE is superfluous.

<a name="localdb"></a>
### Local database

When a package is installed, it is registered in a SQLite database.

The SQLite database allow fast queries and ACID transactions.  It also
allows finding the reverse dependencies reliably without a needing the
__+REQUIRED_BY__ hack.

In order to save space the MTREE is only stored once, which save 18K per
installed package.

pkg supports a `register` command to register packages into the SQLite
database from the ports. The register command can execute the install script,
show pkg-message, ...

<a name="pkginst"></a>
### Installing packages

`pkg add` can install a package archive from the local disk, or from a
remote FTP/HTTP server.

If only a package name is given, it will search the repository catalogues
and download and install the package if it exists. Any dependencies will be
downloaded and installed first.

This is possible because we have the dependency information in the
catalogue of the remote repository.

`pkg add` will check if the user attempts to install a package built
for another arch or release.

<a name="pkgupg"></a>
### Upgrading packages

pkg also supports upgrades of binary packages.

pkg will compare the versions of installed packages and those available in
the repository. It will compute the proper update order and apply them.

<a name="pkgdel"></a>
### Deleting packages

`pkg delete` will remove a package, and (depending on the command line
arguments) any other packages that depend on what you're trying to
delete.

Directory leftovers are automatically removed if they are empty and
not in the MTREE.

<a name="installpkg"></a>
## Installing pkg

There are three ways to install pkg: two for general day-to-day use,
and the third if you want to help with pkg development.

<a name="pkgbootstrap"></a>
### Pkg bootstrap

All supported versions of FreeBSD now contain /usr/sbin/pkg a.k.a
*pkg(7)*.  This is a small placeholder that has just the minimum
functionality required to install the real pkg(8).

To use, simply run any pkg(8) command line.  pkg(7) will intercept the
command, and if you confirm that is your intention, download the
pkg(8) tarball, install pkg(8) from it, bootstrap the local package
database and then proceed to run the command you originally requested.

More recent versions of pkg(7) understand `pkg -N` as a test to see if
pkg(8) is installed without triggering the installation, and
conversely, `pkg bootstrap[-f]` to install pkg(8) (or force it to be
reinstalled) without performing any other actions.

<a name="pkgports"></a>
### pkg in Ports

pkg-1.0 release was committed to the the ports tree on 30th August
2012, and a series of further releases are planned.  To install the
latest release version:

	$ make -C /usr/ports/ports-mgmt/pkg install clean
	$ echo "WITH_PKG=yes" >> /etc/make.conf


<a name="pkggit"></a>
### Building pkg using sources from Git

In order to build pkg from source, you will need to have Gnu
autotools and some other tools installed.

	# pkg install autoconf automake libtool pkgconf

The next thing to do is to get the pkg sources installed on your machine.
You can grab a development snapshot of pkg from the [pkg GitHub repository][1]

To get the latest version of pkg from the Git repo, just clone it:

	% git clone https://github.com/freebsd/pkg

or

	% git clone git@github.com:freebsd/pkg.git

Or you can take an already tagged release of pkg from the above web
page as well.
Just open your browser and download the release you want.

Once you have the pkg sources, installing it is fairly easy:

	% cd pkg
	% ./autogen.sh
	% ./configure
	% make
	# make install

Now you should have the latest pkg installed on your system.  Note
that this build and install procedure does not update the local
package database at all, so you will get some odd effects due to the
packaging system being misled into thinking an older version of pkg is
installed.

Note: if you're running anything other than FreeBSD or DragonFly, you
will need to do some porting work.  The pkg(8) codebase should be
reasonably portable onto anything with a c99 compiler, POSIX compliant
system and capable of running Gnu autotools.  However, various places
in the pkg(8) code make assumptions about OS specific behaviour.  If
you do try anything like this, we'd be very interested to hear how you
get on.

<a name="pkg2ng"></a>
### Converting an old-style pkg database

If you're on a 9.x system or earlier and did not have a release version
of pkg(8) installed previously, you will need to run the pkg2ng
script.  This is only necessary when converting your system from the
old pkg_tools style packages.

In order to register your installed packages to pkg, execute the
commands below:

	# cd pkg/ports
	# sh pkg2ng

Otherwise, running any pkg(8) command that will attempt to write to
the local package database will automatically apply any schema
updates.  Be aware that these may not be backwards compatible --
although usually you should see no more than a warning message if you
try and run an older version of pkg(8) against a newer database
schema.

<a name="usageintro"></a>
## A quick usage introduction to pkg

In this section of the document we will try to give a quick and dirty
introduction on the practical usage of pkg - installing packages,
searching in remote package repositories, updating remote package
repositories and installing from them, etc.

<a name="pkghelp"></a>
### Getting help on the commands usage

In order to get help on any of the pkg commands you should use the `pkg help <command>`
command, which will take the man page of the specified command.

In order to get the available commands in pkg, just execute `pkg help`

	# pkg help
	# pkg help <command>

<a name="pkginfo"></a>
### Querying the local package database

In order to get information about installed packages use the `pkg
info` command.

`pkg info` will query the local package database and display
information about the package you are interested in.

To list all install/registered packages in the local database, use
this command:

	# pkg info -a

For more information on querying the local package database, please
refer to *pkg-info(1)* man page.

<a name="pkginstalling"></a>
### Installing packages

Packages are installed either from a repository, from the results of a
local compilation of software via the ports or from a pkg tarball
independently obtained from some other source.

A repository is a collection of packages which have been gathered
together, had a catalogue created and then published, typically by
exposing the repository via HTTP or some other networking protocol.
You can also publish a repository from a local or NFS mounted
filesystem (using file:// style URLs) or via SSH (using ssh:// URLs.)

<a name="pkgadd"></a>
#### Adding pkg tarballs directly

In order to install the package foo-1.2.3 from a local pkg tarball,
use a command similar to the following:

	# pkg add /path/to/packages/foo-1.2.3.txz

You will need to make sure that all dependencies of foo-1.2.3 are
either also available as tarballs in the same directory, or previously
installed by other means.

You can also install the package foo-1.2.3 tarball from a remote
location using the FTP/HTTP protocol. In order to do that you could
use a command similar to the following:

	# pkg add http://example.org/pkg-repo/foo-1.2.3.txz

Which works in exactly the same way, except that it fetches the
package tarballs using the protocol indicated by the URL.

For more information on installing packages on your FreeBSD system,
please refer to *pkg-add(1)*

<a name="pkgrepos"></a>
### Working with a remote package repository

While pkg(8) can deal with individual package tarballs, the real power
comes from the use of repositories, which publish a 'catalogue' of
meta-data about the packages they contain.

You can configure pkg(8) to use one or several repositories.
Supported versions of FreeBSD now contain a default configuration out
of the box: `/etc/pkg/FreeBSD.conf` which is setup to install packages
from the official package repositories.

To add additional repositories, create a per-repository configuration
file in `/usr/local/etc/pkg/repos` -- it doesn't matter what the
filename is other than it must match '*.conf' and you should make your
preferred repositories sort earlier in the list.

To disable the default FreeBSD.conf, create a file
`/usr/local/etc/pkg/repos/FreeBSD.conf` with the contents:

```
FreeBSD: { enabled: no }
```

To check quickly what repositories you have configured, run `pkg -vv`.

See *pkg.conf(5)* for details of the format of `pkg.conf` and the
per-repository `repo.conf` files.  See *pkg-repository(5)* for more
details about package repositories and how to work with them.

Note that the old style of setting _PACKAGESITE_ in pkg.conf is
no-longer supported.  Setting _PACKAGESITE_ in the environment has
meaning for the pkg(7) shim, but is ignored by pkg(8).

Then fetch the repository catalogues using the command:

	# pkg update

This would fetch the remote package database to your local system. Now
in order to install packages from the remote repository, you would use
the `pkg install` command:

	# pkg install zsh cfengine3

<a name="multirepos"></a>
### Working with multiple repositories

If you have more than one repository defined, then you probably want
to install some packages from a specific repository, but allow others
to be obtained from whatever repository has them available.

You can install a package from a specific repository:

    	# pkg install -r myrepo zsh

where `myrepo` is one of the tags shown in the `pkg -vv` output.  You
can then tell pkg to always use the named repository for upgrades to
that package by:

        # pkg annotate -A zsh repository myrepo

<a name="pkgupdate"></a>
### Updating remote repositories

The first thing to do when working with remote repositories is to
update from them.

Updating remote repositories is done by the `pkg update` command.  By
default his will first update the local copies of the repository
catalogues, unless you specifically configure pkg(8) otherwise.

So, to update your remote repositories, you would execute this command:

	# pkg update

For more information on the remote repositories, please refer to *pkg-update(1)*.

<a name="pkgsearch"></a>
### Searching in remote package repositories

You can search in the remote package repositories using the `pkg search` command.

In order to search in multiple package repositories the environment variable
_PACKAGESITE_ should NOT be defined, in which case `pkg search` will query
the remote package databases found in the /etc/pkg/repositories file.

An example search for a package could be done like this:

	# pkg search -x apache

For more information on the repositories search, please refer to *pkg-search(1)*

<a name="pkginstall"></a>
### Installing from remote repositories

In order to install a package from a remote repository you need to set the
_PACKAGESITE_ environment variable to point to the remote server.

If _PACKAGESITE_ is not defined then the installation process will use
multiple repositories as defined in the /etc/pkg/repositories file.

During installation from multiple repositories the first repository
that is found to has the package is the first one that pkg will use
during the installation. If that repository is not accessible for some reason,
then the next repository which contains the package is the one that is tried.

The process continues until the package is fetched and installed, or all
remote repositories fail to fetch the package.

Remote installations of packages using pkg are done by the `pkg install` command.

Here's an example installation of few packages:

	# pkg install www/apache22
	# pkg install zsh
	# pkg install perl5-5.18.2_4

Or you could also install the packages using only one command, like this:

	# pkg install www/apache22 zsh perl5-5.18.2_4

For more information on the remote package installs, please refer to *pkg-install(1)*

<a name="pkgbackup"></a>
### Backing up your package database

It is a good idea that you backup your local package database on regular basis.

In order to backup the local package database, you should use the `pkg backup` command.

	# pkg backup -d /path/to/pkg-backup.dump

The above command will create a dump of your local package database in
/path/to/pkg-backup.dump

For more information on backing up your local package database, please
refer to *pkg-backup(1)*

<a name="pkgcreate"></a>
### Creating a package repository

You can also use pkg, so that you create a package repository.

In order to create a package repository you need to use the `pkg
create` command.

Here's an example that will create a repository of all your currently
installed packages:

	# cd /path/with/enough/space
	# pkg create -a
	# pkg repo .

The above commands will create a repository of all packages on your system.

Now you can share your repo with other people by letting them know of
your repository :)

<a name="resources"></a>
### Additional resources

* The Git repository of [pkg is hosted on GitHub][1]

* The [pkg Wiki page][2]

* [Jenkins instance for pkg][3]

In order to get in contact with us, you can find us in the
#pkg@FreeNode IRC channel.

If you hit a bug when using pkg, you can always submit an issue in the
[pkg issue tracker][4].

[1]: https://github.com/freebsd/pkg
[2]: http://wiki.freebsd.org/pkgng
[3]: http://jenkins.mouf.net/job/pkg/
[4]: https://github.com/freebsd/pkg/issues
