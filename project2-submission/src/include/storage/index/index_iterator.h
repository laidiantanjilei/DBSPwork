//===----------------------------------------------------------------------===//
//
//                         CMU-DB Project (15-445/645)
//                         ***DO NOT SHARE PUBLICLY***
//
// Identification: src/include/index/index_iterator.h
//
//===----------------------------------------------------------------------===//

#pragma once

#include "buffer/buffer_pool_manager.h"
#include "storage/page/b_plus_tree_leaf_page.h"

namespace bustub {

#define INDEXITERATOR_TYPE IndexIterator<KeyType, ValueType, KeyComparator>

INDEX_TEMPLATE_ARGUMENTS
class IndexIterator {
 public:
  // ctor that pins the leaf page (if leaf_page is not nullptr)
  IndexIterator(BufferPoolManager *buffer_pool_manager, Page *leaf_page, int index);

  // default ctor -> end iterator
  IndexIterator();

  ~IndexIterator();

  auto IsEnd() -> bool;

  // return current (key, value) pair
  auto operator*() -> const MappingType &;

  // advance to next entry; if moved past end, becomes end iterator
  auto operator++() -> IndexIterator &;

  auto operator==(const IndexIterator &other) const -> bool {
    return (is_end_ && other.is_end_) ||
           (leaf_page_ == other.leaf_page_ && index_ == other.index_ && is_end_ == other.is_end_);
  }

  auto operator!=(const IndexIterator &other) const -> bool { return !(*this == other); }

 private:
  using LeafPageType = BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>;

  BufferPoolManager *buffer_pool_manager_;
  Page *leaf_page_;          // pinned page pointer (or nullptr if end)
  LeafPageType *leaf_node_;  // pointer to the leaf page's data region (reinterpret_cast)
  int index_;                // index inside leaf
  bool is_end_;
};

}  // namespace bustub
