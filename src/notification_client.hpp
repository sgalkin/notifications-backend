#pragma once

#include "client.hpp"
#include <wangle/channel/Pipeline.h>
#include <chrono>

class Notification;
typedef wangle::Pipeline<folly::IOBufQueue&, Notification> NotificationClientPipeline;
typedef Client<NotificationClientPipeline> NotificationClient;

class Sender {
public:
    
    void send(NotificationClientPipeline* pipeline, std::string token, std::string payload);

    std::shared_ptr<wangle::PipelineFactory<NotificationClientPipeline>>
    createPipelineFactory(Client<NotificationClientPipeline>* client, std::chrono::seconds reconnectTimeout);
};
