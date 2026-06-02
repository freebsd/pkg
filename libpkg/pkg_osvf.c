/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2025 The FreeBSD Foundation
 *
 * Portions of this software were developed by
 * Tuukka Pasanen <tuukka.pasanen@ilmi.fi> under sponsorship from
 * the FreeBSD Foundation
 */


#include <ctype.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <xmalloc.h>

#include "private/pkg_osvf.h"
#include "private/utils.h"
#include "pkghash.h"

/*
  Open Source Vulnerability format: https://ossf.github.io/osv-schema/
  OSVF schema: https://github.com/ossf/osv-schema/blob/v1.7.5/validation/schema.json
  OSVF schema version: 1.7.5 (Which officially has FreeBSD Ecosystem)
 */
static const char osvf_schema_str[] = "{"
                                      "  \"$schema\": \"https://json-schema.org/draft/2020-12/schema\","
                                      "  \"$id\": \"https://raw.githubusercontent.com/ossf/osv-schema/main/validation/schema.json\","
                                      "  \"title\": \"Open Source Vulnerability\","
                                      "  \"description\": \"A schema for describing a vulnerability in an open source package. See also https://ossf.github.io/osv-schema/\","
                                      "  \"type\": \"object\","
                                      "  \"properties\": {"
                                      "    \"schema_version\": {"
                                      "      \"type\": \"string\""
                                      "    },"
                                      "    \"id\": {"
                                      "      \"$ref\": \"#/$defs/prefix\""
                                      "    },"
                                      "    \"modified\": {"
                                      "      \"$ref\": \"#/$defs/timestamp\""
                                      "    },"
                                      "    \"published\": {"
                                      "      \"$ref\": \"#/$defs/timestamp\""
                                      "    },"
                                      "    \"withdrawn\": {"
                                      "      \"$ref\": \"#/$defs/timestamp\""
                                      "    },"
                                      "    \"aliases\": {"
                                      "      \"type\": ["
                                      "        \"array\","
                                      "        \"null\""
                                      "      ],"
                                      "      \"items\": {"
                                      "        \"type\": \"string\""
                                      "      }"
                                      "    },"
                                      "    \"related\": {"
                                      "      \"type\": \"array\","
                                      "      \"items\": {"
                                      "        \"type\": \"string\""
                                      "      }"
                                      "    },"
                                      "    \"upstream\": {"
                                      "      \"type\": \"array\","
                                      "      \"items\": {"
                                      "        \"type\": \"string\""
                                      "      }"
                                      "    },"
                                      "    \"summary\": {"
                                      "      \"type\": \"string\""
                                      "    },"
                                      "    \"details\": {"
                                      "      \"type\": \"string\""
                                      "    },"
                                      "    \"severity\": {"
                                      "      \"$ref\": \"#/$defs/severity\""
                                      "    },"
                                      "    \"affected\": {"
                                      "      \"type\": ["
                                      "        \"array\","
                                      "        \"null\""
                                      "      ],"
                                      "      \"items\": {"
                                      "        \"type\": \"object\","
                                      "        \"properties\": {"
                                      "          \"package\": {"
                                      "            \"type\": \"object\","
                                      "            \"properties\": {"
                                      "              \"ecosystem\": {"
                                      "                \"$ref\": \"#/$defs/ecosystemWithSuffix\""
                                      "              },"
                                      "              \"name\": {"
                                      "                \"type\": \"string\""
                                      "              },"
                                      "              \"purl\": {"
                                      "                \"type\": \"string\""
                                      "              }"
                                      "            },"
                                      "            \"required\": ["
                                      "              \"ecosystem\","
                                      "              \"name\""
                                      "            ]"
                                      "          },"
                                      "          \"severity\": {"
                                      "            \"$ref\": \"#/$defs/severity\""
                                      "          },"
                                      "          \"ranges\": {"
                                      "            \"type\": \"array\","
                                      "            \"items\": {"
                                      "              \"type\": \"object\","
                                      "              \"properties\": {"
                                      "                \"type\": {"
                                      "                  \"type\": \"string\","
                                      "                  \"enum\": ["
                                      "                    \"GIT\","
                                      "                    \"SEMVER\","
                                      "                    \"ECOSYSTEM\""
                                      "                  ]"
                                      "                },"
                                      "                \"repo\": {"
                                      "                  \"type\": \"string\""
                                      "                },"
                                      "                \"events\": {"
                                      "                  \"title\": \"events must contain an introduced object and may contain fixed, last_affected or limit objects\","
                                      "                  \"type\": \"array\","
                                      "                  \"contains\": {"
                                      "                    \"required\": ["
                                      "                      \"introduced\""
                                      "                    ]"
                                      "                  },"
                                      "                  \"items\": {"
                                      "                    \"type\": \"object\","
                                      "                    \"oneOf\": ["
                                      "                      {"
                                      "                        \"type\": \"object\","
                                      "                        \"properties\": {"
                                      "                          \"introduced\": {"
                                      "                            \"type\": \"string\""
                                      "                          }"
                                      "                        },"
                                      "                        \"required\": ["
                                      "                          \"introduced\""
                                      "                        ]"
                                      "                      },"
                                      "                      {"
                                      "                        \"type\": \"object\","
                                      "                        \"properties\": {"
                                      "                          \"fixed\": {"
                                      "                            \"type\": \"string\""
                                      "                          }"
                                      "                        },"
                                      "                        \"required\": ["
                                      "                          \"fixed\""
                                      "                        ]"
                                      "                      },"
                                      "                      {"
                                      "                        \"type\": \"object\","
                                      "                        \"properties\": {"
                                      "                          \"last_affected\": {"
                                      "                            \"type\": \"string\""
                                      "                          }"
                                      "                        },"
                                      "                        \"required\": ["
                                      "                          \"last_affected\""
                                      "                        ]"
                                      "                      },"
                                      "                      {"
                                      "                        \"type\": \"object\","
                                      "                        \"properties\": {"
                                      "                          \"limit\": {"
                                      "                            \"type\": \"string\""
                                      "                          }"
                                      "                        },"
                                      "                        \"required\": ["
                                      "                          \"limit\""
                                      "                        ]"
                                      "                      }"
                                      "                    ]"
                                      "                  },"
                                      "                  \"minItems\": 1"
                                      "                },"
                                      "                \"database_specific\": {"
                                      "                  \"type\": \"object\""
                                      "                }"
                                      "              },"
                                      "              \"allOf\": ["
                                      "                {"
                                      "                  \"title\": \"GIT ranges require a repo\","
                                      "                  \"if\": {"
                                      "                    \"properties\": {"
                                      "                      \"type\": {"
                                      "                        \"const\": \"GIT\""
                                      "                      }"
                                      "                    }"
                                      "                  },"
                                      "                  \"then\": {"
                                      "                    \"required\": ["
                                      "                      \"repo\""
                                      "                    ]"
                                      "                  }"
                                      "                },"
                                      "                {"
                                      "                  \"title\": \"last_affected and fixed events are mutually exclusive\","
                                      "                  \"if\": {"
                                      "                    \"properties\": {"
                                      "                      \"events\": {"
                                      "                        \"contains\": {"
                                      "                          \"required\": ["
                                      "                            \"last_affected\""
                                      "                          ]"
                                      "                        }"
                                      "                      }"
                                      "                    }"
                                      "                  },"
                                      "                  \"then\": {"
                                      "                    \"not\": {"
                                      "                      \"properties\": {"
                                      "                        \"events\": {"
                                      "                          \"contains\": {"
                                      "                            \"required\": ["
                                      "                              \"fixed\""
                                      "                            ]"
                                      "                          }"
                                      "                        }"
                                      "                      }"
                                      "                    }"
                                      "                  }"
                                      "                }"
                                      "              ],"
                                      "              \"required\": ["
                                      "                \"type\","
                                      "                \"events\""
                                      "              ]"
                                      "            }"
                                      "          },"
                                      "          \"versions\": {"
                                      "            \"type\": \"array\","
                                      "            \"items\": {"
                                      "              \"type\": \"string\""
                                      "            }"
                                      "          },"
                                      "          \"ecosystem_specific\": {"
                                      "            \"type\": \"object\""
                                      "          },"
                                      "          \"database_specific\": {"
                                      "            \"type\": \"object\""
                                      "          }"
                                      "        }"
                                      "      }"
                                      "    },"
                                      "    \"references\": {"
                                      "      \"type\": ["
                                      "        \"array\","
                                      "        \"null\""
                                      "      ],"
                                      "      \"items\": {"
                                      "        \"type\": \"object\","
                                      "        \"properties\": {"
                                      "          \"type\": {"
                                      "            \"type\": \"string\","
                                      "            \"enum\": ["
                                      "              \"ADVISORY\","
                                      "              \"ARTICLE\","
                                      "              \"DETECTION\","
                                      "              \"DISCUSSION\","
                                      "              \"REPORT\","
                                      "              \"FIX\","
                                      "              \"INTRODUCED\","
                                      "              \"GIT\","
                                      "              \"PACKAGE\","
                                      "              \"EVIDENCE\","
                                      "              \"WEB\""
                                      "            ]"
                                      "          },"
                                      "          \"url\": {"
                                      "            \"type\": \"string\","
                                      "            \"format\": \"uri\""
                                      "          }"
                                      "        },"
                                      "        \"required\": ["
                                      "          \"type\","
                                      "          \"url\""
                                      "        ]"
                                      "      }"
                                      "    },"
                                      "    \"credits\": {"
                                      "      \"type\": \"array\","
                                      "      \"items\": {"
                                      "        \"type\": \"object\","
                                      "        \"properties\": {"
                                      "          \"name\": {"
                                      "            \"type\": \"string\""
                                      "          },"
                                      "          \"contact\": {"
                                      "            \"type\": \"array\","
                                      "            \"items\": {"
                                      "              \"type\": \"string\""
                                      "            }"
                                      "          },"
                                      "          \"type\": {"
                                      "            \"type\": \"string\","
                                      "            \"enum\": ["
                                      "              \"FINDER\","
                                      "              \"REPORTER\","
                                      "              \"ANALYST\","
                                      "              \"COORDINATOR\","
                                      "              \"REMEDIATION_DEVELOPER\","
                                      "              \"REMEDIATION_REVIEWER\","
                                      "              \"REMEDIATION_VERIFIER\","
                                      "              \"TOOL\","
                                      "              \"SPONSOR\","
                                      "              \"OTHER\""
                                      "            ]"
                                      "          }"
                                      "        },"
                                      "        \"required\": ["
                                      "          \"name\""
                                      "        ]"
                                      "      }"
                                      "    },"
                                      "    \"database_specific\": {"
                                      "      \"type\": \"object\""
                                      "    }"
                                      "  },"
                                      "  \"required\": ["
                                      "    \"id\","
                                      "    \"modified\""
                                      "  ],"
                                      "  \"allOf\": ["
                                      "    {"
                                      "      \"if\": {"
                                      "        \"required\": ["
                                      "          \"severity\""
                                      "        ]"
                                      "      },"
                                      "      \"then\": {"
                                      "        \"properties\": {"
                                      "          \"affected\": {"
                                      "            \"items\": {"
                                      "              \"properties\": {"
                                      "                \"severity\": {"
                                      "                  \"type\": \"null\""
                                      "                }"
                                      "              }"
                                      "            }"
                                      "          }"
                                      "        }"
                                      "      }"
                                      "    }"
                                      "  ],"
                                      "  \"$defs\": {"
                                      "    \"ecosystemName\": {"
                                      "      \"type\": \"string\","
                                      "      \"title\": \"Currently supported ecosystems\","
                                      "      \"description\": \"These ecosystems are also documented at https://ossf.github.io/osv-schema/#affectedpackage-field\","
                                      "      \"enum\": ["
                                      "        \"AlmaLinux\","
                                      "        \"Alpaquita\","
                                      "        \"Alpine\","
                                      "        \"Android\","
                                      "        \"BellSoft Hardened Containers\","
                                      "        \"Bioconductor\","
                                      "        \"Bitnami\","
                                      "        \"Chainguard\","
                                      "        \"CleanStart\","
                                      "        \"ConanCenter\","
                                      "        \"CRAN\","
                                      "        \"crates.io\","
                                      "        \"Debian\","
                                      "        \"Docker Hardened Images\","
                                      "        \"Echo\","
                                      "        \"FreeBSD\","
                                      "        \"GHC\","
                                      "        \"GitHub Actions\","
                                      "        \"Go\","
                                      "        \"Hackage\","
                                      "        \"Hex\","
                                      "        \"Julia\","
                                      "        \"Kubernetes\","
                                      "        \"Linux\","
                                      "        \"Mageia\","
                                      "        \"Maven\","
                                      "        \"MinimOS\","
                                      "        \"npm\","
                                      "        \"NuGet\","
                                      "        \"opam\","
                                      "        \"openEuler\","
                                      "        \"openSUSE\","
                                      "        \"OSS-Fuzz\","
                                      "        \"Packagist\","
                                      "        \"Photon OS\","
                                      "        \"Pub\","
                                      "        \"PyPI\","
                                      "        \"Red Hat\","
                                      "        \"Rocky Linux\","
                                      "        \"Root\","
                                      "        \"RubyGems\","
                                      "        \"SUSE\","
                                      "        \"SwiftURL\","
                                      "        \"Ubuntu\","
                                      "        \"VSCode\","
                                      "        \"Wolfi\""
                                      "      ]"
                                      "    },"
                                      "    \"ecosystemSuffix\": {"
                                      "      \"type\": \"string\","
                                      "      \"pattern\": \":.+\""
                                      "    },"
                                      "    \"ecosystemWithSuffix\": {"
                                      "      \"type\": \"string\","
                                      "      \"title\": \"Currently supported ecosystems\","
                                      "      \"description\": \"These ecosystems are also documented at https://ossf.github.io/osv-schema/#affectedpackage-field\","
                                      "      \"pattern\": \"^(AlmaLinux|Alpaquita|Alpine|Android|BellSoft Hardened Containers|Bioconductor|Bitnami|Chainguard|CleanStart|ConanCenter|CRAN|crates\\.io|Debian|Docker Hardened Images|Echo|FreeBSD|GHC|GitHub Actions|Go|Hackage|Hex|Julia|Kubernetes|Linux|Mageia|Maven|MinimOS|npm|NuGet|opam|openEuler|openSUSE|OSS-Fuzz|Packagist|Photon OS|Pub|PyPI|Red Hat|Rocky Linux|Root|RubyGems|SUSE|SwiftURL|Ubuntu|VSCode|Wolfi|GIT)(:.+)?$\""
                                      "    },"
                                      "    \"prefix\": {"
                                      "      \"type\": \"string\","
                                      "      \"title\": \"Currently supported home database identifier prefixes\","
                                      "      \"description\": \"These home databases are also documented at https://ossf.github.io/osv-schema/#id-modified-fields\","
                                      "      \"pattern\": \"^(ASB-A|PUB-A|ALPINE|ALSA|ALBA|ALEA|BELL|BIT|CGA|CLEANSTART|CURL|CVE|DEBIAN|DHI|DRUPAL|DSA|DLA|ELA|DTSA|ECHO|EEF|FreeBSD|GHSA|GO|GSD|HSEC|JLSEC|KUBE|LBSEC|LSN|MAL|MINI|MGASA|OESA|OSEC|OSV|openSUSE-SU|PHSA|PSF|PYSEC|RHBA|RHEA|RHSA|RLSA|RXSA|RSEC|ROOT|RUSTSEC|SUSE-[SRFO]U|UBUNTU|USN|V8)-\""
                                      "    },"
                                      "    \"severity\": {"
                                      "      \"type\": ["
                                      "        \"array\","
                                      "        \"null\""
                                      "      ],"
                                      "      \"items\": {"
                                      "        \"type\": \"object\","
                                      "        \"properties\": {"
                                      "          \"type\": {"
                                      "            \"type\": \"string\","
                                      "            \"enum\": ["
                                      "              \"CVSS_V2\","
                                      "              \"CVSS_V3\","
                                      "              \"CVSS_V4\","
                                      "              \"Ubuntu\""
                                      "            ]"
                                      "          },"
                                      "          \"score\": {"
                                      "            \"type\": \"string\""
                                      "          }"
                                      "        },"
                                      "        \"allOf\": ["
                                      "          {"
                                      "            \"if\": {"
                                      "              \"properties\": {"
                                      "                \"type\": {"
                                      "                  \"const\": \"CVSS_V2\""
                                      "                }"
                                      "              }"
                                      "            },"
                                      "            \"then\": {"
                                      "              \"properties\": {"
                                      "                \"score\": {"
                                      "                  \"pattern\": \"^((AV:[NAL]|AC:[LMH]|Au:[MSN]|[CIA]:[NPC]|E:(U|POC|F|H|ND)|RL:(OF|TF|W|U|ND)|RC:(UC|UR|C|ND)|CDP:(N|L|LM|MH|H|ND)|TD:(N|L|M|H|ND)|[CIA]R:(L|M|H|ND))/)*(AV:[NAL]|AC:[LMH]|Au:[MSN]|[CIA]:[NPC]|E:(U|POC|F|H|ND)|RL:(OF|TF|W|U|ND)|RC:(UC|UR|C|ND)|CDP:(N|L|LM|MH|H|ND)|TD:(N|L|M|H|ND)|[CIA]R:(L|M|H|ND))$\""
                                      "                }"
                                      "              }"
                                      "            }"
                                      "          },"
                                      "          {"
                                      "            \"if\": {"
                                      "              \"properties\": {"
                                      "                \"type\": {"
                                      "                  \"const\": \"CVSS_V3\""
                                      "                }"
                                      "              }"
                                      "            },"
                                      "            \"then\": {"
                                      "              \"properties\": {"
                                      "                \"score\": {"
                                      "                  \"pattern\": \"^CVSS:3[.][01]/((AV:[NALP]|AC:[LH]|PR:[NLH]|UI:[NR]|S:[UC]|[CIA]:[NLH]|E:[XUPFH]|RL:[XOTWU]|RC:[XURC]|[CIA]R:[XLMH]|MAV:[XNALP]|MAC:[XLH]|MPR:[XNLH]|MUI:[XNR]|MS:[XUC]|M[CIA]:[XNLH])/)*(AV:[NALP]|AC:[LH]|PR:[NLH]|UI:[NR]|S:[UC]|[CIA]:[NLH]|E:[XUPFH]|RL:[XOTWU]|RC:[XURC]|[CIA]R:[XLMH]|MAV:[XNALP]|MAC:[XLH]|MPR:[XNLH]|MUI:[XNR]|MS:[XUC]|M[CIA]:[XNLH])$\""
                                      "                }"
                                      "              }"
                                      "            }"
                                      "          },"
                                      "          {"
                                      "            \"if\": {"
                                      "              \"properties\": {"
                                      "                \"type\": {"
                                      "                  \"const\": \"CVSS_V4\""
                                      "                }"
                                      "              }"
                                      "            },"
                                      "            \"then\": {"
                                      "              \"properties\": {"
                                      "                \"score\": {"
                                      "                  \"pattern\": \"^CVSS:4[.]0/AV:[NALP]/AC:[LH]/AT:[NP]/PR:[NLH]/UI:[NPA]/VC:[HLN]/VI:[HLN]/VA:[HLN]/SC:[HLN]/SI:[HLN]/SA:[HLN](/E:[XAPU])?(/CR:[XHML])?(/IR:[XHML])?(/AR:[XHML])?(/MAV:[XNALP])?(/MAC:[XLH])?(/MAT:[XNP])?(/MPR:[XNLH])?(/MUI:[XNPA])?(/MVC:[XNLH])?(/MVI:[XNLH])?(/MVA:[XNLH])?(/MSC:[XNLH])?(/MSI:[XNLHS])?(/MSA:[XNLHS])?(/S:[XNP])?(/AU:[XNY])?(/R:[XAUI])?(/V:[XDC])?(/RE:[XLMH])?(/U:(X|Clear|Green|Amber|Red))?$\""
                                      "                }"
                                      "              }"
                                      "            }"
                                      "          },"
                                      "          {"
                                      "            \"if\": {"
                                      "              \"properties\": {"
                                      "                \"type\": {"
                                      "                  \"const\": \"Ubuntu\""
                                      "                }"
                                      "              }"
                                      "            },"
                                      "            \"then\": {"
                                      "              \"properties\": {"
                                      "                \"score\": {"
                                      "                  \"enum\": ["
                                      "                    \"negligible\","
                                      "                    \"low\","
                                      "                    \"medium\","
                                      "                    \"high\","
                                      "                    \"critical\""
                                      "                  ]"
                                      "                }"
                                      "              }"
                                      "            }"
                                      "          }"
                                      "        ],"
                                      "        \"required\": ["
                                      "          \"type\","
                                      "          \"score\""
                                      "        ]"
                                      "      }"
                                      "    },"
                                      "    \"timestamp\": {"
                                      "      \"type\": \"string\","
                                      "      \"format\": \"date-time\","
                                      "      \"pattern\": \"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(\\.[0-9]+)?Z\""
                                      "    }"
                                      "  },"
                                      "  \"additionalProperties\": false"
                                      "}";

struct pkg_osvf_hash
{
	unsigned int value;
	char *name;
};

static struct pkg_osvf_hash references_global[] =
{
	{OSVF_REFERENCE_ADVISORY, "ADVISORY"},
	{OSVF_REFERENCE_ARTICLE, "ARTICLE"},
	{OSVF_REFERENCE_DETECTION, "DETECTION"},
	{OSVF_REFERENCE_DISCUSSION, "DISCUSSION"},
	{OSVF_REFERENCE_REPORT, "REPORT"},
	{OSVF_REFERENCE_FIX, "FIX"},
	{OSVF_REFERENCE_INTRODUCED, "INTRODUCED"},
	{OSVF_REFERENCE_PACKAGE, "PACKAGE"},
	{OSVF_REFERENCE_EVIDENCE, "EVIDENCE"},
	{OSVF_REFERENCE_WEB, "WEB"},
	{OSVF_REFERENCE_UNKNOWN, NULL}
};

static struct pkg_osvf_hash event_global[] =
{
	{OSVF_EVENT_VERSION_SEMVER, "SEMVER"},
	{OSVF_EVENT_VERSION_ECOSYSTEM, "ECOSYSTEM"},
	{OSVF_EVENT_VERSION_GIT, "GIT"},
	{OSVF_EVENT_VERSION_UNKNOWN, NULL}
};

