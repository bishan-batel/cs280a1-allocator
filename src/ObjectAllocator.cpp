#include "ObjectAllocator.h"

#include <cstring>

// NOLINTBEGIN(*-exception-baseclass)

ObjectAllocator::ObjectAllocator(const usize obj_size, const OAConfig& src_config):
    config{src_config}, object_size{obj_size}, page_size{0} {

  // calculate intern and extern alignment
  if (config.Alignment_ != 0) {
    config.LeftAlignSize_ =
      static_cast<u32>((sizeof(GenericObject) + config.PadBytes_ + config.HBlockInfo_.size_) % config.Alignment_);
    config.LeftAlignSize_ = (config.Alignment_ - config.LeftAlignSize_) % config.Alignment_;

    config.InterAlignSize_ =
      static_cast<u32>((object_size + config.PadBytes_ * 2 + config.HBlockInfo_.size_) % config.Alignment_);
    config.InterAlignSize_ = (config.Alignment_ - config.InterAlignSize_) % config.Alignment_;
  }

  block_size = config.HBlockInfo_.size_ + config.PadBytes_ + object_size + config.PadBytes_ + config.InterAlignSize_;

  page_size = sizeof(GenericObject)               // next page ptr
            + config.LeftAlignSize_               // ptr alignment
            + block_size * config.ObjectsPerPage_ // per block size
            - config.InterAlignSize_;             // intern align size - the first ones

  statistics.PageSize_ = page_size;
  statistics.ObjectSize_ = object_size;

  // allocate first page if not using the CPPMemManager
  if (not config.UseCPPMemManager_) {
    allocate_page();
  }
}

ObjectAllocator::~ObjectAllocator() noexcept {
  GenericObject* page = &as_list(page_list);

  while (page) {
    u8* const to_delete = as_bytes(page);
    page = page->Next;
    free_page(to_delete);
  }
}

void* ObjectAllocator::Allocate(const char* label) {
  // if no more free blocks try to allocate a new page

  u8* block{nullptr};

  if (not config.UseCPPMemManager_) {
    if (free_list == nullptr) {
      allocate_page();
    }

    block = free_list;
    free_list = as_bytes(as_list(free_list).Next);
  } else {
    try {
      block = new u8[object_size];
    } catch (const std::bad_alloc&) {
      throw OAException(OAException::E_NO_MEMORY, "'new[]' threw bad alloc.");
    }
  }

  // Bookkeeping
  statistics.ObjectsInUse_++;
  statistics.Allocations_++;
  statistics.FreeObjects_--;

  // unsure if we are allowed to use std::max
  statistics.MostObjects_ =
    statistics.ObjectsInUse_ > statistics.MostObjects_ ? statistics.ObjectsInUse_ : statistics.MostObjects_;

  if (not config.UseCPPMemManager_) {
    setup_allocated_header(block - config.PadBytes_ - config.HBlockInfo_.size_, label);
  }

  if (config.DebugOn_) {
    u8* pos = block - config.PadBytes_;

    if (not config.UseCPPMemManager_) {
      memset(pos, PAD_PATTERN, config.PadBytes_);
      pos += config.PadBytes_;
    }

    memset(pos, ALLOCATED_PATTERN, object_size);
    pos += object_size;

    if (not config.UseCPPMemManager_) {
      memset(pos, PAD_PATTERN, config.PadBytes_);
    }
  }

  return block;
}

void ObjectAllocator::Free(void* const block_void_ptr) {
  if (not block_void_ptr) {
    return;
  }

  u8* const block = static_cast<u8*>(block_void_ptr);

  if (config.DebugOn_ and not config.UseCPPMemManager_) {

    // validate that this is a correct block boundry, throws if not
    validate_boundary(block);

    // check for double free
    if (is_in_free_list(block)) {
      throw OAException(OAException::E_MULTIPLE_FREE, "Block has already been freed");
    }

    // if block pad bytes are overwritten
    if (not validate_block(block)) {
      throw OAException(OAException::E_CORRUPTED_BLOCK, "Corrupted Block");
    }
  }

  // bookkeeping
  statistics.ObjectsInUse_--;
  statistics.Deallocations_++;
  statistics.FreeObjects_++;

  if (config.UseCPPMemManager_) {
    delete[] block;
    return;
  } else {
    // bookkeeping headers
    setup_freed_header(block - config.PadBytes_ - config.HBlockInfo_.size_);
  }

  if (config.DebugOn_) {
    memset(block_void_ptr, FREED_PATTERN, object_size);
  }

  as_list(block).Next = &as_list(free_list);
  free_list = block;
}

