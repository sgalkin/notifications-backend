#include <jemalloc/jemalloc.h>
/*
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wbool-compare"
#include <folly/FBString.h>
#pragma GCC diagnostic pop
*/
#include "flags.hpp"

#include "environment.hpp"
#include "feedback_client.hpp"
#include "notification_client.hpp"
#include "mulitplexed_notification_queue.hpp"

#include "conditional_handler_factory.hpp"
#include "direct_response_handler.hpp"

#include "ssl.hpp"

#include <proxygen/httpserver/HTTPServer.h>
#include <proxygen/httpserver/HTTPServerOptions.h>
#include <proxygen/httpserver/RequestHandlerFactory.h>
#include <proxygen/httpserver/RequestHandler.h>
#include <proxygen/httpserver/ResponseBuilder.h>
#include <proxygen/lib/http/HTTPMessage.h>
#include <wangle/ssl/SSLContextConfig.h>
#include <proxygen/lib/utils/CryptUtil.h>
#include <proxygen/httpserver/filters/DirectResponseHandler.h>

//#include <wangle/codec/LengthFieldBasedFrameDecoder.h>
//#include <wangle/codec/MessageToByteEncoder.h>
//#include <wangle/codec/FixedLengthFrameDecoder.h>
#include <wangle/concurrent/GlobalExecutor.h>
//#include <wangle/concurrent/IOThreadPoolExecutor.h>
//#include <wangle/channel/AsyncSocketHandler.h>
//#include <wangle/channel/EventBaseHandler.h>
//#include <wangle/channel/Pipeline.h>
//#include <wangle/bootstrap/ClientBootstrap.h>
//#include <wangle/ssl/SSLUtil.h>

#include <folly/Exception.h>
#include <folly/ScopeGuard.h>

#include <folly/dynamic.h>
#include <folly/json.h>

#include <folly/SpookyHashV2.h>
#include <folly/io/async/AsyncSignalHandler.h>
//#include <folly/io/async/HHWheelTimer.h>
#include <folly/io/async/NotificationQueue.h>
#include <folly/AtomicLinkedList.h>
#include <glog/logging.h>
//#include <boost/optional/optional_io.hpp>

#include <chrono>

//#include <unistd.h>
#include <signal.h>
#include <netinet/in.h>
//#include <time.h>

//#include <stdexcept>
//#include <sys/types.h>
//#include <sys/stat.h>

#define DEFAULT_ACK_URL "https://sgcldasd.duckdns.org"

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
        
        auto ssl = ssl::createContext(FLAGS_apns_cert, FLAGS_apns_key);

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

class EnsureNoUpgradeFilter : public proxygen::Filter {
public:
    explicit EnsureNoUpgradeFilter(proxygen::RequestHandler* upstream) :
        proxygen::Filter(upstream)
    {}

    virtual void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override {
        if(upstream_) {
            upstream_->onBody(std::move(body));
        }
    }
    
    // Invoked when the session has been upgraded to a different protocol
    virtual void onUpgrade(proxygen::UpgradeProtocol prot) noexcept override {
        CHECK(UNLIKELY(false)) << "Can't upgrade to " << folly::to<std::string>(prot);
        upstream_->onError(proxygen::kErrorMethodNotSupported);
        upstream_ = nullptr;
        proxygen::ResponseBuilder(downstream_).rejectUpgradeRequest();
    }

    virtual void requestComplete() noexcept override {
        if(upstream_) {
            upstream_->requestComplete();
            upstream_ = nullptr;
        }
        delete this;
    }

    virtual void onError(proxygen::ProxygenError err) noexcept override {
        if(upstream_) {
            upstream_->onError(err);
            upstream_ = nullptr;
        }
        delete this;
    }
};



class AccessLogFilter : public proxygen::Filter {
    struct Context {
        proxygen::TimePoint created;
        std::chrono::milliseconds spent{0};
        
        folly::SocketAddress client;
        std::string version;
        std::string method;
        std::string url;
        std::string userAgent;
        size_t bytesIn{0};

        uint16_t statusCode{500};
        size_t bytesOut{0};

        proxygen::ProxygenError error{proxygen::ProxygenError::kErrorNone};

        explicit Context(const proxygen::HTTPMessage* msg) :
            created(msg->getStartTime()),
            client(msg->getClientAddress()),
            version(msg->getVersionString()),
            method(msg->getMethodString()),
            url(msg->getURL()),
            userAgent(msg->getHeaders().getSingleOrEmpty(proxygen::HTTP_HEADER_USER_AGENT)) {
            CHECK(msg->isRequest()) << "Not a request";
        }
        
