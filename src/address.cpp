#include "address.hpp"

namespace utils {
folly::SocketAddress resolveAddress(const std::string& address)
{
    folly::SocketAddress result;
    result.setFromHostPort(address);
    return result;
}
}
