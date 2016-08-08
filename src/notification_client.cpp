#include "notification_client.hpp"

#include <folly/io/IOBuf.h>
#include <wangle/channel/AsyncSocketHandler.h>
#include <wangle/channel/EventBaseHandler.h>
#include <wangle/codec/MessageToByteEncoder.h>
#include <wangle/codec/FixedLengthFrameDecoder.h>
#include "notification_client_options.hpp"

namespace {
    std::array<uint8_t, 32> token(const std::string& input) {
        std::array<uint8_t, 32> token;
        CHECK_EQ(input.size() % 2, 0);
        CHECK_EQ(input.size() / 2, token.size());
            
        int j = 0;
        auto unhex = [](char c) -> int {
            return
            c >= '0' && c <= '9' ? c - '0' :
            c >= 'A' && c <= 'F' ? c - 'A' + 10 :
            c >= 'a' && c <= 'f' ? c - 'a' + 10 :
            -1;
        };

        for (size_t i = 0; i < input.size(); i += 2) {
            int highBits = unhex(input[i]);
            int lowBits = unhex(input[i + 1]);
            CHECK(highBits >= 0 && lowBits >= 0);
            token[j++] = (highBits << 4) + lowBits;
        }
        return token;
    }

    std::unique_ptr<folly::IOBuf> header(uint8_t id, uint16_t size)
    {
        auto len = sizeof(id) + sizeof(size);
        auto header = folly::IOBuf::create(len);

        uint16_t sz = htons(size);
        memcpy(header->writableData(), &id, sizeof(id));
        memcpy(header->writableData() + sizeof(id), &sz, sizeof(sz));

        header->append(len);
        return header;    
    }

    std::unique_ptr<folly::IOBuf> item(uint8_t id, const void* buf, size_t size)
    {
        CHECK_EQ((size & ~0xffff), 0);        

        auto item = folly::IOBuf::create(0);
        item->prependChain(header(id, uint16_t(size)));
        item->prependChain(folly::IOBuf::copyBuffer(buf, size));
        return item;
    }
}


struct NotificationStatus { 
enum class NotificationStatus_ {
    SUCCESS = 0, // No errors encountered
    PROCESSING_ERROR = 1, // Processing error
    NO_TOKEN = 2, // Missing device token
    NO_TOPIC = 3, // Missing topic
    NO_PAYLOAD = 4, // Missing payload
    INVALID_TOKEN_SIZE = 5, // Invalid token size
    INVALID_TOPIC_SIZE = 6, //  Invalid topic size
    INVALID_PAYLOAD_SIZE = 7, // Invalid payload size
    INVALID_TOKEN = 8, // Invalid token
    SHUTDOWN = 10, // Shutdown
    PROTOCOL_ERROR = 128, // Protocol error (APNs could not parse the notification)
    UNKNOWN_ERROR = 255 // None (unknown)
};

};

class Notification {
public:
    explicit Notification(std::string token, std::string payload, uint32_t ttl) :
        token_(::token(std::move(token))),
        payload_(std::move(payload)),
        seq_(htonl(42)),
        ttl_(htonl(ttl)),
        prio_(10)
    {}

    std::unique_ptr<folly::IOBuf> serialize() const {
        auto notification = folly::IOBuf::create(0);
        notification->prependChain(item(1, token_.data(), token_.size()));
        notification->prependChain(item(2, payload_.data(), payload_.size()));
        notification->prependChain(item(3, &seq_, sizeof(seq_)));
        notification->prependChain(item(4, &ttl_, sizeof(ttl_)));
        notification->prependChain(item(5, &prio_, sizeof(prio_)));
        return notification;
    }

    size_t size() const {
        return 5 * 3 + token_.size() + payload_.size() + sizeof(seq_) + sizeof(ttl_) + sizeof(prio_);
    }

private:    
    std::array<uint8_t, 32> token_;
    std::string payload_;
    uint32_t seq_;
    uint32_t ttl_;
    uint8_t prio_;
};


class NotificationEncoder : public wangle::MessageToByteEncoder<Notification> {
public:
    virtual std::unique_ptr<folly::IOBuf> encode(Notification& msg) override {
        auto len = sizeof(uint8_t) + sizeof(uint32_t);
        auto message = folly::IOBuf::create(len);

        *message->writableData() = 2;
        uint32_t size = htonl(msg.size());
        memcpy(message->writableData() + 1, &size, sizeof(size));
        message->append(len); 

        message->appendChain(msg.serialize());
        return message;
    }
};

class NotificationClientPipelineFactory : public wangle::PipelineFactory<NotificationClientPipeline> {
public:
    explicit NotificationClientPipelineFactory(NotificationHandler* handler) :
        handler_(handler) {
        CHECK(handler_) << "NotificationHandler not set";
    }
    NotificationClientPipeline::Ptr newPipeline(std::shared_ptr<folly::AsyncTransportWrapper> sock) {
        auto pipeline = NotificationClientPipeline::create();
        (*pipeline)
            .addBack(wangle::AsyncSocketHandler(sock))
            .addBack(wangle::EventBaseHandler())
            .addBack(wangle::FixedLengthFrameDecoder(6))
//            .addBack(wangle::NotificationStatusDecoder())
            .addBack(NotificationEncoder())
            .addBack(handler_)
            .finalize();
        return pipeline;
    }
private:
    NotificationHandler* const handler_;
};


NotificationClient::NotificationClient(folly::EventBase* ev, NotificationClientOptions options) :
    ev_(ev),
    options_(std::make_unique<NotificationClientOptions>(std::move(options))) {
    CHECK(ev_) << "EventBase not set";
    VLOG(3) << "created for " << options_->address;
}

NotificationClient::~NotificationClient() {
    CHECK(!ev_) << "call stop()";
}
    
void NotificationClient::start() {
    CHECK(ev_->isInEventBaseThread());
    VLOG(3) << "started";
    connect();
}

void NotificationClient::stop() {
    CHECK(ev_->isInEventBaseThread());
    ev_ = nullptr;
    VLOG(3) << "stopped";
}

void NotificationClient::push(std::string token, std::string payload) {
}

void NotificationClient::connect() {
    CHECK(ev_->runInEventBaseThread([this]() {
                VLOG(3) << "AAAA";
            }));
}

void NotificationClient::read(Context* ctx, NotificationStatus msg) {
}

void NotificationClient::readEOF(Context* ctx) {
}

void NotificationClient::readException(Context* ctx, folly::exception_wrapper e) {
}

