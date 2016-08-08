#pragma once

#include <memory>
#include <folly/io/IOBuf.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/AsyncTimeout.h>
#include <wangle/channel/Pipeline.h>
#include <wangle/channel/Handler.h>
#include <wangle/bootstrap/ClientBootstrap.h>

struct Feedback;
class FeedbackClientOptions;

typedef wangle::Pipeline<folly::IOBufQueue&, std::unique_ptr<folly::IOBuf>> FeedbackClientPipeline;
typedef wangle::HandlerAdapter<Feedback, std::unique_ptr<folly::IOBuf>> FeedbackHandler;

class FeedbackClient final : public FeedbackHandler {
public:
    FeedbackClient(folly::EventBase* ev, FeedbackClientOptions options);
    virtual ~FeedbackClient();
    
    void start();
    void stop();
            
private:
    void connect() noexcept;

    // FeedbackHandler
    virtual void read(Context* ctx, Feedback msg) override;
    virtual void readEOF(Context* ctx) override;
    virtual void readException(Context* ctx, folly::exception_wrapper e) override;
    
    folly::EventBase* ev_{nullptr};
    std::unique_ptr<FeedbackClientOptions> options_;
    std::unique_ptr<wangle::ClientBootstrap<FeedbackClientPipeline>> bootstrap_;
    std::unique_ptr<folly::AsyncTimeout> callback_;
};
