//===----------------------------------------------------------------------===//
//
//                         CMU-DB Project (15-445/645)
//                         ***DO NO SHARE PUBLICLY***
//
// Identification: src/page/b_plus_tree_leaf_page.cpp
//
// Copyright (c) 2018, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <sstream>

#include "common/exception.h"
#include "common/rid.h"
#include "storage/page/b_plus_tree_leaf_page.h"

namespace bustub {
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::Init(page_id_t page_id, page_id_t parent_id, int max_size) {
  SetPageId(page_id);
  SetParentPageId(parent_id);
  SetPageType(IndexPageType::LEAF_PAGE);
  SetMaxSize(max_size);
  SetSize(0);
  SetNextPageId(INVALID_PAGE_ID);
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetNextPageId() const -> page_id_t { return this->next_page_id_; }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::SetNextPageId(page_id_t next_page_id) { this->next_page_id_ = next_page_id; }

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::KeyAt(int index) const -> KeyType { return this->array_[index].first; }

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::Insert(const KeyType &key, const ValueType &value, const KeyComparator &comparator)
    -> bool {
  int size = GetSize();
  if (size >= GetMaxSize()) {
    return false;
  }

  // 计算插入位置下标
  auto it = std::lower_bound(array_, array_ + size, key, [&comparator](const MappingType &pair, const KeyType &k) {
    return comparator(pair.first, k) < 0;
  });
  int insertion_index = static_cast<int>(std::distance(array_, it));

  // key 已存在
  if (it != array_ + size && comparator(key, it->first) == 0) {
    return false;
  }

  // 将 [insertion_index, size-1] 向右移动一位
  std::move_backward(array_ + insertion_index, array_ + size, array_ + size + 1);
  array_[insertion_index] = {key, value};
  IncreaseSize(1);

  return true;
}

// 将当前 leaf page 的“右半部分”移动到新分裂出的叶子页
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveHalfTo(BPlusTreeLeafPage *recipient) {
  int size = GetSize();
  // 左边保留前 ceil(size/2)，右边 floor(size/2) 移走
  int split_index = (size + 1) / 2;
  int move_count = size - split_index;

  // 复制元素
  std::copy(array_ + split_index, array_ + size, recipient->array_);
  SetSize(split_index);
  recipient->SetSize(move_count);

  // 修改NextPageId
  recipient->SetNextPageId(GetNextPageId());
  SetNextPageId(recipient->GetPageId());
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetFirstKey() const -> KeyType { return array_[0].first; }

// 删除指定 key-value pair
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::RemoveAndDeleteRecord(const KeyType &key, const KeyComparator &comparator) -> bool {
  int size = GetSize();
  // 找
  auto it = std::lower_bound(array_, array_ + size, key, [&comparator](const MappingType &pair, const KeyType &k) {
    return comparator(pair.first, k) < 0;
  });
  int remove_index = static_cast<int>(std::distance(array_, it));

  // 没找到
  if (it == array_ + size || comparator(key, it->first) != 0) {
    return false;
  }

  // 移动元素
  std::move(array_ + remove_index + 1, array_ + size, array_ + remove_index);
  IncreaseSize(-1);
  return true;
}

// 查找 key 对应的 value
INDEX_TEMPLATE_ARGUMENTS
bool B_PLUS_TREE_LEAF_PAGE_TYPE::Lookup(const KeyType &key, ValueType &value, const KeyComparator &comparator) const {
  int size = GetSize();
  // 开找
  auto it = std::lower_bound(array_, array_ + size, key, [&comparator](const MappingType &pair, const KeyType &k) {
    return comparator(pair.first, k) < 0;
  });

  // 找到啦
  if (it != array_ + size && comparator(it->first, key) == 0) {
    value = it->second;
    return true;
  }
  return false;
}

// 把当前页的所有 KV 追加到 recipient 的末尾
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveAllTo(BPlusTreeLeafPage *recipient) {
  int size = GetSize();
  int recipient_size = recipient->GetSize();

  // 复制
  for (int i = 0; i < size; i++) {
    recipient->array_[recipient_size + i] = array_[i];
  }
  recipient->SetSize(recipient_size + size);

  // 修改属性
  recipient->SetNextPageId(GetNextPageId());
  SetSize(0);
}

// 将当前节点的第一个元素移动到 recipient 的末尾
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveFirstToEndOf(BPlusTreeLeafPage *recipient) {
  if (GetSize() == 0) return;

  int recipient_size = recipient->GetSize();
  recipient->array_[recipient_size] = array_[0];
  recipient->IncreaseSize(1);

  std::move(array_ + 1, array_ + GetSize(), array_);
  IncreaseSize(-1);
}

// 将当前节点的最后一个元素移动到 recipient 的最前面
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveLastToFrontOf(BPlusTreeLeafPage *recipient) {
  int size = GetSize();
  if (size == 0) return;

  MappingType last = array_[size - 1];
  IncreaseSize(-1);

  std::move_backward(recipient->array_, recipient->array_ + recipient->GetSize(),
                     recipient->array_ + recipient->GetSize() + 1);
  recipient->array_[0] = last;
  recipient->IncreaseSize(1);
}

// 返回指定 index 的 value
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::ValueAt(int index) const -> ValueType { return this->array_[index].second; }

template class BPlusTreeLeafPage<GenericKey<4>, RID, GenericComparator<4>>;
template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>>;
template class BPlusTreeLeafPage<GenericKey<16>, RID, GenericComparator<16>>;
template class BPlusTreeLeafPage<GenericKey<32>, RID, GenericComparator<32>>;
template class BPlusTreeLeafPage<GenericKey<64>, RID, GenericComparator<64>>;
}  // namespace bustub
