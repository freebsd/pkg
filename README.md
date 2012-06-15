pkgng - a binary package manager for FreeBSD
============================================

Table of Contents:
------------------

* [libpkg](#libpkg)
* [pkgng package format](#pkgngfmt)
* [Local Database](#localdb)
* [Installation of packages](#pkginst)
* [Upgrades of packages](#pkgupg)
* [Deletion of packages](#pkgdel)
* [pkgng in Ports](#pkgngports)
* [A quick usage introduction to pkgng](#usageintro)
* [Installing pkgng](#installpkgng)
* [Getting help on the commands usage](#pkghelp)
* [Installing packages](#pkgadd)
* [Querying the local package database](#pkginfo)
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

pkgng is built on top of libpkg, a new library to interface with package registration backends.
It abstracts package management details such as registration, remote repositories, package creation,
updating, etc.

<a name="pkgngfmt"></a>
### pkgng package format

pkgng package format is a tar archive which can be raw, or use the following
compression: gz, bzip2 and xz, defaulting in xz format.

The tar itself is composed in two types of elements:

* the special files at the beginning of the archive, starting with a "+"
* the data.

#### The metadata

pkgng supports two files for metadata:

* +MANIFEST
* +MTREE\_DIRS (optional)

##### MANIFEST

The manifest is in [YAML](http://yaml.org) format, it contains all the
information about the package:

	name: foo
	version: 1.0
	origin: category/foo
	comment: this is foo package
	arch: i386
	osversion: 8.2-STABLE-802507
	www: http://www.foo.org
	maintainer: foo@bar.org
	prefix: /usr/local
	licenselogic: or
	licenses: [MIT, MPL]
	flatsize: 482120
	users: [USER1, USER2]
	groups: [GROUP1, GROUP2]
	options: { OPT1: off, OPT2: on }
	desc: |-
	  This is the descrpition
	  Of foo
	  
	  A component of bar
	categories: [bar, plop]
	deps:
	  libiconv: {origin: converters/libiconv, version: 1.13.1_2}
	  perl: {origin: lang/perl5.12, version: 5.12.4 }
	files:
	  /usr/local/bin/foo: 'sha256sum'
	  /usr/local/bin/i_am_a_link: '-'
	  /usr/local/share/foo-1.0/foo.txt: 'sha256sum'
	dirs:
	- /usr/local/share/foo-1.0
	scripts:
	  post-install: |-
	    #!/bin/sh
	    echo post-install
	  pre-install: |-
	    #!/bin/sh
	    echo pre-install

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

Script *MUST* be in sh format nothing else would work. shebang is not necessary.

When the manifest is read by pkg\_create files and dirs accept another format:

	files:
	  /usr/local/bin/foo, 'sha256sum'
	  /usr/local/bin/bar: {sum: 'sha256sum', uname: baruser, gname: foogroup, perm: 0644 }
	dirs:
	- /usr/local/share/foo-1.0
	- /path/to/directory: {uname: foouser, gname: foogroup, perm: 0755}

This allows to override the users, groups and mode of files and directories during package
creation, like for example this allows to create a package
containing root files without being packaged by the root user.

##### MTREE\_DIRS

This is optional, this is used by the package the same way it is done by the
legacy tools which means the MTREE is extracted in prefix before each
installation.

In the future we hope that mtree will be deprecated in favour of a hier package
or in single MTREE that won't be customisable in per package basis and because
pkgng supports packing of empty directories, per package MTREE makes no sense
any more.

<a name="localdb"></a>
### Local database

Once a package is installed, it is registered to a SQLite database.

The SQLite database allow fast queries and ACID transactions.
It also allow to query the reverse dependencies without a __+REQUIRED_BY__ hack.

In order to save space the MTREE is only stored once, which save 18K per
installed package.

pkgng supports a `register` command to register packages into the SQLite
database from the ports. The register command can execute the install script,
show pkg-message, ...

<a name="pkginst"></a>
### Installation of packages

`pkg add` can install a package archive from the local disk, or from a
remote FTP/HTTP server.

If only a package name is given, it will search the remote repository
and download and install the package if it exists. The dependencies will be
downloaded and installed first.

This is possible because we have the dependency information in the
remote repository database.

`pkg add` will check if the user attempts to install a package built for another
arch or release.

<a name="pkgupg"></a>
### Upgrades of packages

pkgng will also support upgrades of binary packages.

pkgng will compare the versions of installed packages and those available on
the repository. It will compute the proper update order and apply them.

<a name="pkgdel"></a>
### Deletion packages

Directory leftovers are automatically removed if they are not in the MTREE.

<a name="pkgngports"></a>
### pkgng in Ports

pkgng beta1 is now in the ports tree. To get it:

	$ make -C /usr/ports/ports-mgmt/pkg
	$ echo "WITH_PKGNG=yes" >> /etc/make.conf

<a name="usageintro"></a>
### A quick usage introduction to pkgng

In this section of the document we will try to give a quick and dirty introduction
on the practical usage of pkgng - installing packages, searching in remote package
repositories, updating remote package repositories and installing from them, etc.

<a name="installpkgng"></a>
### Installing pkgng

The first thing to start with is to get pkgng installed on your machine.

You can grap a development snapshot of pkgng from the [pkgng Github repository][1]

To get the latest version of pkgng from the Git repo, just clone it:

	# git clone https://github.com/pkgng/pkgng

Or you can take an already tagged release of pkgng from the above web page as well.

Just open your browser and download the release you want.

Once you have the pkgng sources, installing it is fairly easy:

	# cd pkgng
	# make && make install && make clean

Now you should have pkgng installed on your system.

Transferring your packages to pkgng is done by the ports/pkg2ng script (also
installed by the port

In order to register your installed packages to pkgng, execute the commands below:

	# cd pkgng/ports
	# sh pkg2ng

<a name="pkghelp"></a>
### Getting help on the commands usage

In order to get help on any of the pkgng commands you should use the `pkg help <command>`
command, which will take the man page of the specified command.

In order to get the available commands in pkgng, just execute `pkg help`

	# pkg help
	# pkg help <command>

<a name="pkgadd"></a>
### Installing packages

Packages can be installed from either a local directory on the file system or from
a remote location using the FTP/HTTP protocol in order to fetch the packages.

In order to install the package foo-1.2.3 from a local directory, you will use a
command similar to the following below:

	# pkg add /path/to/packages/foo-1.2.3.txz

You need to make sure that all dependencies of foo-1.2.3 are also in the directory,
which you use in order to install the package.

You can also install the package foo-1.2.3 from a remote location using the FTP/HTTP
protocol. In order to do that you could use a command similar to the following:

	# pkg add http://example.org/pkgng-repo/foo-1.2.3.txz

This will also install the package foo-1.2.3 and it's dependencies from the remote
server example.org using the HTTP protocol for fetching the packages.

For more information on installing packages on your FreeBSD system, please refer to *pkg-add(1)*

<a name="pkginfo"></a>
### Querying the local package database

In order to get information about installed packages you need to use the `pkg info` command.

`pkg info` will query the local package database and display you information about the
package you are intrested in.

To list all install/registered packages in the local database, you will use the following command:

	# pkg info -a

For more information on querying the local package database, please refer to *pkg-info(1)* man page.

<a name="pkgrepos"></a>
### Working with a remote package repository

pkgng is able to work with remote package repositories as well.

A remote package repository contains a collection of packages which can be
fetched, searched and installed into your systems.

In order to use a remote repository you need to define the _PACKAGESITE_ environment variable,
so that it points to a remote location, which contains packages that can be installed by pkgng,
or set _PACKAGESITE_ in the *pkg.conf(5)* configuration file.

In order to work with a single remote package repository, you would define _PACKAGESITE_ to
your remote server with packages, like for example (or use ${LOCALBASE}/etc/pkg.conf* to set it there):

	TCSH users:

	# setenv PACKAGESITE http://example.org/pkgng-repo/

	SH users:

	# export PACKAGESITE=http://example.org/pkgng-repo/

Then fetch the remote repository using the below command:

	# pkg update

This would fetch the remote package database to your local system. Now in order to install
packages from the remote repository, you would use the `pkg install` command:

	# pkg install zsh cfengine3

<a name="multirepos"></a>
### Working with multiple remote repositories

pkgng is also able to work with multiple remote repositories. In the previous section
we are using only a single remote repository, which is defined by the _PACKAGESITE_ option.

In order to be able to work with multiple remote repositories and instead of changing
each time _PACKAGESITE_, you can tell *pkg(1)* to work in multi-repos mode as well.

To do this, simply enable multi-repos in *pkg.conf(5)* like this:

	# echo "PKG_MULTIREPOS : YES" >> /usr/local/etc/pkg.conf

The next thing is to define your remote repositories in the *pkg.conf(5)* file.

Below is a part from example configuration file, which defines three remote repositories - *repo1*, *repo2* and
the *default* repository.

	repos:
	  default : http://example.org/pkgng/
	  repo1 : http://somewhere.org/pkgng/repo1/
	  repo2 : http://somewhere.org/pkgng/repo2/

It is important that you always define a *default* repository - this is the repository that is being
used when no remote repositories are specified via the `-r <repo>` flag.

Next, fetch the remote repositories:

	# pkg update

And now you can install from the remote repositories using the `pkg install` command like this:

	# pkg install -r repo1 zsh cfengine3

Example repo entries in *pkg.conf(5)* would look like this:

	  default : http://example.com/repos
	  second : http://somewhere.com/repos

If you want to mirror the repositories and make them public as well, please get in contact with us, so that we
can add your repository to the list as well :)

<a name="pkgupdate"></a>
### Updating remote repositories

The first thing to do when working with remote repositories is to update from them.

Updating remote repositories is done by the `pkg update` command.

If _PACKAGESITE_ environment variable is defined to point to a remote package
repository then only this repository will be updated. Otherwise all remote
repositories defined in the /etc/pkg/repositories file will be updated.

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
that is found to has the package is the first one that pkgng will use
during the installation. If that repository is not accessible for some reason,
then the next repository which contains the package is the one that is tried.

The process continues until the package is fetched and installed, or all
remote repositories fail to fetch the package.

Remote installations of packages using pkgng are done by the `pkg install` command.

Here's an example installation of few packages:

	# pkg install www/apache22
	# pkg install zsh
	# pkg install perl-5.12.4

Or you could also install the packages using only one command, like this:

	# pkg install www/apache22 zsh perl-5.12.4

For more information on the remote package installs, please refer to *pkg-install(1)*

<a name="pkgbackup"></a>
### Backing up your package database

It is a good idea that you backup your local package database on regular basis.

In order to backup the local package database, you should use the `pkg backup` command.

	# pkg backup -d /path/to/pkgng-backup.dump

The above command will create a compressed tar archive file of
your local package database in /path/to/pkgng-backup.dump.txz

For more information on backing up your local package database, please refer to *pkg-backup(1)*

<a name="pkgcreate"></a>
### Creating a package repository

You can also use pkgng, so that you create a package repository.

In order to create a package repository you need to use the `pkg create` command.

Here's an example that will create a repository of all your currently installed packages:

	# cd /path/with/enough/space
	# pkg create -a
	# pkg repo .

The above commands will create a repository of all packages on your system.

Now you can share your repo with other people by letting them know of your repository :)

<a name="resources"></a>
### Additional resources

* The Git repository of [pkgng is hosted on Github][1]

* The [pkgng Wiki page][2]

* [Doxygen documentation for libpkg][3]

* [Buildbot for pkgng][4]

* [LLVM scanbuild][6]

* [Jenkins CI instance for pkgng][7]

In order to get in contact with us, you can find us in the #pkgng@FreeNode IRC channel.

If you hit a bug when using pkgng, you can always submit an issue in the [pkgng issue tracker][5].

[1]: https://github.com/pkgng/pkgng
[2]: http://wiki.freebsd.org/pkgng
[3]: http://jenkins.unix-heaven.org/jenkins/job/pkgng-doxygen/
[4]: http://buildbot.etoilebsd.net/
[5]: https://github.com/pkgng/pkgng/issues
[6]: http://scanbuild.etoilebsd.net
[7]: http://jenkins.unix-heaven.org/jenkins/
