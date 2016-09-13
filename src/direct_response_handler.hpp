#pragma once

#include "default_handler.hpp"
#include <proxygen/httpserver/ResponseBuilder.h>
#include <proxygen/lib/http/HTTPMessage.h>
#include <folly/io/IOBuf.h>
#include <string>
#include <memory>

struct CloseConnection {
static proxygen::ResponseBuilder& Apply(proxygen::ResponseBuilder& b) {
    return b.closeConnection();
}
};

struct KeepConnection {
static proxygen::ResponseBuilder& Apply(proxygen::ResponseBuilder& b) {
    return b;
}
};

struct NoStore {
static proxygen::ResponseBuilder& Apply(proxygen::ResponseBuilder& b) {
    return b.header(proxygen::HTTP_HEADER_CACHE_CONTROL, "no-store");
}
};

template<typename... Policy>
class DirectResponseHandler : public DefaultHandler {
public:
    explicit DirectResponseHandler(int code, std::string body = "")
        : code_(code)
        , body_(folly::IOBuf::copyBuffer(body))
    {}

    virtual void onRequest(std::unique_ptr<proxygen::HTTPMessage> headers) noexcept override {}
    virtual void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override {}

    virtual void onEOM() noexcept override {
        auto rb = proxygen::ResponseBuilder(downstream_);
        rb.status(code_, proxygen::HTTPMessage::getDefaultReason(code_));
        for(auto& p: { Policy::Apply... }) p(rb);
        rb.body(std::move(body_));
        rb.sendWithEOM();
    }

private:
    const int code_;
    std::unique_ptr<folly::IOBuf> body_;
};

template<typename... Policy>
using NoStoreDirectResponseHandler = DirectResponseHandler<NoStore, Policy...>;
