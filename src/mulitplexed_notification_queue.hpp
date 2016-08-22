#pragma once

#include <folly/io/async/NotificationQueue.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/EventBaseManager.h>
#include <glog/logging.h>
#include <unordered_map>

template<typename MessageT>
class MulitplexedNotificationQueue final : public folly::NotificationQueue<MessageT>::Consumer {
    using OutputQueue = typename folly::NotificationQueue<std::shared_ptr<MessageT>>;
public:
    using Consumer = typename OutputQueue::Consumer;
    
    explicit MulitplexedNotificationQueue(uint32_t maxSize = 0)
        : input_(maxSize)
        , max_(maxSize) {
        VLOG(4) << "MulitplexedNotificationQueue " << this << " created"; 
    }

    ~MulitplexedNotificationQueue() {
        CHECK(!ev_) << "Still running";
        VLOG(4) << "MulitplexedNotificationQueue " << this << " destroyed"; 
    }

    void start() {
        CHECK(!ev_) << "Already started";
        ev_ = folly::EventBaseManager::get()->getEventBase();
        VLOG(4) << "MulitplexedNotificationQueue " << this << " evb " << ev_;

        this->startConsuming(ev_, &input_);
    }

    void stop() {
        CHECK(ev_) << "Not started";
        this->stopConsuming();
        ev_ = nullptr;
    }

    void addConsumer(typename OutputQueue::Consumer* consumer) {
        CHECK(consumer);
        auto queue = std::make_unique<OutputQueue>(max_);
        consumer->startConsuming(folly::EventBaseManager::get()->getEventBase(), queue.get());
        ev_->runInEventBaseThread([this, consumer, queue = queue.release()]() {
                // std::move in cpture doesn't work for some reason
                CHECK(output_.emplace(consumer, std::unique_ptr<OutputQueue>(queue)).second);
        });
    }

    void removeConsumer(typename OutputQueue::Consumer* consumer) {
        CHECK(consumer);
        consumer->stopConsuming();
        ev_->runInEventBaseThread([this, consumer]() { output_.erase(consumer); });
    }

    
    void setMaxQueueSize(uint32_t max) {
        max_ = max;
        input_.setMaxQueueSize(max_);

        CHECK(ev_) << "Not started";
        ev_->runInEventBaseThread([this]() { for(auto& kv : output_) kv.second->setMaxQueueSize(max_); });
    }

    template<typename M>
    void tryPutMessage(M message) {
        input_.tryPutMessage(std::forward<M>(message));
    }

    template<typename M>
    bool tryPutMessageNoThrow(M message) {
        return input_.tryPutMessageNoThrow(std::forward<M>(message));
    }

    template<typename M>
    void putMessage(M message) {
        return input_.putMessage(std::forward<M>(message));
    }

    template<typename InputIteratorT>
    void putMessages(InputIteratorT first, InputIteratorT last) {
        input_.putMessages(first, last);
    }
    
private:
    virtual void messageAvailable(MessageT&& message) override {
        CHECK(ev_->isInEventBaseThread());
        VLOG(4) << "MulitplexedNotificationQueue " << this << " messageAvailable";
        auto m = std::make_shared<MessageT>(std::move(message));
        for(auto& kv: output_) kv.second->putMessage(m);
    }
    
    // double repost could be avoided, but for now it's easier to keep it.
    // peanlty - 300 microseconds
    folly::NotificationQueue<MessageT> input_;
    std::unordered_map<Consumer*, std::unique_ptr<OutputQueue>> output_;

    folly::EventBase* ev_{nullptr};
    uint32_t max_{0};
};
