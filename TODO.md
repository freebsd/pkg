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

# Explore a rework of the library dependencies

Would be nice to have a better mechanism that could track the symbols of the
libraries (including versions)

RPM already does this

# Allow duplicate package handling

Allow multiple packages having the same name as long as they don't conflict

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

zstd is very interesting compression format that it would be nice to add support for

# Add the notion of triggers

Trigger would allow to only run once from scripts at the end of the upgrade
process.

There should be 2 kind of triggers: pre and post the entire process
Triggers should deduplicate themselves and fexible so packages can provide some themselves

for example:
gtk-update-icon-cache should provide a trigger for everything that provides an icon

if gtk-update-icon-cache is installed on the system and packages are flagged for
"icons" then the gtk icon cache would be updated.

That would remove the gtk-update-icon-cache dependency from every application
that provides icons and make it run only once.

# Teach pkg repo to keep a list of unavailable packages

When a build fail the package is not added to the repository. As such, a user who attempts to install the package is told that it does not exist, rather than it is just temporarily unavailable.

The repo building process could create a list of failed packages during the build, and provide that as part of the repo in the form of a UCL file. If there are no result when looking for a package, we could check the failed packages list, and provide the user with a reason why the package is not available (build failed, RESTRICTED, BROKEN, EXPIRED, etc)

A similar process may also make sense for the MOVED file.

# Add a mechanism to allow a user to know when a package no longer exists in the remote repos

pkg upgrade could grow a way to complain about packages it cannot upgrade because they no longer exist in the repo. When combined with the 'failed packages' list, and possibly the MOVED list, this could provide a reliable mechanism for the user to know that the package they have no longer exists in the ports tree.

# Add a periodic script for reporting pkg check -d output

# add a pkg wrongabi alias if possible in pkg.conf

This would allow after a major upgrade to check what packages are still on the old ABI

# Add a FreeBSD only check on FreeBSD version (which should also be added to packages)

This would allow to only install packages for the right ABI and only if the binary was built on an
OS version which is lower or equal that the one we are running on.

# new groupinstall concept

Add some new ucl files in the ports tree do define groups of files instead of meta ports
