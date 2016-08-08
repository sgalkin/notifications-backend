#include "feedback.hpp"

#include <chrono>
#include <folly/io/async/AsyncSignalHandler.h>
#include <wangle/channel/AsyncSocketHandler.h>
#include <wangle/codec/ByteToMessageDecoder.h>
#include <wangle/codec/LengthFieldBasedFrameDecoder.h>

namespace {
std::string toUTC(time_t ts) {
    struct tm tm{0};
    CHECK(gmtime_r(&ts, &tm));
    char buf[25]{0};
    CHECK(std::strftime(buf, sizeof(buf), "%FT%T%z", &tm));
    return buf;
}
}

class FeedbackClient;
class FeedbackClientSignalHandler : private folly::AsyncSignalHandler {
public:
    explicit FeedbackClientSignalHandler(FeedbackClient* client) :
        folly::AsyncSignalHandler(folly::EventBaseManager::get()->getEventBase()),
        client_(client) {}

    void install(const std::vector<int>& signals) {
        for(auto s: signals) registerSignalHandler(s);
    }

private:
    void signalReceived(int signum) noexcept override;
    
    FeedbackClient* const client_{nullptr};
};


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

class DisconnectHandler : public wangle::BytesToBytesHandler {
public:
    virtual void readEOF(Context* ctx) override {
        VLOG(3) << "EOF";
        close(ctx);
    }
    virtual void readException(Context* ctx, folly::exception_wrapper e) override {
        LOG(ERROR) << folly::exceptionStr(e);
        close(ctx);
    }
};

class FeedbackDecoder : public wangle::InboundHandler<std::unique_ptr<folly::IOBuf>, Feedback> {    
public:
    virtual void read(Context* ctx, std::unique_ptr<folly::IOBuf> buf) override {
        ctx->fireRead(Feedback(std::move(buf)));
    }
};


class FeedbackClientHandler : public wangle::InboundHandler<Feedback>
{
public:
    virtual void read(Context* ctx, Feedback msg) override { LOG(INFO) << msg; }
};

class FeedbackClientPipelineFactory : public wangle::PipelineFactory<FeedbackClientPipeline> {
public:
//    explicit FeedbackClientPipelineFactory()
    FeedbackClientPipeline::Ptr newPipeline(std::shared_ptr<folly::AsyncTransportWrapper> sock) {
        auto pipeline = FeedbackClientPipeline::create();
        (*pipeline)
            .addBack(wangle::AsyncSocketHandler(sock))
//            .addBack(wangle::EventBaseHandler()) // never writes there
            .addBack(DisconnectHandler())
            .addBack(wangle::LengthFieldBasedFrameDecoder(2, UINT_MAX, 4, 0, 0))
            .addBack(FeedbackDecoder())
            .addBack(FeedbackClientHandler())
            .finalize();
        return pipeline;
    }
};

FeedbackClient::FeedbackClient(FeedbackClientOptions options) :
    options_(std::make_unique<FeedbackClientOptions>(std::move(options))) {
}
            
FeedbackClient::~FeedbackClient() { CHECK(!ev_) << "call stop()"; }
    
void FeedbackClient::start() {
    ev_ = folly::EventBaseManager::get()->getEventBase();
    if(!options_->shutdownOn.empty()) {
        handler_ = std::make_unique<FeedbackClientSignalHandler>(this);
        handler_->install(options_->shutdownOn);
    }

    bootstrap_ = std::make_unique<wangle::ClientBootstrap<FeedbackClientPipeline>>();
    bootstrap_->group(std::make_shared<wangle::IOThreadPoolExecutor>(1));
    if(options_->sslContext) bootstrap_->sslContext(options_->sslContext);
    bootstrap_->pipelineFactory(std::make_shared<FeedbackClientPipelineFactory>());

    folly::AsyncTimeout::schedule(std::chrono::seconds(5), *ev_, [this]() noexcept { ping(); });
    ev_->loopForever();
}
            
void FeedbackClient::stop() {
    CHECK(ev_);
    handler_.reset();
    bootstrap_.reset();
    ev_->terminateLoopSoon();
    ev_ = nullptr;
}
            
void FeedbackClient::ping() noexcept {
    VLOG(3) << "FeedbackClient::ping";
    try {
        bootstrap_->connect(options_->address);
    } catch(folly::AsyncSocketException& ex) {
        LOG(ERROR) << ex.what();
    }
}
    

void FeedbackClientSignalHandler::signalReceived(int signum) noexcept {
    CHECK(client_);
    client_->stop();
}

struct FeedbackServiceOptions {    
    std::chrono::milliseconds pollInterval;
};
    

class FeedbackService final {
public:
    explicit FeedbackService(FeedbackServiceOptions options) :
        options_(std::make_unique<FeedbackServiceOptions>(std::move(options))) {}
private:
    std::unique_ptr<FeedbackServiceOptions> options_;
};

