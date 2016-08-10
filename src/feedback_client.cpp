#include "feedback_client.hpp"
#include "socket_pipeline_factory.hpp"
#include "reconnect_handler.hpp"

#include <wangle/codec/ByteToMessageDecoder.h>
#include <wangle/codec/LengthFieldBasedFrameDecoder.h>
#include <wangle/channel/Handler.h>
#include <wangle/channel/EventBaseHandler.h>
#include <wangle/channel/AsyncSocketHandler.h>

#include <chrono>
#include <string>


namespace {
std::string toUTC(time_t ts) {
    struct tm tm{0};
    CHECK(gmtime_r(&ts, &tm));
    char buf[25]{0};
    CHECK(std::strftime(buf, sizeof(buf), "%FT%T%z", &tm));
    return buf;
}
}

class Feedback {
//  Timestamp -- A timestamp (as a four-byte time_t value) indicating when APNs determined that the app no longer exists on the device.
//               This value, which is in network order, represents the seconds since 12:00 midnight on January 1, 1970 UTC.
//  Token length -- The length of the device token as a two-byte integer value in network order.
//  Device token -- The device token in binary format.
public:
    explicit Feedback(std::unique_ptr<folly::IOBuf> buf) :
        buf_(std::move(buf)) {
        CHECK_EQ(buf_->length(), 4 + 2 + tokenLength());
    }
    
    time_t timestamp() const {
        uint32_t ts{0};
        memcpy(&ts, buf_->data(), sizeof(ts));
        return ntohl(ts);
    }
    
    std::string datetime() const {
        return toUTC(timestamp());
    }
    
    std::string hexToken() const {
        std::string hex;
        folly::hexlify(rawToken(), hex);
        return hex;
    }
    
    folly::ByteRange rawToken() const {
        return folly::ByteRange(buf_->data() + 4 + 2, tokenLength());        
    }
    
private:
    uint16_t tokenLength() const {
        uint16_t result{0};
        memcpy(&result, buf_->data() + 4, sizeof(result));
        return ntohs(result);
    } 

    std::unique_ptr<folly::IOBuf> buf_;
};


template<typename Out>
Out& operator<< (Out& o, const Feedback& fb) {
    return o << "feedback received for " << fb.hexToken() << " at " << fb.datetime();
}


class FeedbackDecoder : public wangle::InboundHandler<std::unique_ptr<folly::IOBuf>, Feedback> {    
public:
    virtual void read(Context* ctx, std::unique_ptr<folly::IOBuf> buf) override {
        ctx->fireRead(Feedback(std::move(buf)));
    }
};

class FeedbackHandler : public wangle::HandlerAdapter<Feedback, std::unique_ptr<folly::IOBuf>> {
public:
    virtual void read(Context* ctx, Feedback msg) override {
        LOG(INFO) << msg;
    }
};

std::shared_ptr<wangle::PipelineFactory<FeedbackClientPipeline>> FeedbackClientPipelineFactory(
    Client<FeedbackClientPipeline>* client, std::chrono::seconds reconnectTimeout) {
    return SocketPipelineFactory<FeedbackClientPipeline>::create(
        ReconnectHandler<FeedbackClientPipeline>(client, std::move(reconnectTimeout)),
        wangle::LengthFieldBasedFrameDecoder(2, UINT_MAX, 4, 0, 0),
        FeedbackDecoder(),
        FeedbackHandler());
}