auto ObjectAllocator::validate_boundary(const u8* block) const -> void {
  for (const GenericObject* page = &as_list(page_list); page; page = page->Next) {
    const u8* const page_min = as_bytes(page);
    const u8* const page_max = page_min + page_size;

    // skip if this is not on the page
    if (block < page_min or block >= page_max) {
      continue;
    }

    const u8* first_block =
      page_min + config.LeftAlignSize_ + sizeof(GenericObject) + config.HBlockInfo_.size_ + config.PadBytes_;

    // if block is not on a boundry in this page (or before the first block)
    if (block < first_block or (block - first_block) % static_cast<std::ptrdiff_t>(block_size) != 0) {
      throw OAException(OAException::E_BAD_BOUNDARY, "Invalid Boundry");
    }

    return;
  }

  throw OAException(OAException::E_BAD_BOUNDARY, "Invalid Boundry, not on any pages");
}

u32 ObjectAllocator::DumpMemoryInUse(const DUMPCALLBACK callback) const {
  u32 in_use{0};

  for (const GenericObject* page = &as_list(page_list); page; page = page->Next) {
    const u8* first_block =
      as_bytes(page) + sizeof(GenericObject) + config.LeftAlignSize_ + config.HBlockInfo_.size_ + config.PadBytes_;

    for (usize i = 0; i < config.ObjectsPerPage_; i++) {
      const u8* block = first_block + i * block_size;
      if (not is_in_free_list(block)) {
        in_use++;
        callback(block, object_size);
      }
    }
  }

  return in_use;
}

u32 ObjectAllocator::ValidatePages(const VALIDATECALLBACK callback) const {
  if (not config.DebugOn_ or config.PadBytes_ == 0) {
    return 0;
  }

  const GenericObject* page = &as_list(page_list);
  u32 invalid_count{0};

  while (page) {
    const u8* first_block =
      as_bytes(page) + sizeof(GenericObject) + config.LeftAlignSize_ + config.HBlockInfo_.size_ + config.PadBytes_;

    for (usize i = 0; i < config.ObjectsPerPage_; i++) {
      const u8* const block = first_block + block_size * i;
      if (not validate_block(block)) {
        callback(block, object_size);
        invalid_count++;
      }
    };

    page = page->Next;
  }

  return invalid_count;
}

void ObjectAllocator::free_page(u8* const page) const {
  // no invariants need to be preserved if there is no exernal header (heap-allocated)
  if (config.HBlockInfo_.type_ != OAConfig::hbExternal) {
    delete[] page;
    return;
  }

  u8* const first_header = page + sizeof(GenericObject) + config.LeftAlignSize_;

  for (usize i = 0; i < config.ObjectsPerPage_; i++) {
    MemBlockInfo*& info = *reinterpret_cast<MemBlockInfo**>(first_header + i * block_size);

    if (info == nullptr) {
      continue;
    }

    if (info->label) {
      delete[] info->label;
    }

    delete info;
  }

  delete[] page;
}

u32 ObjectAllocator::FreeEmptyPages() {
  u32 freed{0};

  GenericObject* prev = nullptr;
  GenericObject* page = &as_list(page_list);

  while (page) {

    if (not is_page_empty(as_bytes(page))) {
      prev = page;
      page = page->Next;
      continue;
    }

    freed++;

    GenericObject* next = page->Next;
    cull_free_blocks_in_page(as_bytes(page));
    free_page(as_bytes(page));
    statistics.PagesInUse_--;

    page = next;

    if (prev) {
      prev->Next = page;
    } else {
      page_list = as_bytes(page);
    }

    continue;
  }

  return freed;
}

void ObjectAllocator::cull_free_blocks_in_page(const u8* const page) {

  GenericObject* prev = nullptr;
  GenericObject* free = &as_list(free_list);

  while (free) {
    const u8* bytes = as_bytes(free);

    if (not(bytes > page and bytes < page + page_size)) {
      prev = free;
      free = free->Next;
      continue;
    }

    statistics.FreeObjects_--;

    GenericObject* next = free->Next;
    free = next;

    if (prev) {
      prev->Next = free;
    } else {
      free_list = as_bytes(free);
    }

    continue;
  }
}

