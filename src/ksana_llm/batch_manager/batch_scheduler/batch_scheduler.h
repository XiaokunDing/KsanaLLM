/* Copyright 2024 Tencent Inc.  All rights reserved.

==============================================================================*/
#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include "ksana_llm/batch_manager/batch_scheduler/strategy/strategy_factory.h"
#include "ksana_llm/batch_manager/batch_scheduler/state/batch_state.h"
#include "ksana_llm/runtime/infer_request.h"
#include "ksana_llm/runtime/threadpool.h"
#include "ksana_llm/utils/context.h"
#include "ksana_llm/utils/environment.h"

namespace ksana_llm {

class BatchScheduler {
  public:
    BatchScheduler(const BatchSchedulerConfig &batch_scheduler_config, std::shared_ptr<Context> context);
    ~BatchScheduler() {}

    // Get the next infer reqs that ready to run.
    std::vector<std::shared_ptr<InferRequest>> &Schedule();

    // Add infer request to waiting list.
    Status AddInferRequest(std::vector<std::shared_ptr<InferRequest>> &infer_request_group);

    // Check whether the waiting buffer is empty.
    bool WaitingBufferEmpty();

    // Check whether the swapped queue is empty.
    bool SwappedQueueEmtpy();

  private:
    // True if waiting queue is already full.
    inline bool CheckWaitingQueueFull(int num);

    // True if request length exceed the max input length.
    inline bool CheckRequestExceedLength(const std::shared_ptr<InferRequest> req);

  private:
    BatchSchedulerConfig batch_scheduler_config_;
    std::shared_ptr<Context> context_;

    // The batch state informations, include some queues and mutexes.
    std::shared_ptr<BatchState> batch_state_ = nullptr;

    // The batch strategy implementation.
    std::shared_ptr<BaseScheduleStrategy> schedule_strategy_ = nullptr;
};

}  // namespace ksana_llm
