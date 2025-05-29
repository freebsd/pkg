/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 *​ Copyright (c) 2025 The FreeBSD Foundation
 *​
 *​ Portions of this software were developed by  under sponsorship
 * from the FreeBSD Foundation
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

/*
  Open Source Vulnerability format: https://ossf.github.io/osv-schema/
  OSVF schema: https://github.com/ossf/osv-schema/blob/main/validation/schema.json
  OSVF schema version: 1.7.0
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
                                      "        \"Alpine\","
                                      "        \"Android\","
                                      "        \"Bioconductor\","
                                      "        \"Bitnami\","
                                      "        \"Chainguard\","
                                      "        \"ConanCenter\","
                                      "        \"CRAN\","
                                      "        \"crates.io\","
                                      "        \"Debian\","
                                      "        \"FreeBSD\","
                                      "        \"GHC\","
                                      "        \"GitHub Actions\","
                                      "        \"Go\","
                                      "        \"Hackage\","
                                      "        \"Hex\","
                                      "        \"Kubernetes\","
                                      "        \"Linux\","
                                      "        \"Mageia\","
                                      "        \"Maven\","
                                      "        \"MinimOS\","
                                      "        \"npm\","
                                      "        \"NuGet\","
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
                                      "      \"pattern\": \"^(AlmaLinux|Alpine|Android|Bioconductor|Bitnami|Chainguard|ConanCenter|CRAN|crates\\.io|Debian|FreeBSD:ports|FreeBSD|GHC|GitHub Actions|Go|Hackage|Hex|Kubernetes|Linux|Mageia|Maven|MinimOS|npm|NuGet|openSUSE|OSS-Fuzz|Packagist|Photon OS|Pub|PyPI|Red Hat|Rocky Linux|RubyGems|SUSE|SwiftURL|Ubuntu|Wolfi|GIT)(:.+)?$\""
                                      "    },"
                                      "    \"prefix\": {"
                                      "      \"type\": \"string\","
                                      "      \"title\": \"Currently supported home database identifier prefixes\","
                                      "      \"description\": \"These home databases are also documented at https://ossf.github.io/osv-schema/#id-modified-fields\","
                                      "      \"pattern\": \"^(ASB-A|PUB-A|ALSA|ALBA|ALEA|BIT|CGA|CURL|CVE|DSA|DLA|ELA|FBSD|DTSA|GHSA|GO|GSD|HSEC|KUBE|LBSEC|LSN|MAL|MGASA|OSV|openSUSE-SU|PHSA|PSF|PYSEC|RHBA|RHEA|RHSA|RLSA|RXSA|RSEC|RUSTSEC|SUSE-[SRFO]U|UBUNTU|USN|V8)-\""
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
	if(pkgname->pkgname)
	{
		free(pkgname->pkgname);
		pkgname->pkgname = NULL;
	}

	if(pkgname->next)
	{
		pkg_osvf_free_pkgname(pkgname->next);
		pkgname->next = NULL;
	}

	free(pkgname);
}

void
pkg_osvf_free_version(struct pkg_audit_version *ver)
{
	if(ver->version)
	{
		free(ver->version);
		ver->version = NULL;
	}

	free(ver);
}

void
pkg_osvf_free_range(struct pkg_audit_versions_range *range)
{
	free(range);
}

void
pkg_osvf_free_package(struct pkg_audit_package *package)
{
	if(package->names)
	{
		pkg_osvf_free_pkgname(package->names);
		package->names = NULL;
	}

	if(package->versions)
	{
		pkg_osvf_free_range(package->versions);
		package->versions = NULL;
	}

	if(package->next)
	{
		pkg_osvf_free_package(package->next);
		package->next = NULL;
	}

	free(package);
}

void
pkg_osvf_free_cve(struct pkg_audit_cve *cve)
{
	if(cve->cvename)
	{
		free(cve->cvename);
		cve->cvename = NULL;
	}

	if(cve->next)
	{
		pkg_osvf_free_cve(cve->next);
		cve->next = NULL;
	}

	free(cve);
}

void
pkg_osvf_free_reference(struct pkg_audit_reference *reference)
{
	if(reference->url)
	{
		free(reference->url);
		reference->url = NULL;
	}

	if(reference->next)
	{
		pkg_osvf_free_reference(reference->next);
		reference->next = NULL;
	}

	free(reference);
}

void
pkg_osvf_free_entry(struct pkg_audit_entry *entry)
{
	struct pkg_audit_versions_range *versions = entry->versions;
	struct pkg_audit_versions_range *next_versions = NULL;

	struct pkg_audit_pkgname *names = entry->names;
	struct pkg_audit_pkgname *next_names = NULL;

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

	while(versions)
	{
		next_versions = versions->next;
		free(versions);
		versions = next_versions;
	}

	while(names)
	{
		next_names = names->next;
		free(names);
		names = next_names;
	}

	pkg_osvf_free_package(entry->packages);
	entry->packages = NULL;

	pkg_osvf_free_cve(entry->cve);
	entry->cve = NULL;

	pkg_osvf_free_reference(entry->references);
	entry->references = NULL;

	free(entry);
}

unsigned int
pkg_osvg_get_ecosystem(const char *ecosystem)
{
	if(strncmp(ecosystem, "AlmaLinux", 8) == 0)
	{
		return OSVF_ECOSYSTEM_ALMALINUX;
	}
	else if(strncmp(ecosystem, "Alpine", 5) == 0)
	{
		return OSVF_ECOSYSTEM_ALPINE;
	}
	else if(strncmp(ecosystem, "Android", 6) == 0)
	{
		return OSVF_ECOSYSTEM_ANDROID;
	}
	else if(strncmp(ecosystem, "Bioconductor", 11) == 0)
	{
		return OSVF_ECOSYSTEM_BIOCONDUCTOR;
	}
	else if(strncmp(ecosystem, "Bitnami", 6) == 0)
	{
		return OSVF_ECOSYSTEM_BITNAMI;
	}
	else if(strncmp(ecosystem, "Chainguard", 9) == 0)
	{
		return OSVF_ECOSYSTEM_CHAINGUARD;
	}
	else if(strncmp(ecosystem, "ConanCenter", 10) == 0)
	{
		return OSVF_ECOSYSTEM_CONANCENTER;
	}
	else if(strncmp(ecosystem, "CRAN", 4) == 0)
	{
		return OSVF_ECOSYSTEM_CRAN;
	}
	else if(strncmp(ecosystem, "crates.io", 8) == 0)
	{
		return OSVF_ECOSYSTEM_CRATES_IO;
	}
	else if(strncmp(ecosystem, "Debian", 5) == 0)
	{
		return OSVF_ECOSYSTEM_DEBIAN;
	}
	else if(strncmp(ecosystem, "FreeBSD", 6) == 0)
	{
		return OSVF_ECOSYSTEM_FREEBSD;
	}
	else if(strncmp(ecosystem, "GHC", 3) == 0)
	{
		return OSVF_ECOSYSTEM_GHC;
	}
	else if(strncmp(ecosystem, "GitHub Actions", 13) == 0)
	{
		return OSVF_ECOSYSTEM_GITHUB_ACTIONS;
	}
	else if(strncmp(ecosystem, "Go", 2) == 0)
	{
		return OSVF_ECOSYSTEM_GO;
	}
	else if(strncmp(ecosystem, "Hackage", 6) == 0)
	{
		return OSVF_ECOSYSTEM_HACKAGE;
	}
	else if(strncmp(ecosystem, "Hex", 3) == 0)
	{
		return OSVF_ECOSYSTEM_HEX;
	}
	else if(strncmp(ecosystem, "Kubernetes", 9) == 0)
	{
		return OSVF_ECOSYSTEM_KUBERNETES;
	}
	else if(strncmp(ecosystem, "Linux", 5) == 0)
	{
		return OSVF_ECOSYSTEM_LINUX;
	}
	else if(strncmp(ecosystem, "Mageia", 6) == 0)
	{
		return OSVF_ECOSYSTEM_MAGEIA;
	}
	else if(strncmp(ecosystem, "Maven", 5) == 0)
	{
		return OSVF_ECOSYSTEM_MAVEN;
	}
	else if(strncmp(ecosystem, "MinimOS", 9) == 0)
	{
		return OSVF_ECOSYSTEM_MINIMOS;
	}
	else if(strncmp(ecosystem, "npm", 3) == 0)
	{
		return OSVF_ECOSYSTEM_NPM;
	}
	else if(strncmp(ecosystem, "NuGet", 5) == 0)
	{
		return OSVF_ECOSYSTEM_NUGET;
	}
	else if(strncmp(ecosystem, "openSUSE", 7) == 0)
	{
		return OSVF_ECOSYSTEM_OPENSUSE;
	}
	else if(strncmp(ecosystem, "OSS-Fuzz", 7) == 0)
	{
		return OSVF_ECOSYSTEM_OSS_FUZZ;
	}
	else if(strncmp(ecosystem, "Packagist", 8) == 0)
	{
		return OSVF_ECOSYSTEM_PACKAGIST;
	}
	else if(strncmp(ecosystem, "Photon OS", 8) == 0)
	{
		return OSVF_ECOSYSTEM_PHOTON_OS;
	}
	else if(strncmp(ecosystem, "Pub", 3) == 0)
	{
		return OSVF_ECOSYSTEM_PUB;
	}
	else if(strncmp(ecosystem, "PyPI", 4) == 0)
	{
		return OSVF_ECOSYSTEM_PYPI;
	}
	else if(strncmp(ecosystem, "Red Hat", 7) == 0)
	{
		return OSVF_ECOSYSTEM_RED_HAT;
	}
	else if(strncmp(ecosystem, "Rocky Linux", 10) == 0)
	{
		return OSVF_ECOSYSTEM_ROCKY_LINUX;
	}
	else if(strncmp(ecosystem, "RubyGems", 8) == 0)
	{
		return OSVF_ECOSYSTEM_RUBYGEMS;
	}
	else if(strncmp(ecosystem, "SUSE", 4) == 0)
	{
		return OSVF_ECOSYSTEM_SUSE;
	}
	else if(strncmp(ecosystem, "SwiftURL", 8) == 0)
	{
		return OSVF_ECOSYSTEM_SWIFTURL;
	}
	else if(strncmp(ecosystem, "Ubuntu", 6) == 0)
	{
		return OSVF_ECOSYSTEM_UBUNTU;
	}
	else if(strncmp(ecosystem, "Wolfi", 5) == 0)
	{
		return OSVF_ECOSYSTEM_WOLFI;
	}

	return OSVF_ECOSYSTEM_UNKNOWN;
}

unsigned int
pkg_osvg_get_reference(const char *reference_type)
{

	if(strncmp(reference_type, "ADVISORY", 8) == 0)
	{
		return OSVF_REFERENCE_ADVISORY;
	}
	else if(strncmp(reference_type, "ARTICLE", 5) == 0)
	{
		return OSVF_REFERENCE_ARTICLE;
	}
	else if(strncmp(reference_type, "DETECTION", 5) == 0)
	{
		return OSVF_REFERENCE_DETECTION;
	}
	else if(strncmp(reference_type, "DISCUSSION", 5) == 0)
	{
		return OSVF_REFERENCE_DISCUSSION;
	}
	else if(strncmp(reference_type, "REPORT", 5) == 0)
	{
		return OSVF_REFERENCE_REPORT;
	}
	else if(strncmp(reference_type, "FIX", 5) == 0)
	{
		return OSVF_REFERENCE_FIX;
	}
	else if(strncmp(reference_type, "INTRODUCED", 5) == 0)
	{
		return OSVF_REFERENCE_INTRODUCED;
	}
	else if(strncmp(reference_type, "PACKAGE", 4) == 0)
	{
		return OSVF_REFERENCE_PACKAGE;
	}
	else if(strncmp(reference_type, "EVIDENCE", 8) == 0)
	{
		return OSVF_REFERENCE_EVIDENCE;
	}
	else if(strncmp(reference_type, "WEB", 8) == 0)
	{
		return OSVF_REFERENCE_WEB;
	}

	return OSVF_REFERENCE_UNKNOWN;
}

unsigned int
pkg_osvg_get_event(const char *reference_type)
{
	if(strncmp(reference_type, "SEMVER", 6) == 0)
	{
		return OSVF_EVENT_TYPE_SEMVER;
	}
	else if(strncmp(reference_type, "ECOSYSTEM", 9) == 0)
	{
		return OSVF_EVENT_TYPE_ECOSYSTEM;
	}
	else if(strncmp(reference_type, "GIT", 3) == 0)
	{
		return OSVF_EVENT_TYPE_GIT;
	}

	return OSVF_EVENT_TYPE_UNKNOWN;
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

	package->names->pkgname = xstrdup(ucl_object_tostring(ucl_object_find_key(package_obj, "name")));
	package->ecosystem = pkg_osvg_get_ecosystem(ucl_object_tostring(ucl_object_find_key(package_obj, "ecosystem")));
}

void
pkg_osvf_parse_events(struct pkg_audit_versions_range *range, const ucl_object_t *event_obj, const char *type)
{
	ucl_object_iter_t it = NULL;
	const ucl_object_t *cur = NULL;

	ucl_object_iter_t sub_it = NULL;
	const ucl_object_t *sub_cur = NULL;

	range->type = pkg_osvg_get_event(type);

	/* Parses package structure from events:
	   {
	     "fixed|introduced": "1.0.0"
	   }
	*/

	while ((cur = ucl_iterate_object(event_obj, &it, true)))
	{
		while ((sub_cur = ucl_iterate_object(cur, &sub_it, true)))
		{

			if(strncmp(ucl_object_key(sub_cur), "fixed", 5) == 0)
			{
				range->v2.version = xstrdup(ucl_object_tostring(sub_cur));
				range->v2.type = OSVF_EVENT_FIXED;
			}
			else if(strncmp(ucl_object_key(sub_cur), "introduced", 10) == 0)
			{
				range->v1.version = xstrdup(ucl_object_tostring(sub_cur));
				range->v1.type = OSVF_EVENT_INTRODUCED;
			}
		}
	}
}


