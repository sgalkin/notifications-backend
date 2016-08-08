#pragma once

#include <folly/SocketAddress.h>
#include <folly/io/async/SSLContext.h>
#include <vector>

struct FeedbackClientOptions {
    std::vector<int> shutdownOn;
    folly::SSLContextPtr sslContext;
    folly::SocketAddress address;
};
