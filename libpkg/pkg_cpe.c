/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 *​ Copyright (c) 2025 The FreeBSD Foundation
 *​
 *​ Portions of this software were developed by
 * Tuukka Pasanen <tuukka.pasanen@ilmi.fi> under sponsorship from
 * the FreeBSD Foundation
 *
 * Common Platform Enumeration (CPE) is a standardized method of
 * describing and identifying classes of applications, operating
 * systems, and hardware devices present among an enterprise's
 * computing assets
 *
 * CPE (current version 2.3) looks something like this:
 * cpe:2.3:a:test:test_product:1.0:sp1:1:en-us:14.3:FreeBSD:x86_64:other_things
 *
 * Where parts are named like this:
 * cpe:<cpe_version>:<part>:<vendor>:<product>:<version>:<update>:<edition>:<language>:<sw_edition>:<target_sw>:<target_hw>:<other>
 *
 * Whole spec can be found:
 * https://csrc.nist.gov/pubs/ir/7695/final
 */

#include <xmalloc.h>
#include <ctype.h>
#include <stdlib.h>
#include <xstring.h>

#include "pkg.h"
#include "pkg/audit.h"
#include "private/pkg_cpe.h"

struct pkg_audit_cpe *
pkg_cpe_new()
{
	return (struct pkg_audit_cpe *)xcalloc(1, sizeof(struct pkg_audit_cpe));
}

void
pkg_cpe_free(struct pkg_audit_cpe *cpe)
{
	if(!cpe)
	{
		return;
	}

	if(cpe->vendor)
	{
		free(cpe->vendor);
		cpe->vendor = NULL;
	}

	if(cpe->product)
	{
		free(cpe->product);
		cpe->product = NULL;
	}

	if(cpe->version)
	{
		free(cpe->version);
		cpe->version = NULL;
	}

	if(cpe->update)
	{
		free(cpe->update);
		cpe->update = NULL;
	}

	if(cpe->edition)
	{
		free(cpe->edition);
		cpe->edition = NULL;
	}

	if(cpe->language)
	{
		free(cpe->language);
		cpe->language = NULL;
	}

	if(cpe->sw_edition)
	{
		free(cpe->sw_edition);
		cpe->sw_edition = NULL;
	}

	if(cpe->target_sw)
	{
		free(cpe->target_sw);
		cpe->target_sw = NULL;
	}

	if(cpe->target_hw)
	{
		free(cpe->target_hw);
		cpe->target_hw = NULL;
	}

	if(cpe->other)
	{
		free(cpe->other);
		cpe->other = NULL;
	}

	free(cpe);
}

char *
pkg_cpe_create(struct pkg_audit_cpe *cpe)
{
	char *cpe_str = NULL;
	/* To avoid (null) string in return string*/
	char *cpe_parts[10] = {"", "", "", "", "", "", "", "", "", ""};

	if(cpe->vendor)
	{
		cpe_parts[0] = cpe->vendor;
	}

	if(cpe->product)
	{
		cpe_parts[1] = cpe->product;
	}

	if(cpe->version)
	{
		cpe_parts[2] = cpe->version;
	}

	if(cpe->update)
	{
		cpe_parts[3] = cpe->update;
	}

	if(cpe->edition)
	{
		cpe_parts[4] = cpe->edition;
	}

	if(cpe->language)
	{
		cpe_parts[5] = cpe->language;
	}

	if(cpe->sw_edition)
	{
		cpe_parts[6] = cpe->sw_edition;
	}

	if(cpe->target_sw)
	{
		cpe_parts[7] = cpe->target_sw;
	}

	if(cpe->target_hw)
	{
		cpe_parts[8] = cpe->target_hw;
	}

	if(cpe->other)
	{
		cpe_parts[9] = cpe->other;
	}

	xasprintf(&cpe_str, "cpe:2.3:%c:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s", cpe->part, cpe_parts[0], cpe_parts[1], cpe_parts[2], cpe_parts[3], cpe_parts[4], cpe_parts[5], cpe_parts[6], cpe_parts[7], cpe_parts[8], cpe_parts[9]);

	return cpe_str;
}

struct pkg_audit_cpe *
pkg_cpe_parse(const char *cpe_str)
{
	char cpe_delimiter[] = ":";
	char cpe_atoi[2] = {0x00, 0x00};
	char *cpe_copy = NULL;
	char *cpe_token = NULL;
	struct pkg_audit_cpe *rtn_cpe = NULL;
	unsigned int loc = 0;
	bool is_error = false;

	if(!cpe_str)
	{
		return NULL;
	}

	if(strnlen(cpe_str, 8) < 8)
	{
		return NULL;
	}

	cpe_copy = xstrdup(cpe_str);
	cpe_token = strtok(cpe_copy, cpe_delimiter);

	if(!cpe_token)
	{
		rtn_cpe = NULL;
		goto cpe_return;
	}

	rtn_cpe = pkg_cpe_new();

	while(cpe_token)
	{
		is_error = false;
		switch(loc)
		{
		case 0:
			if(strncmp(cpe_token,"cpe", 3))
			{
				is_error = true;
			}
			break;

		case 1:
			if(!isdigit(cpe_token[0]) || !isdigit(cpe_token[2]))
			{
				is_error = true;
				break;
			}


			cpe_atoi[0] = cpe_token[0];
			rtn_cpe->version_major = atoi(cpe_atoi);

			cpe_atoi[0] = cpe_token[2];
			rtn_cpe->version_minor = atoi(cpe_atoi);

			if(rtn_cpe->version_major != 2 || rtn_cpe->version_minor != 3)
			{
				is_error = true;
			}
			break;

		case 2:
			if(cpe_token[0] != CPE_APPLICATIONS && cpe_token[0] != CPE_HARWARE && cpe_token[0] != CPE_OPERATING_SYSTEMS)
			{
				is_error = true;
			}

			rtn_cpe->part = cpe_token[0];
			break;

		case 3:
			rtn_cpe->vendor = xstrdup(cpe_token);
			break;

		case 4:
			rtn_cpe->product = xstrdup(cpe_token);
			break;

		case 5:
			rtn_cpe->version = xstrdup(cpe_token);
			break;

		case 6:
			rtn_cpe->update = xstrdup(cpe_token);
			break;

		case 7:
			rtn_cpe->edition = xstrdup(cpe_token);
			break;

		case 8:
			rtn_cpe->language = xstrdup(cpe_token);
			break;

		case 9:
			rtn_cpe->sw_edition = xstrdup(cpe_token);
			break;

		case 10:
			rtn_cpe->target_sw = xstrdup(cpe_token);
			break;

		case 11:
			rtn_cpe->target_hw = xstrdup(cpe_token);
			break;

		case 12:
			rtn_cpe->other = xstrdup(cpe_token);
			break;

		default:
			break;
		}

		if(is_error)
		{
			pkg_cpe_free(rtn_cpe);
			rtn_cpe = NULL;
			goto cpe_return;
		}


		loc ++;
		cpe_token = strtok(NULL, cpe_delimiter);

		if(loc >= 13 && cpe_token)
		{
			break;
		}
	}

cpe_return:
	if(loc <= 3)
	{
		pkg_cpe_free(rtn_cpe);
		rtn_cpe = NULL;
	}

	free(cpe_copy);
	cpe_copy = NULL;
	return rtn_cpe;
}
