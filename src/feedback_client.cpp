#include "feedback_client.hpp"

#include <chrono>
#include <folly/io/async/AsyncSignalHandler.h>
#include <wangle/channel/Handler.h>
#include <wangle/channel/EventBaseHandler.h>
#include <wangle/channel/AsyncSocketHandler.h>
#include <wangle/codec/ByteToMessageDecoder.h>
#include <wangle/codec/LengthFieldBasedFrameDecoder.h>

#include "feedback_client_options.hpp"
#include "address.hpp"

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

class FeedbackClientPipelineFactory : public wangle::PipelineFactory<FeedbackClientPipeline> {
public:
    explicit FeedbackClientPipelineFactory(FeedbackHandler* handler) : handler_(handler) {
        CHECK(handler) << "FeedbackHandler not set";
    }
    
    FeedbackClientPipeline::Ptr newPipeline(std::shared_ptr<folly::AsyncTransportWrapper> sock) {
        auto pipeline = FeedbackClientPipeline::create();
        (*pipeline)
            .addBack(wangle::AsyncSocketHandler(sock))
            .addBack(wangle::EventBaseHandler())
            .addBack(wangle::LengthFieldBasedFrameDecoder(2, UINT_MAX, 4, 0, 0))
            .addBack(FeedbackDecoder())
            .addBack(handler_)
            .finalize();
        return pipeline;
    }
    
private:
    FeedbackHandler* const handler_;
};

FeedbackClient::FeedbackClient(folly::EventBase* ev, FeedbackClientOptions options) :
    ev_(ev),
    options_(std::make_unique<FeedbackClientOptions>(std::move(options))) {
    CHECK(ev_) << "EventBase not set";
    VLOG(3) << "created for " << options_->address
            << " with poll interval " << std::chrono::duration_cast<std::chrono::seconds>(options_->pollInterval).count() << "s";
}
            
FeedbackClient::~FeedbackClient() {
    CHECK(!ev_) << "call stop()";
}
    
void FeedbackClient::start() {
    VLOG(3) << "started";
    CHECK(ev_->isInEventBaseThread());

    bootstrap_ = std::make_unique<wangle::ClientBootstrap<FeedbackClientPipeline>>();
    bootstrap_->group(std::make_shared<wangle::IOThreadPoolExecutor>(1));
    if(options_->sslContext) bootstrap_->sslContext(options_->sslContext);
    bootstrap_->pipelineFactory(std::make_shared<FeedbackClientPipelineFactory>(this));

    connect();
}
            
void FeedbackClient::stop() {
    CHECK(ev_) << "EventBase not set";
    CHECK(ev_->isInEventBaseThread());

    if(auto pipeline = bootstrap_->getPipeline())
        pipeline->close().get();
    bootstrap_.reset();

    callback_->cancelTimeout();
    CHECK(!callback_->isScheduled());
    callback_.reset();
    
    ev_ = nullptr;
    VLOG(3) << "stopped";
}
            
void FeedbackClient::connect() noexcept {
    CHECK(ev_->runInEventBaseThread([this]() {
                CHECK(!callback_ || !callback_->isScheduled());
                callback_ = folly::AsyncTimeout::schedule(options_->pollInterval, *ev_, [this]() noexcept {
                        // TODO: kill if too many reconnects
                        try {
                            bootstrap_->connect(utils::resolveAddress(options_->address));
                        } catch(folly::AsyncSocketException& ex) {
                            LOG(ERROR) << ex.what();
                            connect();
                        }
                    });
            }));
}

void FeedbackClient::read(Context* ctx, Feedback msg) {
    LOG(INFO) << msg;
    connect();
}

void FeedbackClient::readEOF(Context* ctx) {
    close(ctx);
    connect();
}

void FeedbackClient::readException(Context* ctx, folly::exception_wrapper e) {
    LOG(ERROR) << folly::exceptionStr(e);
    close(ctx);
    connect();
}
