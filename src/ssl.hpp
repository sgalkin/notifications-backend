#pragma once

#include <string>
#include <folly/io/async/SSLContext.h>

namespace ssl {
folly::SSLContextPtr createContext(const std::string& cert, const std::string& key);
std::string getCommonName(folly::SSLContextPtr ctx);
}
