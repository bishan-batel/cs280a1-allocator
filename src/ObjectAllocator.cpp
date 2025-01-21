#include "ObjectAllocator.h"
#include <cstring>

// NOLINTBEGIN(*-exception-baseclass)

ObjectAllocator::ObjectAllocator(const usize obj_size, const OAConfig& config):
  config{config},
  object_size{obj_size},
  page_size{0} {

  // allocate first page

  // TODO: Calculate InterAlignment & LeftAlignment

  header_stride = config.HBlockInfo_.size_ + 2 * config.PadBytes_ + object_size
                  + config.InterAlignSize_;

  block_size = config.HBlockInfo_.size_ // header size
               + config.PadBytes_ // left intern pad size
               + object_size // acc object size
               + config.PadBytes_; // right intern pad size

  page_size = //
    sizeof(GenericObject) // next page ptr
    + config.LeftAlignSize_ // ptr alignment
    + block_size * config.ObjectsPerPage_ // per block size
    // intern align size - the first ones
    + config.InterAlignSize_ * (config.ObjectsPerPage_ - 1);

  statistics.PageSize_ = page_size;
  statistics.ObjectSize_ = object_size;

  if (not config.UseCPPMemManager_) {
    page_list = allocate_page(nullptr);
  }
}

ObjectAllocator::~ObjectAllocator() noexcept {
  GenericObject* page = &as_list(page_list);

  while (page) {
    const u8* const to_delete = as_bytes(page);
    page = page->Next;
    delete[] to_delete;
  }
}

void* ObjectAllocator::Allocate(const char* label) {
  // TODO: check if there is acc memory available
  // TODO: trigger in-use flag

  // if no more free blocks try to allocate a new page
  if (not config.UseCPPMemManager_ and free_list == nullptr) {
    page_list = allocate_page(&as_list(page_list));
  }

  u8* header{nullptr};

  if (not config.UseCPPMemManager_) {
    header = free_list;
    free_list = as_bytes(as_list(free_list).Next);
  } else {
    try {
      header = new u8[block_size];
    } catch (const std::bad_alloc&) {
      throw OAException(OAException::E_NO_MEMORY, "'new[]' threw bad alloc.");
    }
  }

  // book keeping
  statistics.ObjectsInUse_++;
  statistics.Allocations_++;
  statistics.FreeObjects_--;
  statistics.MostObjects_ = statistics.ObjectsInUse_ > statistics.MostObjects_
                              ? statistics.ObjectsInUse_
                              : statistics.MostObjects_;

  setup_alllocated_header(header, label);

  if (config.DebugOn_) {
    u8* pos = header + config.HBlockInfo_.size_;
    memset(pos, PAD_PATTERN, config.PadBytes_);
    pos += config.PadBytes_;

    memset(pos, ALLOCATED_PATTERN, block_size);
    pos += block_size;

    memset(pos, PAD_PATTERN, config.PadBytes_);
  }

  return header + config.HBlockInfo_.size_ + config.PadBytes_;
}

void ObjectAllocator::Free(void* const block_void_ptr) {
  if (not block_void_ptr) {
    return;
  }

  u8* const block = static_cast<u8*>(block_void_ptr) - config.HBlockInfo_.size_ - config.PadBytes_;

  if (not config.UseCPPMemManager_) {
    validate_boundary(block);
  }

  if (config.DebugOn_) {
    // check for double free
    if (not config.UseCPPMemManager_ and is_in_free_list(as_bytes(block))) {
      throw OAException(
        OAException::E_MULTIPLE_FREE,
        "Block has already been freed"
      );
    }
  }

  setup_freed_header(block);

  statistics.ObjectsInUse_--;
  statistics.Deallocations_++;
  statistics.FreeObjects_++;

  if (config.UseCPPMemManager_) {
    delete[] block;
    return;
  }

  if (config.DebugOn_) {
    memset(block_void_ptr, FREED_PATTERN, object_size);
  }

  as_list(block).Next = &as_list(free_list);
  free_list = as_bytes(block);
}

auto ObjectAllocator::validate_boundary(const u8* block) const -> void {
  if (not config.DebugOn_) {
    return;
  }

  for (const GenericObject* page = &as_list(page_list); page; page = page->Next) {
    const u8* const page_min = as_bytes(page);
    const u8* const page_max = page_min + page_size;

    if (block < page_min or block >= page_max) {
      continue;
    }

    const u8* first_block = as_bytes(page) + config.LeftAlignSize_ + sizeof(GenericObject);

    if ((block - first_block) % header_stride != 0) {
      throw OAException(OAException::E_BAD_BOUNDARY, "Invalid Boundry");
    }

    return;
  }

  throw OAException(OAException::E_BAD_BOUNDARY, "Invalid Boundry, not on any pages");
}

