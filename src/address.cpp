#include "address.hpp"

//#include <folly/futures/Promise.h>
//#include <memory>
#include <system_error>

namespace utils {
folly::Future<folly::SocketAddress> resolveAddress(const std::string& address)
{
    // TODO: implement properly with getaddrinfo_a
    
    //auto promise = std::make_shared<folly::Promise<folly::SocketAddress>>();
    //std::thread([address, promise]() {
    //  VLOG(3) << "sleeping";
    //  sleep(20);
    //  VLOG(3) << "woke up";
            
    // promise->setValue(std::move(result));
    // }).detach();
    // return promise->getFuture();

    try {
        folly::SocketAddress result;
        result.setFromHostPort(address);
        return folly::Future<folly::SocketAddress>(std::move(result));
    } catch(const std::system_error& err) {
        return folly::Future<folly::SocketAddress>(err);
    }
}
}
