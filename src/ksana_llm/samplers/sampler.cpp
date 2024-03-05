/* Copyright 2023 Tencent Inc.  All rights reserved.

==============================================================================*/

#include "ksana_llm/samplers/sampler.h"
#include <curand_kernel.h>
#include "ksana_llm/utils/logger.h"
#include "ksana_llm/utils/memory_utils.h"

namespace ksana_llm {

Sampler::Sampler(const BatchSchedulerConfig& batch_scheduler_config, int rank) {
  batch_schedule_config_ = batch_scheduler_config;
  rank_ = rank;
  auto max_batch_size = batch_scheduler_config.max_batch_size;
  // need to allocate device buffer for sampling
  GetBlockManager()->SetDeviceId(rank_);

  GetBlockManager()->AllocateContiguous(
      (sizeof(uint32_t) * 2 + sizeof(int) + sizeof(curandState_t) + sizeof(int*)) * max_batch_size,
      device_buffer_block_id_);
  GetBlockManager()->GetContiguousPtr(device_buffer_block_id_, device_buffer_);
  NLLM_LOG_DEBUG << "AllocateContiguous device_buffer_ " << device_buffer_ << " size "
                 << (sizeof(uint32_t) * 2 + sizeof(int) + sizeof(curandState_t) + sizeof(int*)) * max_batch_size;
  device_output_tokens_ = static_cast<uint32_t*>(device_buffer_);
  device_offset_ = device_output_tokens_ + max_batch_size;
  device_topKs_ = reinterpret_cast<int*>(device_offset_ + max_batch_size);
  device_curandstates_ = reinterpret_cast<curandState_t*>(device_topKs_ + max_batch_size);
  device_output_tokens_ptrs_ = reinterpret_cast<int**>(device_curandstates_ + max_batch_size);
  if (sizeof(uint32_t) != sizeof(int)) {
    NLLM_LOG_ERROR << fmt::format("sizeof(uint32_t)({}) != sizeof(int)({})", sizeof(uint32_t), sizeof(int));
    abort();
    exit(RetCode::RET_SEGMENT_FAULT);
  }
  std::vector<uint32_t*> host_device_output_tokens_ptrs(max_batch_size);
  for (int i = 0; i < max_batch_size; i++) {
    host_device_output_tokens_ptrs[i] = device_output_tokens_ + i;
  }
  CUDA_CHECK(cudaMemcpyAsync(device_output_tokens_ptrs_, host_device_output_tokens_ptrs.data(),
                             sizeof(uint32_t*) * max_batch_size, cudaMemcpyHostToDevice));
  host_offset_.resize(max_batch_size);
  host_topKs_.resize(max_batch_size);
  host_output_tokens_.resize(max_batch_size);
  topk_sampling_ = new TopkSampling(max_batch_size, batch_scheduler_config.max_vocab_size, device_curandstates_);
}

Sampler::~Sampler() {
  // free device buffer of output tokens
  GetBlockManager()->SetDeviceId(rank_);
  delete topk_sampling_;
  GetBlockManager()->FreeContiguous(device_buffer_block_id_);
}

Status Sampler::Sampling(std::vector<SamplingRequest>& sampling_reqs, cudaStream_t& stream) {
  if (rank_ == 0) {
    bool use_arg_max = true;
    int req_index = 0;
    float* device_logits = nullptr;
    SamplingDevideParameter sampling_devide_parameter;
    sampling_devide_parameter.bs = sampling_reqs.size();
    for (auto& sampling_req : sampling_reqs) {
      const ModelConfig* model_config = sampling_req.model_config;
      const SamplingConfig* sampling_config = sampling_req.sampling_config;
      float* logits = sampling_req.logits_buf[rank_];
      if (device_logits == logits || device_logits == nullptr) {
        device_logits = logits;
        sampling_devide_parameter.vocab_size_padded = model_config->vocab_size;
      } else {
        return Status(RET_SEGMENT_FAULT, "sampling for different logits not implemented");
      }
      host_offset_[req_index] = sampling_req.logits_offset;
      if (sampling_config->beam_width == 1) {
        if (sampling_config->temperature == 0.0f) {
          if (sampling_config->topp == 0.0f || sampling_config->topp == 1.0f) {
            if (sampling_config->topk > 1024) {
              return Status(RET_INVALID_ARGUMENT, "topk > 1024.");
            }
            host_topKs_[req_index] = sampling_config->topk;
            sampling_devide_parameter.max_topK = sampling_devide_parameter.max_topK > sampling_config->topk
                                                     ? sampling_devide_parameter.max_topK
                                                     : sampling_config->topk;
            use_arg_max = use_arg_max && sampling_config->topk == 1;
          } else {
            return Status(RET_INVALID_ARGUMENT, "sampling for topp not implemented");
          }
        } else {
          return Status(RET_INVALID_ARGUMENT, "sampling for temperature not implemented");
        }
      } else {
        return Status(RET_INVALID_ARGUMENT, "sampling for beam_width > 1 not implemented");
      }
      req_index++;
    }
    CUDA_CHECK(cudaMemcpyAsync(device_offset_, host_offset_.data(), sizeof(uint32_t) * sampling_devide_parameter.bs,
                               cudaMemcpyHostToDevice, stream));

    if (!use_arg_max) {
      CUDA_CHECK(cudaMemcpyAsync(device_topKs_, host_topKs_.data(), sizeof(int) * sampling_devide_parameter.bs,
                                 cudaMemcpyHostToDevice, stream));
      sampling_devide_parameter.device_topKs = device_topKs_;
      sampling_devide_parameter.device_output_tokens_ptrs = device_output_tokens_ptrs_;
      sampling_devide_parameter.device_curandstates = device_curandstates_;
    }
    STATUS_CHECK_RETURN(topk_sampling_->Forward(device_logits, device_offset_, device_output_tokens_, nullptr,
                                                sampling_devide_parameter, nullptr, stream));
    CUDA_CHECK(cudaMemcpyAsync(host_output_tokens_.data(), device_output_tokens_,
                               sizeof(uint32_t) * sampling_devide_parameter.bs, cudaMemcpyDeviceToHost, stream));
    cudaStreamSynchronize(stream);
    for (int i = 0; i < sampling_devide_parameter.bs; i++) {
      sampling_reqs[i].output_tokens->push_back(host_output_tokens_[i]);
    }
  }
  return Status();
}

}  // namespace ksana_llm
