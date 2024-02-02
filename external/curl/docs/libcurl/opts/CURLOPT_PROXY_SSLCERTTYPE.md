---
c: Copyright (C) Daniel Stenberg, <daniel.se>, et al.
SPDX-License-Identifier: curl
Title: CURLOPT_PROXY_SSLCERTTYPE
Section: 3
Source: libcurl
See-also:
  - CURLOPT_PROXY_SSLCERT (3)
  - CURLOPT_PROXY_SSLKEY (3)
  - CURLOPT_SSLCERTTYPE (3)
---

# NAME

CURLOPT_PROXY_SSLCERTTYPE - type of the proxy client SSL certificate

# SYNOPSIS

~~~c
#include <curl/curl.h>

CURLcode curl_easy_setopt(CURL *handle, CURLOPT_PROXY_SSLCERTTYPE, char *type);
~~~

# DESCRIPTION

Pass a pointer to a null-terminated string as parameter. The string should be
the format of your client certificate used when connecting to an HTTPS proxy.

Supported formats are "PEM" and "DER", except with Secure Transport or
Schannel. OpenSSL (versions 0.9.3 and later), Secure Transport (on iOS 5 or
later, or OS X 10.7 or later) and Schannel support "P12" for PKCS#12-encoded
files.

The application does not have to keep the string around after setting this
option.

# DEFAULT

"PEM"

# PROTOCOLS

All

# EXAMPLE

~~~c
int main(void)
{
  CURL *curl = curl_easy_init();
  if(curl) {
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "https://example.com/");
    curl_easy_setopt(curl, CURLOPT_PROXY, "https://proxy");
    curl_easy_setopt(curl, CURLOPT_PROXY_SSLCERT, "client.pem");
    curl_easy_setopt(curl, CURLOPT_PROXY_SSLCERTTYPE, "PEM");
    curl_easy_setopt(curl, CURLOPT_PROXY_SSLKEY, "key.pem");
    curl_easy_setopt(curl, CURLOPT_PROXY_KEYPASSWD, "s3cret");
    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
  }
}
~~~

# AVAILABILITY

Added in 7.52.0

If built TLS enabled.

# RETURN VALUE

Returns CURLE_OK if TLS is supported, CURLE_UNKNOWN_OPTION if not, or
CURLE_OUT_OF_MEMORY if there was insufficient heap space.
