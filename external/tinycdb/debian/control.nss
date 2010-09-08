
Package: libnss-cdb
Architecture: any
Section: libs
Depends: ${shlibs:Depends}, tinycdb
Description: cdb-based nss (name service switch) module
 tinycdb is a small, fast and reliable utility and subroutine
 library for creating and reading constant databases. The database
 structure is tuned for fast reading.
 .
 This package provides a name service switch (nsswitch) module
 to use to index /etc/password files for fast access.
 This module works only for passwd, group and shadow databases,
 not for hosts, networks, services and others.