        void captureResponse(const proxygen::HTTPMessage* msg) {
            CHECK(msg->isResponse()) << "Not a response";
            statusCode = msg->getStatusCode();
        }

        Context& finalize(proxygen::ProxygenError err = proxygen::ProxygenError::kErrorNone) { 
            spent = proxygen::millisecondsSince(created);
            error = err;
            return *this;
        }
    };

    template<typename Out>
    friend Out& operator<<(Out& out, const Context& ctx);
    
public:    
    explicit AccessLogFilter(proxygen::RequestHandler* upstream, const proxygen::HTTPMessage* msg) :
        proxygen::Filter(upstream),
        ctx_(msg)
    {}

    virtual void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override {
        ctx_.bytesIn += body->length();
        proxygen::Filter::onBody(std::move(body));
    }

    virtual void requestComplete() noexcept override {
        DLOG(INFO) << ctx_.finalize(); // todo: send to file/db
        proxygen::Filter::requestComplete();
    }

    virtual void sendHeaders(proxygen::HTTPMessage& msg) noexcept override {
        ctx_.captureResponse(&msg);
        proxygen::Filter::sendHeaders(msg);
    }
    
    virtual void sendBody(std::unique_ptr<folly::IOBuf> body) noexcept override {
        ctx_.bytesOut += body->length();
        proxygen::Filter::sendBody(std::move(body));
    }

    virtual void onError(proxygen::ProxygenError err) noexcept override {
        DLOG(ERROR) << ctx_.finalize(err); // todo: send to file/db
        proxygen::Filter::onError(err);
    }

private:
    Context ctx_;
};

template<typename Out>
Out& operator<<(Out& out, const AccessLogFilter::Context& ctx) {
    proxygen::SystemTimePoint tp{proxygen::toSystemTimePoint(ctx.created)};
    auto tm = decltype(tp)::clock::to_time_t(tp);

    // [date] [ip] [port] [method] [url] [in] [code] [out] [spent] [user_agent] [error]
    return out << std::put_time(std::gmtime(&tm), "%FT%T.")
               << std::chrono::duration_cast<std::chrono::microseconds>(
                   tp - std::chrono::time_point_cast<std::chrono::seconds>(tp)).count() << "+0000 "
               << ctx.client.getAddressStr() << " " << ctx.client.getPort() << " "
               << "\"" << (ctx.method.empty() ? "-" : ctx.method) << " " << ctx.url << " HTTP/" << ctx.version << "\" "
               << ctx.bytesIn << " " << ctx.statusCode << " " << ctx.bytesOut << " " << ctx.spent.count() << " "
               << "\"" << ctx.userAgent << "\" "
               << (ctx.error == proxygen::ProxygenError::kErrorNone ? "-" : proxygen::getErrorString(ctx.error));
}
                  

class OKHandler : public proxygen::RequestHandler {
public:
    virtual void onRequest(std::unique_ptr<proxygen::HTTPMessage> headers) noexcept override {}
    virtual void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override {}
    virtual void onEOM() noexcept override {}
    virtual void onUpgrade(proxygen::UpgradeProtocol prot) noexcept override {}

    /**
     * Invoked when request processing has been completed and nothing more
     * needs to be done. This may be a good place to log some stats and
     * clean up resources. This is distinct from onEOM() because it is
     * invoked after the response is fully sent. Once this callback has been
     * received, `downstream_` should be considered invalid.
     */
    virtual void requestComplete() noexcept override {
        delete this;
    }

    /**
     * Request failed. Maybe because of read/write error on socket or client
     * not being able to send request in time.
     *
     * NOTE: Can be invoked at any time (except for before onRequest).
     *
     * No more callbacks will be invoked after this. You should clean up after
     * yourself.
     */
    virtual void onError(proxygen::ProxygenError err) noexcept override {
        delete this;
    }
};


    // /m - monitoring
    // /r - register
    // /p - push
    // /a - acknowledge
    // /s - stats



