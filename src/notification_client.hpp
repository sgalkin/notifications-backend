#pragma once

#include <memory>
#include <folly/io/async/EventBase.h>
#include <wangle/channel/Pipeline.h>
#include <wangle/channel/Handler.h>
#include <wangle/bootstrap/ClientBootstrap.h>

class Notification;
class NotificationStatus;
class NotificationClientOptions;

typedef wangle::Pipeline<folly::IOBufQueue&, Notification> NotificationClientPipeline;
typedef wangle::HandlerAdapter<NotificationStatus, Notification> NotificationHandler;

class NotificationClient final : public NotificationHandler {
public:
    NotificationClient(folly::EventBase* ev, NotificationClientOptions options);
    ~NotificationClient();
    
    void start();
    void stop();

    void push(std::string token, std::string payload);

private:
    void connect();

    // NotificationHandler
    virtual void read(Context* ctx, NotificationStatus msg) override;
    virtual void readEOF(Context* ctx) override;
    virtual void readException(Context* ctx, folly::exception_wrapper e) override;
        
    folly::EventBase* ev_{nullptr};
    std::unique_ptr<NotificationClientOptions> options_;
    std::unique_ptr<wangle::ClientBootstrap<NotificationClientPipeline>> bootstrap_;
};