u32 ObjectAllocator::DumpMemoryInUse(const DUMPCALLBACK callback) const {
  usize in_use{0};

  for (const GenericObject* page = &as_list(page_list); page; page = page->Next) {
    const u8* first_block = as_bytes(page) + +config.LeftAlignSize_;

    for (usize i = 0; i < config.ObjectsPerPage_; i++) {
      const u8* block = first_block + i * header_stride;
      if (is_in_free_list(block)) {
        in_use++;
        callback(block, block_size);
      }
    }
  }

  return 0;
}

u32 ObjectAllocator::ValidatePages(const VALIDATECALLBACK callback) const {
  if (not config.DebugOn_ or config.PadBytes_ == 0) {
    return 0;
  }

  const GenericObject* page = &as_list(page_list);
  usize invalid_count{0};

  while (page) {
    if (not validate_page(as_bytes(page))) {
      callback(page, block_size);
      invalid_count++;
    }
    page = page->Next;
  }

  return 0;
}

u32 ObjectAllocator::FreeEmptyPages() {
  return 0;
}

void ObjectAllocator::SetDebugState(const bool State) {
  config.DebugOn_ = State;
}

const void* ObjectAllocator::GetFreeList() const { return free_list; }

const void* ObjectAllocator::GetPageList() const { return page_list; }

const OAConfig& ObjectAllocator::GetConfig() const { return config; }

const OAStats& ObjectAllocator::GetStats() const { return statistics; }

bool ObjectAllocator::ImplementedExtraCredit() { return false; }

GenericObject& ObjectAllocator::as_list(
  u8* const bytes // NOLINT(*-non-const-parameter)
) {
  return *reinterpret_cast<GenericObject*>(bytes);
}

const GenericObject& ObjectAllocator::as_list(const u8* const bytes) {
  return *reinterpret_cast<const GenericObject*>(bytes);
}

u8* ObjectAllocator::allocate_page(GenericObject* const next) {
  if (config.MaxPages_ != 0 and allocated_pages >= config.MaxPages_) {
    throw OAException(OAException::E_NO_PAGES, "Out of pages");
  }

  allocated_pages++;

  u8* memory;

  try {
    memory = new u8[page_size];
  } catch (const std::bad_alloc& err) {
    throw OAException(OAException::E_NO_MEMORY, err.what());
  }

  page_list = memory;

  // signing
  if (config.DebugOn_) {
    memset(page_list, UNALLOCATED_PATTERN, page_size);

    memset(
      page_list + sizeof(GenericObject),
      ALIGN_PATTERN,
      config.LeftAlignSize_
    );
  }

  as_list(page_list).Next = next;

  // up the stat, was added to the list
  statistics.PagesInUse_++;

  u8* const first_obj =
    page_list + sizeof(GenericObject) // pointer
    + config.LeftAlignSize_; // alignment bytes before first header

  for (usize i = 1; i < config.ObjectsPerPage_; i++) {
    u8* const header_block = first_obj + header_stride * i;
    u8* const prev_block = first_obj + header_stride * (i - 1);
    as_list(header_block).Next = &as_list(prev_block);

    if (config.DebugOn_) {
      memset(
        prev_block + config.PadBytes_ * 2 + object_size,
        ALIGN_PATTERN,
        config.InterAlignSize_
      );
    }
  }

  // initialise header blocks
  init_header_blocks_for_page(first_obj);

  GenericObject& old_free = as_list(free_list);

  free_list = first_obj + header_stride * (config.ObjectsPerPage_ - 1);
  as_list(first_obj).Next = &old_free;

  statistics.FreeObjects_ += config.ObjectsPerPage_;
  return memory;
}

void ObjectAllocator::init_header_blocks_for_page(
  u8* const first_header
) const {
  if (config.HBlockInfo_.size_ == 0) {
    return;
  }

  if (config.HBlockInfo_.type_ == OAConfig::hbExternal) {
    for (usize i = 0; i < config.ObjectsPerPage_; i++) {
      u8* header = first_header + i * header_stride;
      *reinterpret_cast<void**>(header) = nullptr;
    }

    return;
  }

  // setup first header as default
  {
    u8* header = first_header;
    if (config.HBlockInfo_.type_ == OAConfig::hbExtended) {
      // part of the header for user defined bits
      memset(first_header, 0, config.HBlockInfo_.additional_);
      header += config.HBlockInfo_.additional_;

      // part of the header that is the allocation tracker
      *reinterpret_cast<u16*>(header) = 0;
      header += sizeof(u16);
    }

    // part of the header (4bytes) is the allocation #
    *reinterpret_cast<u32*>(header) = 0;
    header += sizeof(u32);

    // part of the header (1byte) is the in use flag
    *header = 0;
  }

  // memcpy the default header to each one to skip multiple branches
  for (usize i = 1; i < config.ObjectsPerPage_; i++) {
    memcpy(
      first_header + i * header_stride,
      first_header,
      config.HBlockInfo_.size_
    );
  }
}

