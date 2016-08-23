#pragma once

#include <proxygen/httpserver/RequestHandler.h>
#include <folly/Exception.h>

class DefaultHandler : public proxygen::RequestHandler {
public:
    virtual void onUpgrade(proxygen::UpgradeProtocol) noexcept override { CHECK(UNLIKELY(false)); }
    virtual void requestComplete() noexcept override { delete this; }
    virtual void onError(proxygen::ProxygenError) noexcept override { delete this; }
};
