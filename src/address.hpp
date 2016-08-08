#pragma once

#include <string>
#include <folly/SocketAddress.h>

namespace utils {
folly::SocketAddress resolveAddress(const std::string& address);
}
