---
c: Copyright (C) Daniel Stenberg, <daniel.se>, et al.
SPDX-License-Identifier: curl
Title: CURLOPT_SSLKEY
Section: 3
Source: libcurl
See-also:
  - CURLOPT_SSLCERT (3)
  - CURLOPT_SSLKEYTYPE (3)
  - CURLOPT_SSLKEY_BLOB (3)
---

# NAME

CURLOPT_SSLKEY - private key file for TLS and SSL client cert

# SYNOPSIS

~~~c
#include <curl/curl.h>

CURLcode curl_easy_setopt(CURL *handle, CURLOPT_SSLKEY, char *keyfile);
~~~

# DESCRIPTION

Pass a pointer to a null-terminated string as parameter. The string should be
the filename of your private key. The default format is "PEM" and can be
changed with CURLOPT_SSLKEYTYPE(3).

(Windows, iOS and Mac OS X) This option is ignored by Secure Transport and
Schannel SSL backends because they expect the private key to be already present
in the key-chain or PKCS#12 file containing the certificate.

The application does not have to keep the string around after setting this
option.

# DEFAULT

NULL

# PROTOCOLS

All TLS based protocols: HTTPS, FTPS, IMAPS, POP3S, SMTPS etc.

# EXAMPLE

~~~c
int main(void)
{
  CURL *curl = curl_easy_init();
  if(curl) {
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "https://example.com/");
    curl_easy_setopt(curl, CURLOPT_SSLCERT, "client.pem");
    curl_easy_setopt(curl, CURLOPT_SSLKEY, "key.pem");
    curl_easy_setopt(curl, CURLOPT_KEYPASSWD, "s3cret");
    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
  }
}
~~~

# AVAILABILITY

If built TLS enabled.

# RETURN VALUE

Returns CURLE_OK if TLS is supported, CURLE_UNKNOWN_OPTION if not, or
CURLE_OUT_OF_MEMORY if there was insufficient heap space.
