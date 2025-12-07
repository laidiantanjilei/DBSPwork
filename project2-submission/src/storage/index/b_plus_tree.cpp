#include <mutex>
#include <string>

#include "common/exception.h"
#include "common/logger.h"
#include "common/rid.h"
#include "storage/index/b_plus_tree.h"
#include "storage/page/header_page.h"

namespace bustub {
INDEX_TEMPLATE_ARGUMENTS
BPLUSTREE_TYPE::BPlusTree(std::string name, BufferPoolManager *buffer_pool_manager, const KeyComparator &comparator,
                          int leaf_max_size, int internal_max_size)
    : index_name_(std::move(name)),
      root_page_id_(INVALID_PAGE_ID),
      buffer_pool_manager_(buffer_pool_manager),
      comparator_(comparator),
      leaf_max_size_(leaf_max_size),
      internal_max_size_(internal_max_size) {}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetRootPageId() -> page_id_t { return root_page_id_; }

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::IsEmpty() const -> bool { return root_page_id_ == INVALID_PAGE_ID; }

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetValue(const KeyType &key, std::vector<ValueType> *result, Transaction *transaction) -> bool {
  std::lock_guard<std::mutex> guard(tree_latch_);
  if (IsEmpty()) {
    return false;
  }

  // 找到包含该key的叶子页面
  Page *page = FindLeafPageNoLock(key, false);

  if (page == nullptr) {
    return false;
  }
  // 将 Page中的数据reinterpret_cast成LeafPage 结构
  auto *leaf = reinterpret_cast<LeafPage *>(page->GetData());

  // 在叶子节点中查找key，找到时将value写入v
  ValueType v{};
  bool found = leaf->Lookup(key, v, comparator_);
  if (found) {
    result->push_back(v);
  }

  buffer_pool_manager_->UnpinPage(leaf->GetPageId(), false);
  return found;
}

// 当整棵B+树为空时，用第一个kv创建一棵新树
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::StartNewTree(const KeyType &key, const ValueType &value) {
  // 新建一个page，作为根节点
  page_id_t page_id;
  Page *page = buffer_pool_manager_->NewPage(&page_id);
  if (page == nullptr) {
    throw Exception(ExceptionType::OUT_OF_MEMORY, "Cannot allocate new page");
  }

  // 将page页数据reinterpret_cast为叶子节点的结构
  auto *leaf = reinterpret_cast<LeafPage *>(page->GetData());
  leaf->Init(page_id, INVALID_PAGE_ID, leaf_max_size_);
  leaf->Insert(key, value, comparator_);

  root_page_id_ = page_id;
  // 同步更新HeaderPage中的root记录
  UpdateRootPageId(1);

  buffer_pool_manager_->UnpinPage(page_id, true);
}

// 插入单个kv到叶子节点
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::InsertIntoLeaf(const KeyType &key, const ValueType &value, Transaction *transaction) -> bool {
  std::lock_guard<std::mutex> guard(tree_latch_);

  if (IsEmpty()) {
    StartNewTree(key, value);
    return true;
  }

  Page *page = FindLeafPageNoLock(key, false);
  if (page == nullptr) {
    return false;
  }

  auto *leaf = reinterpret_cast<LeafPage *>(page->GetData());
  page_id_t leaf_page_id = leaf->GetPageId();

  // 存在重复键
  ValueType temp_value;
  if (leaf->Lookup(key, temp_value, comparator_)) {
    buffer_pool_manager_->UnpinPage(leaf_page_id, false);
    return false;
  }

  // 如果叶子节点未满，直接插入
  if (leaf->GetSize() < leaf->GetMaxSize()) {
    bool success = leaf->Insert(key, value, comparator_);
    buffer_pool_manager_->UnpinPage(leaf_page_id, success);
    return success;
  }

  // 叶子节点已满，需要分裂
  page_id_t new_page_id;
  // 分配一个新页面作为新的叶子节点
  Page *new_page_raw = buffer_pool_manager_->NewPage(&new_page_id);
  if (new_page_raw == nullptr) {
    buffer_pool_manager_->UnpinPage(leaf_page_id, false);
    return false;
  }

  // 初始化分配到的子页
  auto *new_leaf = reinterpret_cast<LeafPage *>(new_page_raw->GetData());
  new_leaf->Init(new_page_id, leaf->GetParentPageId(), leaf_max_size_);

  // 创建一个临时数组包含所有元素和新元素
  int original_size = leaf->GetSize();
  std::vector<std::pair<KeyType, ValueType>> temp;
  temp.reserve(original_size + 1);

  // 将原叶子节点的元素添加到临时数组
  for (int i = 0; i < original_size; i++) {
    temp.emplace_back(leaf->KeyAt(i), leaf->ValueAt(i));
  }

  // 添加新元素
  temp.emplace_back(key, value);

  // 按key排序
  std::sort(temp.begin(), temp.end(),
            [this](const auto &a, const auto &b) { return comparator_(a.first, b.first) < 0; });

  // 清空原叶子节点
  leaf->SetSize(0);

  // 将前一半元素放回原叶子节点
  int split_index = (original_size + 1) / 2;
  for (int i = 0; i < split_index; i++) {
    leaf->Insert(temp[i].first, temp[i].second, comparator_);
  }

  // 将后一半元素放入新叶子节点
  for (int i = split_index; i < original_size + 1; i++) {
    new_leaf->Insert(temp[i].first, temp[i].second, comparator_);
  }

  // 更新链表指针
  new_leaf->SetNextPageId(leaf->GetNextPageId());
  leaf->SetNextPageId(new_page_id);

  // 更新父节点，让父节点插入new_leaf的第一key
  InsertIntoParent(leaf, new_leaf->KeyAt(0), new_leaf, transaction);

  buffer_pool_manager_->UnpinPage(leaf_page_id, true);
  buffer_pool_manager_->UnpinPage(new_page_id, true);

  return true;
}

// 分裂后将新键插入父节点
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::InsertIntoParent(BPlusTreePage *old_node, const KeyType &key, BPlusTreePage *new_node,
                                      Transaction *transaction) {
  // 若old_node本身是根节点，说明需要新建一个根节点
  if (old_node->IsRootPage()) {
    page_id_t new_root_id;
    // 创建一个新的根页面
    Page *root_raw = buffer_pool_manager_->NewPage(&new_root_id);
    // 将其视为内部节点
    auto *root = reinterpret_cast<InternalPage *>(root_raw->GetData());
    // 初始化：parent = INVALID_PAGE_ID，最大容量 = internal_max_size_
    root->Init(new_root_id, INVALID_PAGE_ID, internal_max_size_);

    // 把old_node和new_node两个孩子挂到新根下
    root->PopulateNewRoot(old_node->GetPageId(), key, new_node->GetPageId());

    // 更新两个孩子的父指针
    old_node->SetParentPageId(new_root_id);
    new_node->SetParentPageId(new_root_id);

    // 更新根指针并写入 header page
    root_page_id_ = new_root_id;
    UpdateRootPageId(0);

    buffer_pool_manager_->UnpinPage(new_root_id, true);
    return;
  }

  //// old_node不是根节点，则直接插入它的父节点中
  page_id_t parent_id = old_node->GetParentPageId();
  Page *parent_raw = buffer_pool_manager_->FetchPage(parent_id);
  auto *parent = reinterpret_cast<InternalPage *>(parent_raw->GetData());

  // 把key和new_node插入到old_node之后
  parent->InsertNodeAfter(old_node->GetPageId(), key, new_node->GetPageId());
  new_node->SetParentPageId(parent_id);

  // 若父节点未超出容量，则完成插入
  if (parent->GetSize() > parent->GetMaxSize()) {
    // 父节点满了，需要分裂
    page_id_t new_internal_id;
    Page *new_internal_raw = buffer_pool_manager_->NewPage(&new_internal_id);
    auto *new_internal = reinterpret_cast<InternalPage *>(new_internal_raw->GetData());

    // new_internal的parent=旧parent的parent
    new_internal->Init(new_internal_id, parent->GetParentPageId(), internal_max_size_);
    // 把parent的后一半元素移动到new_internal中
    parent->MoveHalfTo(new_internal, buffer_pool_manager_);
    // 分裂出来的new_internal中第0个key需要上提到更高层
    KeyType up_key = new_internal->KeyAt(0);
    // 递归向父节点插入up_key
    InsertIntoParent(parent, up_key, new_internal, transaction);

    buffer_pool_manager_->UnpinPage(new_internal_id, true);
  }

  buffer_pool_manager_->UnpinPage(parent_id, true);
}

// 找到对应key的叶子节点
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::FindLeafPageNoLock(const KeyType &key, bool leftMost) -> Page * {
  if (root_page_id_ == INVALID_PAGE_ID) {
    return nullptr;
  }

  page_id_t current_page_id = root_page_id_;
  Page *page = buffer_pool_manager_->FetchPage(current_page_id);
  auto *tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());