static ucl_object_t *
create_schema_obj(void)
{
	struct ucl_parser *uclparser;
	ucl_object_t *obj = NULL;

	uclparser = ucl_parser_new (0);
	ucl_parser_add_string(uclparser, osvf_schema_str, 0);
	if (ucl_parser_get_error(uclparser) != NULL)
	{
		pkg_emit_error("Error occurred: %s\n", ucl_parser_get_error (uclparser));
		ucl_parser_free (uclparser);
		return (NULL);
	}

	obj = ucl_parser_get_object(uclparser);
	ucl_parser_free(uclparser);
	return obj;
}


ucl_object_t *
pkg_osvf_open(const char *location)
{
	ucl_object_t *obj = NULL;
	int fd;
	ucl_object_t *schema = NULL;
	struct ucl_schema_error err;

	fd = open(location, O_RDONLY);
	if (fd == -1)
	{
		pkg_emit_error("Unable to open OSVF file: %s", location);
		return (NULL);
	}

	obj = ucl_parse_fd(fd, location);
	close(fd);

	if (obj == NULL)
	{
		pkg_emit_error("UCL definition %s cannot be validated: %s",
		               location, err.msg);
		return (NULL);
	}

	schema = create_schema_obj();

	if (schema == NULL)
	{
		return (NULL);
	}

	if (!ucl_object_validate(schema, obj, &err))
	{
		pkg_emit_error("UCL definition %s cannot be validated: %s",
		               location, err.msg);
		ucl_object_unref(schema);
		ucl_object_unref(obj);
		return (NULL);
	}

	ucl_object_unref(schema);

	return (obj);
}

