/*
 * Copyright (c) 2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Include other headers if needed */
#include <stdio.h>

/* Include pkg */
#include <pkg.h>

/* Include any plugin headers here */
#include "template.h"

/* Define the plugin name */
#define PLUGIN_NAME "template"

/*
 * The plugin *must* provide an init function that is called by the library.
 *
 * The plugin's init function takes care of registering a hook in the library,
 * which is handled by the pkg_plugins_hook() function.
 *
 * Every plugin *must* provide a 'pkg_plugins_init_<plugin>' function, which is
 * called upon plugin loading for registering a hook in the library.
 *
 * The plugin's init function prototype should be in the following form:
 *
 * int pkg_plugins_init_<plugin> (void);
 *
 * No arguments are passed to the plugin's init function.
 *
 * Upon successful initialization of the plugin EPKG_OK (0) is returned and
 * upon failure EPKG_FATAL ( > 0 ) is returned to the caller.
 */
int
pkg_plugins_init_template(void)
{
	/*
	 * Register two functions for hooking into the library
	 *
	 * my_callback1() will be triggered directly before any install actions are taken, which is
	 * specified by the PKG_PLUGINS_HOOK_PRE_INSTALL hook.
	 *
	 * my_callback2() will be triggered directly after any install actions were taken, which is
	 * specified by the PKG_PLUGINS_HOOK_POST_INSTALL hook.
	 *
	 */

	/* printf(">>> Plugin '%s' is about to hook into pkgng.. yay! :)\n", PLUGIN_NAME); */
	
	if (pkg_plugins_hook(PLUGIN_NAME, PKG_PLUGINS_HOOK_PRE_INSTALL, &my_callback1) != EPKG_OK) {
		fprintf(stderr, "Plugin '%s' failed to hook into the library\n", PLUGIN_NAME);
		return (EPKG_FATAL);
	}
	
	if (pkg_plugins_hook(PLUGIN_NAME, PKG_PLUGINS_HOOK_POST_INSTALL, &my_callback2) != EPKG_OK) {
		fprintf(stderr, "Plugin '%s' failed to hook into the library\n", PLUGIN_NAME);
		return (EPKG_FATAL);
	}
	
	return (EPKG_OK);
}

/*
 * Plugins may optionally provide a shutdown function.
 *
 * When a plugin provides a shutdown function, it is called
 * before a plugin is being unloaded. This is useful in cases
 * where a plugin needs to perform a cleanup for example, or
 * perform any post-actions like reporting for example.
 *
 * The plugin's shutdown function prototype should be in the following form:
 *
 * int pkg_plugins_shutdown_<plugin> (void);
 *
 * Upon successful shutdown of the plugin EPKG_OK (0) is returned and
 * upon failure EPKG_FATAL ( > 0 ) is returned to the caller.
 */
int
pkg_plugins_shutdown_template(void)
{
	/* printf(">>> Plugin '%s' is shutting down, enough working for today.. :)\n", PLUGIN_NAME); */

	/*
	 * Perform any cleanup if needed, e.g.:
	 * 
	  if (tidy) {
	   	rc = perform_cleanup();
	   	if (rc != EPKG_OK)
	   		return (EPKG_FATAL);
	  }
	*/

	return (EPKG_OK);
}

/*
 * And now we need to define our workers,
 * the plugin functions that carry out the real work.
 *
 * A plugin callback function accepts only one argument and
 * should return EPKG_OK (0) on success and EPKG_FATAL ( > 0 ) on failure.
 *
 * Plugin callbacks must also take care of proper casting of the (void *data) argument.
 *
 * Depending on where a plugin hooks into the library the data passed to the callback is
 * different.
 *
 * For example if a plugin hooks into PKG_PLUGINS_HOOK_PRE_INSTALL the (void *data) passed to the
 * called is (struct pkg_jobs *), so the plugin callback must cast it explicitely.
 */
int
my_callback1(void *data, struct pkgdb *db)
{
	printf("Hey, I was just called by the library, lets see what we've got here..\n");

	if (data == NULL)
		printf("Hmm.. no data for me today, guess I'll just go and grab a mohito then..\n");
	else
		printf("Got some data.. okay, okay.. I'll do something useful then..\n");

	return (EPKG_OK);
}

/*
 * Second callback function
 */
int
my_callback2(void *data, struct pkgdb *db)
{
	printf("Hey, I was just called again, lets see what its all about this time..\n");

	if (data == NULL)
		printf("Hmm.. no data, no work.. today is my lucky day!\n");
	else
		printf("Work never ends.. I'll do something useful again..\n");
	
	return (EPKG_OK);
}
