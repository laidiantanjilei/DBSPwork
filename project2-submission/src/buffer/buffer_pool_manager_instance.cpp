//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager_instance.cpp
//
// Identification: src/buffer/buffer_pool_manager_instance.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/buffer_pool_manager_instance.h"

#include "common/exception.h"
#include "common/macros.h"

namespace bustub {

BufferPoolManagerInstance::BufferPoolManagerInstance(size_t pool_size, DiskManager *disk_manager, size_t replacer_k,
                                                     LogManager *log_manager)
    : pool_size_(pool_size), disk_manager_(disk_manager), log_manager_(log_manager) {
  // allocate continuous memory for buffer pool
  pages_ = new Page[pool_size_];
  page_table_ = new ExtendibleHashTable<page_id_t, frame_id_t>(bucket_size_);
  replacer_ = new LRUKReplacer(pool_size, replacer_k);

  // Initially, every frame is free.
  for (size_t i = 0; i < pool_size_; ++i) {
    free_list_.emplace_back(static_cast<int>(i));
  }
}

BufferPoolManagerInstance::~BufferPoolManagerInstance() {
  delete[] pages_;
  delete page_table_;
  delete replacer_;
}

auto BufferPoolManagerInstance::NewPgImp(page_id_t *page_id) -> Page * {
  std::lock_guard<std::mutex> lock(latch_);
  frame_id_t frame_id;

  // 1) Acquire a free frame (from free list or replacer)
  if (!free_list_.empty()) {
    frame_id = free_list_.front();
    free_list_.pop_front();
  } else {
    if (!replacer_->Evict(&frame_id)) {
      // no frame available
      *page_id = INVALID_PAGE_ID;
      return nullptr;
    }

    // evict victim
    Page *victim = &pages_[frame_id];
    page_id_t old_pid = victim->page_id_;
    if (victim->IsDirty()) {
      disk_manager_->WritePage(old_pid, victim->GetData());
      victim->is_dirty_ = false;
    }
    // remove old mapping
    page_table_->Remove(old_pid);

    // reset victim metadata to be safe (will be reinitialized below)
    victim->ResetMemory();
    victim->page_id_ = INVALID_PAGE_ID;
    victim->pin_count_ = 0;
  }

  // 2) Allocate new page id
  page_id_t new_pid = AllocatePage();

  // 3) Initialize the frame
  Page *page = &pages_[frame_id];
  page->ResetMemory();
  page->page_id_ = new_pid;
  page->pin_count_ = 1;
  page->is_dirty_ = false;

  // write an empty page to disk (initialization)
  disk_manager_->WritePage(new_pid, page->GetData());

  // 4) Update page table and replacer state
  page_table_->Insert(new_pid, frame_id);
  replacer_->RecordAccess(frame_id);
  replacer_->SetEvictable(frame_id, false);

  *page_id = new_pid;
  return page;
}

auto BufferPoolManagerInstance::FetchPgImp(page_id_t page_id) -> Page * {
  std::lock_guard<std::mutex> lock(latch_);
  frame_id_t frame_id;

  // 1) If page is already in buffer pool -> hit
  if (page_table_->Find(page_id, frame_id)) {
    Page *page = &pages_[frame_id];
    page->pin_count_++;
    replacer_->RecordAccess(frame_id);
    replacer_->SetEvictable(frame_id, false);
    return page;
  }

  // 2) Need to bring page from disk -> get a free frame
  if (!free_list_.empty()) {
    frame_id = free_list_.front();
    free_list_.pop_front();
  } else {
    if (!replacer_->Evict(&frame_id)) {
      // no available frame
      return nullptr;
    }

    // evict victim
    Page *victim = &pages_[frame_id];
    page_id_t old_pid = victim->page_id_;
    if (victim->IsDirty()) {
      disk_manager_->WritePage(old_pid, victim->GetData());
      victim->is_dirty_ = false;
    }
    page_table_->Remove(old_pid);

    // reset victim metadata
    victim->ResetMemory();
    victim->page_id_ = INVALID_PAGE_ID;
    victim->pin_count_ = 0;
  }

  // 3) Read requested page from disk into frame
  Page *page = &pages_[frame_id];
  disk_manager_->ReadPage(page_id, page->GetData());

  // 4) Set metadata
  page->page_id_ = page_id;
  page->pin_count_ = 1;
  page->is_dirty_ = false;

  // 5) Update page table and replacer
  page_table_->Insert(page_id, frame_id);
  replacer_->RecordAccess(frame_id);
  replacer_->SetEvictable(frame_id, false);

  return page;
}

auto BufferPoolManagerInstance::UnpinPgImp(page_id_t page_id, bool is_dirty) -> bool {
  std::lock_guard<std::mutex> lock(latch_);
  frame_id_t frame_id;

  if (!page_table_->Find(page_id, frame_id)) {
    return false;
  }

  Page *page = &pages_[frame_id];
  if (page->pin_count_ <= 0) {
    return false;
  }

  page->pin_count_--;
  if (is_dirty) {
    page->is_dirty_ = true;
  }

  if (page->pin_count_ == 0) {
    replacer_->SetEvictable(frame_id, true);
  }

  return true;
}

auto BufferPoolManagerInstance::FlushPgImp(page_id_t page_id) -> bool {
  std::lock_guard<std::mutex> lock(latch_);
  frame_id_t frame_id;

  if (!page_table_->Find(page_id, frame_id)) {
    return false;
  }

  Page *page = &pages_[frame_id];
  disk_manager_->WritePage(page_id, page->GetData());
  page->is_dirty_ = false;
  return true;
}

void BufferPoolManagerInstance::FlushAllPgsImp() {
  std::lock_guard<std::mutex> lock(latch_);
  for (size_t fid = 0; fid < pool_size_; ++fid) {
    Page *page = &pages_[fid];
    if (page->page_id_ != INVALID_PAGE_ID && page->IsDirty()) {
      disk_manager_->WritePage(page->page_id_, page->GetData());
      page->is_dirty_ = false;
    }
  }
}

auto BufferPoolManagerInstance::DeletePgImp(page_id_t page_id) -> bool {
  std::lock_guard<std::mutex> lock(latch_);
  frame_id_t frame_id;

  // If page not in buffer pool, for initial project simply return true.
  if (!page_table_->Find(page_id, frame_id)) {
    return true;
  }

  Page *page = &pages_[frame_id];
  // cannot delete pinned page
  if (page->pin_count_ > 0) {
    return false;
  }

  // if dirty, write back before removal
  if (page->IsDirty()) {
    disk_manager_->WritePage(page_id, page->GetData());
    page->is_dirty_ = false;
  }

  // remove from replacer, page table, reset and return frame to free list
  replacer_->Remove(frame_id);
  page_table_->Remove(page_id);

  page->ResetMemory();
  page->page_id_ = INVALID_PAGE_ID;
  page->pin_count_ = 0;
  page->is_dirty_ = false;

  free_list_.push_back(frame_id);
  return true;
}

auto BufferPoolManagerInstance::AllocatePage() -> page_id_t { return next_page_id_++; }

}  // namespace bustub