void
pkg_osvf_free_ecosystem(struct pkg_audit_ecosystem *ecosystem)
{
	if(!ecosystem)
	{
		return;
	}

	free(ecosystem->original);
	ecosystem->original = NULL;

	free(ecosystem->name);
	ecosystem->name = NULL;


	ucl_object_unref(ecosystem->params);
	ecosystem->params = NULL;

	free(ecosystem);
}

static void
pkg_osvf_free_reference(struct pkg_osvf_reference *reference)
{
	if(!reference)
	{
		return;
	}

	free(reference->url);
	reference->url = NULL;

	pkg_osvf_free_reference(reference->next);
	reference->next = NULL;

	free(reference);
}

static void
pkg_osvf_free_audit_entry(struct pkg_audit_entry *e)
{
	vec_foreach(e->packages, pi) {
		struct pkg_audit_package *p = &e->packages.d[pi];
		vec_foreach(p->names, ni)
			free(p->names.d[ni].pkgname);
		vec_free(&p->names);
		vec_foreach(p->versions, vi) {
			free(p->versions.d[vi].v1.version);
			free(p->versions.d[vi].v2.version);
		}
		vec_free(&p->versions);
		pkg_osvf_free_ecosystem(p->ecosystem);
	}
	vec_free(&e->packages);
	vec_foreach(e->cve, ci)
		free(e->cve.d[ci].cvename);
	vec_free(&e->cve);
	free(e->url);
	free(e->desc);
	free(e->id);
}

