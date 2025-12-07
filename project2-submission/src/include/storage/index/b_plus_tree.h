//===----------------------------------------------------------------------===//
//
//                         CMU-DB Project (15-445/645)
//                         ***DO NO SHARE PUBLICLY***
//
// Identification: src/include/index/b_plus_tree.h
//
// Copyright (c) 2018, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#pragma once

#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include "concurrency/transaction.h"
#include "storage/index/index_iterator.h"
#include "storage/page/b_plus_tree_internal_page.h"
#include "storage/page/b_plus_tree_leaf_page.h"

namespace bustub {

#define BPLUSTREE_TYPE BPlusTree<KeyType, ValueType, KeyComparator>
INDEX_TEMPLATE_ARGUMENTS
class BPlusTree {
  using InternalPage = BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>;
  using LeafPage = BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>;

 public:
  explicit BPlusTree(std::string name, BufferPoolManager *buffer_pool_manager, const KeyComparator &comparator,
                     int leaf_max_size = LEAF_PAGE_SIZE, int internal_max_size = INTERNAL_PAGE_SIZE);

  auto IsEmpty() const -> bool;

  auto Insert(const KeyType &key, const ValueType &value, Transaction *transaction = nullptr) -> bool;

  void Remove(const KeyType &key, Transaction *transaction);

  auto GetValue(const KeyType &key, std::vector<ValueType> *result, Transaction *transaction = nullptr) -> bool;

  auto GetRootPageId() -> page_id_t;

  auto Begin() -> INDEXITERATOR_TYPE;
  auto Begin(const KeyType &key) -> INDEXITERATOR_TYPE;
  auto End() -> INDEXITERATOR_TYPE;

  void Print(BufferPoolManager *bpm);

  void Draw(BufferPoolManager *bpm, const std::string &outf);

  void InsertFromFile(const std::string &file_name, Transaction *transaction = nullptr);

  void RemoveFromFile(const std::string &file_name, Transaction *transaction = nullptr);

 private:
  void UpdateRootPageId(int insert_record = 0);

  void ToGraph(BPlusTreePage *page, BufferPoolManager *bpm, std::ofstream &out) const;

  void ToString(BPlusTreePage *page, BufferPoolManager *bpm) const;

  void StartNewTree(const KeyType &key, const ValueType &value);
  auto InsertIntoLeaf(const KeyType &key, const ValueType &value, Transaction *transaction = nullptr) -> bool;
  void InsertIntoParent(BPlusTreePage *old_node, const KeyType &key, BPlusTreePage *new_node,
                        Transaction *transaction = nullptr);
  auto FindLeafPage(const KeyType &key, bool leftMost = false) -> Page *;

  void CoalesceOrRedistribute(LeafPage *leaf, Transaction *txn);

  void CoalesceOrRedistributeInternal(InternalPage *node, Transaction *txn);

  void RemoveFromParent(BPlusTreePage *child, InternalPage *parent, int index, Transaction *txn);

  void AdjustRoot(BPlusTreePage *old_root_node);
  auto FindLeafPageNoLock(const KeyType &key, bool leftMost = false) -> Page *;

  std::string index_name_;
  page_id_t root_page_id_;
  BufferPoolManager *buffer_pool_manager_;
  KeyComparator comparator_;
  int leaf_max_size_;
  int internal_max_size_;
  mutable std::mutex tree_latch_;
};

}  // namespace bustub
