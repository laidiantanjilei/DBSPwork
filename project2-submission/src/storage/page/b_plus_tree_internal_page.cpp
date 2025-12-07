//===----------------------------------------------------------------------===//
//
//                         CMU-DB Project (15-445/645)
//                         ***DO NO SHARE PUBLICLY***
//
// Identification: src/page/b_plus_tree_internal_page.cpp
//
// Copyright (c) 2018, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>

#include "common/exception.h"
#include "storage/page/b_plus_tree_internal_page.h"

namespace bustub {
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Init(page_id_t page_id, page_id_t parent_id, int max_size) {
  SetPageId(page_id);
  SetParentPageId(parent_id);
  SetPageType(IndexPageType::INTERNAL_PAGE);
  SetMaxSize(max_size);
  SetSize(0);
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::KeyAt(int index) const -> KeyType { return array_[index].first; }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetKeyAt(int index, const KeyType &key) { array_[index].first = key; }

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueAt(int index) const -> ValueType { return array_[index].second; }

// 在internal node中查找key应该下沉到哪个child
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::Lookup(const KeyType &key, const KeyComparator &comparator) const -> ValueType {
  int size = GetSize();
  // 寻找第一个>key的Ki，返回其左侧的pointer，从array_ + 1搜索，因为index=0没key
  auto it = std::upper_bound(array_ + 1, array_ + size, key,
                             [&](const KeyType &k, const MappingType &m) { return comparator(k, m.first) < 0; });
  int index = static_cast<int>(std::distance(array_, it));
  return array_[index - 1].second;
}

// 在internal page中找到值为old_value的child pointer，在它之后插入新的键值对
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::InsertNodeAfter(const ValueType &old_value, const KeyType &new_key,
                                                     const ValueType &new_value) {
  int sz = GetSize();
  int old_index = -1;
  for (int i = 0; i < sz; i++) {
    if (array_[i].second == old_value) {
      old_index = i;
      break;
    }
  }
  if (old_index == -1) {
    throw Exception("InsertNodeAfter: old_value not found");
  }
  std::move_backward(array_ + old_index + 1, array_ + sz, array_ + sz + 1);
  array_[old_index + 1].first = new_key;
  array_[old_index + 1].second = new_value;
  IncreaseSize(1);
}

// 把当前internal page的右半部分移动到recipient
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveHalfTo(BPlusTreeInternalPage *recipient,
                                                BufferPoolManager *buffer_pool_manager) {
  int size = GetSize();
  int split_index = size / 2;
  int move_count = size - split_index;
  // 复制，修改属性
  std::copy(array_ + split_index, array_ + size, recipient->array_);
  SetSize(split_index);
  recipient->SetSize(move_count);

  // 更新被移动的每个child的parent_page_id
  page_id_t recipient_page_id = recipient->GetPageId();
  for (int i = 0; i < move_count; i++) {
    // 获取child的pageid
    page_id_t child_page_id = recipient->ValueAt(i);
    // 根据pageid获取page
    auto *child_page = buffer_pool_manager->FetchPage(child_page_id);
    if (child_page == nullptr) {
      throw Exception("Failed to fetch child page during internal page split.");
    }
    // 重新解释内存
    BPlusTreePage *child_b_page = reinterpret_cast<BPlusTreePage *>(child_page->GetData());
    child_b_page->SetParentPageId(recipient_page_id);
    // 解除pin，同时标记为脏
    buffer_pool_manager->UnpinPage(child_page_id, true);
  }
}

// 将当前 nternal page的所有(key, pointer)追加到recipient的尾部
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveAllTo(BPlusTreeInternalPage *recipient,
                                               BufferPoolManager *buffer_pool_manager) {
  int size = GetSize();
  int recipient_old_size = recipient->GetSize();

  std::copy(array_, array_ + size, recipient->array_ + recipient_old_size);
  recipient->IncreaseSize(size);
  SetSize(0);

  // 更新被移动的每个child的parent_page_id
  page_id_t recipient_page_id = recipient->GetPageId();
  for (int i = recipient_old_size; i < recipient->GetSize(); i++) {
    page_id_t child_page_id = recipient->ValueAt(i);
    auto *child_page = buffer_pool_manager->FetchPage(child_page_id);
    if (child_page == nullptr) {
      throw Exception("Failed to fetch child page during internal page merge.");
    }
    BPlusTreePage *child_b_page = reinterpret_cast<BPlusTreePage *>(child_page->GetData());
    child_b_page->SetParentPageId(recipient_page_id);
    buffer_pool_manager->UnpinPage(child_page_id, true);
  }
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::GetFirstKey() const -> KeyType { return array_[0].first; }

// 删除index位置的键值对
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Remove(int index) {
  int size = GetSize();
  if (index < 0 || index >= size) {
    throw Exception("Remove: index out of bounds");
  }
  // 移动后面元素
  std::move(array_ + index + 1, array_ + size, array_ + index);
  IncreaseSize(-1);
}

// 把当前internal node的第一个child pointe移动到recipient的尾部，使用 middle_key作为key
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveFirstToEndOf(BPlusTreeInternalPage *recipient, const KeyType &middle_key,
                                                      BufferPoolManager *buffer_pool_manager) {
  ValueType child_page_id = ValueAt(0);
  int recipient_size = recipient->GetSize();

  // 复制元素
  recipient->array_[recipient_size].first = middle_key;
  recipient->array_[recipient_size].second = child_page_id;
  recipient->IncreaseSize(1);

  // 更新被移动child的parent pointer
  auto *child_page = buffer_pool_manager->FetchPage(child_page_id);
  if (child_page == nullptr) {
    throw Exception("Failed to fetch child page during MoveFirstToEndOf.");
  }
  BPlusTreePage *child_b_page = reinterpret_cast<BPlusTreePage *>(child_page->GetData());
  child_b_page->SetParentPageId(recipient->GetPageId());
  buffer_pool_manager->UnpinPage(child_page_id, true);
  // 移除第一个 entry
  std::move(array_ + 1, array_ + GetSize(), array_);
  IncreaseSize(-1);
}

// 把当前internal node的最后一个child pointer移动到recipient的最前面，使用 middle_key作为key
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveLastToFrontOf(BPlusTreeInternalPage *recipient, const KeyType &middle_key,
                                                       BufferPoolManager *buffer_pool_manager) {
  int size = GetSize();
  if (size <= 0) {
    throw Exception("MoveLastToFrontOf: empty source");
  }
  ValueType child_page_id = ValueAt(size - 1);

  std::move_backward(recipient->array_, recipient->array_ + recipient->GetSize(),
                     recipient->array_ + recipient->GetSize() + 1);

  recipient->array_[0].first = middle_key;
  recipient->array_[0].second = child_page_id;
  recipient->IncreaseSize(1);

  IncreaseSize(-1);

  auto *child_page = buffer_pool_manager->FetchPage(child_page_id);
  if (child_page == nullptr) {
    throw Exception("Failed to fetch child page during MoveLastToFrontOf.");
  }
  BPlusTreePage *child_b_page = reinterpret_cast<BPlusTreePage *>(child_page->GetData());
  child_b_page->SetParentPageId(recipient->GetPageId());
  buffer_pool_manager->UnpinPage(child_page_id, true);
}

// 将当前internal node的所有元素合并到recipient中，并在合并的分界处插入middle_key
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveAllTo(BPlusTreeInternalPage *recipient, const KeyType &middle_key,
                                               BufferPoolManager *buffer_pool_manager) {
  int size = GetSize();
  int recipient_old_size = recipient->GetSize();

  recipient->array_[recipient_old_size].first = middle_key;
  recipient->array_[recipient_old_size].second = ValueAt(0);

  if (size > 1) {
    std::copy(array_ + 1, array_ + size, recipient->array_ + recipient_old_size + 1);
  }
  recipient->IncreaseSize(size);
  SetSize(0);

  page_id_t recipient_page_id = recipient->GetPageId();
  int new_elements_start_index = recipient_old_size;
  int new_size = recipient->GetSize();

  // 更新被移到recipient的每个child的parent_page_id
  for (int i = new_elements_start_index; i < new_size; i++) {
    page_id_t child_page_id = recipient->ValueAt(i);
    auto *child_page = buffer_pool_manager->FetchPage(child_page_id);
    if (child_page == nullptr) {
      throw Exception("Failed to fetch child page during internal page merge.");
    }
    BPlusTreePage *child_b_page = reinterpret_cast<BPlusTreePage *>(child_page->GetData());
    child_b_page->SetParentPageId(recipient_page_id);
    buffer_pool_manager->UnpinPage(child_page_id, true);
  }
}

// 当根节点分裂时，创建一个新的root internal page
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::PopulateNewRoot(const ValueType &old_value, const KeyType &new_key,
                                                     const ValueType &new_value) {
  // P0指向旧的左子树
  array_[0].second = old_value;
  // 新的separator key
  array_[1].first = new_key;
  // P1指向右子树
  array_[1].second = new_value;
  // root的元素数为 2
  SetSize(2);
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueIndex(const ValueType &value) const -> int {
  int size = GetSize();
  for (int i = 0; i < size; i++) {
    if (array_[i].second == value) {
      return i;
    }
  }
  return -1;
}

// valuetype for internalNode should be page id_t
template class BPlusTreeInternalPage<GenericKey<4>, page_id_t, GenericComparator<4>>;
template class BPlusTreeInternalPage<GenericKey<8>, page_id_t, GenericComparator<8>>;
template class BPlusTreeInternalPage<GenericKey<16>, page_id_t, GenericComparator<16>>;
template class BPlusTreeInternalPage<GenericKey<32>, page_id_t, GenericComparator<32>>;
template class BPlusTreeInternalPage<GenericKey<64>, page_id_t, GenericComparator<64>>;
}  // namespace bustub
