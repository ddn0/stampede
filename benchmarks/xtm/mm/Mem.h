/** heap building blocks -*- C++ -*-
 * @file
 * @section License
 *
 * Galois, a framework to exploit amorphous data-parallelism in irregular
 * programs.
 *
 * Copyright (C) 2013, The University of Texas at Austin. All rights reserved.
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
 * Strongly inspired by heap layers:
 *  http://www.heaplayers.org/
 * FSB is modified from:
 *  http://warp.povusers.org/FSBAllocator/
 *
 * @author Andrew Lenharth <andrewl@lenharth.org>
 */
#ifndef GALOIS_RUNTIME_MEM_H
#define GALOIS_RUNTIME_MEM_H

#include <cstdlib>
#include <cstddef>

namespace Galois {
namespace Runtime {
//! Memory management functionality.
namespace MM {

const size_t smallPageSize = 4*1024;
const size_t pageSize = 2*1024*1024;
void* pageAlloc();
void  pageFree(void*);
//! Preallocate numpages large pages for each thread
void pagePreAlloc(int numpages);
//! Forces the given block to be paged into physical memory
void pageIn(void *buf, size_t len);

//! This implements a bump pointer though chunks of memory
template<typename SourceHeap>
class SimpleBumpPtr : public SourceHeap {
  struct Block {
    union {
      Block* next;
      double dummy; // for alignment
    };
  };

  Block* head;
  int offset;

  void refill() {
    void* P = SourceHeap::allocate(SourceHeap::AllocSize);
    Block* BP = (Block*)P;
    BP->next = head;
    head = BP;
    offset = sizeof(Block);
  }

public:
  struct Mark { Block* head; int offset; };

  enum { AllocSize = 0 };

  SimpleBumpPtr(): SourceHeap(), head(0), offset(0) { }
  
  ~SimpleBumpPtr() { clear(); }

  Mark getMark() {
    Mark m = { head, offset };
    return m;
  }

  void clearToMark(const Mark& mark) {
    while (head != mark.head) {
      Block* B = head;
      head = B->next;
      SourceHeap::deallocate(B);
    }
    offset = mark.offset;
  }

  void clear() {
    while (head) {
      Block* B = head;
      head = B->next;
      SourceHeap::deallocate(B);
    }
  }

  inline void* allocate(size_t size) {
    //increase to alignment
    size = (size + sizeof(double) - 1) & ~(sizeof(double) - 1);
    //Check current block
    if (!head || offset + size > SourceHeap::AllocSize)
      refill();
    //Make sure this will fit
    if (offset + size > SourceHeap::AllocSize) {
      return 0;
    }
    char* retval = (char*)head;
    retval += offset;
    offset += size;
    return retval;
  }

  inline void deallocate(void* ptr) {}
};

//! This is the base source of memory for all allocators.
//! It maintains a freelist of hunks acquired from the system
class SystemBaseAlloc {
public:
  enum { AllocSize = pageSize };

  SystemBaseAlloc();
  ~SystemBaseAlloc();

  inline void* allocate(size_t size) {
    return pageAlloc();
  }

  inline void deallocate(void* ptr) {
    pageFree(ptr);
  }
};

//! Maintain a freelist
template<class SourceHeap>
class FreeListHeap : public SourceHeap {
  struct FreeNode {
    FreeNode* next;
  };
  FreeNode* head;

public:
  enum { AllocSize = SourceHeap::AllocSize };

  void clear() {
    while (head) {
      FreeNode* N = head;
      head = N->next;
      SourceHeap::deallocate(N);
    }
  }

  FreeListHeap() : head(0) {}
  ~FreeListHeap() {
    clear();
  }

  inline void* allocate(size_t size) {
    if (head) {
      void* ptr = head;
      head = head->next;
      return ptr;
    }
    return SourceHeap::allocate(size);
  }

  inline void deallocate(void* ptr) {
    if (!ptr) return;
    FreeNode* NH = (FreeNode*)ptr;
    NH->next = head;
    head = NH;
  }

};


}
}
} // end namespace Galois

#endif
