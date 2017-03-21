/** Simple Spin Lock -*- C++ -*-
 * @file
 * @section License
 *
 * Galois, a framework to exploit amorphous data-parallelism in
 * irregular programs.
 *
 * Copyright (C) 2011, The University of Texas at Austin. All rights
 * reserved.  UNIVERSITY EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES
 * CONCERNING THIS SOFTWARE AND DOCUMENTATION, INCLUDING ANY
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR ANY PARTICULAR PURPOSE,
 * NON-INFRINGEMENT AND WARRANTIES OF PERFORMANCE, AND ANY WARRANTY
 * THAT MIGHT OTHERWISE ARISE FROM COURSE OF DEALING OR USAGE OF
 * TRADE.  NO WARRANTY IS EITHER EXPRESS OR IMPLIED WITH RESPECT TO
 * THE USE OF THE SOFTWARE OR DOCUMENTATION. Under no circumstances
 * shall University be liable for incidental, special, indirect,
 * direct or consequential damages or loss of profits, interruption of
 * business, or related expenses which may arise from use of Software
 * or Documentation, including but not limited to those resulting from
 * defects in Software and/or Documentation, or loss or inaccuracy of
 * data of any kind.  
 *
 * @section Description
 *
 * This contains the basic spinlock used in Galois.  We use a
 * test-and-test-and-set approach, with pause instructions on x86 and
 * compiler barriers on unlock.
 *
 * @author Andrew Lenharth <andrew@lenharth.org>
 */

#ifndef GALOIS_RUNTIME_LL_SIMPLE_LOCK_H
#define GALOIS_RUNTIME_LL_SIMPLE_LOCK_H

#include <cassert>

#include "CompilerSpecific.h"

namespace Galois {
namespace Runtime {
namespace LL {

class SimpleLock {
  volatile mutable int _lock; //Allow locking a const

  void slowLock() const {
    int oldval = 0;
    do {
      while (true) {
        __sync_synchronize();
        oldval = _lock;
        __sync_synchronize();
        if (oldval == 0)
          break;
        asmPause();
      }
    } while (!__sync_bool_compare_and_swap(&_lock, 0, 1));
  }

public:
  SimpleLock() : _lock(0) {  }

  inline void lock() const {
    if (_lock)
      goto slow_path;
    if (!__sync_bool_compare_and_swap(&_lock, 0, 1))
      goto slow_path;
    return;
slow_path:
    slowLock();
  }

  inline void unlock() const {
    assert(_lock);
    __sync_synchronize();
    _lock = 0;
    __sync_synchronize();
  }

  inline bool try_lock() const {
    int l = _lock;
    if (_lock)
      return false;
    return __sync_bool_compare_and_swap(&_lock, 0, 1);
  }

  inline bool is_locked() const {
    __sync_synchronize();
    bool r = _lock & 1;
    __sync_synchronize();
    return r;
  }
};

class DummyLock {
public:
  inline void lock() const {}
  inline void unlock() const {}
  inline bool try_lock() const { return true; }
  inline bool is_locked() const { return false; }
};

}
}
} // end namespace Galois

#endif