int main(int argc, char* argv[])
{
	gflags::SetUsageMessage(std::move(std::string("Usage: ") + argv[0]));
	gflags::ParseCommandLineFlags(&argc, &argv, true);
	
	google::InitGoogleLogging(argv[0]);
	auto logguard = folly::makeGuard([]() {
			LOG(INFO) << "server is shutting down";
            malloc_stats_print(NULL, NULL, NULL);
			google::ShutdownGoogleLogging();
            gflags::ShutDownCommandLineFlags();
		});

	google::InstallFailureSignalHandler();

    auto cpus = sysconf(_SC_NPROCESSORS_ONLN);
    folly::checkUnixError(cpus, "unable to detect number of active cpus");
    VLOG(3) << cpus << " active cpu(s)";

    auto always = [](auto, auto) { return true; };
    
    proxygen::HTTPServerOptions options;
    options.threads = std::max(1l, cpus - 1);
    options.handlerFactories = proxygen::RequestHandlerChain()
        .addThen<ConditionalHandlerFactory<EnsureNoUpgradeFilter>>(always)
        .addThen<ConditionalHandlerFactory<AccessLogFilter>>(
            always,
            ConditionalHandlerFactory<AccessLogFilter>::Constructor(
                [](auto h, auto m) { return new AccessLogFilter(h, m); }))
        .addThen<ConditionalHandlerFactory<DirectResponseHandler<CloseConnection>>>(
            [](auto h, auto m) { return h == nullptr; }, 404)
        .addThen<ConditionalHandlerFactory<DirectResponseHandler<CloseConnection>>>(
            [](auto h, auto m) { return m->getPath() == "/m"; }, 204)
        .build();
    options.idleTimeout = std::chrono::seconds{300};
    options.listenBacklog = 1024;
    options.shutdownOn = {SIGTERM, SIGINT}; // temporary
    options.supportsConnect = false;
    options.enableContentCompression = true;
    options.contentCompressionMinimumSize = 1280;
    options.contentCompressionLevel = 3;

    wangle::SSLContextConfig sslConfig;
    sslConfig.certificates = {
        wangle::SSLContextConfig::CertificateInfo(FLAGS_https_cert, FLAGS_https_key, "")};
    sslConfig.isLocalPrivateKey = true;
    sslConfig.isDefault = true;
    
    std::vector<proxygen::HTTPServer::IPConfig> ips{{
         folly::SocketAddress(folly::IPAddressV6(), FLAGS_https_port),
         proxygen::HTTPServer::Protocol::HTTP}};
    ips.front().sslConfigs = {std::move(sslConfig)};
    
    proxygen::HTTPServer server(std::move(options));
    server.bind(ips);
    // server.setSessionInfoCallback
    auto t = std::thread([&server] { server.start(); });
    
    //   sleep(10);
    //nm.putMessage(42);

    t.join();
    return 0;
    
    apn::binary::PushService service;
    std::thread thread([&service] { service.start(); });

    sleep(5);
    std::string token{"81e716225a5b100add0e73181285ec57531692a5d8bc0c83ddab120b49676d54"};
//    for(size_t i = 0; i < 10; ++i) {
//    while(true) {
    while(false) {
        service.send(token, folly::json::serialize(
                         folly::dynamic::object("url", ack(DEFAULT_ACK_URL, token)),
                         folly::json::serialization_opts()));
        sleep(5);
    }
    
    thread.join();
    return EXIT_SUCCESS;
}


/***

struct A {
    A() { VLOG(3) << "A::A " << this; }
    A(const A&) = delete;
    A& operator=(const A&) = delete;
    A(A&& a) { VLOG(3) << "M " << &a << " -> " << this; }
    A& operator=(A&&) = default;
    ~A() { VLOG(3) << "A::~A " << this; }
};

    MulitplexedNotificationQueue<A> nm;
    auto io = std::make_shared<wangle::IOThreadPoolExecutor>(2);
    wangle::setIOExecutor(io);
//    std::thread th([&nm](){ nm.start(); });
    wangle::getIOExecutor()->add([&nm]() { nm.start(); });
    sleep(2);
    auto h = [](std::shared_ptr<A>&& p) noexcept { VLOG(3) << "Got it!"; };
    auto c1 = MulitplexedNotificationQueue<A>::Consumer::make(h);
    auto c2 = MulitplexedNotificationQueue<A>::Consumer::make(h);
    wangle::getIOExecutor()->add([&c1, &nm]() { nm.addConsumer(c1.get()); });
    wangle::getIOExecutor()->add([&c2, &nm]() { nm.addConsumer(c2.get()); });
                sleep(2);
                
    nm.putMessage(A());
    A a;
    //nm.putMessage(a);
    nm.putMessage(std::move(a));
//    folly::NotificationQueue<int> nq;

    
    sleep(10);
    wangle::getIOExecutor()->add([&c1, &nm]() { nm.removeConsumer(c1.get()); });
    wangle::getIOExecutor()->add([&c2, &nm]() { nm.removeConsumer(c2.get()); });
    sleep(6);
    nm.stop();
    io->join();
    return 0;

 ***/
