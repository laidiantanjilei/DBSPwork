//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// lru_k_replacer.cpp
//
// Identification: src/buffer/lru_k_replacer.cpp
//
// Copyright (c) 2015-2022, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/lru_k_replacer.h"

namespace bustub {

LRUKReplacer::LRUKReplacer(size_t num_frames, size_t k) : replacer_size_(num_frames), k_(k) {}

auto LRUKReplacer::Evict(frame_id_t *frame_id) -> bool {
  std::lock_guard<std::mutex> lock(latch_);
  if (curr_size_ == 0) {
    return false;
  }

  if (!inf_distance_list_.empty()) {
    *frame_id = inf_distance_list_.front();
    inf_distance_list_.pop_front();
    node_store_[*frame_id].is_in_inf_ = false;
  } else {
    auto min_it = finite_distance_map_.begin();
    for (auto it = finite_distance_map_.begin(); it != finite_distance_map_.end(); it++) {
      if (it->second < min_it->second) {
        min_it = it;
      }
    }
    *frame_id = min_it->first;
    finite_distance_map_.erase(min_it);
  }
  node_store_.erase(*frame_id);
  curr_size_--;
  return true;
}

void LRUKReplacer::RecordAccess(frame_id_t frame_id) {
  std::lock_guard<std::mutex> lock(latch_);
  BUSTUB_ASSERT(frame_id < replacer_size_, "Invalid frame_id");
  auto &frame = node_store_[frame_id];
  if (!frame.is_present_) {
    frame.is_present_ = true;
  }

  current_timestamp_++;

  frame.history_.push_back(current_timestamp_);

  if (frame.history_.size() > k_) {
    frame.history_.pop_front();
  }

  if (frame.is_evictable_) {
    if (frame.history_.size() == k_ && k_ > 0 && frame.is_in_inf_) {
      inf_distance_list_.remove(frame_id);
      frame.is_in_inf_ = false;
      finite_distance_map_[frame_id] = frame.history_.front();
    } else if (frame.history_.size() == k_ && k_ > 0) {
      finite_distance_map_[frame_id] = frame.history_.front();
    }
  }
}

void LRUKReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  std::lock_guard<std::mutex> lock(latch_);
  BUSTUB_ASSERT(frame_id < replacer_size_, "Invalid frame_id");
  if (node_store_.find(frame_id) == node_store_.end() || !node_store_[frame_id].is_present_) {
    return;
  }

  auto &frame = node_store_[frame_id];
  bool currently_evictable = frame.is_evictable_;

  if (currently_evictable == set_evictable) {
    return;
  }

  if (set_evictable) {
    frame.is_evictable_ = true;
    curr_size_++;
    if (frame.history_.size() < k_ || k_ == 0) {
      if (k_ > 0) {
        inf_distance_list_.push_back(frame_id);
        frame.is_in_inf_ = true;
      }
    } else {
      finite_distance_map_[frame_id] = frame.history_.front();
    }
  } else {
    frame.is_evictable_ = false;
    curr_size_--;
    if (frame.is_in_inf_) {
      inf_distance_list_.remove(frame_id);
      frame.is_in_inf_ = false;
    } else {
      finite_distance_map_.erase(frame_id);
    }
  }
}

void LRUKReplacer::Remove(frame_id_t frame_id) {
  std::lock_guard<std::mutex> lock(latch_);
  if (node_store_.find(frame_id) == node_store_.end() || !node_store_[frame_id].is_present_) {
    return;
  }

  auto &frame = node_store_[frame_id];

  if (!frame.is_evictable_) {
    BUSTUB_ASSERT(false, "Attempting to remove a non-evictable frame!");
    return;
  }

  if (frame.is_in_inf_) {
    inf_distance_list_.remove(frame_id);
  } else {
    finite_distance_map_.erase(frame_id);
  }
  curr_size_--;
  node_store_.erase(frame_id);
}

auto LRUKReplacer::Size() -> size_t {
  std::lock_guard<std::mutex> lock(latch_);
  return curr_size_;
}

}  // namespace bustub
