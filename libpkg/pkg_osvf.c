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

struct pkg_osvf_hash
{
	unsigned int value;
	char *name;
};

/* This is used to for parsing Ecosystem. In future there would
 * be template for ecosystem parsing. Now hardcoded.. like it should
 */
struct pkg_osvf_hash ecosystem_global[] =
{
	{OSVF_ECOSYSTEM_ALMALINUX, "AlmaLinux"},
	{OSVF_ECOSYSTEM_ALPINE, "Alpine"},
	{OSVF_ECOSYSTEM_ANDROID, "Android"},
	{OSVF_ECOSYSTEM_BIOCONDUCTOR, "Bioconductor"},
	{OSVF_ECOSYSTEM_BITNAMI, "Bitnami"},
	{OSVF_ECOSYSTEM_CHAINGUARD, "Chainguard"},
	{OSVF_ECOSYSTEM_CONANCENTER, "ConanCenter"},
	{OSVF_ECOSYSTEM_CRAN, "CRAN"},
	{OSVF_ECOSYSTEM_CRATES_IO, "crates.io"},
	{OSVF_ECOSYSTEM_DEBIAN, "Debian"},
	{OSVF_ECOSYSTEM_FREEBSD, "FreeBSD"},
	{OSVF_ECOSYSTEM_GHC, "GHC"},
	{OSVF_ECOSYSTEM_GITHUB_ACTIONS, "GitHub Actions"},
	{OSVF_ECOSYSTEM_GO, "Go"},
	{OSVF_ECOSYSTEM_HACKAGE, "Hackage"},
	{OSVF_ECOSYSTEM_HEX, "Hex"},
	{OSVF_ECOSYSTEM_KUBERNETES, "Kubernetes"},
	{OSVF_ECOSYSTEM_LINUX, "Linux"},
	{OSVF_ECOSYSTEM_MAGEIA, "Mageia"},
	{OSVF_ECOSYSTEM_MAVEN, "Maven"},
	{OSVF_ECOSYSTEM_MINIMOS, "MinimOS"},
	{OSVF_ECOSYSTEM_NPM, "npm"},
	{OSVF_ECOSYSTEM_NUGET, "NuGet"},
	{OSVF_ECOSYSTEM_OPENSUSE, "openSUSE"},
	{OSVF_ECOSYSTEM_OSS_FUZZ, "OSS-Fuzz"},
	{OSVF_ECOSYSTEM_PACKAGIST, "Packagist"},
	{OSVF_ECOSYSTEM_PHOTON_OS, "Photon OS"},
	{OSVF_ECOSYSTEM_PUB, "Pub"},
	{OSVF_ECOSYSTEM_PYPI, "PyPI"},
	{OSVF_ECOSYSTEM_RED_HAT, "Red Hat"},
	{OSVF_ECOSYSTEM_ROCKY_LINUX, "Rocky Linux"},
	{OSVF_ECOSYSTEM_RUBYGEMS, "RubyGems"},
	{OSVF_ECOSYSTEM_SUSE, "SUSE"},
	{OSVF_ECOSYSTEM_SWIFTURL, "SwiftURL"},
	{OSVF_ECOSYSTEM_UBUNTU, "Ubuntu"},
	{OSVF_ECOSYSTEM_WOLFI, "Wolfi"},
	{OSVF_ECOSYSTEM_UNKNOWN, NULL},
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
	{OSVF_EVENT_TYPE_SEMVER, "SEMVER"},
	{OSVF_EVENT_TYPE_ECOSYSTEM, "ECOSYSTEM"},
	{OSVF_EVENT_TYPE_GIT, "GIT"},
	{OSVF_EVENT_TYPE_UNKNOWN, NULL}
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

	free(ver->version);
	ver->version = NULL;

	free(ver);
}

void
pkg_osvf_free_range(struct pkg_audit_versions_range *range)
{
	free(range);
}