void
pkg_osvf_free_entry(struct pkg_osvf_entry *entry)
{
	if(!entry)
	{
		return;
	}

	pkg_osvf_free_audit_entry(&entry->audit);
	pkg_osvf_free_reference(entry->references);

	free(entry);
}

static struct pkghash *
pkg_osvf_create_seek_hash(struct pkg_osvf_hash *osvf_ptr)
{
	struct pkghash *hash_table = pkghash_new();

	while(osvf_ptr->name)
	{
		pkghash_add(hash_table, osvf_ptr->name, osvf_ptr, NULL);
		osvf_ptr ++;
	}

	return hash_table;
}

static unsigned int
pkg_osvf_get_hash(const char *key, struct pkg_osvf_hash *global, unsigned int unknow)
{
	struct pkghash *hash = NULL;
	pkghash_entry *entry = NULL;
	struct pkg_osvf_hash *rtn_struct = NULL;
	unsigned int rtn_value = unknow;

	if(!key)
	{
		return rtn_value;
	}

	/*
	 * Create seek table with struct
	 * Make easier to seek
	 */
	hash = pkg_osvf_create_seek_hash(global);

	entry = pkghash_get(hash, key);

	/*
	 * If there was key then get it and
	 * free hash table as we don't need it anymore
	 */
	if(entry)
	{
		rtn_struct = (struct pkg_osvf_hash *) entry->value;
		rtn_value = rtn_struct->value;
		pkghash_destroy(hash);
	}

	return rtn_value;
}


