/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 *​ Copyright (c) 2025 The FreeBSD Foundation
 *​
 *​ Portions of this software were developed by
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
#include "pkghash.h"

/*
  Open Source Vulnerability format: https://ossf.github.io/osv-schema/
  OSVF schema: https://github.com/ossf/osv-schema/blob/main/validation/schema.json
  OSVF schema version: 1.7.4
  From git version: https://raw.githubusercontent.com/ossf/osv-schema/094e5ca4fdf4b115bbdaaaf519b4c20809661ee2/validation/schema.json
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
                                      "        \"openEuler\","
                                      "        \"openSUSE\","
                                      "        \"OSS-Fuzz\","
                                      "        \"Packagist\","
                                      "        \"Photon OS\","
                                      "        \"Pub\","
                                      "        \"PyPI\","
                                      "        \"Red Hat\","
                                      "        \"Rocky Linux\","
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
                                      "      \"pattern\": \"^(AlmaLinux|Alpaquita|Alpine|Android|BellSoft Hardened Containers|Bioconductor|Bitnami|Chainguard|CleanStart|ConanCenter|CRAN|crates\\.io|Debian|Echo|FreeBSD|GHC|GitHub Actions|Go|Hackage|Hex|Julia|Kubernetes|Linux|Mageia|Maven|MinimOS|npm|NuGet|openEuler|openSUSE|OSS-Fuzz|Packagist|Photon OS|Pub|PyPI|Red Hat|Rocky Linux|RubyGems|SUSE|SwiftURL|Ubuntu|VSCode|Wolfi|GIT)(:.+)?$\""
                                      "    },"
                                      "    \"prefix\": {"
                                      "      \"type\": \"string\","
                                      "      \"title\": \"Currently supported home database identifier prefixes\","
                                      "      \"description\": \"These home databases are also documented at https://ossf.github.io/osv-schema/#id-modified-fields\","
                                      "      \"pattern\": \"^(ASB-A|PUB-A|ALPINE|ALSA|ALBA|ALEA|BELL|BIT|CGA|CURL|CVE|DEBIAN|DRUPAL|DSA|DLA|ELA|DTSA|ECHO|EEF|FreeBSD|GHSA|GO|GSD|HSEC|JLSEC|KUBE|LBSEC|LSN|MAL|MINI|MGASA|OESA|OSV|openSUSE-SU|PHSA|PSF|PYSEC|RHBA|RHEA|RHSA|RLSA|RXSA|RSEC|RUSTSEC|SUSE-[SRFO]U|UBUNTU|USN|V8)-\""
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

struct pkg_osvf_hash references_global[] =
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

struct pkg_osvf_hash event_global[] =
{
	{OSVF_EVENT_VERSION_SEMVER, "SEMVER"},
	{OSVF_EVENT_VERSION_ECOSYSTEM, "ECOSYSTEM"},
	{OSVF_EVENT_VERSION_GIT, "GIT"},
	{OSVF_EVENT_VERSION_UNKNOWN, NULL}
};

static ucl_object_t *
create_schema_obj()
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
	struct ucl_parser *uclparser;
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

	uclparser = ucl_parser_new(0);
	if (!ucl_parser_add_fd(uclparser, fd))
	{
		pkg_emit_error("Error parsing UCL file '%s': %s'",
		               location, ucl_parser_get_error(uclparser));
		ucl_parser_free(uclparser);
		close(fd);
		return (NULL);
	}
	close(fd);

	obj = ucl_parser_get_object(uclparser);
	ucl_parser_free(uclparser);

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

struct pkg_audit_entry *
pkg_osvf_new_entry()
{
	struct pkg_audit_entry *entry = xcalloc(1, sizeof(struct pkg_audit_entry));

	entry->packages = xcalloc(1, sizeof(struct pkg_audit_package));
	entry->names = xcalloc(1, sizeof(struct pkg_audit_pkgname));
	entry->versions = xcalloc(1, sizeof(struct pkg_audit_versions_range));
	entry->cve = xcalloc(1, sizeof(struct pkg_audit_cve));
	entry->references = xcalloc(1, sizeof(struct pkg_audit_reference));

	return entry;
}

void
pkg_osvf_free_pkgname(struct pkg_audit_pkgname *pkgname)
{
	if(!pkgname)
	{
		return;
	}

	free(pkgname->pkgname);
	pkgname->pkgname = NULL;

	pkg_osvf_free_pkgname(pkgname->next);
	pkgname->next = NULL;

	free(pkgname);
}

void
pkg_osvf_free_version(struct pkg_audit_version *ver)
{
	if(!ver)
	{
		return;
	}

	if(ver->version)
	{
		free(ver->version);
		ver->version = NULL;
	}
}

void
pkg_osvf_free_range(struct pkg_audit_versions_range *range)
{
	if(!range)
	{
		return;
	}

	pkg_osvf_free_version(&range->v1);
	pkg_osvf_free_version(&range->v2);

	pkg_osvf_free_range(range->next);
	range->next = NULL;

	free(range);
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

void
pkg_osvf_free_package(struct pkg_audit_package *package)
{
	if(!package)
	{
		return;
	}

	pkg_osvf_free_pkgname(package->names);
	package->names = NULL;

	pkg_osvf_free_range(package->versions);
	package->versions = NULL;

	pkg_osvf_free_ecosystem(package->ecosystem);
	package->ecosystem = NULL;

	pkg_osvf_free_package(package->next);
	package->next = NULL;

	free(package);
}

void
pkg_osvf_free_cve(struct pkg_audit_cve *cve)
{
	if(!cve)
	{
		return;
	}

	if(cve->cvename) {
		free(cve->cvename);
		cve->cvename = NULL;
	}

	pkg_osvf_free_cve(cve->next);
	cve->next = NULL;

	free(cve);
}

void
pkg_osvf_free_reference(struct pkg_audit_reference *reference)
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

void
pkg_osvf_free_entry(struct pkg_audit_entry *entry)
{
	struct pkg_audit_pkgname *names = NULL;
	struct pkg_audit_pkgname *next_names = NULL;

	struct pkg_audit_cve *cve = NULL;

	if(!entry)
	{
		return;
	}

	names = entry->names;
	cve = entry->cve;

	if(entry->id)
	{
		free(entry->id);
		entry->id = NULL;
	}
	if(entry->desc)
	{
		free(entry->desc);
		entry->desc = NULL;
	}

	pkg_osvf_free_range(entry->versions);
	entry->versions = NULL;

	while(names)
	{
		next_names = names->next;
		free(names);
		names = next_names;
	}

	pkg_osvf_free_package(entry->packages);
	entry->packages = NULL;

	pkg_osvf_free_cve(cve);
	entry->cve = NULL;

	pkg_osvf_free_reference(entry->references);
	entry->references = NULL;

	free(entry);
}

struct pkghash *
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

unsigned int
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


const char *
pkg_osvf_ucl_string(const ucl_object_t *obj, char *key)
{
	const ucl_object_t *key_obj = ucl_object_find_key(obj, key);

	if(key_obj && ucl_object_type(key_obj) == UCL_STRING)
	{
		return ucl_object_tostring(key_obj);
	}

	return "";
}

void
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

	package->names->pkgname = xstrdup(pkg_osvf_ucl_string(package_obj, "name"));
	package->ecosystem = pkg_osvf_get_ecosystem(pkg_osvf_ucl_string(package_obj, "ecosystem"));
}

void
pkg_osvf_parse_events(struct pkg_audit_versions_range *range, const ucl_object_t *event_array, const char *type)
{
	ucl_object_iter_t it = NULL;
	const ucl_object_t *cur = NULL;
	struct pkg_audit_versions_range *cur_range = range;

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
			cur_range->v2.version = xstrdup(pkg_osvf_ucl_string(cur, "fixed"));
			cur_range->v2.type = LTE;
			cur_range->v2.osv_type = OSVF_EVENT_FIXED;
		}
		else if(ucl_object_find_key(cur, "introduced"))
		{
			cur_range->v1.version = xstrdup(pkg_osvf_ucl_string(cur, "introduced"));
			cur_range->v1.type = GTE;
			cur_range->v1.osv_type = OSVF_EVENT_INTRODUCED;
		}
	}
}


void
pkg_osvf_parse_ranges(struct pkg_audit_versions_range *range, const ucl_object_t *range_array)
{
	ucl_object_iter_t it = NULL;
	const ucl_object_t *cur = NULL;
	struct pkg_audit_versions_range *next_range = NULL;
	struct pkg_audit_versions_range *cur_range = range;
	const ucl_object_t *sub_obj = NULL;
	bool is_first = true;

	if(!range || !range_array || ucl_object_type(range_array) != UCL_ARRAY)
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
		if(is_first == false)
		{
			next_range = xcalloc(1, sizeof(struct pkg_audit_versions_range));
			cur_range->next = next_range;
			cur_range = next_range;
		}

		sub_obj = ucl_object_find_key(cur, "events");

		if(sub_obj && ucl_object_type(sub_obj) == UCL_ARRAY)
		{
			pkg_osvf_parse_events(cur_range, ucl_object_find_key(cur, "events"), pkg_osvf_ucl_string(cur, "type"));
		}

		is_first = false;
	}
}

void
pkg_osvf_parse_reference(struct pkg_audit_reference *ref, const ucl_object_t *ref_obj)
{
	if(!ref_obj || ucl_object_type(ref_obj) != UCL_OBJECT)
	{
		return;
	}

	/*
	   Parses refrence to struct
	   {
	     "type": "ADVISORY",
	     "url": "https://www.freebsd.org/"
	   }
	*/
	ref->url = xstrdup(pkg_osvf_ucl_string(ref_obj, "url"));
	ref->type = pkg_osvf_get_reference(pkg_osvf_ucl_string(ref_obj, "type"));
}


