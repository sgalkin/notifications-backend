#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <ctime>
#include <stdexcept>

#include <glog/logging.h>
//#include <boost/optional/optional_io.hpp>

#include <folly/Exception.h>
#include <folly/ScopeGuard.h>
#include <folly/dynamic.h>
#include <folly/json.h>
//#include <folly/SpookyHashV2.h>
#include <folly/io/async/SSLContext.h>
#include <folly/io/async/AsyncSignalHandler.h>
//#include <folly/io/async/HHWheelTimer.h>

#include <wangle/codec/LengthFieldBasedFrameDecoder.h>
#include <wangle/codec/MessageToByteEncoder.h>
#include <wangle/codec/FixedLengthFrameDecoder.h>

#include <wangle/concurrent/IOThreadPoolExecutor.h>
#include <wangle/channel/AsyncSocketHandler.h>
#include <wangle/channel/EventBaseHandler.h>
#include <wangle/bootstrap/ClientBootstrap.h>
//#include <wangle/ssl/SSLUtil.h>

//#include <proxygen/httpserver/HTTPServer.h>
//#include <proxygen/httpserver/HTTPServerOptions.h>
#include <proxygen/lib/utils/CryptUtil.h>
//#include <proxygen/lib/http/HTTPMessage.h>

#include "flags.hpp"
#include "ssl.hpp"
#include "feedback_client_options.hpp"
#include "feedback.hpp"

//#define APNS_SANDBOX "gateway.sandbox.push.apple.com"
#define APNS "gateway.push.apple.com"
#define APNS_PORT 2195

//#define FEEDBACK_SANDBOX "feedback.sandbox.push.apple.com"
//#define FEEDBACK "feedback.push.apple.com"
//#define FEEDBACK_PORT 2196

//#define DEFAULT_CHAT_SANDBOX_TOKEN "da085c855269dab30b703c41f556b42e9c3be8473cdd1422927f5b78abbc7ae4"
#define DEFAULT_VOIP_SANDBOX_TOKEN "81e716225a5b100add0e73181285ec57531692a5d8bc0c83ddab120b49676d54"

#define DEFAULT_ACK_URL "http://foo.bar.com"

#if 0
//#include <openssl/sha.h>
int main(int argc, char* argv[])
{
	gflags::SetUsageMessage(std::move(std::string("Usage: ") + argv[0]));
	gflags::ParseCommandLineFlags(&argc, &argv, true);
	
	google::InitGoogleLogging(argv[0]);
	auto logguard = folly::makeGuard([]() {
			LOG(INFO) << "server is shutting down";
			google::ShutdownGoogleLogging();
            gflags::ShutDownCommandLineFlags();
		});

	google::InstallFailureSignalHandler();

//    std::array<uint8_t, SHA_DIGEST_LENGTH> buf;
//    CHECK(SHA1((uint8_t*)"Hello", 5, buf.data()));
//    std::string out;
//    CHECK(folly::hexlify(buf, out));
//    LOG(INFO) << out;
    

    FeedbackClient(FeedbackClientOptions{
                {SIGTERM, SIGINT},
                ssl::createContext(FLAGS_cert, FLAGS_key),
                {FLAGS_host, static_cast<uint16_t>(FLAGS_port), true}
        }).start();
    
//    std::thread thread([&client] { client.start(); });
//    thread.join();
    
    return EXIT_SUCCESS;
}
#endif
#if 0
#define PUSH_CN "Apple Development IOS Push Services: com.skype.hackathon"
#define VOIP_CN "VoIP Services: com.skype.hackathon"

auto ev = folly::EventBaseManager::get()->getEventBase();
//    ev->loopForever();
    auto evguard = folly::makeGuard([] {
            folly::EventBaseManager::get()->clearEventBase();
        });

    wangle::SSLContextManager ctx{ev, "XXX", true, nullptr};
    auto cert = [](const std::string& dir, const std::string& name) {
        return wangle::SSLContextConfig::CertificateInfo(
            dir + "/" + name + ".crt", dir + "/" + name + ".key", "");
    };
    for(auto&& x: { cert("/home/sgalkin/certs", "hv"), cert("/home/sgalkin/certs", "hc") }) {
        wangle::SSLContextConfig cfg;
        cfg.certificates.emplace_back(std::move(x));
        ctx.addSSLContextConfig(cfg, {}, nullptr, {}, nullptr);
    }

    CHECK(ctx.getSSLCtx(wangle::SSLContextKey(VOIP_CN)));
    CHECK(ctx.getSSLCtx(wangle::SSLContextKey(PUSH_CN)));
#endif

#if 1
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


