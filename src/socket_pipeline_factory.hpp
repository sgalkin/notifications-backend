#pragma once

#include <wangle/channel/Pipeline.h>
#include <wangle/channel/Handler.h>
#include <wangle/channel/EventBaseHandler.h>
#include <wangle/channel/AsyncSocketHandler.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/Demangle.h>
#include <glog/logging.h>
#include <tuple>
#include <memory>
#include <utility>

namespace detail {

template<size_t I, typename Handlers>
typename std::enable_if<I == std::tuple_size<Handlers>::value, wangle::PipelineBase&>::type
add(Handlers*, wangle::PipelineBase& p) { return p; }

template<size_t I = 0, typename Handlers>
typename std::enable_if<I < std::tuple_size<Handlers>::value, wangle::PipelineBase&>::type
add(Handlers* handlers, wangle::PipelineBase& pipeline) {
    VLOG(3) << "adding " << folly::demangle(typeid(typename std::tuple_element<I, Handlers>::type).name());
    return add<I + 1>(handlers, pipeline.addBack(&std::get<I>(*handlers)));
}

template<typename Pipeline, typename... Args>
class SocketPipelineFactory : public wangle::PipelineFactory<Pipeline> {
public:
    explicit SocketPipelineFactory(Args... args) :
        handlers_(std::forward_as_tuple(args...))
    {}

    virtual ~SocketPipelineFactory() = default;

    virtual typename Pipeline::Ptr newPipeline(std::shared_ptr<folly::AsyncTransportWrapper> sock) override {
        auto pipeline = Pipeline::create();
        pipeline->addBack(wangle::AsyncSocketHandler(sock));
        pipeline->addBack(wangle::EventBaseHandler());

        detail::add(&handlers_, *pipeline);
        
        pipeline->finalize();
        return pipeline;
    }

private:    
    std::tuple<Args...> handlers_;
};
}


template<typename Pipeline>
struct SocketPipelineFactory { 
    template<typename... Args>
    static std::shared_ptr<typename wangle::PipelineFactory<Pipeline>> create(Args... args) {
        return std::make_shared<detail::SocketPipelineFactory<Pipeline, Args...>>(std::forward<Args>(args)...);
    }
};
