/** Page Allocator Implementation -*- C++ -*-
 * @file
 * @section License
 *
 * Galois, a framework to exploit amorphous data-parallelism in irregular
 * programs.
 *
 * Copyright (C) 2011, The University of Texas at Austin. All rights reserved.
 * UNIVERSITY EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES CONCERNING THIS
 * SOFTWARE AND DOCUMENTATION, INCLUDING ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR ANY PARTICULAR PURPOSE, NON-INFRINGEMENT AND WARRANTIES OF
 * PERFORMANCE, AND ANY WARRANTY THAT MIGHT OTHERWISE ARISE FROM COURSE OF
 * DEALING OR USAGE OF TRADE.  NO WARRANTY IS EITHER EXPRESS OR IMPLIED WITH
 * RESPECT TO THE USE OF THE SOFTWARE OR DOCUMENTATION. Under no circumstances
 * shall University be liable for incidental, special, indirect, direct or
 * consequential damages or loss of profits, interruption of business, or
 * related expenses which may arise from use of Software or Documentation,
 * including but not limited to those resulting from defects in Software and/or
 * Documentation, or loss or inaccuracy of data of any kind.
 *
 * @section Description
 *
 * @author Andrew Lenharth <andrewl@lenharth.org>
 */

#include "mm/Mem.h"
#include "ll/TID.h"
#include "ll/SimpleLock.h"
#include "ll/PtrLock.h"
#include "ll/CacheLineStorage.h"
#include "ll/StaticInstance.h"

#include <cstdlib>
#include <map>
#include <vector>
#include <sys/mman.h>

// mmap flags
static const int _PROT = PROT_READ | PROT_WRITE;
static const int _MAP_BASE = MAP_ANONYMOUS | MAP_PRIVATE;
#ifdef MAP_POPULATE
static const int _MAP_POP  = MAP_POPULATE | _MAP_BASE;
#endif
#ifdef MAP_HUGETLB
static const int _MAP_HUGE_POP = MAP_HUGETLB | _MAP_POP;
static const int _MAP_HUGE = MAP_HUGETLB;
#endif

namespace {
struct FreeNode {
  FreeNode* next;
};
 
typedef Galois::Runtime::LL::PtrLock<FreeNode, true> HeadPtr;
typedef Galois::Runtime::LL::CacheLineStorage<HeadPtr> HeadPtrStorage;

// Tracks pages allocated
struct PAState {
  std::vector<int> counts;
  std::map<void*, HeadPtr*> ownerMap;
  PAState() { 
    // FIXME(ddn): hardcoded max threads
    counts.resize(128, 0);
  }
};

static Galois::Runtime::LL::StaticInstance<PAState> PA;

#ifdef __linux__
  static Galois::Runtime::LL::SimpleLock allocLock;
#else
  static Galois::Runtime::LL::DummyLock allocLock;
#endif
static Galois::Runtime::LL::SimpleLock dataLock;
static __thread HeadPtr* head = 0;

void* allocFromOS() {
  //linux mmap can introduce unbounded sleep!
  allocLock.lock();

  void* ptr = 0;
#ifdef MAP_HUGETLB
  //First try huge
  ptr = mmap(0, Galois::Runtime::MM::pageSize, _PROT, _MAP_HUGE_POP, -1, 0);
#endif

  //FIXME: improve failure case to ensure pageSize alignment
#ifdef MAP_POPULATE
  //Then try populate
  if (!ptr || ptr == MAP_FAILED)
    ptr = mmap(0, Galois::Runtime::MM::pageSize, _PROT, _MAP_POP, -1, 0);
#endif
  //Then try normal
  if (!ptr || ptr == MAP_FAILED) {
    ptr = mmap(0, Galois::Runtime::MM::pageSize, _PROT, _MAP_BASE, -1, 0);
  }
  
  allocLock.unlock();
  if (!ptr || ptr == MAP_FAILED) {
    abort();
  }

  //protect the tracking structures
  dataLock.lock();
  HeadPtr*& h = head;
  if (!h) { //first allocation
    h = &((new HeadPtrStorage())->data);
  }
  PAState& p = *PA.get();
  p.ownerMap[ptr] = h;
  p.counts[Galois::Runtime::LL::getTID()] += 1;
  dataLock.unlock();
  return ptr;
}

} // end anon namespace

void Galois::Runtime::MM::pageIn(void* buf, size_t len) {
  volatile char* ptr = reinterpret_cast<volatile char*>(buf);
  for (size_t i = 0; i < len; i += smallPageSize)
    ptr[i];
}

void* Galois::Runtime::MM::pageAlloc() {
  HeadPtr* phead = head;
  if (phead) {
    phead->lock();
    FreeNode* h = phead->getValue();
    if (h) {
      phead->unlock_and_set(h->next);
      return h;
    }
    phead->unlock();
  }
  return allocFromOS();
}

void Galois::Runtime::MM::pageFree(void* m) {
  dataLock.lock();
  HeadPtr* phead = PA.get()->ownerMap[m];
  dataLock.unlock();
  assert(phead);
  phead->lock();
  FreeNode* nh = reinterpret_cast<FreeNode*>(m);
  nh->next = phead->getValue();
  phead->unlock_and_set(nh);
}

void Galois::Runtime::MM::pagePreAlloc(int numPages) {
  while (numPages--)
    Galois::Runtime::MM::pageFree(allocFromOS());
}
