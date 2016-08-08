#pragma once

#include <string>
#include <chrono>
#include <folly/io/async/SSLContext.h>

#define FEEDBACK_SANDBOX "feedback.sandbox.push.apple.com:2196"
#define FEEDBACK "feedback.push.apple.com:2196"

struct FeedbackClientOptions {
    folly::SSLContextPtr sslContext;
    std::string address{FEEDBACK};
    std::chrono::seconds pollInterval{3600};
};
