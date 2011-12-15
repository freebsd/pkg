pkgng - Frequently Asked Questions (FAQ)
========================================

Table of Contents
-----------------

* [Is there an equivalent for pkg-orphan/pkg_cutleaves with pkgng?](#1)
* [How pkgng is different from the FreeBSD pkg_* tools? What is the motivation behind pkgng?](#2)
* [How pkgng is different from PC-BSD PBI packages?](#3)
* [What is the difference between `pkg delete -y` and `pkg delete -f`?](#4)
* [Where is `pkg info -R`, the old `pkg_info` had `-R`?](#5)
* [Can pkgng replace a package with an another version, eg. pkg upgrade pkg-1.0 pkg-2.0?](#6)
* [How pkgng deals with dependencies?](#7)
* [The repository format of pkgng is different from the old one. Will pkgng adapt the old format too?](#8)
* [Does `pkg repo` include symlinked packages?](#9)
* [How do I know if I have packages with the same origin?](#10)

<a name="1"></a>
### Q: Is there an equivalent for pkg-orphan/pkg_cutleaves with pkgng?

`pkg autoremove` is what you are looking for.

<a name="2"></a>
### Q: How pkgng is different from the FreeBSD pkg_* tools? What is the motivation behind pkgng?

The [README](https://github.com/pkgng/pkgng/blob/master/README.md) should explain all of that :)

<a name="3"></a>
### Q: How pkgng is different from PC-BSD PBI packages?

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
### Q: How pkgng deals with dependencies?

If `pkgA-1.0` depends on `pkgB-1.0` and `pkgB-1.0` is updated to `pkgB-2.0`, will `pkgA` notice the change?

Yes, `pkgA` will automatically notice the change.

<a name="8"></a>
### Q: The repository format of pkgng is different from the old one. Will pkgng adapt the old format too?

The documented (README) way to create a new repository creates all packages in one directory. 

This is different from earlier repository format, which creates it in separate directories.

Pkgng doesn not depend on a hierarchy, it recursively finds the packages from the provided directory entry.

<a name="9"></a>
### Q: Does `pkg repo` include symlinked packages?

The default hierarchy has lots of symlinks which should just be ignored and thus pkgng doesn not read symlinks.

<a name="10"></a>
### Q: How do I know if I have packages with the same origin?

Hee is how to do that:

    sh -c 'find . -name "*.txz" -exec pkg query -f {} %o \;' | sort | uniq -d
