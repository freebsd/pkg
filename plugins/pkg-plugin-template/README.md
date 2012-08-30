## General Information

The *pkg-plugin-template* plugin is a plugin meant to be used as a template
when developing a new plugin for pkgng.

It contains some examples and hints how a plugin should be structured and built.

## How to build the plugin?

In order to build the plugin enter into the plugin's directory and run make(1), e.g.:

	$ cd /path/to/pkg-plugins-template
	$ make
	
Once the plugin is built you can install it using the following command:

	$ make install 
	
The plugin will be installed as a shared library in ${PREFIX}/lib/libpkg-plugin-template.so

## Configuring the plugin

In order to configure the plugin simply copy the *template.conf* file to the pkgng plugins directory,
which by default is set to */usr/local/etc/pkg/plugins*, unless you've specified it elsewhere by 
using the *PKG\_PLUGINS\_DIR* option.

	$ cp /path/to/pkg-plugins-template/template.conf /usr/local/etc/pkg/plugins/
	
## Testing the plugin

To test the plugin, first check that it is recongnized and
loaded by pkgng by executing the `pkg plugins` command:

	$ pkg plugins
	NAME       DESC                                VERSION    LOADED    
	foo        Foo plugin for pkg                  1.1        YES       
	template   Template plugin for pkgng           1.0        YES       

If the plugin shows up correctly then you are good to go! :)

