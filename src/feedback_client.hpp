#pragma once

#include "client.hpp"
#include <wangle/channel/Pipeline.h>
#include <chrono>

typedef wangle::DefaultPipeline FeedbackClientPipeline;
typedef Client<FeedbackClientPipeline> FeedbackClient;

std::shared_ptr<wangle::PipelineFactory<FeedbackClientPipeline>>
FeedbackClientPipelineFactory(Client<FeedbackClientPipeline>* client, std::chrono::seconds reconnectTimeout);