struct pkg_audit_ecosystem *
pkg_osvf_get_ecosystem(const char *ecosystem)
{
	char ecosystem_delimiter[] = ":";
	char *ecosystem_copy = NULL;
	char *ecosystem_token = NULL;
	struct pkg_audit_ecosystem *rtn_ecosystem = NULL;

	if(!ecosystem)
	{
		return NULL;
	}

	ecosystem_copy = xstrdup(ecosystem);
	ecosystem_token = strtok(ecosystem_copy, ecosystem_delimiter);

	if(!ecosystem_token)
	{
		free(ecosystem_copy);
		return NULL;
	}

	rtn_ecosystem = xcalloc(1, sizeof(struct pkg_audit_ecosystem));
	rtn_ecosystem->original = xstrdup(ecosystem);
	rtn_ecosystem->name = xstrdup(ecosystem_token);
	rtn_ecosystem->params = ucl_object_typed_new(UCL_ARRAY);

	/*
	 * Parse other information out of Ecosystem tags like
	 * Alpine:v3.16
	 * FreeBSD:ports
	 * FreeBSD:kernel:14.3
	 * FreeBSD:src:14.3
	 * Mageia:9
	 * Maven:https://repo1.maven.org/maven2/
	 * Photon OS:3.0
	 * Red Hat:rhel_aus:8.4::appstream
	 * Ubuntu:22.04:LTS
	 * Ubuntu:Pro:18.04:LTS
	 * to array for more processing further
	 */
	while(ecosystem_token)
	{
		ecosystem_token = strtok(NULL, ecosystem_delimiter);
		if(ecosystem_token)
		{
			ucl_array_append(rtn_ecosystem->params, ucl_object_fromstring(ecosystem_token));
		}
	}

	free(ecosystem_copy);
	ecosystem_copy = NULL;

	return rtn_ecosystem;
}

unsigned int
pkg_osvf_get_reference(const char *reference_type)
{
	return pkg_osvf_get_hash(reference_type, references_global, OSVF_REFERENCE_UNKNOWN);
}

unsigned int
pkg_osvf_get_event(const char *event_type)
{
	return pkg_osvf_get_hash(event_type, event_global, OSVF_EVENT_VERSION_UNKNOWN);
}


static const char *
pkg_osvf_ucl_string(const ucl_object_t *obj, const char *key)
{
	const ucl_object_t *key_obj = ucl_object_find_key(obj, key);

	if(key_obj && ucl_object_type(key_obj) == UCL_STRING)
	{
		return ucl_object_tostring(key_obj);
	}

	return "";
}

static void
pkg_osvf_parse_package(struct pkg_audit_package *package, const ucl_object_t *package_obj)
{
	/* Parses package structure:
	   "package": {
	     "ecosystem": "FreeBSD:ports",
	     "name": "packagename"
	    },
	*/

	if(!package_obj || ucl_object_type(package_obj) != UCL_OBJECT)
	{
		return;
	}

	vec_push(&package->names,
	    ((struct pkg_audit_pkgname){ .pkgname = xstrdup(pkg_osvf_ucl_string(package_obj, "name")) }));
	package->ecosystem = pkg_osvf_get_ecosystem(pkg_osvf_ucl_string(package_obj, "ecosystem"));
}

static void
pkg_osvf_parse_events(struct pkg_audit_versions_range *range, const ucl_object_t *event_array, const char *type)
{
	ucl_object_iter_t it = NULL;
	const ucl_object_t *cur = NULL;

	if(!event_array || ucl_object_type(event_array) != UCL_ARRAY)
	{
		return;
	}

	if(!type)
	{
		return;
	}

	range->type = pkg_osvf_get_event(type);

	/* Parses package structure from events:
	   {
	     "fixed|introduced": "1.0.0"
	   }
	*/

	while ((cur = ucl_iterate_object(event_array, &it, true)))
	{
		if(ucl_object_find_key(cur, "fixed"))
		{
			range->v2.version = xstrdup(pkg_osvf_ucl_string(cur, "fixed"));
			printf("Fixed: %s\n", range->v2.version);
			range->v2.type = OSVF_EVENT_FIXED;
		}
		else if(ucl_object_find_key(cur, "introduced"))
		{
			range->v1.version = xstrdup(pkg_osvf_ucl_string(cur, "introduced"));
			printf("Intro: %s\n", range->v1.version);
			range->v1.type = OSVF_EVENT_INTRODUCED;
		}
	}
}


static void
pkg_osvf_parse_ranges(audit_versv_t *versions, const ucl_object_t *range_array)
{
	ucl_object_iter_t it = NULL;
	const ucl_object_t *cur = NULL;
	const ucl_object_t *sub_obj = NULL;

	if(!range_array || ucl_object_type(range_array) != UCL_ARRAY)
	{
		return;
	}

	/* Parses events structure
	   [
	   "type": "SEMVER",
	   "events": [
	     {
	       "fixed": "1.0.0"
	     },
	     {
	     "introduced": "0.0.1"
	     },
	   ]
	*/

	while ((cur = ucl_iterate_object(range_array, &it, true)))
	{
		vec_push(versions, ((struct pkg_audit_versions_range){0}));
		struct pkg_audit_versions_range *range =
		    &versions->d[versions->len - 1];

		sub_obj = ucl_object_find_key(cur, "events");

		if(sub_obj && ucl_object_type(sub_obj) == UCL_ARRAY)
		{
			pkg_osvf_parse_events(range, ucl_object_find_key(cur, "events"), pkg_osvf_ucl_string(cur, "type"));
		}
	}
}

static void
pkg_osvf_parse_reference(struct pkg_osvf_reference *ref, const ucl_object_t *ref_obj)
{
	if(!ref_obj || ucl_object_type(ref_obj) != UCL_OBJECT)
	{
		return;
	}

	/*
	   Parses reference to struct
	   {
	     "type": "ADVISORY",
	     "url": "https://www.freebsd.org/"
	   }
	*/
	ref->url = xstrdup(pkg_osvf_ucl_string(ref_obj, "url"));
	ref->type = pkg_osvf_get_reference(pkg_osvf_ucl_string(ref_obj, "type"));
}

