#include "stm.h"
#include "mm/Mem.h"
#include "ll/PtrLock.h"
#include "ll/SimpleLock.h"
#include "ll/CompilerSpecific.h"
#include <list>
#include <string>

struct Stats {
  long commits;
  long aborts;
  long retries;

  Stats(): commits(0), aborts(0), retries(0) { }
};

template<typename T>
struct xtm_inline_lockable {
  T data;
  T oldData;
  Galois::Runtime::LL::PtrLock<stm_tx,true> owner;
  xtm_inline_lockable* next;
};

static const int lockTableShift = 20;

struct xtm_lockable;

struct LockTable {
  typedef Galois::Runtime::LL::PtrLock<stm_tx,true> Lock;
  Lock* locks;

  LockTable() {
    locks = new Lock[1<<lockTableShift];
  }

  ~LockTable() {
    delete [] locks;
  }

private:
  LockTable(const LockTable&);
  LockTable& operator=(const LockTable&);
};

struct xtm_lockable {
  void* addr;
  stm_word_t oldData;
  LockTable::Lock* lock;
  xtm_lockable* next;
};

namespace mm = Galois::Runtime::MM;

class stm_tx {
  typedef mm::SimpleBumpPtr<mm::FreeListHeap<mm::SystemBaseAlloc> > Heap;

  LockTable* lockTable;
  sigjmp_buf jmpBuf;
  Heap heap;
  Heap lockableHeap;
  Heap::Mark mark;
#ifdef XTM_USE_INLINE_LOCKABLE
  xtm_inline_lockable<stm_word_t>* lockedWords;
  xtm_inline_lockable<float>* lockedFloats;
#else
  xtm_lockable* lockedWords;
  xtm_lockable* lockedFloats;
#endif

public:
  Stats stats;

  stm_tx(LockTable* l): lockTable(l), lockedWords(0), lockedFloats(0)
  { }

private:
#ifdef XTM_USE_INLINE_LOCKABLE
  template<bool undo, typename T>
  void release(T** locks) {
    while (*locks) {
      T* cur = *locks;
      T* next = cur->next;
      cur->next = NULL;
      if (undo) 
        cur->data = cur->oldData;
      *locks = next;

      cur->owner.unlock_and_clear();
    }
  }

  template<typename Locks, typename T>
  void acquire(Locks** locks, T* lockable) {
    if (lockable->owner.try_lock()) {
      lockable->owner.setValue(this);
      lockable->oldData = lockable->data;
      lockable->next = *locks;
      *locks = lockable;
    } else if (lockable->owner.getValue() == this) {
      ;
    } else {
      this->abort(1);
    }
  }
#else
  template<bool undo>
  void release(xtm_lockable** locks) {
    bool undo32 = locks == &lockedFloats;
    while (*locks) {
      xtm_lockable* cur = *locks;
      xtm_lockable* next = cur->next;
      cur->next = NULL;
      if (undo) {
        if (undo32) {
          *((uint32_t*)cur->addr) = cur->oldData;
        } else {
          *((stm_word_t*)cur->addr) = cur->oldData;
        }
      }
      *locks = next;
      cur->lock->unlock_and_clear();
    }
  }

  template<typename T>
  void acquire(xtm_lockable** locks, T* lockable) {
    uintptr_t idx = (((uintptr_t) lockable) >> 3) & ((((uintptr_t)1)<<lockTableShift)-1);
    typename LockTable::Lock* lock = &lockTable->locks[idx];
    if (lock->try_lock()) {
      lock->setValue(this);
      xtm_lockable* newl = (xtm_lockable*) lockableHeap.allocate(sizeof(*newl));
      newl->addr = lockable;
      newl->oldData = *lockable;
      newl->lock = lock;
      newl->next = *locks;
      *locks = newl;
    } else if (lock->getValue() == this) {
      ;
    } else {
      this->abort(1);
    }
  }
#endif

public:
  float* acquire_float(void* addr) {
#ifdef XTM_USE_INLINE_LOCKABLE
    xtm_inline_lockable<float>* lockable = (xtm_inline_lockable<float>*) addr;
#else
    uint32_t* lockable = (uint32_t*) addr;
#endif
    acquire(&lockedFloats, lockable);
#ifdef XTM_USE_INLINE_LOCKABLE
    return &lockable->data;
#else
    return (float*) addr;
#endif
  }

