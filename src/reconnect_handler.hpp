#pragma once

#include "client.hpp"
#include <wangle/channel/Handler.h>
#include <wangle/channel/Pipeline.h>
#include <folly/io/IOBuf.h>
#include <glog/logging.h>
#include <chrono>

class ReconnectHandler : public wangle::BytesToBytesHandler {
public:
    ReconnectHandler(Client<typename wangle::Pipeline<rin, win>>* client, std::chrono::milliseconds timeout) :
        client_(client),
        timeout_(std::move(timeout)) {
        CHECK(client_);
        VLOG(3) << "reconnect timeout: " << timeout_.count() << "ms";
    }

    virtual void readEOF(Context* ctx) override {
        close(ctx);
        client_->connect(timeout_);
    }
    
    virtual void readException(Context* ctx, folly::exception_wrapper ex) override {
        LOG(ERROR) << folly::exceptionStr(std::move(ex));
        close(ctx);
        client_->connect(timeout_);
    }

private:
    Client<wangle::DefaultPipeline>* const client_;
    std::chrono::milliseconds timeout_;
};