void
pkg_osvf_parse_cvename(struct pkg_audit_entry *entry, const ucl_object_t *cvename_obj)
{
	ucl_object_iter_t it = NULL;
	const ucl_object_t *cur = NULL;
	bool is_first = true;
	struct pkg_audit_cve *cve = entry->cve;
	struct pkg_audit_cve *next_cve = NULL;

	if(!cvename_obj || ucl_object_type(cvename_obj) != UCL_ARRAY)
	{
		return;
	}

	/*
	    Parses database_spefic CVE entries to linked list
	    "references": {
	        "cvename": [
	            "CVE-2003-0031",
	            "CVE-2003-0032"
	    ]
	*/

	while ((cur = ucl_iterate_object(cvename_obj, &it, true)))
	{
		if(is_first == false)
		{
			next_cve = xcalloc(1, sizeof(struct pkg_audit_reference));
			cve->next = next_cve;
			cve = next_cve;
		}

		if(ucl_object_type(cur) == UCL_STRING)
		{
			cve->cvename = xstrdup(ucl_object_tostring(cur));
		}
		else
		{
			cve->cvename = xstrdup("");
		}

		is_first = false;
	}
}


void
pkg_osvf_parse_references(struct pkg_audit_entry *entry, const ucl_object_t *ref_obj)
{
	ucl_object_iter_t it = NULL;
	const ucl_object_t *cur = NULL;
	bool is_first = true;
	struct pkg_audit_reference *reference = entry->references;
	struct pkg_audit_reference *next_package = NULL;


	if(!ref_obj || ucl_object_type(ref_obj) != UCL_ARRAY)
	{
		return;
	}

	/*
	   Parses refereces array to linked list
	   "references": [
	     {
	       "type": "ADVISORY",
	       "url": "https://www.freebsd.org/"
	     }
	    ]
	*/

	while ((cur = ucl_iterate_object(ref_obj, &it, true)))
	{
		if(is_first == false)
		{
			next_package = xcalloc(1, sizeof(struct pkg_audit_reference));
			reference->next = next_package;
			reference = next_package;
		}

		if(ucl_object_type(cur) == UCL_OBJECT)
		{
			pkg_osvf_parse_reference(reference, cur);
		}

		is_first = false;
	}
}

