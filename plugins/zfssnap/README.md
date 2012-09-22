## General Information

The *pkg-plugin-zfssnap* plugin is a plugin meant to be used for creating snapshots
on a system with ZFS prior any install/deinstall actions are taken.

*pkg-plugin-zfssnap* is useful in a way that if something breaks in case your installation or
deinstallation of package(s) fails you will be able to rollback to a previously known and working
state of your system.

## How to build the plugin?

In order to build the plugin enter into the plugin's directory and run make(1), e.g.:

	$ cd /path/to/pkg-plugins-zfssnap
	$ make
	
Once the plugin is built you can install it using the following command:

	$ make install 
	
The plugin will be installed as a shared library in ${PREFIX}/lib/libpkg-plugin-zfssnap.so

## Configuring the plugin

In order to configure the plugin simply copy the *zfssnap.conf* file to the pkgng plugins directory,
which by default is set to */usr/local/etc/pkg/plugins*, unless you've specified it elsewhere by 
using the *PKG\_PLUGINS\_DIR* option.

	$ cp /path/to/pkg-plugins-zfssnap/zfssnap.conf /usr/local/etc/pkg/plugins/
	
Next, open */usr/local/etc/pkg/plugins/zfssnap.conf* and configure any ZFS related options.
	
## Testing the plugin

To test the plugin, first check that it is recognized and
loaded by pkgng by executing the `pkg plugins` command:

	$ pkg plugins
	NAME       DESC                                VERSION    LOADED    
	zfssnap    ZFS snapshot plugin for pkgng       1.0        YES       

If the plugin shows up correctly then you are good to go! :)

Once you start installing/deinstall package(s) zfssnap will create a snapshot for you! 

