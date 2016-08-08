#pragma once

#include <memory>
#include <folly/io/IOBuf.h>
#include <folly/io/async/EventBase.h>
#include <wangle/channel/Pipeline.h>
#include <wangle/bootstrap/ClientBootstrap.h>
#include "feedback_client_options.hpp"

typedef wangle::Pipeline<folly::IOBufQueue&, std::unique_ptr<folly::IOBuf>> FeedbackClientPipeline;

class FeedbackClientSignalHandler;

class FeedbackClient final {//: public wangle::InboundHandler<std::unique_ptr<folly::IOBuf>>{
public:
    explicit FeedbackClient(FeedbackClientOptions options);
    ~FeedbackClient();
    
    void start();            
    void stop();
            
private:
    void ping() noexcept;
    
    folly::EventBase* ev_{nullptr};
    std::unique_ptr<FeedbackClientOptions> options_;
    std::unique_ptr<FeedbackClientSignalHandler> handler_;
    std::unique_ptr<wangle::ClientBootstrap<FeedbackClientPipeline>> bootstrap_;
};