void
pkg_osvf_parse_affected(struct pkg_audit_entry *entry, const ucl_object_t *aff_obj)
{
	ucl_object_iter_t it = NULL;
	const ucl_object_t *cur = NULL;
	const ucl_object_t *array_obj = NULL;
	bool is_first = true;
	struct pkg_audit_package *package = entry->packages;
	struct pkg_audit_package *next_package = NULL;

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
		if(is_first == false)
		{
			next_package = xcalloc(1, sizeof(struct pkg_audit_package));
			package->next = next_package;
			package = next_package;
		}

		array_obj = ucl_object_find_key(cur, "package");

		if(array_obj && ucl_object_type(aff_obj) == UCL_ARRAY)
		{
			package->names = xcalloc(1, sizeof(struct pkg_audit_pkgname));
			pkg_osvf_parse_package(package, array_obj);
		}

		array_obj = ucl_object_find_key(cur, "ranges");

		if(array_obj && ucl_object_type(array_obj) == UCL_ARRAY)
		{
			package->versions = xcalloc(1, sizeof(struct pkg_audit_versions_range));
			pkg_osvf_parse_ranges(package->versions, array_obj);
		}

		is_first = false;
	}
}

struct pkg_audit_versions_range *
pkg_osvf_append_version_range(struct pkg_audit_versions_range *to, struct pkg_audit_versions_range *from)
{
	struct pkg_audit_versions_range *ptr_from = from;
	struct pkg_audit_versions_range *ptr_to = to;
	struct pkg_audit_versions_range *next_to = NULL;

	if(!to)
	{
		return NULL;
	}

	if(!from)
	{
		return NULL;
	}

	while(ptr_from) {
		ptr_to->v1.osv_type = ptr_from->v1.osv_type;
		ptr_to->v1.type = ptr_from->v1.type;

		if(ptr_from->v1.version) {
			ptr_to->v1.version = xstrdup(ptr_from->v1.version);
		} else {
			ptr_to->v1.version = NULL;
		}

		ptr_to->v2.osv_type = ptr_from->v2.osv_type;
		ptr_to->v2.type = ptr_from->v2.type;

		if(ptr_from->v2.version) {
			ptr_to->v2.version = xstrdup(ptr_from->v2.version);
		} else {
			ptr_to->v2.version = NULL;
		}
		ptr_to->type = ptr_from->type;

		if(ptr_from->next) {
			next_to = xcalloc(1, sizeof(struct pkg_audit_versions_range));
			ptr_to->next = next_to;
			ptr_to = next_to;
		}

		ptr_from = ptr_from->next;
	}

	return ptr_to;
}