bool ObjectAllocator::is_page_empty(u8* page) const {
  const u8* first_block =
    page + sizeof(GenericObject) + config.LeftAlignSize_ + config.HBlockInfo_.size_ + config.PadBytes_;

  for (usize i = 0; i < config.ObjectsPerPage_; i++) {
    const u8* block = first_block + i * block_size;
    if (not is_in_free_list(block)) {
      return false;
    }
  }
  return true;
}

void ObjectAllocator::SetDebugState(const bool State) { config.DebugOn_ = State; }

const void* ObjectAllocator::GetFreeList() const { return free_list; }

const void* ObjectAllocator::GetPageList() const { return page_list; }

const OAConfig& ObjectAllocator::GetConfig() const { return config; }

const OAStats& ObjectAllocator::GetStats() const { return statistics; }

bool ObjectAllocator::ImplementedExtraCredit() { return true; }

GenericObject& ObjectAllocator::as_list(u8* const bytes // NOLINT(*-non-const-parameter)
) {
  return *reinterpret_cast<GenericObject*>(bytes);
}

const GenericObject& ObjectAllocator::as_list(const u8* const bytes) {
  return *reinterpret_cast<const GenericObject*>(bytes);
}

void ObjectAllocator::allocate_page() {
  if (config.MaxPages_ != 0 and statistics.PagesInUse_ >= config.MaxPages_) {
    throw OAException(OAException::E_NO_PAGES, "Out of pages");
  }

  u8* memory;

  try {
    memory = new u8[page_size]{};
  } catch (const std::bad_alloc& err) {
    throw OAException(OAException::E_NO_MEMORY, err.what());
  }

  // up the stat, was added to the list
  statistics.PagesInUse_++;

  as_list(memory).Next = &as_list(page_list);
  page_list = memory;

  // signing
  //
  if (config.DebugOn_) {
    memset(page_list + sizeof(GenericObject), ALIGN_PATTERN, config.LeftAlignSize_);
  }

  u8* const first_obj =
    page_list + sizeof(GenericObject) + config.HBlockInfo_.size_ + config.LeftAlignSize_ + config.PadBytes_;

  for (usize i = 0; i < config.ObjectsPerPage_; i++) {
    u8* block = first_obj + block_size * i - config.PadBytes_;

    // skipover header
    memset(block, PAD_PATTERN, config.PadBytes_);
    block += config.PadBytes_;

    memset(block, UNALLOCATED_PATTERN, object_size);
    block += object_size;

    memset(block, PAD_PATTERN, config.PadBytes_);
  }

  for (usize i = 1; i < config.ObjectsPerPage_; i++) {
    u8* const block = first_obj + block_size * i;
    u8* const prev_block = first_obj + block_size * (i - 1);

    if (config.DebugOn_) {
      memset(prev_block + object_size + config.PadBytes_, ALIGN_PATTERN, config.InterAlignSize_);
    }

    as_list(block).Next = &as_list(prev_block);
  }

  // initialise header blocks
  init_header_blocks_for_page(first_obj - config.PadBytes_ - config.HBlockInfo_.size_);

  as_list(first_obj).Next = &as_list(free_list);
  free_list = first_obj + block_size * (config.ObjectsPerPage_ - 1);

  statistics.FreeObjects_ += config.ObjectsPerPage_;
}

void ObjectAllocator::init_header_blocks_for_page(u8* const first_header) const {
  if (config.HBlockInfo_.size_ == 0) {
    return;
  }
  // memcpy the default header to each one to skip multiple branches
  for (usize i = 0; i < config.ObjectsPerPage_; i++) {
    memset(first_header + i * block_size, 0, config.HBlockInfo_.size_);
  }
}

bool ObjectAllocator::is_in_free_list(const u8* const block) const {
  const u8* header = block - config.PadBytes_ - config.HBlockInfo_.size_;

  switch (config.HBlockInfo_.type_) {
    case OAConfig::hbBasic:
      {
        return (*(header + sizeof(u32)) & 0x1) == 0;
      }

    case OAConfig::hbExtended:
      {
        const u8 flag = header[sizeof(u16) + sizeof(u32) + config.HBlockInfo_.additional_];
        return (flag & 0x1) == 0;
      }

    case OAConfig::hbExternal:
      {
        return *reinterpret_cast<const MemBlockInfo* const*>(header) == nullptr;
      }

    case OAConfig::hbNone:
    default: break;
  }

  const GenericObject* free = &as_list(free_list);

  for (; free; free = free->Next) {
    if (free == &as_list(block)) {
      return true;
    }
  }

  return false;
}

