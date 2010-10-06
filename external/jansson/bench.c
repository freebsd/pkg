#include <stdlib.h>
#include <stdio.h>
#include "jansson.h"

int
main()
{
	void *m;
	json_t *j;
	json_t *n;
	json_error_t e;
m = malloc(1024);
	printf("Reading...");
	j = json_load_file("/var/db/pkg/ruby-1.8.7.248_4,1/+MANIFEST", &e);
	printf(" done!\n");
	if (j == NULL)
		printf("%s\n", e.text);	
	else {
		n = json_object_get(j, "name");
		printf("Name=%s\n", json_string_value(n));
		json_decref(j);
	}	
}
