#include "notification_client.hpp"
#include "socket_pipeline_factory.hpp"
#include "reconnect_handler.hpp"

#include <wangle/channel/Handler.h>
#include <wangle/codec/MessageToByteEncoder.h>
#include <wangle/codec/FixedLengthFrameDecoder.h>
#include <folly/io/IOBuf.h>

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


class NotificationStatus
{
public:
    explicit NotificationStatus(std::unique_ptr<folly::IOBuf> buf) :
        buf_(std::move(buf)) {
        CHECK_EQ(buf_->length(), 6);
        CHECK_EQ(*buf_->data(), 8);
    }

    const std::string& status() const { return names_.at(Status(*(buf_->data() + 1))); }
    const uint32_t seq() const { uint32_t s{0}; memcpy(&s, buf_->data() + 2, sizeof(s)); return ntohl(s); }
    
private:
    enum class Status : std::uint8_t {
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

    
    std::unique_ptr<folly::IOBuf> buf_;
    const std::map<Status, std::string> names_{
        {Status::SUCCESS, "No errors encountered"},
        {Status::PROCESSING_ERROR, "Processing error"},
        {Status::NO_TOKEN, "Missing device token"},
        {Status::NO_TOPIC, "Missing topic"},
        {Status::NO_PAYLOAD, "Missing payload"},
        {Status::INVALID_TOKEN_SIZE, "Invalid token size"},
        {Status::INVALID_TOPIC_SIZE, "Invalid topic size"},
        {Status::INVALID_PAYLOAD_SIZE, "Invalid payload size"},
        {Status::INVALID_TOKEN, "Invalid token"},
        {Status::SHUTDOWN, "Shutdown"},
        {Status::PROTOCOL_ERROR, "Protocol error (APNs could not parse the notification)"},
        {Status::UNKNOWN_ERROR, "None (unknown)"}};

};

template<typename Out>
Out& operator<< (Out& o, const NotificationStatus& ns) {
    return o << "notification " << ns.seq() << " failed " << ns.status();
}

class NotificationStatusDecoder : public wangle::InboundHandler<std::unique_ptr<folly::IOBuf>, NotificationStatus> {    
public:
    virtual void read(Context* ctx, std::unique_ptr<folly::IOBuf> buf) override {
        ctx->fireRead(NotificationStatus(std::move(buf)));
    }
};

class NotificationStatusHandler : public wangle::InboundHandler<NotificationStatus, folly::Unit> {
public:
    virtual void read(Context* ctx, NotificationStatus status) override {
        LOG(ERROR) << std::move(status);
    }    
};

class Notification {
public:
    Notification(std::string token, std::string payload, uint32_t ttl) :
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




void Sender::send(NotificationClientPipeline* pipeline, std::string token, std::string payload) {
    CHECK(pipeline);
    VLOG(3) << "sending notification to " << token;
    pipeline->write(Notification(std::move(token), std::move(payload), 0));
}


std::shared_ptr<wangle::PipelineFactory<NotificationClientPipeline>> Sender::createPipelineFactory(
    Client<NotificationClientPipeline>* client, std::chrono::seconds reconnectTimeout) {
    return SocketPipelineFactory<NotificationClientPipeline>::create(
        ReconnectHandler<NotificationClientPipeline>(client, std::move(reconnectTimeout)),
        wangle::FixedLengthFrameDecoder(6),
        NotificationStatusDecoder(),
        NotificationStatusHandler(),
        NotificationEncoder());
}
