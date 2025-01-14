#include "ObjectAllocator.h"
#include <cstring>

// TODO: remove before submission
#include <stdexcept>

ObjectAllocator::ObjectAllocator(const usize obj_size, const OAConfig& config):
  config{config},
  object_size{obj_size},
  page_size{0} {

  // allocate first page

  // TODO: Calculate InterAlignment & LeftAlignment

  block_size = config.HBlockInfo_.size_ // header size
               + object_size // acc object size
               + config.PadBytes_ // left intern pad size
               + config.InterAlignSize_ // internal alignment
               + config.PadBytes_; // right intern pad size

  page_size = sizeof(GenericObject) // next page ptr
              + config.LeftAlignSize_ // ptr alignment
              + config.PadBytes_ // ptr padding
              + block_size * config.ObjectsPerPage_ // per block size
              + config.PadBytes_; // Trailing pad byte

  if (config.ObjectsPerPage_ > 0) {
    // the last block doesnt have the extra pad & align
    page_size -= config.PadBytes_ - config.InterAlignSize_;
  }

  page_list = allocate_raw_page(nullptr, free_list);

  statistics.PageSize_ = page_size;
  statistics.ObjectSize_ = object_size;
}

ObjectAllocator::~ObjectAllocator() noexcept {
  /* TODO: free memory */
}

void* ObjectAllocator::Allocate(const char*) {
  // TODO: check if there is acc memory available

  // TODO: trigger in-use flag

  if (free_list == nullptr) {
    page_list = allocate_raw_page(&as_list(page_list), free_list);
  }

  u8* obj = free_list;

  free_list = as_bytes(as_list(free_list).Next);

  // TODO: debug patterns
  if (config.DebugOn_) {
    // memset(free_list, ALLOCATED_PATTERN, object_size);
    // memset(free_list + object_size, PAD_PATTERN, config.PadBytes_);
  }

  statistics.MostObjects_++;
  return obj;
}

void ObjectAllocator::Free(void* block_void_ptr) {
  // TODO: check for boundry errors ()
  /* OAException::E_BAD_BOUNDARY; */

  u8* block = reinterpret_cast<u8*>(block_void_ptr);

  as_list(block).Next = &as_list(free_list);
  free_list = as_bytes(block);
  statistics.MostObjects_--;
}

unsigned ObjectAllocator::DumpMemoryInUse(DUMPCALLBACK) const {
  throw std::logic_error("Unimplemented");
}

unsigned ObjectAllocator::ValidatePages(VALIDATECALLBACK) const {
  throw std::logic_error("Unimplemented");
}

unsigned ObjectAllocator::FreeEmptyPages() {
  throw std::logic_error("Unimplemented");
}

void ObjectAllocator::SetDebugState(bool State) { config.DebugOn_ = State; }

const void* ObjectAllocator::GetFreeList() const { return free_list; }

const void* ObjectAllocator::GetPageList() const { return page_list; }

const OAConfig& ObjectAllocator::GetConfig() const { return config; }

const OAStats& ObjectAllocator::GetStats() const { return statistics; }

bool ObjectAllocator::ImplementedExtraCredit() { return false; }

GenericObject& ObjectAllocator::as_list(u8* bytes) {
  return *reinterpret_cast<GenericObject*>(bytes);
}

const GenericObject& ObjectAllocator::as_list(const u8* bytes) {
  return *reinterpret_cast<const GenericObject*>(bytes);
}

u8* ObjectAllocator::allocate_raw_page(
  GenericObject* next,
  u8*& free_list_ref
) {
  allocated_pages++;

  if (allocated_pages > config.MaxPages_) {
    throw OAException(OAException::E_NO_PAGES, "Out of pages");
  }

  u8* memory = nullptr;

  try {
    memory = new u8[page_size];
  } catch (const std::bad_alloc& err) {
    throw OAException(OAException::E_NO_MEMORY, err.what());
  }

  if (config.DebugOn_) {
    statistics.Allocations_++;
  }

  page_list = memory;

  if (config.DebugOn_) {
    memset(page_list, UNALLOCATED_PATTERN, page_size);
  }

  as_list(page_list).Next = next;

  const usize stride = block_size;

  u8* const first_obj = page_list + sizeof(GenericObject);

  for (usize i = 1; i < config.ObjectsPerPage_; i++) {
    as_list(first_obj + stride * i).Next =
      &as_list(first_obj + stride * (i - 1));
  }

  GenericObject& old_free = as_list(free_list_ref);
  free_list_ref = first_obj + stride * (config.ObjectsPerPage_ - 1);
  as_list(free_list).Next = &old_free;

  as_list(first_obj).Next = nullptr;
  statistics.FreeObjects_ += config.ObjectsPerPage_;
  return memory;
}