void
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
		printf("Sematic Version 2.0\n");
		break;
	case OSVF_EVENT_VERSION_ECOSYSTEM:
		printf("Ecosystem\n");
		break;
	case OSVF_EVENT_VERSION_GIT:
		printf("Git hash\n");
		break;
	}
}

void
pkg_osvf_print_version(struct pkg_audit_version *version)
{
	if(!version)
	{
		return;
	}

	switch(version->osv_type)
	{
	case OSVF_EVENT_UNKNOWN:
		printf("\t\tUnknown type ");
		break;
	case OSVF_EVENT_INTRODUCED:
		printf("\t\tIntroduced ");
		break;
	case OSVF_EVENT_FIXED:
		printf("\t\tFixed ");
		break;
	case OSVF_EVENT_LAST_AFFECTED:
		printf("\t\tAffected ");
		break;
	case OSVF_EVENT_LIMIT:
		printf("\t\tLimit ");
		break;
	}

	switch(version->type)
	{
	case EQ:
		printf("(=): ");
		break;
	case LT:
		printf("(<) ");
		break;
	case LTE:
		printf("(<=): ");
		break;
	case GT:
		printf("(>): ");
		break;
	case GTE:
		printf("(>=): ");
		break;
	}

	printf("%s\n", version->version);
}

