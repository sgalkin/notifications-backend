#pragma once

#include "address.hpp"

#include <wangle/bootstrap/ClientBootstrap.h>
#include <wangle/concurrent/IOThreadPoolExecutor.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/SSLContext.h>
#include <folly/futures/Future.h>
#include <folly/futures/SharedPromise.h>
#include <glog/logging.h>

#include <chrono>
#include <memory>

struct ClientOptions {
    folly::SSLContextPtr sslContext;
    std::string address;
    std::chrono::milliseconds reconnectTimeout{50000};
};

template<typename Pipeline>
class Client final {
public:
    explicit Client(ClientOptions options) :
        options_(std::make_unique<ClientOptions>(std::move(options))) {
        CHECK(!options_->address.empty()) << "address is empty";
        VLOG(3) << "created client for " << options_->address
                << " reconnect timeout " << options_->reconnectTimeout.count() << "ms";
    }

    ~Client() {
        CHECK(!ev_) << "not stopped";
    }

    void start(std::shared_ptr<wangle::PipelineFactory<Pipeline>> factory) {
        CHECK(!ev_) << "already started";
        
        ev_ = folly::EventBaseManager::get()->getEventBase();

        bootstrap_ = std::make_unique<wangle::ClientBootstrap<Pipeline>>();
        // TODO: pass IO thread pool from outside
        bootstrap_->group(std::make_shared<wangle::IOThreadPoolExecutor>(1));
        if(options_->sslContext) bootstrap_->sslContext(options_->sslContext);
        bootstrap_->pipelineFactory(factory);
 
        connect(std::chrono::milliseconds::zero());
    }

    void stop() {
        CHECK(ev_) << "not started";
        CHECK(ev_->isInEventBaseThread());
 
        timer_.reset();
        bootstrap_.reset();
       
        ev_ = nullptr;
    }
    
    void connect(std::chrono::milliseconds delay) noexcept {
        VLOG(3) << "scheduling connect in " << delay.count() << "ms";

        auto schedule = [this, delay{std::move(delay)}]() {
            auto connect = [this]() noexcept { this->connect(); };

            pipeline_ = std::make_unique<folly::SharedPromise<Pipeline*>>();
            if(delay == std::chrono::milliseconds::zero())
                this->connect();
            else
                timer_ = folly::AsyncTimeout::schedule(delay, *ev_, connect);
        };
   
        CHECK(ev_->runInEventBaseThread(schedule));
    }

    // TODO: think about that
    folly::Future<Pipeline*> pipeline() { return pipeline_->getFuture(); }
            
private:
    void connect() noexcept {
        CHECK(ev_->isInEventBaseThread());
    
        auto resolve = [this]() { return utils::resolveAddress(options_->address); };
        auto connect = [this](folly::SocketAddress a) { return bootstrap_->connect(std::move(a)); };
        auto update = [this](Pipeline* p) { CHECK(p); pipeline_->setValue(p); };
        auto reconnect = [this](folly::exception_wrapper ew) {
            LOG(ERROR) << folly::exceptionStr(ew);
            this->connect(options_->reconnectTimeout);
        };

        // WARNING: blocking operation in reoslveAddress in EvB
        resolve().then(ev_, std::move(connect)).then(update).onError(std::move(reconnect));
    }

    folly::EventBase* ev_{nullptr};
    std::unique_ptr<folly::AsyncTimeout> timer_;

    std::unique_ptr<ClientOptions> options_;
    std::unique_ptr<wangle::ClientBootstrap<Pipeline>> bootstrap_;
    std::unique_ptr<folly::SharedPromise<Pipeline*>> pipeline_;
};
