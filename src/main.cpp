#include "flags.hpp"

#include "environment.hpp"
#include "feedback_client.hpp"
#include "notification_client.hpp"

#include "ssl.hpp"

//#include <proxygen/httpserver/HTTPServer.h>
//#include <proxygen/httpserver/HTTPServerOptions.h>
#include <proxygen/lib/utils/CryptUtil.h>
//#include <proxygen/lib/http/HTTPMessage.h>

#include <wangle/codec/LengthFieldBasedFrameDecoder.h>
#include <wangle/codec/MessageToByteEncoder.h>
#include <wangle/codec/FixedLengthFrameDecoder.h>

#include <wangle/concurrent/IOThreadPoolExecutor.h>
#include <wangle/channel/AsyncSocketHandler.h>
#include <wangle/channel/EventBaseHandler.h>
#include <wangle/channel/Pipeline.h>
#include <wangle/bootstrap/ClientBootstrap.h>
//#include <wangle/ssl/SSLUtil.h>

#include <folly/Exception.h>
#include <folly/ScopeGuard.h>
#include <folly/dynamic.h>
#include <folly/json.h>
#include <folly/SpookyHashV2.h>
#include <folly/io/async/AsyncSignalHandler.h>
#include <folly/io/async/HHWheelTimer.h>

#include <glog/logging.h>
//#include <boost/optional/optional_io.hpp>

#include <chrono>

#include <unistd.h>
#include <signal.h>
#include <time.h>

#include <stdexcept>
#include <sys/types.h>
#include <sys/stat.h>

#define DEFAULT_ACK_URL "http://foo.bar.com"

namespace apn {
namespace binary {
class PushService final {
public:
    ~PushService() {
        CHECK(!ev_) << "not stopped";
    }

    void start() {
        CHECK(!ev_) << "already started";
        
        ev_ = folly::EventBaseManager::get()->getEventBase();
        signal_handler_ = std::make_unique<SignalHandler>(this, std::vector<int>{SIGINT, SIGTERM});
        
        auto ssl = ssl::createContext(FLAGS_cert, FLAGS_key);

        sender_ = std::make_unique<Sender>();

        namespace env = apn::binary::production;        
        apn_ = std::make_unique<NotificationClient>(ClientOptions{ssl, env::gateway});
        apn_->start(sender_->createPipelineFactory(apn_.get(), std::chrono::seconds{1}));
    
        feedback_ = std::make_unique<FeedbackClient>(ClientOptions{ssl, env::feedback, std::chrono::seconds{6}});
        feedback_->start(FeedbackClientPipelineFactory(feedback_.get(), std::chrono::seconds{60}));
        
        ev_->loopForever();
    }
            
    void stop() {
        CHECK(ev_) << "not started";

        signal_handler_.reset();

        sender_.reset();
        
        apn_->stop();
        apn_.reset();

        feedback_->stop();        
        feedback_.reset();
        
        ev_->terminateLoopSoon();
        ev_ = nullptr;       
    }

    void send(std::string token, std::string payload) {
        apn_->pipeline().then([&, this](NotificationClientPipeline* p) {
                sender_->send(p, std::move(token), std::move(payload));
            });
    }
    
private:
    class SignalHandler;
    
    folly::EventBase* ev_{nullptr};
    std::unique_ptr<SignalHandler> signal_handler_;

    std::unique_ptr<Sender> sender_;
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
}

namespace {
std::string ack(const std::string& base, const std::string& token)
{
    std::chrono::nanoseconds ts = std::chrono::system_clock::now().time_since_epoch();
    uint64_t hash = folly::hash::SpookyHashV2::Hash64(token.c_str(), token.size(), ts.count());
    uint8_t* bytes = reinterpret_cast<uint8_t*>(&hash);
    std::string base64 = proxygen::base64Encode(folly::ByteRange(bytes, bytes + sizeof(hash)));
    std::for_each(base64.begin(), base64.end(), [](char& x) { x = x == '/' ? '-' : x == '+' ? '_' : x; });
    return base + "/" + base64.erase(base64.size() - 1);
}

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

    apn::binary::PushService service;
    std::thread thread([&service] { service.start(); });

    sleep(5);
    std::string token{"71e716225a5b100add0e73181285ec57531692a5d8bc0c83ddab120b49676d54"};
    for(size_t i = 0; i < 10; ++i) {
        service.send(token, folly::json::serialize(
                         folly::dynamic::object("url", ack(DEFAULT_ACK_URL, token)),
                         folly::json::serialization_opts()));
        sleep(5);
    }
    
    thread.join();
    return EXIT_SUCCESS;
}
