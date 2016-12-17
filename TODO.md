# Fix all bugs reported

This is the number one requirement :)

# Replace dependencies by dep formula

Dep formula is a definition of a formula where can represent requirements
(keyword) and packages but also can define alternatives of those:

name1 = 1.0 | name2 != 1.0, name3 > 1.0 < 2.0 != 1.5, name4 +opt1 -opt2

Difficulty: hard

# Remove sqlite calls from frontend for (r)query

sqlite should be a technical detail and thus should not be exposed by the
library

Difficulty: hard

# auto backup libraries

When upgrading a package that contains a library which would be removed by the
upgrade plan, pkg could create automatically a <name-of-the-pkg>-compat new
package that would keep around the given library

The new package should be flagged as automatic

pkg autoremove could then remove it if nothing is depending on it anymore

Difficulty: medium

# (explore a ) rework of the library dependencies

Would be nice to have a better mechanism that could track the symbols of the
libraries (including versions)

RPM is doing that

# Allow duplicate package handling

to allow multiple packages having the same name as long as they don't conflicts

* pkg.conf:

  duplicatenumber: 2
  duplicate: { FreeBSD-kernel-*: 3, clibs: 5 }

* Drop unique index on packages(name) or make it on name,version
* annotation saveme
* Upgrade:

  * get_local_pkg()

    * check saveme
    *  ensure only one
    *  in summary: check for saveme annotation, change to install

  * pkg_add()

    * check for saveme annotation, remove upgrade flag

# Add support for zstd format

zstd is very interesting compression format that it would be nice to add support to

# Add the notion of triggers

Trigger would allow to only run once from scripts at the end of the upgrade
process.

There should be 2 kind of triggers: pre and post the entire process
Triggers should deduplicate themselves and fexible so packages can provide some themselves

for example:
gtk-update-icon-cache should provide a trigger for everything that provides an icon

if gtk-update-icon-cache is installed on the system and packages are flagged for
"icons" then the gtk icon cache would be upgraded.

That would remove the gtk-update-icon-cache dependence from every application
that provides icons and make it run only once.