void
pkg_osvf_parse_ranges(struct pkg_audit_versions_range *range, const ucl_object_t *range_obj)
{
	ucl_object_iter_t it = NULL;
	const ucl_object_t *cur = NULL;
	struct pkg_audit_versions_range *next_range = NULL;
	bool is_first = true;

	/* Parses events structure
	   "events": [
	     {
	       "fixed": "1.0.0"
	     },
	     {
	     "introduced": "0.0.1"
	     },
	   ]
	*/

	while ((cur = ucl_iterate_object(range_obj, &it, true)))
	{
		if(is_first == false)
		{
			next_range = xcalloc(1, sizeof(struct pkg_audit_versions_range));
			range->next = next_range;
			range = next_range;
		}

		pkg_osvf_parse_events(range, ucl_object_find_key(cur, "events"), ucl_object_tostring(ucl_object_find_key(cur, "type")));

		is_first = false;
	}
}

void
pkg_osvf_parse_reference(struct pkg_audit_reference *ref, const ucl_object_t *ref_obj)
{
	/*
	   Parses refrence to struct
	   {
	     "type": "ADVISORY",
	     "url": "https://www.freebsd.org/"
	   }
	*/
	ref->url = xstrdup(ucl_object_tostring(ucl_object_find_key(ref_obj, "url")));
	ref->type = pkg_osvg_get_reference(ucl_object_tostring(ucl_object_find_key(ref_obj, "type")));
}

