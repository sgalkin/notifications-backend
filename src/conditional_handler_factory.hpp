#pragma once

#include <proxygen/httpserver/RequestHandlerFactory.h>
#include <proxygen/httpserver/RequestHandler.h>
#include <proxygen/httpserver/Filters.h>
#include <folly/Function.h>
#include <glog/logging.h>
#include <type_traits>

template<typename Handler>
class ConditionalHandlerFactory : public proxygen::RequestHandlerFactory {
public:
    using Condition = folly::Function<bool(proxygen::RequestHandler* h, proxygen::HTTPMessage* m)>;
    using Constructor = folly::Function<proxygen::RequestHandler*(proxygen::RequestHandler* h, proxygen::HTTPMessage* m)>;

    template<typename Condition, typename... Args>
    explicit ConditionalHandlerFactory(Condition condition, Args... args)
        : condition_(std::forward<Condition>(condition))
        , constructor_(make_constructor(std::forward<Args>(args)...))
    {}

    template<typename Condition>
    explicit ConditionalHandlerFactory(Condition condition, Constructor constructor)
        : condition_(std::forward<Condition>(condition))
        , constructor_(std::move(constructor))
    {}
    
    virtual void onServerStart(folly::EventBase* evb) noexcept override {
        VLOG(3) << "onServerStart " << evb;
    }
    
    virtual void onServerStop() noexcept override {
        VLOG(3) << "onServerStop";
    }
    
    virtual proxygen::RequestHandler* onRequest(proxygen::RequestHandler* h, proxygen::HTTPMessage* m) noexcept override {
        return condition_(h, m) ? checkPrecondition(h, m), constructor_(h, m) : h;
    }

private:
    template<typename T> using HasFilterBase = std::is_base_of<proxygen::Filter, T>;
    template<typename T> using HasHandlerBase = std::is_base_of<proxygen::RequestHandler, T>;

    template<typename T> using IsFilter = typename std::enable_if<HasFilterBase<T>::value>::type;
    template<typename T> using IsHandler = typename std::enable_if<HasHandlerBase<T>::value && !HasFilterBase<T>::value>::type;

    template<typename... Args, typename U = Handler, IsFilter<U>* = nullptr>
    static auto make_constructor(Args... args) {
        return [args...](auto h, auto m) { return new U(h, args...); };
    }
    
    template<typename... Args, typename U = Handler, IsHandler<U>* = nullptr>
    static auto make_constructor(Args... args) {
        return [args...](auto h, auto m) { return new U(args...); };
    }

    template<typename U = Handler, IsFilter<U>* = nullptr>
    static void checkPrecondition(proxygen::RequestHandler* h, proxygen::HTTPMessage*) {
        CHECK(h != nullptr);
    }

    template<typename U = Handler, IsHandler<U>* = nullptr>
    static void checkPrecondition(proxygen::RequestHandler* h, proxygen::HTTPMessage*) {
        CHECK(h == nullptr);
    }

    Condition condition_;
    Constructor constructor_;
};
