---
c: Copyright (C) Daniel Stenberg, <daniel.se>, et al.
SPDX-License-Identifier: curl
Title: CURLOPT_PROXY_KEYPASSWD
Section: 3
Source: libcurl
See-also:
  - CURLOPT_KEYPASSWD (3)
  - CURLOPT_PROXY_SSLKEY (3)
  - CURLOPT_SSH_PRIVATE_KEYFILE (3)
  - CURLOPT_SSLKEY (3)
---

# NAME

CURLOPT_PROXY_KEYPASSWD - passphrase for the proxy private key

# SYNOPSIS

~~~c
#include <curl/curl.h>

CURLcode curl_easy_setopt(CURL *handle, CURLOPT_PROXY_KEYPASSWD, char *pwd);
~~~

# DESCRIPTION

This option is for connecting to an HTTPS proxy, not an HTTPS server.

Pass a pointer to a null-terminated string as parameter. It is used as the
password required to use the CURLOPT_PROXY_SSLKEY(3) private key. You
never need a pass phrase to load a certificate but you need one to load your
private key.

The application does not have to keep the string around after setting this
option.

# DEFAULT

NULL

# PROTOCOLS

Used with HTTPS proxy

# EXAMPLE

~~~c
int main(void)
{
  CURL *curl = curl_easy_init();
  if(curl) {
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "https://example.com/foo.bin");
    curl_easy_setopt(curl, CURLOPT_PROXY, "https://proxy:443");
    curl_easy_setopt(curl, CURLOPT_PROXY_KEYPASSWD, "superman");
    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
  }
}
~~~

# AVAILABILITY

Added in 7.52.0

# RETURN VALUE

Returns CURLE_OK if TLS enabled, CURLE_UNKNOWN_OPTION if not, or
CURLE_OUT_OF_MEMORY if there was insufficient heap space.
