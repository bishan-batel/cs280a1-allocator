#include "ObjectAllocator.h"
#include <cstring>

// TODO: remove before submission
#include <stdexcept>

ObjectAllocator::ObjectAllocator(const usize obj_size, const OAConfig &config) :
    config{config}, object_size{obj_size}, page_size{0} {

  // allocate first page

  page_size = object_size * config.ObjectsPerPage_ + sizeof(GenericObject);

  page_list = allocate_raw_page(nullptr, &free_list);
}

ObjectAllocator::~ObjectAllocator() noexcept { /* TODO: free memory */ }

void *ObjectAllocator::Allocate(const char *label) {
  // TODO: check if there is acc memory available

  u8 *obj = free_list;

  free_list = as_bytes(as_list(free_list).Next);

  return obj;
}

void ObjectAllocator::Free(void *Object) {}

unsigned ObjectAllocator::DumpMemoryInUse(DUMPCALLBACK) const { throw std::logic_error("Unimplemented"); }

unsigned ObjectAllocator::ValidatePages(VALIDATECALLBACK) const { throw std::logic_error("Unimplemented"); }

unsigned ObjectAllocator::FreeEmptyPages() { throw std::logic_error("Unimplemented"); }

void ObjectAllocator::SetDebugState(bool State) { throw std::logic_error("Unimplemented"); }

const void *ObjectAllocator::GetFreeList() const { return free_list; }

const void *ObjectAllocator::GetPageList() const { return page_list; }

const OAConfig &ObjectAllocator::GetConfig() const { return config; }

const OAStats &ObjectAllocator::GetStats() const { return statistics; }

bool ObjectAllocator::ImplementedExtraCredit() { return false; }

GenericObject &ObjectAllocator::as_list(u8 *bytes) { return *reinterpret_cast<GenericObject *>(bytes); }

const GenericObject &ObjectAllocator::as_list(const u8 *bytes) {
  return *reinterpret_cast<const GenericObject *>(bytes);
}

const u8 *ObjectAllocator::as_bytes(const GenericObject *bytes) { return reinterpret_cast<const u8 *>(bytes); }

u8 *ObjectAllocator::as_bytes(GenericObject *bytes) { return reinterpret_cast<u8 *>(bytes); }

const u8 *ObjectAllocator::as_bytes(const GenericObject &bytes) { return as_bytes(&bytes); }

u8 *ObjectAllocator::as_bytes(GenericObject &bytes) { return as_bytes(&bytes); }

u8 *ObjectAllocator::allocate_raw_page(GenericObject *next, u8 **const free_list) const {
  u8 *memory = new u8[page_size];

  if (config.DebugOn_) {
    memset(memory + sizeof(GenericObject), UNALLOCATED_PATTERN, page_size - sizeof(GenericObject));
  }

  as_list(memory).Next = next;

  const usize stride = object_size;
  u8 *const object_pos = page_list + sizeof(GenericObject);

  u8 *const first_obj = page_list + sizeof(GenericObject);

  for (usize i = 1; i < config.ObjectsPerPage_; i++) {
    as_list(first_obj + stride * i).Next = &as_list(first_obj + stride * (i - 1));
  }

  if (free_list != nullptr) {
    as_list(*free_list).Next = &as_list(first_obj + stride * config.ObjectsPerPage_);
  }

  as_list(first_obj).Next = nullptr;
  return memory;
}
