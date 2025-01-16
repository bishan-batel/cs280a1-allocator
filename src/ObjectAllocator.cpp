#include "ObjectAllocator.h"
#include <cstring>

// TODO: remove before submission
#include <memory>
#include <stdexcept>

ObjectAllocator::ObjectAllocator(const usize obj_size, const OAConfig& config):
    config{config}, object_size{obj_size}, page_size{0} {

  // allocate first page

  // TODO: Calculate InterAlignment & LeftAlignment

  header_stride = config.HBlockInfo_.size_ + config.PadBytes_ + object_size
                + config.InterAlignSize_;

  block_size = config.HBlockInfo_.size_   // header size
             + config.PadBytes_           // left intern pad size
             + object_size                // acc object size
             + config.PadBytes_;          // right intern pad size

  page_size =                             //
    sizeof(GenericObject)                 // next page ptr
    + config.LeftAlignSize_               // ptr alignment
    + block_size * config.ObjectsPerPage_ // per block size
    // intern align size - the first ones
    + config.InterAlignSize_ * (config.ObjectsPerPage_ - 1);

  statistics.PageSize_ = page_size;
  statistics.ObjectSize_ = object_size;

  page_list = allocate_raw_page(nullptr);
}

ObjectAllocator::~ObjectAllocator() noexcept {
  GenericObject* page = &as_list(page_list);

  while (page) {
    const u8* const to_delete = as_bytes(page);
    page = page->Next;
    delete[] to_delete;
  }
}

void* ObjectAllocator::Allocate(const char*) {
  // TODO: check if there is acc memory available

  // TODO: trigger in-use flag

  if (free_list == nullptr) {
    page_list = allocate_raw_page(&as_list(page_list));
  }

  u8* header = free_list;

  free_list = as_bytes(as_list(free_list).Next);

  // book keeping
  statistics.ObjectsInUse_++;
  statistics.Allocations_++;
  statistics.MostObjects_ = statistics.ObjectsInUse_ > statistics.MostObjects_
                            ? statistics.ObjectsInUse_
                            : statistics.MostObjects_;

  u8* block = header + config.HBlockInfo_.size_ + config.PadBytes_;

  if (config.HBlockInfo_.size_ == 0) {
    return block;
  }

  if (config.HBlockInfo_.type_ == OAConfig::hbExternal) {
    return block;
  }

  return block;
}

void ObjectAllocator::Free(void* const block_void_ptr) {
  if (not block_void_ptr) {
    return;
  }
  // TODO: check for boundry errors ()
  /* OAException::E_BAD_BOUNDARY; */

  if (config.DebugOn_) {
    // check for double free
    if (is_in_free_list(block_void_ptr)) {
      throw OAException(
        OAException::E_MULTIPLE_FREE,
        "Block has already been freed"
      );
    }
  }
  statistics.ObjectsInUse_--;
  statistics.Deallocations_++;

  u8* const block = static_cast<u8*>(block_void_ptr);

  as_list(block).Next = &as_list(free_list);
  free_list = as_bytes(block);
  statistics.ObjectsInUse_--;
}

u32 ObjectAllocator::DumpMemoryInUse(DUMPCALLBACK) const {
  throw std::logic_error("Unimplemented");
}

u32 ObjectAllocator::ValidatePages(VALIDATECALLBACK) const {
  throw std::logic_error("Unimplemented");
}

u32 ObjectAllocator::FreeEmptyPages() {
  throw std::logic_error("Unimplemented");
}

void ObjectAllocator::SetDebugState(const bool State) {
  config.DebugOn_ = State;
}

const void* ObjectAllocator::GetFreeList() const { return free_list; }

const void* ObjectAllocator::GetPageList() const { return page_list; }

const OAConfig& ObjectAllocator::GetConfig() const { return config; }

const OAStats& ObjectAllocator::GetStats() const { return statistics; }

bool ObjectAllocator::ImplementedExtraCredit() { return false; }

GenericObject& ObjectAllocator::as_list(u8* const bytes) {
  return *reinterpret_cast<GenericObject*>(bytes);
}

const GenericObject& ObjectAllocator::as_list(const u8* const bytes) {
  return *reinterpret_cast<const GenericObject*>(bytes);
}

u8* ObjectAllocator::allocate_raw_page(GenericObject* next) {
  allocated_pages++;

  if (allocated_pages > config.MaxPages_) {
    throw OAException(OAException::E_NO_PAGES, "Out of pages");
  }

  u8* memory = nullptr;

  try {
    memory = new u8[page_size];
  } catch (const std::bad_alloc& err) {
    throw OAException(OAException::E_NO_MEMORY, err.what());

  if (config.DebugOn_) {
    statistics.Allocations_++;
  }

  page_list = memory;

  if (config.DebugOn_) {
    memset(page_list, UNALLOCATED_PATTERN, page_size);

    memset(
      page_list + sizeof(GenericObject),
      ALIGN_PATTERN,
      config.LeftAlignSize_
    );
  }

  as_list(page_list).Next = next;

  u8* const first_obj =
    page_list + sizeof(GenericObject) // pointer
    + config.LeftAlignSize_;          // alignment bytes before first header

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

  as_list(free_list).Next = &old_free;

  statistics.FreeObjects_ += config.ObjectsPerPage_;
  return memory;
}

auto ObjectAllocator::init_header_blocks_for_page(u8* const first_header)
  -> void {
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
    *reinterpret_cast<u32*>(header) = statistics.Allocations_;
    header += sizeof(u32);

    // part of the header (1byte) is the in use flag
    *header = false;
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

bool ObjectAllocator::is_in_free_list(void* ptr) const {
  for (const GenericObject* free = &as_list(free_list); free;
       free = free->Next) {
    if (free == ptr) {
      return true;
    }
  }
  return false;
}