static void
pkg_osvf_parse_references(struct pkg_osvf_entry *oentry, const ucl_object_t *ref_obj)
{
	ucl_object_iter_t it = NULL;
	const ucl_object_t *cur = NULL;
	bool is_first = true;
	struct pkg_osvf_reference *reference = oentry->references;
	struct pkg_osvf_reference *next_ref = NULL;


	if(!ref_obj || ucl_object_type(ref_obj) != UCL_ARRAY)
	{
		return;
	}

	/*
	   Parses references array to linked list
	   "references": [
	     {
	       "type": "ADVISORY",
	       "url": "https://www.freebsd.org/"
	     }
	    ]
	*/

	while ((cur = ucl_iterate_object(ref_obj, &it, true)))
	{
		if (!is_first)
		{
			next_ref = xcalloc(1, sizeof(struct pkg_osvf_reference));
			reference->next = next_ref;
			reference = next_ref;
		}

		if(ucl_object_type(cur) == UCL_OBJECT)
		{
			pkg_osvf_parse_reference(reference, cur);
		}

		is_first = false;
	}

}

static void
pkg_osvf_parse_affected(struct pkg_audit_entry *entry, const ucl_object_t *aff_obj)
{
	ucl_object_iter_t it = NULL;
	const ucl_object_t *cur = NULL;
	const ucl_object_t *array_obj = NULL;

	if(!aff_obj || ucl_object_type(aff_obj) != UCL_ARRAY)
	{
		return;
	}

	/* Parse affected to correct structs
	   "affected": [
	     {
	       "package": {
	       "ecosystem": "FreeBSD:ports",
	       "name": "osvf-test-package10"
	     },
	   "ranges": [
	     {
	       "type": "SEMVER",
	       "events": [
	        {
	          "fixed": "1.0.0"
	        },
	        {
	          "introduced": "0.0.1"
	        },
	      ]
	     }
	     ]
	    }
	   ]
	*/
	while ((cur = ucl_iterate_object(aff_obj, &it, true)))
	{
		vec_push(&entry->packages, ((struct pkg_audit_package){0}));
		struct pkg_audit_package *package =
		    &entry->packages.d[entry->packages.len - 1];

		array_obj = ucl_object_find_key(cur, "package");

		if(array_obj && ucl_object_type(aff_obj) == UCL_ARRAY)
		{
			pkg_osvf_parse_package(package, array_obj);
		}

		array_obj = ucl_object_find_key(cur, "ranges");

		if(array_obj && ucl_object_type(array_obj) == UCL_ARRAY)
		{
			pkg_osvf_parse_ranges(&package->versions, array_obj);
		}
	}
}

static void
pkg_osvf_print_version_type(struct pkg_audit_versions_range *versions)
{
	if(!versions)
	{
		return;
	}

	printf("\t\tVersion type: ");
	switch(versions->type)
	{
	case OSVF_EVENT_VERSION_UNKNOWN:
		printf("UNKNOWN\n");
		break;
	case OSVF_EVENT_VERSION_SEMVER:
		printf("Semantic Version 2.0\n");
		break;
	case OSVF_EVENT_VERSION_ECOSYSTEM:
		printf("Ecosystem\n");
		break;
	case OSVF_EVENT_VERSION_GIT:
		printf("Git hash\n");
		break;
	}
}

static void
pkg_osvf_print_version(struct pkg_audit_version *version)
{
	if(!version)
	{
		return;
	}

	switch(version->type)
	{
	case OSVF_EVENT_UNKNOWN:
		printf("\t\tUnknown type: ");
		break;
	case OSVF_EVENT_INTRODUCED:
		printf("\t\tIntroduced: ");
		break;
	case OSVF_EVENT_FIXED:
		printf("\t\tFixed: ");
		break;
	case OSVF_EVENT_LAST_AFFECTED:
		printf("\t\tAffected: ");
		break;
	case OSVF_EVENT_LIMIT:
		printf("\t\tLimit: ");
		break;
	}

	printf("%s\n", version->version);
}

static void
pkg_osvf_print_ecosystem(struct pkg_audit_ecosystem *ecosystem)
{
	ucl_object_iter_t it = NULL;
	const ucl_object_t *cur = NULL;
	int loc = 0;

	if(!ecosystem)
	{
		return;
	}

	printf("\t\tEcosystem: ");

	if(ecosystem->name)
	{
		printf("%s (", ecosystem->name);
	}

	while ((cur = ucl_iterate_object(ecosystem->params, &it, true)))
	{
		if(loc)
		{
			printf(",");
		}

		if(ucl_object_type(cur) == UCL_STRING)
		{
			printf("%s", ucl_object_tostring(cur));
		}

		loc ++;
	}

	printf(")\n");
}

