/*-
 * Copyright (c) 2015-2022 Baptiste Daroussin <bapt@FreeBSD.org>
 *~
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *~
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

#include <atf-c.h>
#include <err.h>
#include <unistd.h>
#include <pkg.h>
#include <private/pkg.h>

ATF_TC_WITHOUT_HEAD(check_symlinks);
ATF_TC_WITHOUT_HEAD(check_pkg);
ATF_TC_WITHOUT_HEAD(check_types);
ATF_TC_WITHOUT_HEAD(check_validity);

ATF_TC_BODY(check_validity, tc)
{
	ATF_REQUIRE_EQ(pkg_checksum_is_valid("mehe", 2), false);
	ATF_REQUIRE_EQ(pkg_checksum_is_valid("mehe", 4), false);
	ATF_REQUIRE_EQ(pkg_checksum_is_valid("m$he", 4), false);
	ATF_REQUIRE_EQ(pkg_checksum_is_valid("2$166004b891d84ee8d263e4fe1ddc3be09a11a053bf8f6c3804bad6ca3248c332d", 66), false);
	ATF_REQUIRE_EQ(pkg_checksum_is_valid("2$42$66004b891d84ee8d263e4fe1ddc3be09a11a053bf8f6c3804bad6ca3248c332d", 66), false);
	ATF_REQUIRE_EQ(pkg_checksum_is_valid("2$1$66004b891d84ee8d263e4fe1ddc3be09a11a053bf8f6c3804bad6ca3248c332d", 66), true);
}

ATF_TC_BODY(check_types, tc)
{
	ATF_REQUIRE_EQ(pkg_checksum_type_from_string("sha256_base32"), PKG_HASH_TYPE_SHA256_BASE32);
	ATF_REQUIRE_EQ(pkg_checksum_type_from_string("sha256_hex"), PKG_HASH_TYPE_SHA256_HEX);
	ATF_REQUIRE_EQ(pkg_checksum_type_from_string("blake2_base32"), PKG_HASH_TYPE_BLAKE2_BASE32);
	ATF_REQUIRE_EQ(pkg_checksum_type_from_string("sha256_raw"), PKG_HASH_TYPE_SHA256_RAW);
	ATF_REQUIRE_EQ(pkg_checksum_type_from_string("blake2_raw"), PKG_HASH_TYPE_BLAKE2_RAW);
	ATF_REQUIRE_EQ(pkg_checksum_type_from_string("blake2s_base32"), PKG_HASH_TYPE_BLAKE2S_BASE32);
	ATF_REQUIRE_EQ(pkg_checksum_type_from_string("blake2s_raw"), PKG_HASH_TYPE_BLAKE2S_RAW);
	ATF_REQUIRE_EQ(pkg_checksum_type_from_string("plop"), PKG_HASH_TYPE_UNKNOWN);

	ATF_REQUIRE_EQ(pkg_checksum_get_type("0$0$", 0), PKG_HASH_TYPE_SHA256_BASE32);
	ATF_REQUIRE_EQ(pkg_checksum_get_type("0$1$", 0), PKG_HASH_TYPE_SHA256_HEX);
	ATF_REQUIRE_EQ(pkg_checksum_get_type("0$2$", 0), PKG_HASH_TYPE_BLAKE2_BASE32);
	ATF_REQUIRE_EQ(pkg_checksum_get_type("0$3$", 0), PKG_HASH_TYPE_SHA256_RAW);
	ATF_REQUIRE_EQ(pkg_checksum_get_type("0$4$", 0), PKG_HASH_TYPE_BLAKE2_RAW);
	ATF_REQUIRE_EQ(pkg_checksum_get_type("0$5$", 0), PKG_HASH_TYPE_BLAKE2S_BASE32);
	ATF_REQUIRE_EQ(pkg_checksum_get_type("0$6$", 0), PKG_HASH_TYPE_BLAKE2S_RAW);
	ATF_REQUIRE_EQ(pkg_checksum_get_type("0$42$", 0), PKG_HASH_TYPE_UNKNOWN);
	ATF_REQUIRE_EQ(pkg_checksum_get_type("", 0), PKG_HASH_TYPE_UNKNOWN);

	ATF_REQUIRE_EQ(pkg_checksum_file_get_type("0$", 0), PKG_HASH_TYPE_SHA256_BASE32);
	ATF_REQUIRE_EQ(pkg_checksum_file_get_type("1$", 0), PKG_HASH_TYPE_SHA256_HEX);
	ATF_REQUIRE_EQ(pkg_checksum_file_get_type("2$", 0), PKG_HASH_TYPE_BLAKE2_BASE32);
	ATF_REQUIRE_EQ(pkg_checksum_file_get_type("3$", 0), PKG_HASH_TYPE_SHA256_RAW);
	ATF_REQUIRE_EQ(pkg_checksum_file_get_type("4$", 0), PKG_HASH_TYPE_BLAKE2_RAW);
	ATF_REQUIRE_EQ(pkg_checksum_file_get_type("5$", 0), PKG_HASH_TYPE_BLAKE2S_BASE32);
	ATF_REQUIRE_EQ(pkg_checksum_file_get_type("6$", 0), PKG_HASH_TYPE_BLAKE2S_RAW);
	ATF_REQUIRE_EQ(pkg_checksum_file_get_type("42$", 0), PKG_HASH_TYPE_UNKNOWN);
	ATF_REQUIRE_EQ(pkg_checksum_file_get_type("", 0), PKG_HASH_TYPE_UNKNOWN);
}

ATF_TC_BODY(check_symlinks, tc)
{
	unsigned char *sum;

	ATF_REQUIRE_EQ(symlink("foo", "bar"), 0);

	sum = pkg_checksum_symlink("bar", PKG_HASH_TYPE_SHA256_HEX);
	ATF_REQUIRE_STREQ(sum, "2c26b46b68ffc68ff99b453c1d30413413422d706483bfa0f98a5e886266e7ae");

	ATF_CHECK(pkg_checksum_validate_file("bar", "2c26b46b68ffc68ff99b453c1d30413413422d706483bfa0f98a5e886266e7ae") == 0);
	free(sum);

	sum = pkg_checksum_generate_file("bar", PKG_HASH_TYPE_SHA256_HEX);
	ATF_REQUIRE_STREQ(sum, "1$2c26b46b68ffc68ff99b453c1d30413413422d706483bfa0f98a5e886266e7ae");
	free(sum);

	sum = pkg_checksum_generate_file("bar", PKG_HASH_TYPE_BLAKE2_BASE32);
	ATF_REQUIRE_STREQ(sum, "2$kgygnaah7wxsgn1wkuic4j78zq8dicmx53picmma99ogmkbd7k5nhuxr5xxemz6yzjab15oor3tjt7nupj8mh764y7kddbne7qw9agn");
	free(sum);
	sum = pkg_checksum_generate_file("bar", PKG_HASH_TYPE_BLAKE2S_BASE32);
	ATF_REQUIRE_STREQ(sum, "5$eoiiccdoiuz9acwfo7fxi6abnrfdtg81mz5ccx7tbg5ny9755g7y");
	free(sum);

	ATF_CHECK(pkg_checksum_validate_file("bar", "2$kgygnaah7wxsgn1wkuic4j78zq8dicmx53picmma99ogmkbd7k5nhuxr5xxemz6yzjab15oor3tjt7nupj8mh764y7kddbne7qw9agn") == 0);
	ATF_CHECK(pkg_checksum_validate_file("bar", "5$eoiiccdoiuz9acwfo7fxi6abnrfdtg81mz5ccx7tbg5ny9755g7y") == 0);
	ATF_CHECK(pkg_checksum_validate_file("bar", "1$2c26b46b68ffc68ff99b453c1d30413413422d706483bfa0f98a5e886266e7ae") == 0);
}

ATF_TC(check_files);

ATF_TC_HEAD(check_files, tc)
{
	atf_tc_set_md_var(tc, "descr", "testing checksums on files");
}

ATF_TC_BODY(check_files, tc)
{
	FILE *f;
	unsigned char *sum;

	f = fopen("foo", "w");
	fprintf(f, "bar\n");
	fclose(f);

	sum = pkg_checksum_file("foo", PKG_HASH_TYPE_SHA256_HEX);
	ATF_REQUIRE_STREQ(sum, "7d865e959b2466918c9863afca942d0fb89d7c9ac0c99bafc3749504ded97730");

	ATF_CHECK(pkg_checksum_validate_file("foo", "7d865e959b2466918c9863afca942d0fb89d7c9ac0c99bafc3749504ded97730") == 0);
	free(sum);

	sum=pkg_checksum_generate_file("foo", PKG_HASH_TYPE_SHA256_HEX);
	ATF_REQUIRE_STREQ(sum, "1$7d865e959b2466918c9863afca942d0fb89d7c9ac0c99bafc3749504ded97730");
	free(sum);

	sum=pkg_checksum_generate_file("foo", PKG_HASH_TYPE_SHA256_HEX);
	ATF_CHECK(pkg_checksum_validate_fileat(AT_FDCWD, "foo", "7d865e959b2466918c9863afca942d0fb89d7c9ac0c99bafc3749504ded97730") == 0);
	free(sum);

	ATF_REQUIRE_EQ(pkg_checksum_generate_fileat(AT_FDCWD, "nonexistent", PKG_HASH_TYPE_BLAKE2_BASE32), NULL);

	ATF_REQUIRE_EQ(pkg_checksum_symlinkat(AT_FDCWD, "nonexistent", PKG_HASH_TYPE_BLAKE2_BASE32), NULL);
	ATF_REQUIRE_EQ(pkg_checksum_file("nonexistent", 42), NULL);
	ATF_REQUIRE_EQ(pkg_checksum_data("a", 1, 42), NULL);
	ATF_REQUIRE_STREQ(pkg_checksum_data("a", 0, PKG_HASH_TYPE_BLAKE2_BASE32), "u3xsc8fhkf9ntjikcz3hcsg1h5n59yqmz8s483emc8gessm4qnpk7ikhgqcmmz98ci391sdx565bazeffh1djkzkep7j1qqgeawsc6y");

	sum = pkg_checksum_file("foo", PKG_HASH_TYPE_BLAKE2_BASE32);
	ATF_REQUIRE_STREQ(sum, "gf8mcrnmm6p6hg6wa9xkfb98zo8g6nxu8z4q7s93boz8hzf5ogrsr4qgpsb7utd6speio3op18ocyrsa9ms8jj15byttiq7ofbih8gn");
	free(sum);

	sum = pkg_checksum_file("foo", PKG_HASH_TYPE_BLAKE2S_BASE32);
	ATF_REQUIRE_STREQ(sum, "dqi4rzroazhfbq4sd33ektsg3jjsrye7mc37ggsa9bt3mhxsyddy");
	free(sum);

}

ATF_TC_BODY(check_pkg, tc)
{
	struct pkg *p = NULL;
	pkg_new(&p, PKG_INSTALLED);
	pkg_set(p, PKG_NAME, "test");
	pkg_set(p, PKG_ORIGIN, "origin");
	pkg_set(p, PKG_ARCH, "*");
	char *sum = xcalloc(pkg_checksum_type_size(PKG_HASH_TYPE_SHA256_HEX) * 2, sizeof(char));

	ATF_REQUIRE_EQ(pkg_checksum_generate(NULL, sum, pkg_checksum_type_size(PKG_HASH_TYPE_SHA256_HEX) * 2, PKG_HASH_TYPE_SHA256_HEX, false, false, false), EPKG_FATAL);
	ATF_REQUIRE_EQ(pkg_checksum_generate(p, sum, pkg_checksum_type_size(PKG_HASH_TYPE_SHA256_HEX) * 2, 42, false, false, false), EPKG_FATAL);
	ATF_REQUIRE_EQ(pkg_checksum_generate(p, sum, 2, PKG_HASH_TYPE_SHA256_HEX, false, false, false), EPKG_FATAL);
	ATF_CHECK(pkg_checksum_generate(p, sum, pkg_checksum_type_size(PKG_HASH_TYPE_SHA256_HEX) * 2, PKG_HASH_TYPE_SHA256_HEX, false, false, false) == EPKG_OK);
	ATF_REQUIRE_STREQ(sum, "2$1$22c6baf7d22b7035be18ffe04f43717f907f4848b3d5d72bfc44bb8435053ea4");
	ATF_REQUIRE_EQ(pkg_checksum_get_type(sum, -1), PKG_HASH_TYPE_SHA256_HEX);
	free(sum);

	sum = xcalloc(pkg_checksum_type_size(PKG_HASH_TYPE_BLAKE2_BASE32) * 2, sizeof(char));
	ATF_CHECK(pkg_checksum_generate(p, sum, pkg_checksum_type_size(PKG_HASH_TYPE_BLAKE2_BASE32) * 2, PKG_HASH_TYPE_BLAKE2_BASE32, false, false, false) == EPKG_OK);
	ATF_REQUIRE_STREQ(sum, "2$2$iskiim4jgor5sie8tkthjksomnpyuynaqfxbmgt3x7rn9atyebiwk5njiiyxpyqm5eimq6g44bd9tnuwf3mfesqp6r8tim8un7jfday");
	ATF_REQUIRE_EQ(pkg_checksum_get_type(sum, -1), PKG_HASH_TYPE_BLAKE2_BASE32);
	free(sum);

	sum = xcalloc(pkg_checksum_type_size(PKG_HASH_TYPE_BLAKE2S_BASE32) * 2, sizeof(char));
	ATF_CHECK(pkg_checksum_generate(p, sum, pkg_checksum_type_size(PKG_HASH_TYPE_BLAKE2S_BASE32) * 2, PKG_HASH_TYPE_BLAKE2S_BASE32, false, false, false) == EPKG_OK);
	ATF_REQUIRE_STREQ(sum, "2$5$9819ezi7ytn58y3mwhcxaqbkiaik7ui9o3obewhqmuyx99kmb95y");
	ATF_REQUIRE_EQ(pkg_checksum_get_type(sum, -1), PKG_HASH_TYPE_BLAKE2S_BASE32);
	free(sum);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, check_symlinks);
	ATF_TP_ADD_TC(tp, check_files);
	ATF_TP_ADD_TC(tp, check_pkg);
	ATF_TP_ADD_TC(tp, check_types);
	ATF_TP_ADD_TC(tp, check_validity);

	return (atf_no_error());
}
