pkgng - Frequently Asked Questions (FAQ)
========================================

Table of Contents
-----------------

* [How can I start using pkgng?](#0)
* [Is there an equivalent for pkg-orphan/pkg_cutleaves with pkgng?](#1)
* [How is pkgng different from the FreeBSD pkg_* tools? What is the motivation behind pkgng?](#2)
* [How is pkgng different from PC-BSD PBI packages?](#3)
* [What is the difference between `pkg delete -y` and `pkg delete -f`?](#4)
* [Where is `pkg info -R`, the old `pkg_info` had `-R`?](#5)
* [Can pkgng replace a package with an another version, eg. pkg upgrade pkg-1.0 pkg-2.0?](#6)
* [How does pkgng deal with dependencies?](#7)
* [The repository format of pkgng is different from the old one. Will pkgng adapt the old format too?](#8)
* [Does `pkg repo` include symlinked packages?](#9)
* [How do I know if I have packages with the same origin?](#10)
* [How to start working with multi-repos in pkgng?](#11)
* [Why is `pkg create` slow?](#12)
* [Does pkgng work with portaudit?](#13)
* [When will pkgng be the default package manager?](#14)
* [How can I use pkgng with portmaster?](#15)
* [How can I use pkgng with portupgrade?](#16)
* [pkgng does not work it says: /usr/local/sbin/pkg: Undefined symbol "pkg_event_register"](#17)
* [Can pkgng cope with parallel installs?  What happpens if I simultaneously (attempt to) install conflicting packages?](#18)
* [If I use "pkg delete -f", what happens to packages that depended on the forcibly-deleted package?](#19)
* [What happens if I delete a package where I've modified one of the files managed by the package?](#20)
* [What facilities does it have for auditing and repairing the package database? (ie checking for inconsistencies between installed files and the content of the package database)?](#21)
* [Will it detect that a package install would overwrite an existing?](#22)
* [If so, what happens to the file metadata (particularly uid, gid and mtime)?](#23)
* [Can it track user-edited configuration files that are associated with packages?](#24)
* [Can it do 2- or 3-way merges of package configuration files?](#25)
* [The README states "Directory leftovers are automatically removed if they are not in the MTREE."  How does this work for directories that are shared between multiple packages?  Does this mean that if I add a file to a directory that was created by a package, that file will be deleted automatically if I delete the package?](#26)
* [How to create a new plugin for pkgng?](#27)

<a name="0"></a>
### Q: How can I start using pkgng?

In order to start using pkgng, please follow the steps below.

Install ports-mgmt/pkg:

        # make -C /usr/ports/ports-mgmt/pkg install clean
        # echo "WITH_PKGNG=yes" >> /etc/make.conf

Now register all packages you have in the pkgng database:

	# pkg2ng

And that was it. Please read the man pages for more information on the commands.

<a name="1"></a>
### Q: Is there an equivalent for pkg-orphan/pkg_cutleaves with pkgng?

`pkg autoremove` is what you are looking for.

<a name="2"></a>
### Q: How is pkgng different from the FreeBSD pkg_* tools? What is the motivation behind pkgng?

The [README](https://github.com/freebsd/pkg/blob/master/README.md) should explain all of that :)

<a name="3"></a>
### Q: How is pkgng different from PC-BSD PBI packages?

PBI are flat/complete packages, where pkgng do package ports as there are.

<a name="4"></a>
### Q: What is the difference between `pkg delete -y` and `pkg delete -f`?

By default pkgng will ask before doing something, `-y == yes` means yes do it.
But if a package it depends on it will fail saying it is depend on, `-f == force` means that delete it anyway.

<a name="5"></a>
### Q: Where is `pkg info -R`, the old `pkg_info` had `-R`?

New flags are: `pkg info -d` for depends on, and `pkg info -r` for reverse dependencies.

<a name="6"></a>
### Q: Can pkgng replace a package with an another version, e.g. `pkg upgrade pkg-1.0 pkg-2.0`?

Currently not, but it is in the todo list.

<a name="7"></a>
### Q: How does pkgng deal with dependencies? If `pkgA-1.0` depends on `pkgB-1.0` and `pkgB-1.0` is updated to `pkgB-2.0`, will `pkgA` notice the change?

Yes, `pkgA` will automatically notice the change.

<a name="8"></a>
### Q: The repository format of pkgng is different from the old one. Will pkgng adapt the old format too?

The documented (README) way to create a new repository creates all packages in one directory.

This is different from earlier repository format, which creates it in separate directories.

Pkgng does not depend on a hierarchy, it recursively finds the packages from the provided directory entry.

<a name="9"></a>
### Q: Does `pkg repo` include symlinked packages?

The default hierarchy has lots of symlinks which should just be ignored and thus pkgng does not read symlinks.

<a name="10"></a>
### Q: How do I know if I have packages with the same origin?

Here is how to do that:

    sh -c 'find . -type f -name "*-*.txz" -exec pkg query -F {} %o \;' | sort | uniq -d

As of beta17, `pkg repo` will emit a warning message and ignore any
older versions if it finds multiple packages from the same origin
when building a repo.

<a name="11"></a>
### Q: How to start working with multi-repos in pkgng?

Please refer to the [README](https://github.com/freebsd/pkg/blob/master/README.md#multirepos), which explains how to enable and get started with multi-repos in pkgng.

<a name="12"></a>
### Q: Why is `pkg create` slow?

The number one reason is the XZ compression, which is slow.

<a name="13"></a>
### Q: Does pkgng work with portaudit?

No, pkgng uses internal `pkg audit` command.

<a name="14"></a>
### Q: When will pkgng be the default package manager of FreeBSD?

Possibly in version 9.2+

<a name="15"></a>
### Q: How can I use pkgng with portmaster?

Ensure your ports tree is up-to-date, select the PKGNG option, and install/upgrade portmaster:

    # portsnap fetch update
    # make -C /usr/ports/ports-mgmt/portmaster config clean build deinstall install
    # echo "WITH_PKGNG=yes" >> /etc/make.conf
    # pkg2ng

<a name="16"></a>
### Q: How can I use pkgng with portupgrade?

Install the latest **ports-mgmt/portupgrade**, or **ports-mgmt/portupgrade-devel**. Both support pkgng.

    # portsnap fetch update
    # portupgrade ports-mgmt/portupgrade
    # echo "WITH_PKGNG=yes" >> /etc/make.conf
    # pkg2ng

More information can be found in the portupgrade [NEWS](https://github.com/freebsd/portupgrade/blob/master/NEWS.md) file.

<a name="17"></a>
### Q: pkgng does not work it says: /usr/local/sbin/pkg: Undefined symbol "pkg_init"

You forgot to run `make delete-old-libs` when you upgraded your system.

During 9-CURRENT life the pkg_install tools have been split to provide a
shared library: libpkg.so.0. This has been reverted, this error message means
that this library is still on your system. Check for and delete /usr/lib/libpkg.so.0.

<a name="18"></a>
### Q: Can pkgng cope with parallel installs?  What happpens if I simultaneously (attempt to) install conflicting packages?

No.  Parallel installs will not work -- the first to start will lock the DB, and
the second won't be able to proceed.

<a name="19"></a>
### Q: If I use "pkg delete -f", what happens to packages that depended on the forcibly-deleted package?

Nothing.  If you forcibly delete a package it's assumed you understand that you know you're doing something that can break your system.
pkg check will detect missing dependency packages and reinstall as required.

<a name="20"></a>
### Q: What happens if I delete a package where I've modified one of the files managed by the package?

The package is removed, but modified file is not:

	# pkg check -s pciids
	pciids-20120625: checksum mismatch for /usr/local/share/pciids/pci.ids
	# pkg delete pciids
	The following packages will be deinstalled:
	
		pciids-20120625
	
	The deinstallation will free 788 kB
	Deinstalling pciids-20120625...pkg: /usr/local/share/pciids/pci.ids
	fails original SHA256 checksum, not removing
	pkg: rmdir(/usr/local/share/pciids/): Directory not empty
	done
	# pkg info pciids
	pkg: No package(s) matching pciids
	# ls -l /usr/local/share/pciids/pci.ids
	-rw-r--r--  1 root  wheel  752925 Jul 16 07:05
	/usr/local/share/pciids/pci.ids

<a name="21"></a>
### Q: What facilities does it have for auditing and repairing the package database? (ie checking for inconsistencies between installed files and the content of the package database)

See pkg-check(8)

<a name="22"></a>
### Q: Will it detect that a package install would overwrite an existing

Yes exactly that.  Files in the older package that are identical in the
newer one are left untouched.  Otherwise, files from the older package
are removed, and files from the newer package are installed.

<a name="23"></a>
### Q: If so, what happens to the file metadata (particularly uid, gid and mtime)?

Nothing.

<a name="24"></a>
### Q: Can it track user-edited configuration files that are associated with packages?

This works in exactly the same way as it does currently in the ports. Except if the package provide the configuration with a .pkgconf extension
In that if there is no user config the default configuration is created otherwise, the user edited version is kept

<a name="25"></a>
### Q: Can it do 2- or 3-way merges of package configuration files?

No.  In general the package will install sample configuration files,
and will only touch the live config files if either the live configs
don't exist, or the live configs are identical to the sample configs.
This is the standard way things work in the ports at the moment.

<a name="26"></a>
### Q: The README states "Directory leftovers are automatically removed if they are not in the MTREE."  How does this work for directories that are shared between multiple packages?  Does this mean that if I add a file to a directory that was created by a package, that file will be deleted automatically if I delete the package?

No.  Directories have to be empty before they will be removed.

<a name="27"></a>
### Q: How to create a new plugin for pkgng?

If you are interested in creating a new plugin for pkgng you might want to check the following handbook which is an introduction to plugins writing for pkgng.

* [Introduction to writing plugins for pkgng in FreeBSD](http://unix-heaven.org/writing-plugins-for-pkgng)