void
pkg_osvf_print_entry(struct pkg_osvf_entry *oentry)
{
	char buf[255];
	struct pkg_audit_entry *entry = &oentry->audit;
	struct pkg_osvf_reference *references;

	if(!oentry)
	{
		return;
	}

	printf("OSVF Vulnerability information:\n");

	/* Print first package name as the primary name */
	if (entry->packages.len > 0 && entry->packages.d[0].names.len > 0)
		printf("\tPackage name: %s\n", entry->packages.d[0].names.d[0].pkgname);

	/* Print all package names */
	printf("\tPackage names: ");
	{
		bool is_first = true;
		vec_foreach(entry->packages, pi) {
			struct pkg_audit_package *p = &entry->packages.d[pi];
			vec_foreach(p->names, ni) {
				if (!is_first)
					printf(", ");
				printf("%s", p->names.d[ni].pkgname);
				is_first = false;
			}
		}
	}

	printf("\n");

	printf("\tPackage id: %s\n", entry->id);
	printf("\tPackage description: %s\n", entry->desc);
	printf("\tPackage url: %s\n", entry->url);

	strftime(buf, sizeof(buf), "%d %b %Y %H:%M", &entry->discovery);
	printf("\tEntry discovered: %s\n", buf);

	strftime(buf, sizeof(buf), "%d %b %Y %H:%M", &entry->published);
	printf("\tEntry published: %s\n", buf);

	strftime(buf, sizeof(buf), "%d %b %Y %H:%M", &entry->modified);
	printf("\tEntry modified: %s\n", buf);

	printf("Vulnerable versions:\n");

	/* Print all versions from all packages */
	vec_foreach(entry->packages, pi) {
		struct pkg_audit_package *p = &entry->packages.d[pi];
		vec_foreach(p->versions, vi) {
			struct pkg_audit_versions_range *vers = &p->versions.d[vi];
			pkg_osvf_print_version_type(vers);
			pkg_osvf_print_version(&vers->v1);
			pkg_osvf_print_version(&vers->v2);
		}
	}

	printf("Vulnerable packages:\n");

	vec_foreach(entry->packages, pi) {
		struct pkg_audit_package *p = &entry->packages.d[pi];
		if (p->names.len > 0)
			printf("\tPackage name: %s\n", p->names.d[0].pkgname);
		pkg_osvf_print_ecosystem(p->ecosystem);
		vec_foreach(p->versions, vi) {
			struct pkg_audit_versions_range *vers = &p->versions.d[vi];
			pkg_osvf_print_version_type(vers);
			pkg_osvf_print_version(&vers->v1);
			pkg_osvf_print_version(&vers->v2);
		}
	}

	printf("Vulnerability references:\n");

	references = oentry->references;

	while(references)
	{
		switch(references->type)
		{
		case OSVF_REFERENCE_UNKNOWN:
			printf("\tUNKNOWN: %s\n", references->url);
			break;
		case OSVF_REFERENCE_ADVISORY:
			printf("\tADVISORY: %s\n", references->url);
			break;
		case OSVF_REFERENCE_ARTICLE:
			printf("\tARTICLE: %s\n", references->url);
			break;
		case OSVF_REFERENCE_DETECTION:
			printf("\tDETECTION: %s\n", references->url);
			break;
		case OSVF_REFERENCE_DISCUSSION:
			printf("\tDISCUSSIONn: %s\n", references->url);
			break;
		case OSVF_REFERENCE_REPORT:
			printf("\tREPORT: %s\n", references->url);
			break;
		case OSVF_REFERENCE_FIX:
			printf("\tFIX: %s\n", references->url);
			break;
		case OSVF_REFERENCE_INTRODUCED:
			printf("\tINTRODUCED: %s\n", references->url);
			break;
		case OSVF_REFERENCE_PACKAGE:
			printf("\tPACKAGE: %s\n", references->url);
			break;
		case OSVF_REFERENCE_EVIDENCE:
			printf("\tEVIDENCE: %s\n", references->url);
			break;
		case OSVF_REFERENCE_WEB:
			printf("\tWEB: %s\n", references->url);
			break;
		}
		references = references->next;
	}
}

struct pkg_osvf_entry *
pkg_osvf_create_entry(ucl_object_t *osvf_obj)
{
	struct pkg_osvf_entry *oentry = NULL;
	struct pkg_audit_entry *entry = NULL;
	const ucl_object_t *sub_obj = NULL;
	/* Date format is in RFC3339 */
	const char *date_time_str = "%Y-%m-%dT%H:%M:%SZ";

	/* We are probably out of memory or JSON does not exist */
	if(osvf_obj == NULL)
	{
		return NULL;
	}

	oentry = xcalloc(1, sizeof(struct pkg_osvf_entry));
	entry = &oentry->audit;
	oentry->references = xcalloc(1, sizeof(struct pkg_osvf_reference));

	memset(&entry->modified, 0, sizeof(struct tm));
	memset(&entry->published, 0, sizeof(struct tm));
	memset(&entry->discovery, 0, sizeof(struct tm));

	/* Data has been validated on load so we can assume
	   there is all needed information */

	entry->id = xstrdup(pkg_osvf_ucl_string(osvf_obj, "id"));
	entry->desc = xstrdup(pkg_osvf_ucl_string(osvf_obj, "summary"));

	sub_obj = ucl_object_find_key(osvf_obj, "affected");

	if(sub_obj && ucl_object_type(sub_obj) == UCL_ARRAY)
	{
		pkg_osvf_parse_affected(entry, ucl_object_find_key(osvf_obj, "affected"));
	}
	else
	{
		pkg_osvf_free_entry(oentry);
		return NULL;
	}

	sub_obj = ucl_object_find_key(osvf_obj, "references");

	if(sub_obj && ucl_object_type(sub_obj) == UCL_ARRAY)
	{
		pkg_osvf_parse_references(oentry, ucl_object_find_key(osvf_obj, "references"));
	}
	else
	{
		pkg_osvf_free_entry(oentry);
		return NULL;
	}

	/* Set entry->url from first reference */
	if (oentry->references != NULL && oentry->references->url != NULL)
		entry->url = xstrdup(oentry->references->url);

	if(ucl_object_find_key(osvf_obj, "modified"))
	{
		strptime(ucl_object_tostring(ucl_object_find_key(osvf_obj, "modified")), date_time_str, &entry->modified);
	}

	if(ucl_object_find_key(osvf_obj, "published"))
	{
		strptime(ucl_object_tostring(ucl_object_find_key(osvf_obj, "published")), date_time_str, &entry->published);
	}

	if(ucl_object_find_key(osvf_obj, "database_specific"))
	{
		sub_obj = ucl_object_find_key(osvf_obj, "database_specific");
		if(ucl_object_find_key(sub_obj, "discovery"))
		{
			strptime(ucl_object_tostring(ucl_object_find_key(sub_obj, "discovery")), date_time_str, &entry->discovery);
		}
	}

	return oentry;
}