void
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
pkg_osvf_print_entry(struct pkg_audit_entry *entry)
{
	char buf[255];
	struct pkg_audit_package *packages = NULL;
	struct pkg_audit_versions_range *versions = NULL;
	struct pkg_audit_reference *references;
	struct pkg_audit_pkgname *names = NULL;
	bool is_first = true;

	if(!entry)
	{
		return;
	}

	names = entry->names;

	printf("OSVF Vulnerability information:\n");
	printf("\tPackage name: %s\n", entry->pkgname);
	printf("\tPackage names: ");
	while(names)
	{
		if(is_first == false)
		{
			printf(", ");
		}
		printf("%s", names->pkgname);
		names = names->next;
		is_first = false;
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

	versions = entry->versions;

	while(versions)
	{
		pkg_osvf_print_version_type(versions);
		pkg_osvf_print_version(&versions->v1);
		pkg_osvf_print_version(&versions->v2);
		versions = versions->next;
	}

	printf("Vulnerable packages:\n");

	packages = entry->packages;

	while(packages)
	{
		printf("\tPackage name: %s\n", packages->names->pkgname);
		pkg_osvf_print_ecosystem(packages->ecosystem);
		versions = packages->versions;

		while(versions)
		{
			pkg_osvf_print_version_type(versions);
			pkg_osvf_print_version(&versions->v1);
			pkg_osvf_print_version(&versions->v2);
			versions = versions->next;
		}

		packages = packages->next;
	}

	printf("Vulnerability references:\n");

	references = entry->references;

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

struct pkg_audit_entry *
pkg_osvf_create_entry(ucl_object_t *osvf_obj)
{
	struct pkg_audit_entry *entry = NULL;
	struct pkg_audit_package *packages = NULL;
	struct pkg_audit_pkgname *names = NULL;
	struct pkg_audit_versions_range *versions = NULL;
	const ucl_object_t *sub_obj = NULL;
	const ucl_object_t *sub_sub_obj = NULL;
	/* Date format is in RFC3339 */
	const char *date_time_str = "%Y-%m-%dT%H:%M:%SZ";

	entry = pkg_osvf_new_entry();

	/* We are probably out of memory or JSON does not exist */
	if(osvf_obj == NULL || entry == NULL)
	{
		return NULL;
	}

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
		pkg_osvf_free_entry(entry);
		return NULL;
	}

	sub_obj = ucl_object_find_key(osvf_obj, "references");

	if(sub_obj && ucl_object_type(sub_obj) == UCL_ARRAY)
	{
		pkg_osvf_parse_references(entry, ucl_object_find_key(osvf_obj, "references"));
	}

	sub_obj = ucl_object_find_key(osvf_obj, "database_specific");

	if(sub_obj && ucl_object_type(sub_obj) == UCL_OBJECT)
	{
		sub_sub_obj = ucl_object_find_key(sub_obj, "references");
		if(sub_sub_obj && ucl_object_type(sub_sub_obj) == UCL_OBJECT)
		{
			sub_obj = ucl_object_find_key(sub_sub_obj, "cvename");
			pkg_osvf_parse_cvename(entry, sub_obj);
		}
	}

	entry->url = entry->references->url;

	packages = entry->packages;
	names = entry->names;
	versions = entry->versions;

	names->pkgname = packages->names->pkgname;
	versions = pkg_osvf_append_version_range(versions, packages->versions);

	while(packages->next)
	{
		packages = packages->next;
		names->next = xcalloc(1, sizeof(struct pkg_audit_pkgname));
		names = names->next;
		names->pkgname = packages->names->pkgname;
		versions->next = xcalloc(1, sizeof(struct pkg_audit_versions_range));
		versions = versions->next;
		versions = pkg_osvf_append_version_range(versions, packages->versions);
	}

	entry->pkgname = entry->names->pkgname;

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

	return entry;
}
