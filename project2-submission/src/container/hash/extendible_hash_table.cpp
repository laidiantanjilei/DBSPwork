//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// extendible_hash_table.cpp
//
// Identification: src/container/hash/extendible_hash_table.cpp
//
// Copyright (c) 2022, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <cassert>
#include <cstdlib>
#include <functional>
#include <list>
#include <utility>
#include <vector>

#include "container/hash/extendible_hash_table.h"
#include "storage/page/page.h"

namespace bustub {

template <typename K, typename V>
ExtendibleHashTable<K, V>::ExtendibleHashTable(size_t bucket_size)
    : bucket_size_(bucket_size), global_depth_(0), num_buckets_(0) {
  // 初始化目录：global_depth_ = 0 -> dir_ 大小为 1，放置第一个 bucket
  dir_.resize(1 << global_depth_);
  auto first_bucket = std::make_shared<Bucket>(bucket_size_, /*depth=*/0);
  dir_[0] = first_bucket;
  num_buckets_ = 1;
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::IndexOf(const K &key) -> size_t {
  // mask: 当 global_depth_ == 0 时，mask = 0，IndexOf 返回 0
  size_t mask = (global_depth_ == 0) ? 0 : ((1UL << global_depth_) - 1);
  return std::hash<K>()(key) & mask;
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetGlobalDepth() const -> int {
  std::scoped_lock<std::mutex> lock(latch_);
  return GetGlobalDepthInternal();
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetGlobalDepthInternal() const -> int {
  return global_depth_;
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetLocalDepth(int dir_index) const -> int {
  std::scoped_lock<std::mutex> lock(latch_);
  return GetLocalDepthInternal(dir_index);
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetLocalDepthInternal(int dir_index) const -> int {
  return dir_[dir_index]->GetDepth();
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetNumBuckets() const -> int {
  std::scoped_lock<std::mutex> lock(latch_);
  return GetNumBucketsInternal();
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::GetNumBucketsInternal() const -> int {
  return num_buckets_;
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::Find(const K &key, V &value) -> bool {
  std::scoped_lock<std::mutex> lock(latch_);
  size_t dir_index = IndexOf(key);
  return dir_[dir_index]->Find(key, value);
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::Remove(const K &key) -> bool {
  std::scoped_lock<std::mutex> lock(latch_);
  size_t dir_index = IndexOf(key);
  return dir_[dir_index]->Remove(key);
}

template <typename K, typename V>
void ExtendibleHashTable<K, V>::Insert(const K &key, const V &value) {
  std::scoped_lock<std::mutex> lock(latch_);

  while (true) {
    size_t dir_index = IndexOf(key);
    std::shared_ptr<Bucket> bucket = dir_[dir_index];

    // 如果当前 bucket 插入成功 -> 返回
    if (bucket->Insert(key, value)) {
      return;
    }

    // 需要分裂
    int local_depth = bucket->GetDepth();

    // 如果 local depth == global depth，需要先扩展目录
    if (local_depth == global_depth_) {
      // 扩展 global depth
      size_t old_dir_size = (1UL << global_depth_);
      global_depth_++;
      size_t new_dir_size = (1UL << global_depth_);
      dir_.resize(new_dir_size);
      // 复制旧目录到高位
      for (size_t i = 0; i < old_dir_size; ++i) {
        dir_[i + old_dir_size] = dir_[i];
      }
    }

    // 增加 bucket 的 local depth
    bucket->IncrementDepth();
    int new_local_depth = bucket->GetDepth();

    // 创建新的 bucket（深度为 new_local_depth）
    auto new_bucket = std::make_shared<Bucket>(bucket_size_, new_local_depth);
    num_buckets_++;

    // 计算 distinguishing bit
    size_t distinguishing_bit_mask = 1UL << (new_local_depth - 1);

    // 重新指向目录中原来指向旧 bucket 的那些索引
    // 只把在 distinguishing bit 为 1 的那些 index 指向 new_bucket
    for (size_t i = 0; i < dir_.size(); ++i) {
      if (dir_[i] == bucket) {
        if ((i & distinguishing_bit_mask) != 0) {
          dir_[i] = new_bucket;
        }
      }
    }

    // 迁移旧 bucket 中的元素：先复制一份，再清空旧 bucket
    auto items = bucket->list_;  // 可以直接访问 Bucket 的成员（在同一实现文件中）
    bucket->list_.clear();

    // 重新插入 items（使用 IndexOf 重新计算目录索引）
    for (const auto &pr : items) {
      size_t new_index = IndexOf(pr.first);
      dir_[new_index]->Insert(pr.first, pr.second);
    }

    // 循环重试插入（外层 while 会再次尝试）
  }
}

//===--------------------------------------------------------------------===//
// Bucket
//===--------------------------------------------------------------------===//
template <typename K, typename V>
ExtendibleHashTable<K, V>::Bucket::Bucket(size_t array_size, int depth) : size_(array_size), depth_(depth) {}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::Bucket::Find(const K &key, V &value) -> bool {
  for (const auto &pair : list_) {
    if (pair.first == key) {
      value = pair.second;
      return true;
    }
  }
  return false;
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::Bucket::Remove(const K &key) -> bool {
  for (auto it = list_.begin(); it != list_.end(); ++it) {
    if (it->first == key) {
      list_.erase(it);
      return true;
    }
  }
  return false;
}

template <typename K, typename V>
auto ExtendibleHashTable<K, V>::Bucket::Insert(const K &key, const V &value) -> bool {
  // 先检查是否 key 已存在 -> 替换
  for (auto &pair : list_) {
    if (pair.first == key) {
      pair.second = value;
      return true;
    }
  }

  // 如果已满，返回 false，触发分裂
  if (static_cast<size_t>(list_.size()) >= size_) {
    return false;
  }

  // 否则插入并返回 true
  list_.push_back({key, value});
  return true;
}

// explicit template instantiations for common types
template class ExtendibleHashTable<page_id_t, Page *>;
template class ExtendibleHashTable<Page *, std::list<Page *>::iterator>;
template class ExtendibleHashTable<int, int>;
// test purpose
template class ExtendibleHashTable<int, std::string>;
template class ExtendibleHashTable<int, std::list<int>::iterator>;

}  // namespace bustub