bool ObjectAllocator::is_in_free_list(const u8* const header) const {
  switch (config.HBlockInfo_.type_) {
    case OAConfig::hbBasic: return header[ALLOC_ID_BYTES] & 0x1;

    case OAConfig::hbExtended: {
      const u8 flag = header[USE_COUNTER_BYTES + ALLOC_ID_BYTES + config.HBlockInfo_.additional_];
      return flag & 0x1;
    }

    case OAConfig::hbNone:
    case OAConfig::hbExternal:
    default: break;
  }

  for (const GenericObject* free = &as_list(free_list); free; free = free->Next) {
    if (as_bytes(free) == header) {
      return true;
    }
  }

  return false;
}

bool ObjectAllocator::validate_page(const u8* const page) const {
  // skip over to the first block

  const u8* first_block = page + sizeof(GenericObject) + config.LeftAlignSize_;

  for (usize i = 0; i < config.ObjectsPerPage_ - 1; i++) {
    const u8* const block = first_block + header_stride * i;

    // left pad byte signature check
    if (not is_signed_as(block + config.HBlockInfo_.size_, config.PadBytes_, PAD_PATTERN)) {
      return false;
    }

    // right pad byte signature check
    if (not is_signed_as(block + config.HBlockInfo_.size_ + object_size, config.PadBytes_, PAD_PATTERN)) {
      return false;
    }
  };

  return true;
}

void ObjectAllocator::setup_alllocated_header(u8* const header, const char* label) const {
  if (config.HBlockInfo_.size_ == 0) {
    return;
  }

  switch (config.HBlockInfo_.type_) {
    case OAConfig::hbBasic: {
      // set allocation number ID
      *reinterpret_cast<u32*>(header) = statistics.Allocations_;

      // set in use flag to on
      header[ALLOC_ID_BYTES] |= 0x1;
      return;
    }
    case OAConfig::hbExtended: {
      u8* pos = header;

      // set additional user defined bytes to 0
      memset(pos, 0, config.HBlockInfo_.additional_);
      pos += config.HBlockInfo_.additional_;

      // set allocation number ID
      (*reinterpret_cast<u16*>(pos))++;
      pos += USE_COUNTER_BYTES;

      *reinterpret_cast<u32*>(pos) = statistics.Allocations_;
      pos += ALLOC_ID_BYTES;

      // set in use flag to on
      *pos |= 0x1;
      return;
    }
    case OAConfig::hbExternal: {

      char* label_copy = nullptr;

      if (label != nullptr) {
        label_copy = new char[strlen(label) + 1]{};
        // strcpy(label_copy, label);
      }

      *reinterpret_cast<MemBlockInfo**>(header) = new MemBlockInfo{
        true,
        label_copy,
        statistics.Allocations_
      };

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
    case OAConfig::hbBasic: {
      // set in use flag to off
      header[ALLOC_ID_BYTES] &= ~0x1;
      return;
    }
    case OAConfig::hbExtended: {
      u8* pos = header;

      // skip over user bytes
      pos += config.HBlockInfo_.additional_;

      // skip over user counter
      pos += USE_COUNTER_BYTES;

      // skip over allocated ID
      pos += ALLOC_ID_BYTES;

      // set in use flag to off
      *pos &= ~0x1;
      return;
    }
    case OAConfig::hbExternal: {
      MemBlockInfo** info = reinterpret_cast<MemBlockInfo**>(header);

      // delete the label
      delete[] (*info)->label;

      // delete the external data
      delete *info;

      *info = nullptr;

      return;
    }
    case OAConfig::hbNone:
    default: break;
  }
}

bool ObjectAllocator::is_signed_as(
  const u8* ptr,
  const usize extents,
  const u8 pattern
) {

  usize valid_bytes{0};

  for (usize i = 0; i < extents; i++) {
    valid_bytes += ptr[i] == pattern;
  }

  return valid_bytes == extents;
}

// NOLINTEND(*-exception-baseclass)
