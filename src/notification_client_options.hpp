#pragma once

#include <string>
#include <folly/io/async/SSLContext.h>

#define APNS_SANDBOX "gateway.sandbox.push.apple.com:2195"
#define APNS "gateway.push.apple.com:2195"

struct NotificationClientOptions {
    folly::SSLContextPtr sslContext;
    std::string address{APNS};
};
