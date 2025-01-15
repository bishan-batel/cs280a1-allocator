#include "ObjectAllocator.h"
#include <cstring>

// TODO: remove before submission
#include <memory>
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

  page_list = allocate_raw_page(nullptr);

  statistics.PageSize_ = page_size;
  statistics.ObjectSize_ = object_size;
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

  u8* obj = free_list;

  free_list = as_bytes(as_list(free_list).Next);

  // TODO: debug patterns
  if (config.DebugOn_) {
    memset(obj, ALLOCATED_PATTERN, object_size);
    memset(obj + object_size, PAD_PATTERN, config.PadBytes_);
  }

  statistics.ObjectsInUse_++;

  // m = max(m, o)
  statistics.MostObjects_ = statistics.ObjectsInUse_ > statistics.MostObjects_
                              ? statistics.ObjectsInUse_
                              : statistics.MostObjects_;
  return obj;
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
    statistics.ObjectsInUse_--;
  }

  u8* block = static_cast<u8*>(block_void_ptr);

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
  GenericObject* next
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

  GenericObject& old_free = as_list(free_list);

  free_list = first_obj + stride * (config.ObjectsPerPage_ - 1);

  as_list(free_list).Next = &old_free;

  statistics.FreeObjects_ += config.ObjectsPerPage_;
  return memory;
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