bool ObjectAllocator::validate_page(const u8* const page) const {
  // skip over to the first block

  const u8* first_block =
    page + sizeof(GenericObject) + config.LeftAlignSize_ + config.HBlockInfo_.size_ + config.PadBytes_;

  for (usize i = 0; i < config.ObjectsPerPage_ - 1; i++) {
    const u8* const block = first_block + block_size * i;
    if (not validate_block(block)) {
      return false;
    }
  };

  return true;
}

bool ObjectAllocator::validate_block(const u8* block) const {
  // left pad byte signature check
  if (not is_signed_as(block - config.PadBytes_, config.PadBytes_, PAD_PATTERN)) {
    return false;
  }

  // right pad byte signature check
  if (not is_signed_as(block + object_size, config.PadBytes_, PAD_PATTERN)) {
    return false;
  }

  return true;
}

void ObjectAllocator::setup_allocated_header(u8* const header, const char* label) const {
  if (config.HBlockInfo_.size_ == 0) {
    return;
  }

  switch (config.HBlockInfo_.type_) {
    case OAConfig::hbBasic:
      {
        // set allocation number ID
        *reinterpret_cast<u32*>(header) = statistics.Allocations_;

        // set in use flag to on
        *(header + sizeof(u32)) |= 0x1;
        return;
      }
    case OAConfig::hbExtended:
      {
        u8* pos = header;

        // set additional user defined bytes to 0
        memset(pos, 0, config.HBlockInfo_.additional_);
        pos += config.HBlockInfo_.additional_;

        // set allocation number ID
        (*reinterpret_cast<u16*>(pos))++;
        pos += sizeof(u16);

        *reinterpret_cast<u32*>(pos) = statistics.Allocations_;
        pos += sizeof(u32);

        // set in use flag to on
        *pos |= 0x1;
        return;
      }
    case OAConfig::hbExternal:
      {

        char* label_copy = nullptr;

        if (label != nullptr) {
          label_copy = new char[strlen(label) + 1]{};
          strcpy(label_copy, label);
        }

        *reinterpret_cast<MemBlockInfo**>(header) = new MemBlockInfo{true, label_copy, statistics.Allocations_};

        return;
      }
    case OAConfig::hbNone:
    default: break;
  }
}

void ObjectAllocator::setup_freed_header(u8* header) const {
  if (config.HBlockInfo_.size_ == 0) {
    return;
  }

  switch (config.HBlockInfo_.type_) {
    case OAConfig::hbBasic:
      {
        // set in use flag to off
        *reinterpret_cast<u32*>(header) = 0;
        header[sizeof(u32)] &= static_cast<u8>(~0x1);
        return;
      }
    case OAConfig::hbExtended:
      {
        u8* pos = header;

        // skip over user bytes
        pos += config.HBlockInfo_.additional_;

        // skip over user counter
        pos += sizeof(u16);

        *reinterpret_cast<u32*>(pos) = 0;
        pos += sizeof(u32);

        // set in use flag to off
        *pos &= static_cast<u8>(~0x1);
        return;
      }
    case OAConfig::hbExternal:
      {
        MemBlockInfo** const info = reinterpret_cast<MemBlockInfo**>(header);

        // delete the label
        if ((*info)->label) {
          delete[] (*info)->label;
        }

        // delete the external data
        delete *info;

        *info = nullptr;

        return;
      }
    case OAConfig::hbNone:
    default: break;
  }
}

bool ObjectAllocator::is_signed_as(const u8* ptr, const usize extents, const u8 pattern) {

  for (usize i = 0; i < extents; i++) {
    if (ptr[i] != pattern) {
      return false;
    }
  }

  return true;
}

// NOLINTEND(*-exception-baseclass)
const u8* ObjectAllocator::as_bytes(const GenericObject* bytes) { return reinterpret_cast<const u8*>(bytes); }

u8* ObjectAllocator::as_bytes(GenericObject* bytes) { return reinterpret_cast<u8*>(bytes); }
