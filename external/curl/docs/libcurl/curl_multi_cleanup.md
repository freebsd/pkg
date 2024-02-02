---
c: Copyright (C) Daniel Stenberg, <daniel.se>, et al.
SPDX-License-Identifier: curl
Title: curl_multi_cleanup
Section: 3
Source: libcurl
See-also:
  - curl_easy_cleanup (3)
  - curl_easy_init (3)
  - curl_multi_get_handles (3)
  - curl_multi_init (3)
---

# NAME

curl_multi_cleanup - close down a multi session

# SYNOPSIS

~~~c
#include <curl/curl.h>

CURLMcode curl_multi_cleanup(CURLM *multi_handle);
~~~

# DESCRIPTION

Cleans up and removes a whole multi stack. It does not free or touch any
individual easy handles in any way - they still need to be closed
individually, using the usual curl_easy_cleanup(3) way. The order of
cleaning up should be:

1 - curl_multi_remove_handle(3) before any easy handles are cleaned up

2 - curl_easy_cleanup(3) can now be called independently since the easy
handle is no longer connected to the multi handle

3 - curl_multi_cleanup(3) should be called when all easy handles are
removed

Passing in a NULL pointer in *multi_handle* makes this function return
CURLM_BAD_HANDLE immediately with no other action.

# EXAMPLE

~~~c
int main(void)
{
  CURLM *multi = curl_multi_init();

  /* when the multi transfer is done ... */

  /* remove all easy handles, then: */
  curl_multi_cleanup(multi);
}
~~~

# AVAILABILITY

Added in 7.9.6

# RETURN VALUE

CURLMcode type, general libcurl multi interface error code. On success,
CURLM_OK is returned.
