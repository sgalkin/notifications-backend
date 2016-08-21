#define GOOGLE_STRIP_LOG 0

#include <jemalloc/jemalloc.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wbool-compare"
#include <folly/FBString.h>
#pragma GCC diagnostic pop

#include "flags.hpp"

#include "environment.hpp"
#include "feedback_client.hpp"
#include "notification_client.hpp"

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
    explicit EnsureNoUpgradeFilter(proxygen::RequestHandler* upstream, proxygen::HTTPMessage*) :
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
                  
template<typename F>
class SimpleFilterFactory : public proxygen::RequestHandlerFactory {
public:
    /*
    explicit SimpleFilterFactory(C check=[](proxygen::HTTPMessage*){return true;}) :
        check_(std::forward<C>(check))
    {}
    */  
    virtual void onServerStart(folly::EventBase* evb) noexcept override {}
    virtual void onServerStop() noexcept override {}
    virtual proxygen::RequestHandler* onRequest(proxygen::RequestHandler* h, proxygen::HTTPMessage* m) noexcept override {
        return new F(h, m);
    }
private:
    //C check_;
};

class OKHandler : public proxygen::RequestHandler {
public:
    virtual void onRequest(std::unique_ptr<proxygen::HTTPMessage> headers) noexcept override {
        VLOG(3) << "onRequest " << this;
    }

     // Invoked when we get part of body for the request.
    virtual void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override {
        VLOG(3) << "onBody " << this;
    }

    virtual void onEOM() noexcept override {
        VLOG(3) << "onEOM " << this;
        proxygen::ResponseBuilder(downstream_)
            .status(204, "No Content")
            .closeConnection()
            .sendWithEOM();
    }
    
    virtual void onUpgrade(proxygen::UpgradeProtocol prot) noexcept override { CHECK(UNLIKELY(false)); }

    /**
     * Invoked when request processing has been completed and nothing more
     * needs to be done. This may be a good place to log some stats and
     * clean up resources. This is distinct from onEOM() because it is
     * invoked after the response is fully sent. Once this callback has been
     * received, `downstream_` should be considered invalid.
     */
    virtual void requestComplete() noexcept override {
        VLOG(3) << "requestComplete " << this;
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



class OKHandlerFactory : public proxygen::RequestHandlerFactory {

    struct Consumer : public folly::NotificationQueue<int>::Consumer {
        virtual void messageAvailable(int&& message) override {
            VLOG(3) << message;
        }
    };
    
public:
    // /m - monitoring
    // /r - register
    // /p - push
    // /a - acknowledge
    // /s - stats
    
    explicit OKHandlerFactory(folly::NotificationQueue<int>* queue, bool x) :
        queue_(queue), x_(x) {
        CHECK(queue_);
        VLOG(3) << "OKHandlerFactory";
    }
    
    virtual void onServerStart(folly::EventBase* evb) noexcept override {
        VLOG(3) << "onServerStart " << evb;
        consumer_.reset(new Consumer);
        consumer_->startConsuming(evb, queue_);
    }
    virtual void onServerStop() noexcept override {
        VLOG(3) << "onServerStop";
        consumer_->stopConsuming();
        consumer_.reset();
        queue_ = nullptr;
    }
    virtual proxygen::RequestHandler* onRequest(proxygen::RequestHandler* h, proxygen::HTTPMessage* m) noexcept override {
        VLOG(3) << "onRequest " << h;
        m->dumpMessage(4);
        //if((m->getPath() == "/m") == x_)
        //{
        //    auto z = new OKHandler;
        //    VLOG(3) << "here " << z;
        //    return z;//proxygen::DirectResponseHandler(204, "No Content", "");
        // }
        //VLOG(3) << "here";
        return new OKHandler;//proxygen::DirectResponseHandler(404, "Not Found", "");
        /*class DirectResponseHandler : public RequestHandler {
        public:
            DirectResponseHandler(int code,
                                  std::string message,
                                  std::string body)
                : code_(code),
                  message_(message),
                  body_(folly::IOBuf::copyBuffer(body)) {
            }

            void onRequest(std::unique_ptr<HTTPMessage> headers) noexcept override {
            }

            void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override {
            }

            void onUpgrade(proxygen::UpgradeProtocol prot) noexcept override {
            }

            void onEOM() noexcept override {
                ResponseBuilder(downstream_)
                    .status(code_, message_)
                    .body(std::move(body_))
                    .sendWithEOM();
            }

            void requestComplete() noexcept override {
                delete this;
            }

            void onError(ProxygenError err) noexcept override {
                delete this;
            }

        private:
            const int code_;
            const std::string message_;
            std::unique_ptr<folly::IOBuf> body_;
        };
        */
            //new OKHandler;
        //OKHandler();
    }
private:
    folly::NotificationQueue<int>* queue_;
    folly::ThreadLocalPtr<Consumer> consumer_;
    bool x_;
};


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

    folly::NotificationQueue<int> nq;
    
    proxygen::HTTPServerOptions options;
    options.threads = 4;
    options.handlerFactories = proxygen::RequestHandlerChain()
        .addThen<SimpleFilterFactory<EnsureNoUpgradeFilter>>()
        .addThen<SimpleFilterFactory<AccessLogFilter>>()
        .addThen<OKHandlerFactory>(&nq, true)
        .build();
    options.idleTimeout = std::chrono::seconds{300};
    options.listenBacklog = 1024;
    options.shutdownOn = {SIGTERM, SIGINT}; // temporary
    options.supportsConnect = false;
    options.enableContentCompression = true;
    options.contentCompressionMinimumSize = 512;
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
    
    sleep(10);
    nq.putMessage(42);

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