void
pkg_osvf_parse_references(struct pkg_audit_entry *entry, const ucl_object_t *ref_obj)
{
	ucl_object_iter_t it = NULL;
	const ucl_object_t *cur = NULL;
	bool is_first = true;
	struct pkg_audit_reference *reference = entry->references;
	struct pkg_audit_reference *next_package = NULL;

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

		pkg_osvf_parse_reference(reference, cur);

		is_first = false;
	}

}

void
pkg_osvf_parse_affected(struct pkg_audit_entry *entry, const ucl_object_t *aff_obj)
{
	ucl_object_iter_t it = NULL;
	const ucl_object_t *cur = NULL;
	bool is_first = true;
	struct pkg_audit_package *package = entry->packages;
	struct pkg_audit_package *next_package = NULL;

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

		package->names = xcalloc(1, sizeof(struct pkg_audit_pkgname));
		pkg_osvf_parse_package(package, ucl_object_find_key(cur, "package"));

		package->versions = xcalloc(1, sizeof(struct pkg_audit_versions_range));
		pkg_osvf_parse_ranges(package->versions, ucl_object_find_key(cur, "ranges"));

		is_first = false;
	}
}

void
pkg_osvf_append_version_range(struct pkg_audit_versions_range *to, struct pkg_audit_versions_range *from)
{
	struct pkg_audit_versions_range *ptr_from = from;
	struct pkg_audit_versions_range *ptr_to = to;

	to->v1.type = from->v1.type;
	to->v1.version = from->v1.version;
	to->v2.type = from->v2.type;
	to->v2.version = from->v2.version;
	to->type = from->type;

	while(ptr_from->next)
	{
		ptr_to->next = xcalloc(1, sizeof(struct pkg_audit_versions_range));

		ptr_to = ptr_to->next;
		ptr_from = ptr_from->next;

		ptr_to->v1.type = ptr_from->v1.type;
		ptr_to->v1.version = ptr_from->v1.version;
		ptr_to->v2.type = ptr_from->v2.type;
		ptr_to->v2.version = ptr_from->v2.version;
		ptr_to->type = ptr_from->type;
	}
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
	case OSVF_EVENT_TYPE_UNKNOWN:
		printf("UNKNOWN\n");
		break;
	case OSVF_EVENT_TYPE_SEMVER:
		printf("Sematic Version 2.0\n");
		break;
	case OSVF_EVENT_TYPE_ECOSYSTEM:
		printf("Ecosystem\n");
		break;
	case OSVF_EVENT_TYPE_GIT:
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

	entry->id = xstrdup(ucl_object_tostring(ucl_object_find_key(osvf_obj, "id")));
	entry->desc = xstrdup(ucl_object_tostring(ucl_object_find_key(osvf_obj, "summary")));

	pkg_osvf_parse_affected(entry, ucl_object_find_key(osvf_obj, "affected"));
	pkg_osvf_parse_references(entry, ucl_object_find_key(osvf_obj, "references"));

	entry->url = entry->references->url;

	packages = entry->packages;
	names = entry->names;
	versions = entry->versions;

	names->pkgname = packages->names->pkgname;
	pkg_osvf_append_version_range(versions, packages->versions);

	while(packages->next)
	{
		packages = packages->next;
		names->next = xcalloc(1, sizeof(struct pkg_audit_pkgname));
		names = names->next;
		names->pkgname = packages->names->pkgname;
		versions->next = xcalloc(1, sizeof(struct pkg_audit_versions_range));
		versions = versions->next;
		pkg_osvf_append_version_range(versions, packages->versions);
	}

	entry->pkgname = entry->names->pkgname;

	strptime(ucl_object_tostring(ucl_object_find_key(osvf_obj, "modified")), date_time_str, &entry->modified);
	strptime(ucl_object_tostring(ucl_object_find_key(osvf_obj, "published")), date_time_str, &entry->published);

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
