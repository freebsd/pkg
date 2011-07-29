pkgng - a binary package manager for FreeBSD
============================================

Table of Contents:

	1. libpkg
	2. pkgng package format
	3. Local Database
	4. Installation of packages
	5. Upgrades of packages
	6. Deletion of packages
	7. pkgng in Ports
	8. A quick usage introduction to pkgng
		8.1. Installing pkgng
		8.2. Getting help on the commands usage
		8.3. Installing packages
		8.4. Querying the local package database
		8.5. Using remote package repositories
		8.6. Updating remote repositories
		8.7. Searching in remote package repositories
		8.8. Installing from remote repositories
		8.9. Backing up your package database
	9. Additional resources

1. libpkg
---------

pkgng is built on top of libpkg, a new library to interface with package registration backends.
It abstracts package management details such as registration, remote repositories, package creation,
updating, etc.

2. pkgng package format
-----------------------

pkgng uses a new +MANIFEST file to describe a package. It is very similar to
the old +CONTENTS file, but cleaned (no more @comment which in fact can be an
hidden key-value!). The new manifest has new metadatas such as the
architecture and the release it is built for, the maintainer of the package,
the ports options it was build with.

@exec and @unexec are deprecated. They are still executed, but pkgng print a
deprecation warning message.

pkgng supports new scripts: +PREINSTALL +POSTINSTALL, +PREDEINSTALL, +POSTDEINSTALL,
+PREUPGRADE, +POSTUPGRADE as well as the original scripts : +INSTALL +DEINSTALL +UPGRADE

The prefered compression format of pkgng for package archive is .txz. It is
faster to decompress than bzip2, thus allow faster installation with a smaller
archive file. Of course, pkgng can manage .tbz, .tgz and .tar archives as well.

3. Local database
-----------------

Once a package is installed, it is registered to a SQLite database.

The SQLite database allow fast queries and ACID transactions.
It also allow to query the reverse dependencies without a +REQUIRED_BY hack.

In order to save space the MTREE is only stored once, which save 18K per
installed package.

pkgng supports a `register' command to register packages into the SQLite
database from the ports. The register command can execute the install script,
show pkg-message, ...

4. Installation of packages
---------------------------

`pkg add' can install a package archive from the local disk, on from a
retote FTP/HTTP remote server.

If only a package name is given, it will search the remote repository
and download and install the package if it exists. The dependencies will be
downloaded and installed first. 

This is possible because we have the dependencies informations in the 
remote repository database.

