/**
 * index_iterator.cpp
 */

#include <cassert>

#include "storage/index/index_iterator.h"

namespace bustub {

/*
 * NOTE: you can change the destructor/constructor method here
 * set your own input parameters
 */
INDEX_TEMPLATE_ARGUMENTS
IndexIterator<KeyType, ValueType, KeyComparator>::IndexIterator(BufferPoolManager *buffer_pool_manager, Page *leaf_page,
                                                                int index)
    : buffer_pool_manager_(buffer_pool_manager),
      leaf_page_(leaf_page),
      leaf_node_(nullptr),
      index_(index),
      is_end_(false) {
  if (leaf_page_ == nullptr) {
    leaf_node_ = nullptr;
    is_end_ = true;
    return;
  }
  leaf_node_ = reinterpret_cast<LeafPageType *>(leaf_page_->GetData());
  // if index out of range -> end
  if (index_ >= leaf_node_->GetSize()) {
    is_end_ = true;
  }
}

INDEX_TEMPLATE_ARGUMENTS
IndexIterator<KeyType, ValueType, KeyComparator>::IndexIterator()
    : buffer_pool_manager_(nullptr), leaf_page_(nullptr), leaf_node_(nullptr), index_(0), is_end_(true) {}

INDEX_TEMPLATE_ARGUMENTS
IndexIterator<KeyType, ValueType, KeyComparator>::~IndexIterator() {
  if (leaf_page_ != nullptr) {
    if (buffer_pool_manager_ != nullptr) {
      buffer_pool_manager_->UnpinPage(leaf_page_->GetPageId(), false);
    }
    leaf_page_ = nullptr;
    leaf_node_ = nullptr;
  }
}

INDEX_TEMPLATE_ARGUMENTS
auto IndexIterator<KeyType, ValueType, KeyComparator>::IsEnd() -> bool { return is_end_; }

INDEX_TEMPLATE_ARGUMENTS
auto IndexIterator<KeyType, ValueType, KeyComparator>::operator*() -> const MappingType & {
  if (is_end_ || leaf_node_ == nullptr) {
    throw std::out_of_range("IndexIterator dereference at end");
  }
  return leaf_node_->GetItem(index_);
}

INDEX_TEMPLATE_ARGUMENTS
auto IndexIterator<KeyType, ValueType, KeyComparator>::operator++() -> IndexIterator & {
  if (is_end_) {
    return *this;
  }

  // advance index
  ++index_;
  if (index_ < leaf_node_->GetSize()) {
    return *this;
  }

  // we reached past current leaf -> move to next leaf
  page_id_t next_page_id = leaf_node_->GetNextPageId();

  // unpin current leaf
  if (leaf_page_ != nullptr && buffer_pool_manager_ != nullptr) {
    buffer_pool_manager_->UnpinPage(leaf_page_->GetPageId(), false);
  }

  if (next_page_id == INVALID_PAGE_ID) {
    // becomes end iterator
    leaf_page_ = nullptr;
    leaf_node_ = nullptr;
    index_ = 0;
    is_end_ = true;
    return *this;
  }

  // fetch next leaf and pin it
  Page *next_page = buffer_pool_manager_->FetchPage(next_page_id);
  if (next_page == nullptr) {
    // if fetch failed, mark end (defensive)
    leaf_page_ = nullptr;
    leaf_node_ = nullptr;
    index_ = 0;
    is_end_ = true;
    return *this;
  }

  leaf_page_ = next_page;
  leaf_node_ = reinterpret_cast<LeafPageType *>(leaf_page_->GetData());
  index_ = 0;
  is_end_ = (leaf_node_->GetSize() == 0);
  return *this;
}

template class IndexIterator<GenericKey<4>, RID, GenericComparator<4>>;

template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>>;

template class IndexIterator<GenericKey<16>, RID, GenericComparator<16>>;

template class IndexIterator<GenericKey<32>, RID, GenericComparator<32>>;

template class IndexIterator<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
