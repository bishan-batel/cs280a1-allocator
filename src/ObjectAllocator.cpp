#include "ObjectAllocator.h"
#include <stdexcept>
ObjectAllocator::ObjectAllocator(const usize obj_size, const OAConfig &config) : object_size{obj_size}, config{config} {

  // allocate first page

  const usize page_size = obj_size * config.ObjectsPerPage_;

  page_list = new u8[](10UL);
}

ObjectAllocator::~ObjectAllocator() noexcept {}

void *ObjectAllocator::Allocate(const char *label) {}

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