  // 自顶向下，直到到达leaf
  while (!tree_page->IsLeafPage()) {
    auto *internal_page = reinterpret_cast<InternalPage *>(tree_page);
    page_id_t next_page_id;

    if (leftMost) {
      // 若 leftMost = true，则一直往最左侧孩子走
      next_page_id = internal_page->ValueAt(0);
    } else {
      // 否则根据 key 做普通查找
      next_page_id = internal_page->Lookup(key, comparator_);
    }
    buffer_pool_manager_->UnpinPage(current_page_id, false);

    // 往下一层走
    current_page_id = next_page_id;
    page = buffer_pool_manager_->FetchPage(current_page_id);
    tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());
  }

  return page;
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::FindLeafPage(const KeyType &key, bool leftMost) -> Page * {
  std::lock_guard<std::mutex> guard(tree_latch_);
  return FindLeafPageNoLock(key, leftMost);
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Insert(const KeyType &key, const ValueType &value, Transaction *transaction) -> bool {
  return InsertIntoLeaf(key, value, transaction);
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Remove(const KeyType &key, Transaction *transaction) {
  std::lock_guard<std::mutex> guard(tree_latch_);
  if (IsEmpty()) {
    return;
  }

  // 找不到叶子节点
  Page *page = FindLeafPageNoLock(key, false);
  if (page == nullptr) {
    return;
  }
  auto *leaf = reinterpret_cast<LeafPage *>(page->GetData());

  // 删除并返回是否删除成功
  bool existed = leaf->RemoveAndDeleteRecord(key, comparator_);

  // key不存在
  if (!existed) {
    buffer_pool_manager_->UnpinPage(leaf->GetPageId(), false);
    return;
  }

  // leaf是根节点
  if (leaf->IsRootPage()) {
    if (leaf->GetSize() == 0) {
      // 删除后为空，树变空
      root_page_id_ = INVALID_PAGE_ID;
      UpdateRootPageId(0);
    }
    buffer_pool_manager_->UnpinPage(leaf->GetPageId(), true);
    return;
  }

  // 删除后仍然满足最小大小要求
  if (leaf->GetSize() >= leaf->GetMinSize()) {
    buffer_pool_manager_->UnpinPage(leaf->GetPageId(), true);
    return;
  }

  // 不够最小尺寸，需要合并或借 key
  CoalesceOrRedistribute(leaf, transaction);

  buffer_pool_manager_->UnpinPage(leaf->GetPageId(), true);
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin() -> INDEXITERATOR_TYPE {
  std::lock_guard<std::mutex> guard(tree_latch_);

  // 若树为空，返回一个默认构造的迭代器
  if (root_page_id_ == INVALID_PAGE_ID) {
    return INDEXITERATOR_TYPE();
  }

  // 找到最左（最小 key）的叶子页。传入KeyType()和leftMost=true
  Page *leaf_page = FindLeafPageNoLock(KeyType(), true);
  if (leaf_page == nullptr) {
    return INDEXITERATOR_TYPE();
  }
  auto *leaf = reinterpret_cast<LeafPage *>(leaf_page->GetData());

  // 若最左叶子没有元素（可能因为并发删除），释放页并返回end
  if (leaf->GetSize() == 0) {
    buffer_pool_manager_->UnpinPage(leaf_page->GetPageId(), false);
    return INDEXITERATOR_TYPE();
  }

  // 返回指向最左叶子第0个元素的迭代器（迭代器持有 leaf_page 的 pin）
  return INDEXITERATOR_TYPE(buffer_pool_manager_, leaf_page, 0);
}

// 带 key 的 Begin：返回第一个 >= key 的位置
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin(const KeyType &key) -> INDEXITERATOR_TYPE {
  std::lock_guard<std::mutex> guard(tree_latch_);

  // 空树直接返回end
  if (root_page_id_ == INVALID_PAGE_ID) {
    return INDEXITERATOR_TYPE();
  }

  // 找到包含 key 的叶子页
  Page *leaf_page = FindLeafPageNoLock(key, false);
  if (leaf_page == nullptr) {
    return INDEXITERATOR_TYPE();
  }
  auto *leaf = reinterpret_cast<LeafPage *>(leaf_page->GetData());

  int idx = 0;
  int size = leaf->GetSize();

  // 在叶子节点内线性扫描找到第一个不小于key的索引
  while (idx < size && comparator_(leaf->KeyAt(idx), key) < 0) {
    ++idx;
  }
  if (idx >= size) {
    // 若该叶子的所有key都<key，则需要转到右兄弟节点
    page_id_t next = leaf->GetNextPageId();
    // 先 unpin 当前页（读取时未标记为脏）
    buffer_pool_manager_->UnpinPage(leaf_page->GetPageId(), false);
    if (next == INVALID_PAGE_ID) {
      // 没有右兄弟，则没有满足条件的元素，返回 end
      return INDEXITERATOR_TYPE();
    }
    // 否则取右兄弟页并检查其合法性
    Page *next_page = buffer_pool_manager_->FetchPage(next);
    if (next_page == nullptr) {
      return INDEXITERATOR_TYPE();
    }
    auto *next_leaf = reinterpret_cast<LeafPage *>(next_page->GetData());
    if (next_leaf->GetSize() == 0) {
      // 右兄弟为空，释放并返回 end
      buffer_pool_manager_->UnpinPage(next_page->GetPageId(), false);
      return INDEXITERATOR_TYPE();
    }
    // 返回指向右兄弟的第 0 个元素
    return INDEXITERATOR_TYPE(buffer_pool_manager_, next_page, 0);
  }

  // 在当前叶子中找到了位置，返回对应迭代器（并持有该页）
  return INDEXITERATOR_TYPE(buffer_pool_manager_, leaf_page, idx);
}

// 返回默认构造的迭代器表示 end（不持有任何页）
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::End() -> INDEXITERATOR_TYPE { return INDEXITERATOR_TYPE(); }

// 更新header page中记录的root_page_id
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::UpdateRootPageId(int insert_record) {
  // insert_record != 0表示要插入一条新记录；否则更新已有记录
  auto *header_page = static_cast<HeaderPage *>(buffer_pool_manager_->FetchPage(HEADER_PAGE_ID));
  if (insert_record != 0) {
    // 在header中插入 (index_name_ -> root_page_id_) 的映射，首次创建索引时使用
    header_page->InsertRecord(index_name_, root_page_id_);
  } else {
    // 更新已有记录
    header_page->UpdateRecord(index_name_, root_page_id_);
  }
  // 更新后unpin header page，并标记为已修改（dirty）
  buffer_pool_manager_->UnpinPage(HEADER_PAGE_ID, true);
}

// 叶子层的合并或重分配
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::CoalesceOrRedistribute(LeafPage *leaf, Transaction *txn) {
  if (leaf->GetSize() >= leaf->GetMinSize()) {
    return;
  }

  Page *parent_page = buffer_pool_manager_->FetchPage(leaf->GetParentPageId());
  auto *parent = reinterpret_cast<InternalPage *>(parent_page->GetData());

  int index = parent->ValueIndex(leaf->GetPageId());

  LeafPage *sibling = nullptr;
  Page *sibling_page = nullptr;

  bool borrow_from_left = (index > 0);
  if (borrow_from_left) {
    sibling_page = buffer_pool_manager_->FetchPage(parent->ValueAt(index - 1));
  } else {
    sibling_page = buffer_pool_manager_->FetchPage(parent->ValueAt(index + 1));
  }

  sibling = reinterpret_cast<LeafPage *>(sibling_page->GetData());

  // Try redistribute first
  if (sibling->GetSize() > sibling->GetMinSize()) {
    if (borrow_from_left) {
      sibling->MoveLastToFrontOf(leaf);
      parent->SetKeyAt(index, leaf->KeyAt(0));
    } else {
      sibling->MoveFirstToEndOf(leaf);
      parent->SetKeyAt(index + 1, sibling->KeyAt(0));
    }

    buffer_pool_manager_->UnpinPage(sibling->GetPageId(), true);
    buffer_pool_manager_->UnpinPage(parent->GetPageId(), true);
    return;
  }

  // Otherwise coalesce
  if (borrow_from_left) {
    sibling->MoveAllTo(leaf);
    buffer_pool_manager_->UnpinPage(sibling->GetPageId(), true);
    buffer_pool_manager_->DeletePage(sibling->GetPageId());

    parent->Remove(index - 1);
  } else {
    leaf->MoveAllTo(sibling);
    buffer_pool_manager_->UnpinPage(sibling->GetPageId(), true);
    buffer_pool_manager_->DeletePage(leaf->GetPageId());

    parent->Remove(index);
  }

  // Handle parent underflow
  if (parent->IsRootPage()) {
    AdjustRoot(parent);
  } else if (parent->GetSize() < parent->GetMinSize()) {
    CoalesceOrRedistributeInternal(parent, txn);
  }

  buffer_pool_manager_->UnpinPage(parent->GetPageId(), true);
}

// 内部节点的合并或重分配
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::CoalesceOrRedistributeInternal(InternalPage *node, Transaction *txn) {
  if (node->GetSize() >= node->GetMinSize()) {
    return;
  }

  Page *parent_page = buffer_pool_manager_->FetchPage(node->GetParentPageId());
  auto *parent = reinterpret_cast<InternalPage *>(parent_page->GetData());

  int index = parent->ValueIndex(node->GetPageId());

  InternalPage *sibling = nullptr;
  Page *sibling_page = nullptr;

  bool borrow_from_left = (index > 0);
  if (borrow_from_left) {
    sibling_page = buffer_pool_manager_->FetchPage(parent->ValueAt(index - 1));
  } else {
    sibling_page = buffer_pool_manager_->FetchPage(parent->ValueAt(index + 1));
  }

  sibling = reinterpret_cast<InternalPage *>(sibling_page->GetData());

  // Try redistribute first
  if (sibling->GetSize() > sibling->GetMinSize()) {
    if (borrow_from_left) {
      KeyType middle_key = parent->KeyAt(index);
      sibling->MoveLastToFrontOf(node, middle_key, buffer_pool_manager_);
      parent->SetKeyAt(index, node->KeyAt(0));
    } else {
      KeyType middle_key = parent->KeyAt(index + 1);
      sibling->MoveFirstToEndOf(node, middle_key, buffer_pool_manager_);
      parent->SetKeyAt(index + 1, sibling->KeyAt(0));
    }

    buffer_pool_manager_->UnpinPage(sibling->GetPageId(), true);
    buffer_pool_manager_->UnpinPage(parent->GetPageId(), true);
    return;
  }

  // Otherwise coalesce
  if (borrow_from_left) {
    KeyType middle_key = parent->KeyAt(index);
    sibling->MoveAllTo(node, middle_key, buffer_pool_manager_);
    buffer_pool_manager_->UnpinPage(sibling->GetPageId(), true);
    buffer_pool_manager_->DeletePage(sibling->GetPageId());

    parent->Remove(index - 1);
  } else {
    KeyType middle_key = parent->KeyAt(index + 1);
    node->MoveAllTo(sibling, middle_key, buffer_pool_manager_);
    buffer_pool_manager_->UnpinPage(sibling->GetPageId(), true);
    buffer_pool_manager_->DeletePage(node->GetPageId());

    parent->Remove(index);
  }

  // Handle parent underflow
  if (parent->IsRootPage()) {
    AdjustRoot(parent);
  } else if (parent->GetSize() < parent->GetMinSize()) {
    CoalesceOrRedistributeInternal(parent, txn);
  }

  buffer_pool_manager_->UnpinPage(parent->GetPageId(), true);
}

// 根节点调整：当根被删除或只有一个孩子时调用
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::AdjustRoot(BPlusTreePage *old_root_node) {
  // If root is empty, tree becomes empty
  if (old_root_node->GetSize() == 0) {
    root_page_id_ = INVALID_PAGE_ID;
    UpdateRootPageId(0);
    buffer_pool_manager_->DeletePage(old_root_node->GetPageId());
    return;
  }

  // If root has only one child (must be internal node), make that child the new root
  if (old_root_node->GetSize() == 1 && !old_root_node->IsLeafPage()) {
    auto *internal_root = reinterpret_cast<InternalPage *>(old_root_node);
    page_id_t new_root_id = internal_root->ValueAt(0);

    Page *new_root_page = buffer_pool_manager_->FetchPage(new_root_id);
    auto *new_root = reinterpret_cast<BPlusTreePage *>(new_root_page->GetData());
    new_root->SetParentPageId(INVALID_PAGE_ID);

    root_page_id_ = new_root_id;
    UpdateRootPageId(0);

    buffer_pool_manager_->UnpinPage(new_root_id, true);
    buffer_pool_manager_->DeletePage(old_root_node->GetPageId());
  }
}

// 从 parent 中移除 index 对应的 child 和分隔键
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::RemoveFromParent(BPlusTreePage *child, InternalPage *parent, int index, Transaction *txn) {
  parent->Remove(index);

  if (parent->GetSize() >= parent->GetMinSize()) {
    return;
  }

  if (parent->IsRootPage()) {
    AdjustRoot(parent);
    return;
  }

  CoalesceOrRedistributeInternal(parent, txn);
}

template class BPlusTree<GenericKey<4>, RID, GenericComparator<4>>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>>;
template class BPlusTree<GenericKey<16>, RID, GenericComparator<16>>;
template class BPlusTree<GenericKey<32>, RID, GenericComparator<32>>;
template class BPlusTree<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub