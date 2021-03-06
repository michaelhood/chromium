// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_COMPONENT_UPDATER_COMPONENT_UPDATER_UTILS_H_
#define CHROME_BROWSER_COMPONENT_UPDATER_COMPONENT_UPDATER_UTILS_H_

#include <string>

class GURL;

namespace net {
class URLFetcher;
class URLFetcherDelegate;
class URLRequestContextGetter;
}

namespace component_updater {

// An update protocol request starts with a common preamble which includes
// version and platform information for Chrome and the operating system,
// followed by a request body, which is the actual payload of the request.
// For example:
//
// <?xml version="1.0" encoding="UTF-8"?>
// <request protocol="3.0" version="chrome-32.0.1.0"  prodversion="32.0.1.0"
//        requestid="{7383396D-B4DD-46E1-9104-AAC6B918E792}"
//        updaterchannel="canary" arch="x86" nacl_arch="x86-64">
//   <os platform="win" version="6.1" arch="x86"/>
//   ... REQUEST BODY ...
// </request>

// Builds a protocol request string by creating the outer envelope for
// the request and including the request body specified as a parameter.
std::string BuildProtocolRequest(const std::string& request_body);

// Sends a protocol request to the the service endpoint specified by |url|.
// The body of the request is provided by |protocol_request| and it is
// expected to contain XML data. The caller owns the returned object.
net::URLFetcher* SendProtocolRequest(
    const GURL& url,
    const std::string& protocol_request,
    net::URLFetcherDelegate* url_fetcher_delegate,
    net::URLRequestContextGetter* url_request_context_getter);

}  // namespace component_updater

#endif  // CHROME_BROWSER_COMPONENT_UPDATER_COMPONENT_UPDATER_UTILS_H_

