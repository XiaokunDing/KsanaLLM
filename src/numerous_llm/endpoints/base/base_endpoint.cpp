/* Copyright 2023 Tencent Inc.  All rights reserved.

==============================================================================*/

#include "numerous_llm/endpoints/base/base_endpoint.h"

namespace numerous_llm {

BaseEndpoint::BaseEndpoint(const EndpointConfig &endpoint_config,
                           Channel<std::pair<Status, std::shared_ptr<Request>>> &request_queue)
    : request_queue_(request_queue), endpoint_config_(endpoint_config) {}

RpcEndpoint::RpcEndpoint(const EndpointConfig &endpoint_config,
                         Channel<std::pair<Status, std::shared_ptr<Request>>> &request_queue)
    : BaseEndpoint(endpoint_config, request_queue) {}

}  // namespace numerous_llm