  stm_word_t* acquire_word(void* addr) {
#ifdef XTM_USE_INLINE_LOCKABLE
    xtm_inline_lockable<stm_word_t>* lockable = (xtm_inline_lockable<stm_word_t>*) addr;
#else
    stm_word_t* lockable = (stm_word_t*) addr;
#endif
    acquire(&lockedWords, lockable);
#ifdef XTM_USE_INLINE_LOCKABLE
    return &lockable->data;
#else
    return (stm_word_t*) addr;
#endif
  }

  void* allocate(size_t size) {
    //return malloc(size);
    return heap.allocate(size);
  }

  void abort(int abort_reason) {
    stats.aborts += 1;
    // XXX(ddn): don't know why this doesn't work
    //heap.clearToMark(mark);
    release<true>(&lockedWords);
    release<true>(&lockedFloats);
#ifndef XTM_USE_INLINE_LOCKABLE
    lockableHeap.clear();
#endif
    siglongjmp(jmpBuf, abort_reason);
  }

  int commit() {
    stats.commits += 1;
    release<false>(&lockedWords);
    release<false>(&lockedFloats);
#ifndef XTM_USE_INLINE_LOCKABLE
    lockableHeap.clear();
#endif
    return 0;
  }

  sigjmp_buf* start() {
    mark = heap.getMark();
    return &jmpBuf;
  }
};

static LockTable* lockTable;
static Galois::Runtime::LL::SimpleLock allTxLock;
static std::list<stm_tx*> allTx;
static __thread stm_tx* myTx;

void stm_init(void) {
#ifndef XTM_USE_INLINE_LOCKABLE
  lockTable = new LockTable();
#endif
}

void stm_exit(void) { 
#ifndef XTM_USE_INLINE_LOCKABLE
  delete lockTable;
#endif
}

void *stm_malloc(size_t size) {
  return myTx->allocate(size);
}

void stm_free(void *addr, size_t size) { }

void mod_mem_init(int gc) {
  // FIXME(ddn): only allocates on one thread
  Galois::Runtime::MM::pagePreAlloc(16);
}

void mod_stats_init(void) { }

int stm_get_global_stats(const char *name, unsigned long *val) {
  std::string commitsKey("global_nb_commits");
  std::string abortsKey("global_nb_aborts");
  std::string retriesKey("global_max_retries");
  
  unsigned long sum = 0;

  if (commitsKey.compare(name) == 0) {
    for (std::list<stm_tx*>::iterator ii = allTx.begin(), ei = allTx.end(); ii != ei; ++ii)
      sum += (*ii)->stats.commits;
    *val = sum;
    return 1;
  } else if (abortsKey.compare(name) == 0) {
    for (std::list<stm_tx*>::iterator ii = allTx.begin(), ei = allTx.end(); ii != ei; ++ii)
      sum += (*ii)->stats.aborts;
    *val = sum;
    return 1;
  } else {
    return 0;
  }
}

void stm_abort(int abort_reason) {
  myTx->abort(abort_reason);
}

int stm_commit(void) {
  return myTx->commit();
}

struct stm_tx *stm_init_thread(void) {
  if (!myTx) {
    myTx = new stm_tx(lockTable);
    allTxLock.lock();
    allTx.push_front(myTx);
    allTxLock.unlock();
  }
  
  return myTx;
}

void stm_exit_thread(void) { }

sigjmp_buf *stm_start(stm_tx_attr_t attr) {
  return myTx->start();
}

stm_word_t stm_load(volatile stm_word_t *addr) {
  return *myTx->acquire_word((void*) addr);
}

float stm_load_float(volatile float *addr) {
  return *myTx->acquire_float((void*) addr);
}

void *stm_load_ptr(volatile void **addr) {
  return (void*) *myTx->acquire_word((void*) addr);
}

void stm_store(volatile stm_word_t *addr, stm_word_t value) {
  *myTx->acquire_word((void*) addr) = value;
}

void stm_store_float(volatile float *addr, float value) {
  *myTx->acquire_float((void*) addr) = value;
}

void stm_store_ptr(volatile void **addr, void *value) {
  *myTx->acquire_word((void*) addr) = (stm_word_t) value;
}