void
pkg_osvf_free_ecosystem(struct pkg_audit_ecosystem *ecosystem)
{
	if(!ecosystem)
	{
		return;
	}

	free(ecosystem->name);
	ecosystem->name = NULL;

	free(ecosystem->version);
	ecosystem->version = NULL;

	free(ecosystem->type);
	ecosystem->type = NULL;

	free(ecosystem->addition);
	ecosystem->addition = NULL;

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

	free(cve->cvename);
	cve->cvename = NULL;

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
	struct pkg_audit_versions_range *versions = NULL;
	struct pkg_audit_versions_range *next_versions = NULL;

	struct pkg_audit_pkgname *names = NULL;
	struct pkg_audit_pkgname *next_names = NULL;

	if(!entry)
	{
		return;
	}

	versions = entry->versions;
	names = entry->names;

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

struct pkghash *pkg_osvf_create_seek_hash(struct pkg_osvf_hash *osvf_ptr)
{
	struct pkghash *ecosystem_hash = pkghash_new();

	while(osvf_ptr->name)
	{
		pkghash_add(ecosystem_hash, osvf_ptr->name, osvf_ptr, NULL);
		osvf_ptr ++;
	}

	return ecosystem_hash;
}

struct pkg_audit_ecosystem *
pkg_osvf_get_ecosystem(const char *ecosystem)
{
	char ecosystem_delimiter[] = ":";
	char *ecosystem_copy = NULL;
	char *ecosystem_token = NULL;
	struct pkghash *ecosystem_hash = NULL;
	struct pkg_audit_ecosystem *rtn_ecosystem = NULL;
	int current_pos = 0;
	char ecosystem_url[1024];

	memset(ecosystem_url, 0x00, 1024);

	if(!ecosystem)
	{
		return OSVF_ECOSYSTEM_UNKNOWN;
	}

	ecosystem_copy = xstrdup(ecosystem);
	ecosystem_token = strtok(ecosystem_copy, ecosystem_delimiter);
	ecosystem_hash = pkg_osvf_create_seek_hash(ecosystem_global);

	if(!ecosystem_token)
	{
		return OSVF_ECOSYSTEM_UNKNOWN;
	}

	pkghash_entry *ecosystem_entry = pkghash_get(ecosystem_hash, ecosystem_token);

	if(ecosystem_entry)
	{
		struct pkg_osvf_hash *ecosystem_struct = (struct pkg_osvf_hash *) ecosystem_entry->value;
		rtn_ecosystem = xcalloc(1, sizeof(struct pkg_audit_ecosystem));
		rtn_ecosystem->ecosystem = ecosystem_struct->value;
		rtn_ecosystem->name = xstrdup(ecosystem_struct->name);

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
		 */
		while(ecosystem_token)
		{
			ecosystem_token = strtok(NULL, ecosystem_delimiter);
			if(ecosystem_token)
			{
				switch(rtn_ecosystem->ecosystem)
				{
				case OSVF_ECOSYSTEM_ALMALINUX:
				case OSVF_ECOSYSTEM_DEBIAN:
				case OSVF_ECOSYSTEM_MAGEIA:
				case OSVF_ECOSYSTEM_PHOTON_OS:
					if(current_pos == 0)
					{
						/* These have TAG:VERSION */
						rtn_ecosystem->version = xstrdup(ecosystem_token);
					}
					break;
				case OSVF_ECOSYSTEM_ALPINE:
					if(current_pos == 0)
					{
						/* version is v3.14 so get the 'v' out */
						rtn_ecosystem->version = xstrdup(ecosystem_token + 1);
					}
					break;
				case OSVF_ECOSYSTEM_FREEBSD:
					if(current_pos == 0)
					{
						/* This can be ports, kernel, src */
						rtn_ecosystem->type = xstrdup(ecosystem_token);
					}
					else if(current_pos == 1)
					{
						rtn_ecosystem->version = xstrdup(ecosystem_token);
					}
					break;
				case OSVF_ECOSYSTEM_MAVEN:
					/* Maven can have URL after ecosystem tag */
					if(current_pos == 0)
					{
						strncpy(ecosystem_url, ecosystem_token, 1023);
						strncat(ecosystem_url, ":", 1023);
					}
					else if(current_pos == 1)
					{
						strncat(ecosystem_url, ecosystem_token, 1023);
						rtn_ecosystem->addition = xstrdup(ecosystem_url);
						memset(ecosystem_url, 0x00, 1024);
					}
					break;
				case OSVF_ECOSYSTEM_RED_HAT:
					/* Red Hat has lot of info in tag which should be parsed */
					if(current_pos == 0)
					{
						rtn_ecosystem->type = xstrdup(ecosystem_token);
					}
					else if(current_pos == 1)
					{
						rtn_ecosystem->version = xstrdup(ecosystem_token);
					}
					else if(current_pos == 2)
					{
						rtn_ecosystem->addition = xstrdup(ecosystem_token);
					}
					break;
				case OSVF_ECOSYSTEM_UBUNTU:
					/* Ubuntu has difficult system but try to parse them */
					if(current_pos == 0)
					{
						if(isdigit(ecosystem_token[0]))
						{
							rtn_ecosystem->version = xstrdup(ecosystem_token);
						}
						else
						{
							rtn_ecosystem->addition = xstrdup(ecosystem_token);
						}
					}
					else if(current_pos == 1)
					{
						if(isdigit(ecosystem_token[0]))
						{
							rtn_ecosystem->version = xstrdup(ecosystem_token);
						}
						else
						{
							rtn_ecosystem->type = xstrdup(ecosystem_token);
						}
					}
					else if(current_pos == 2)
					{
						rtn_ecosystem->type = xstrdup(ecosystem_token);
					}
				}
			}
			current_pos ++;
		}
	}

	if(!rtn_ecosystem->type)
	{
		rtn_ecosystem->type = xstrdup("");
	}

	if(!rtn_ecosystem->version)
	{
		rtn_ecosystem->version = xstrdup("");
	}

	if(!rtn_ecosystem->addition)
	{
		rtn_ecosystem->addition = xstrdup("");
	}


	pkghash_destroy(ecosystem_hash);
	free(ecosystem_copy);

	ecosystem_hash = NULL;
	ecosystem_copy = NULL;

	return rtn_ecosystem;
}

unsigned int
pkg_osvf_get_hash(const char *key, struct pkg_osvf_hash *global, unsigned int unknow)
{
	struct pkghash *hash = NULL;
	pkghash_entry *entry = NULL;
	struct pkg_osvf_hash *rtn_struct = NULL;

	if(!key)
	{
		return unknow;
	}

	hash = pkg_osvf_create_seek_hash(global);

	entry = pkghash_get(hash, key);

	if(entry)
	{
		rtn_struct = (struct pkg_osvf_hash *) entry->value;
		return rtn_struct->value;
	}

	return unknow;
}

unsigned int
pkg_osvf_get_reference(const char *reference_type)
{
	return pkg_osvf_get_hash(reference_type, references_global, OSVF_REFERENCE_UNKNOWN);
}

unsigned int
pkg_osvf_get_event(const char *event_type)
{
	return pkg_osvf_get_hash(event_type, event_global, OSVF_EVENT_TYPE_UNKNOWN);
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


void
pkg_osvf_parse_ranges(struct pkg_audit_versions_range *range, const ucl_object_t *range_array)
{
	ucl_object_iter_t it = NULL;
	const ucl_object_t *cur = NULL;
	struct pkg_audit_versions_range *next_range = NULL;
	const ucl_object_t *sub_obj = NULL;
	bool is_first = true;

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
		if(is_first == false)
		{
			next_range = xcalloc(1, sizeof(struct pkg_audit_versions_range));
			range->next = next_range;
			range = next_range;
		}

		sub_obj = ucl_object_find_key(cur, "events");

		if(sub_obj && ucl_object_type(sub_obj) == UCL_ARRAY)
		{
			pkg_osvf_parse_events(range, ucl_object_find_key(cur, "events"), pkg_osvf_ucl_string(cur, "type"));
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
pkg_osvf_print_ecosystem(struct pkg_audit_ecosystem *ecosystem)
{
	if(!ecosystem)
	{
		return;
	}

	printf("\t\tEcosystem: ");

	if(ecosystem->name)
	{
		printf("%s ", ecosystem->name);
	}

	if(ecosystem->version)
	{
		printf("%s ", ecosystem->version);
	}

	if(ecosystem->type)
	{
		printf("%s ", ecosystem->type);
	}

	if(ecosystem->addition)
	{
		printf("%s", ecosystem->addition);
	}

	printf("\n");
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
		return NULL;
	}

	sub_obj = ucl_object_find_key(osvf_obj, "references");

	if(sub_obj && ucl_object_type(sub_obj) == UCL_ARRAY)
	{
		pkg_osvf_parse_references(entry, ucl_object_find_key(osvf_obj, "references"));
	}
	else
	{
		return NULL;
	}

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
