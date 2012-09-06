## General Information

The *pkg-plugin-mystats* plugin is used for displaying package statistics.

It is meant to serve for demonstration purposes on how to write pkgng plugins which
provides commands to be executed by running `pkg <plugin-command>`

The command uses the code from `pkg stats` for displaying package stats.

## How to build the plugin?

In order to build the plugin enter into the plugin's directory and run make(1), e.g.:

	$ cd /path/to/pkg-plugins-mystats
	$ make
	
Once the plugin is built you can install it using the following command:

	$ make install 
	
The plugin will be installed as a shared library in ${PREFIX}/lib/libpkg-plugin-mystats.so

## Configuring the plugin

In order to configure the plugin simply copy the *mystats.conf* file to the pkgng plugins directory,
which by default is set to */usr/local/etc/pkg/plugins*, unless you've specified it elsewhere by 
using the *PKG\_PLUGINS\_DIR* option.

	$ cp /path/to/pkg-plugins-mystats/mystats.conf /usr/local/etc/pkg/plugins/
	
## Testing the plugin

To test the plugin, first check that it is recongnized and
loaded by pkgng by executing the `pkg plugins` command:

	$ pkg plugins
	NAME       DESC                                VERSION    LOADED  
	mystats    Plugin command for displaying stats 1.0        YES       

If the plugin shows up correctly then you are good to go!

Now go ahead and execute `pkg mystats` and see the plugin in action! :)

