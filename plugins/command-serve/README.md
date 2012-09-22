## General Information

The *pkg-plugin-serve* plugin is used for serving files/packages.

The plugin uses the [Mongoose](https://github.com/valenok/mongoose) library for 
serving the files/packages over HTTP.

## How to build the plugin?

In order to build the plugin enter into the plugin's directory and run make(1), e.g.:

	$ cd /path/to/pkg-plugins-serve
	$ make
	
Once the plugin is built you can install it using the following command:

	$ make install 
	
The plugin will be installed as a shared library in ${PREFIX}/lib/libpkg-plugin-serve.so and the
Mongoose library will be installed in ${PREFIX}/lib/libmongoose.so

## Configuring the plugin

In order to configure the plugin simply copy the *serve.conf* file to the pkgng plugins directory,
which by default is set to */usr/local/etc/pkg/plugins*, unless you've specified it elsewhere by 
using the *PKG\_PLUGINS\_DIR* option.

	$ cp /path/to/pkg-plugins-serve/plugin/serve.conf /usr/local/etc/pkg/plugins/
	
## Testing the plugin

To test the plugin, first check that it is recongnized and
loaded by pkgng by executing the `pkg plugins` command:

	$ pkg plugins
	NAME       DESC                                VERSION    LOADED    
	stats      Plugin for displaying package stats 1.0        YES       
	template   Template plugin for pkgng           1.0        NO        
	zfssnap    ZFS snapshot plugin for pkgng       1.0        NO        
	mystats    Plugin command for displaying stats 1.0        YES       
	serve      A mongoose plugin for serving files 1.0        YES   

If the plugin shows up correctly then you are good to go!

Now go ahead and test the plugin, example command is given below:

	$ pkg serve -d /path/to/pkgng-repository

