#include <check.h>

#include "tests.h"

int
main()
{
	int nfailed = 0;
	Suite *s = suite_create("pkgng");

	suite_add_tcase(s, tcase_manifest());
	suite_add_tcase(s, tcase_pkg());

	/* Run the tests ...*/
	SRunner *sr = srunner_create(s);
	srunner_set_log(sr, "test.log");
	srunner_run_all(sr, CK_NORMAL);
	nfailed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (nfailed == 0 ? 0 : 1);
}
