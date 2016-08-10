#pragma once

#include <folly/SocketAddress.h>
#include <folly/futures/Future.h>
#include <string>

namespace utils {
folly::Future<folly::SocketAddress> resolveAddress(const std::string& address);
}