`pkg add' will check if the user attempts to install a package built for another
arch or release.

5. Upgrades of packages
-----------------------

pkgng will also support upgrades of binary packages.

pkgng will compare the versions of installed packages and those available on
the repository. It will compute the proper update order and apply them.

6. Deletion packages
--------------------

Directory leftovers are automatically removed if they are not in the MTREE.

7. pkgng in Ports
-----------------

To use pkgng from ports currently we need to include bsd.pkgng.mk in bsd.port.mk,
the line before  .if defined(USE_LOCAL_MK)

8. A quick usage introduction to pkgng
--------------------------------------

In this section of the document we will try to give a quick and dirty introduction
on the practical usage of pkgng - installing packages, searching in remote package
repositories, updating remote package repositories and installing from them, etc.

8.1. Installing pkgng
---------------------

The first thing to start with is to get pkgng installed on your machine.

You can grap a development snapshot of pkgng at the following Github repo:

	- https://github.com/pkgng/pkgng

To get the latest version of pkgng from the Git repo, just clone it:

	# git clone https://github.com/pkgng/pkgng

Or you can take an already tagged release of pkgng from the above web page as well.

Just open your browser and download the release you want.

Once you have the pkgng sources, installing it is fairly easy:

	# cd pkgng
	# make && make install && make clean

Now you should have pkgng installed on your system.

Transferring your packages to pkgng is done by the ports/pkg2ng script.

In order to register your installed packages to pkgng, execute the command below:

	# cd pkgng/ports
	# sh pkg2ng

8.2. Getting help on the commands usage
---------------------------------------

In order to get help on any of the pkgng commands you should use the 'pkg help <command>'
command, which will take the man page of the specified command.

In order to get the available commands in pkgng, just execute 'pkg help'

	# pkg help 
	# pkg help <command>

8.3. Installing packages
------------------------

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

For more information on installing packages on your FreeBSD system, please refer to pkg-add(1)

8.4. Querying the local package database
----------------------------------------

In order to get information about installed packages you need to use the 'pkg info' command.

'pkg info' will query the local package database and display you information about the
package you are intrested in.

To list all install/registered packages in the local database, you will use the following command:

	# pkg info -a

For more information on querying the local package database, please refer to pkg-info(1) man page.

8.5. Using remote package repositories
--------------------------------------

The good thing about pkgng is that it is able to use remote package repositories.

A remote package repository contains a collection of packages which can be
fetched, searched and installed into your systems.

The other good thing of pkgng is that it is able to work with multiple remote
repositories, so you can actually fetch, search and install from multiple locations!

	NOTE: Multple remote repositories are currently considered experimental.
	      Currently multiple remote repositories can be used to fetch, search and
	      install packages from. Upgrading from multiple repositories will be available
	      in the next releases of pkgng. For now upgrading can only be done from
	      a single remote package repository.

In order to use a remote repository you need to define the PACKAGESITE environment variable,
so that it points to a remote location, which contains packages that can be installed by pkgng.

If the PACKAGESITE environment variable is not defined then pkgng will work in multiple 
repositories mode, using the repositories which are defined in the /etc/pkg/repositories file.

In order to work with a single remote package repository, you would define PACKAGESITE to
your remote server with packages, like for example:

	TCSH users:

	# setenv PACKAGESITE http://example.org/pkgng-repo/

	SH users:

	# export PACKAGESITE=http://example.org/pkgng-repo/

For multiple remote repositories the PACKAGESITE variable needs NOT to be defined and the
remote package repositories should be definend in the /etc/pkg/repositories file.

A remote package repository in the /etc/pkg/repositories file uses the following format:

	repo-name = url

The file format is the same as described in pkg.conf(5).

Example remote repository definition might look like this:
	
	# dnaeon's repo of i386 packages
	dnaeon-i386   = http://unix-heaven.org/FreeBSD/dnaeon-i386/
	
	# dnaeon's repo of amd64 packages
	dnaeon-amd64  = http://unix-heaven.org/FreeBSD/dnaeon-amd64/

Please check the included sample 'repositories' file for example definitions of remote
packages repositories and pkg.conf(5) for the file format of the file.

8.6. Updating remote repositories
---------------------------------

The first thing to do when working with remote repositories is to update from them.

Updating remote repositories is done by the 'pkg update' command.

If PACKAGESITE environment variable is defined to point to a remote package
repository then only this repository will be updated. Otherwise all remote 
repositories defined in the /etc/pkg/repositories file will be updated.

So, to update your remote repositories, you would execute this command:

	# pkg update

For more information on the remote repositories, please refer to pkg-update(1).

8.7. Searching in remote package repositories
---------------------------------------------

You can search in the remote package repositories using the 'pkg search' command.

In order to search in multiple package repositories the enviornment variable
PACKAGESITE should NOT be defined, in which case 'pkg search' will query 
the remote package databases found in the /etc/pkg/repositories file.

An example search for a package could be done like this:

	# pkg search -x apache

For more information on the repositories search, please refer to pkg-search(1)

8.8. Installing from remote repositories
----------------------------------------

In order to install a package from a remote repository you need to set the
PACKAGESITE environment variable to point to the remote server.

If PACKAGESITE is not defined then the installation process will use
multiple repositories as defined in the /etc/pkg/repositories file.

During installation from multiple repositories the first repository
that is found to has the package is the first one that pkgng will use 
during the installation. If that repository is not accessible for some reason,
then the next repository which contains the package is the one that is tried.

The process continues until the package is fetched and installed, or all 
remote repositories fail to fetch the package.

Remote installations of packages using pkgng are done by the 'pkg install' command.

Here's an example installation of the Apache web server package:

	# pkg install www/apache22

For more information on the remote package installs, please refer to pkg-install(1)

8.9. Backing up your package database
-------------------------------------

It is a good idea that you backup your local package database on regular basis.

In order to backup the local package database, you should use the 'pkg backup' command.

	# pkg backup -d /path/to/pkgng-backup.dump

The above command will dump the local package database in the /path/to/pkgng-backup.dump
file and compress it.

For more information on backing up your local package database, please refer to pkg-backup(1)

9. Additional resources
-----------------------

The Git repository of pkgng is hosted on Github at the following address:

	- https://github.com/pkgng/pkgng

For more information on pkgng you can visit it's Wiki page (still needs some work there):

	- http://wiki.freebsd.org/pkgng

Doxygen documentation of libpkg is available at the address below, which is generated
every 30min from the master pkgng repository:

	- http://pkgng.unix-heaven.org/

Buildbot for pkgng:

	- https://buildbot.etoilebsd.net/

In order to get in contact with us, you can find us in the following IRC channel:

	- #pkgng@freenode

If you hit a bug when using pkgng, you can always submit an issue at the address below:

	- https://github.com/pkgng/pkgng/issues
