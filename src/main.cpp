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
//#include <folly/dynamic.h>
//#include <folly/json.h>
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

#include "feedback_client.hpp"
#include "feedback_client_options.hpp"
#include "notification_client.hpp"
#include "notification_client_options.hpp"

#define DEFAULT_ACK_URL "http://foo.bar.com"

#if 1

namespace apple {
class PushService final {
public:
//    PushService(folly::EventBase* ev) : ev_(ev) {}
    
    ~PushService() { CHECK(!ev_) << "call stop()"; }

    void start() {
        ev_ = folly::EventBaseManager::get()->getEventBase();
/*
        if(!options_->shutdownOn.empty()) {
            handler_ = std::make_unique<FeedbackClientSignalHandler>(this);
            handler_->install(options_->shutdownOn);
        }

        bootstrap_ = std::make_unique<wangle::ClientBootstrap<FeedbackClientPipeline>>();
        bootstrap_->group(std::make_shared<wangle::IOThreadPoolExecutor>(1));
        if(options_->sslContext) bootstrap_->sslContext(options_->sslContext);
        bootstrap_->pipelineFactory(std::make_shared<FeedbackClientPipelineFactory>());

        folly::AsyncTimeout::schedule(std::chrono::seconds(5), *ev_, [this]() noexcept { ping(); });
*/
        signal_handler_ = std::make_unique<SignalHandler>(this, std::vector<int>{SIGINT, SIGTERM});
        
        auto ssl = ssl::createContext(FLAGS_cert, FLAGS_key);
        
        apn_ = std::make_unique<NotificationClient>(ev_, NotificationClientOptions{ssl, APNS});
        apn_->start();

        feedback_ = std::make_unique<FeedbackClient>(ev_, FeedbackClientOptions{ssl, FEEDBACK, std::chrono::seconds{5}});
        feedback_->start();
        
        ev_->loopForever();
    }
            
    void stop() {
        CHECK(ev_) << "service not started";

        signal_handler_.reset();
        
        apn_->stop();
        apn_.reset();

        feedback_->stop();
        feedback_.reset();
        
        ev_->terminateLoopSoon();
        ev_ = nullptr;       
    }
    
private:
    class SignalHandler;
    
    folly::EventBase* ev_{nullptr};
    std::unique_ptr<SignalHandler> signal_handler_;
    std::unique_ptr<NotificationClient> apn_;
    std::unique_ptr<FeedbackClient> feedback_;
};

class PushService::SignalHandler : folly::AsyncSignalHandler {
public:
    explicit SignalHandler(PushService* host, std::vector<int> signals) :
        folly::AsyncSignalHandler(folly::EventBaseManager::get()->getEventBase()),
        host_(host) {
        for(auto s: signals) registerSignalHandler(s);
    }

private:
    virtual void signalReceived(int signum) noexcept override {
        CHECK(host_);
        host_->stop();
    }
    
    PushService* const host_;
};
    
}

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

    apple::PushService service;
    std::thread thread([&service] { service.start(); });
        
    thread.join();
    return EXIT_SUCCESS;
}
#endif
#if 0
//#include <openssl/sha.h>

//    std::array<uint8_t, SHA_DIGEST_LENGTH> buf;
//    CHECK(SHA1((uint8_t*)"Hello", 5, buf.data()));
//    std::string out;
//    CHECK(folly::hexlify(buf, out));
//    LOG(INFO) << out;

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

#if 0


//typedef wangle::Pipeline<folly::IOBufQueue&> FeedbackBinaryPipeline;


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