class Notification {
public:
    explicit Notification(const std::string& payload, uint32_t ttl) :
        token_(token(std::string(DEFAULT_VOIP_SANDBOX_TOKEN))), payload_(payload), seq_(htonl(42)), ttl_(htonl(ttl)), prio_(10)
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

typedef wangle::Pipeline<folly::IOBufQueue&, Notification> ApnsBinaryPipeline;
//typedef wangle::Pipeline<folly::IOBufQueue&> FeedbackBinaryPipeline;

class NotificationEncoder : public wangle::MessageToByteEncoder<Notification>
{
public:
    virtual std::unique_ptr<folly::IOBuf> encode(Notification& msg) override
    {
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

class ApnsErrorHandler : public wangle::InboundHandler<std::unique_ptr<folly::IOBuf>, std::string>
{
public:
    virtual void read(Context* ctx, std::unique_ptr<folly::IOBuf> msg) override {
        std::string error = folly::hexDump(msg->data(), msg->length());
        LOG(INFO) << "evicted " << error;
        ctx->fireRead(error);
    }

    virtual void readEOF(Context* ctx) override {
        VLOG(3) << "EOF";
        //ctx->fireClose();
    }

    virtual void readException(Context* ctx, folly::exception_wrapper e) override {
        LOG(ERROR) << folly::exceptionStr(e);
        //ctx->fireClose();
    }       
};

class ApnsBinaryPipelineFactory : public wangle::PipelineFactory<ApnsBinaryPipeline> {
public:
    ApnsBinaryPipeline::Ptr newPipeline(std::shared_ptr<folly::AsyncTransportWrapper> sock) {
        auto pipeline = ApnsBinaryPipeline::create();
        (*pipeline)
            .addBack(wangle::AsyncSocketHandler(sock))
            .addBack(wangle::EventBaseHandler())
            .addBack(NotificationEncoder())            
            .addBack(wangle::FixedLengthFrameDecoder(6))
            .addBack(ApnsErrorHandler())
            .finalize();
        return pipeline;
    }
};

std::string ack(const std::string& base, const std::string& token)
{
    std::chrono::nanoseconds ts = std::chrono::system_clock::now().time_since_epoch();
    uint64_t hash = folly::hash::SpookyHashV2::Hash64(token.c_str(), token.size(), ts.count());
    uint8_t* bytes = reinterpret_cast<uint8_t*>(&hash);
    std::string base64 = proxygen::base64Encode(folly::ByteRange(bytes, bytes + sizeof(hash)));
    std::for_each(base64.begin(), base64.end(), [](char& x) { x = x == '/' ? '-' : x == '+' ? '_' : x; });
    return base + "/" + base64.erase(base64.size() - 1);
}


int main(int argc, char* argv[])
{
	gflags::SetUsageMessage(std::move(std::string("Usage: ") + argv[0]));
	gflags::ParseCommandLineFlags(&argc, &argv, true);
	
	google::InitGoogleLogging(argv[0]);
	auto guard = folly::makeGuard([]() {
			LOG(INFO) << "server is shutting down";
			google::ShutdownGoogleLogging();
		});

	google::InstallFailureSignalHandler();
    

//    return 0;
    
    auto tp = std::make_shared<wangle::IOThreadPoolExecutor>(1);
    auto voip_ssl = ssl::createContext("/home/sgalkin/certs/hv.crt", "/home/sgalkin/certs/hv.key");
       
    auto apns_client = std::make_unique<wangle::ClientBootstrap<ApnsBinaryPipeline>>();
    auto apns = apns_client
        ->group(tp)
        ->sslContext(voip_ssl)
        ->pipelineFactory(std::make_shared<ApnsBinaryPipelineFactory>())
        ->connect(folly::SocketAddress(APNS/*_SANDBOX*/, APNS_PORT, true)).get();
    
    folly::dynamic notification = folly::dynamic::object("url", ack(DEFAULT_ACK_URL, DEFAULT_VOIP_SANDBOX_TOKEN));
/*
    auto feedback_client = std::make_unique<wangle::ClientBootstrap<FeedbackBinaryPipeline>>();
*/
    for(int i = 0; i < 10; ++i)
    {
        apns->write(Notification(folly::json::serialize(notification, folly::json::serialization_opts()), 0)).get();
        
        sleep(15);
/*
                try {
            auto chat_feedback = feedback_client
                ->group(tp)
                ->sslContext(voip_ssl)
                ->pipelineFactory(std::make_shared<FeedbackBinaryPipelineFactory>())
                ->connect(folly::SocketAddress(FEEDBACK, FEEDBACK_PORT, true)).get();
            / *
              auto voip_feedback = feedback_client
              ->group(tp)
              ->sslContext(voip_ssl)
              ->pipelineFactory(std::make_shared<FeedbackBinaryPipelineFactory>())
              ->connect(folly::SocketAddress(FEEDBACK_SANDBOX, FEEDBACK_PORT, true)).get();
            * /
            sleep(5);
            
        } catch(folly::AsyncSocketException& ex) {
            LOG(ERROR) << ex.what();
        }
*/
    }

//    apns->close().get();    

//    sleep(10);
//    while(true)
//    {
//    }
    
	return EXIT_SUCCESS;
}
#endif
