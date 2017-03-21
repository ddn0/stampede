#ifndef XTM_H
#define XTM_H

#include <stdint.h>
#include <stdlib.h>
#include <setjmp.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uintptr_t stm_word_t;

struct stm_tx;

#ifdef XTM_USE_INLINE_LOCKABLE
#  define XTM_DECL_LOCKABLE(type, name) \
  struct __XTM_##name { \
    type data; \
    type oldData; \
    struct stm_tx* owner; \
    void* next; \
  } name
#  define XTM_LOCKABLE_VALUE(expr) (expr).data
#  define XTM_LOCKABLE_INIT(expr) do { (expr).owner = 0; (expr).next = 0; } while (0)
#else
#  define XTM_DECL_LOCKABLE(type, name) type name
#  define XTM_LOCKABLE_VALUE(expr) (expr)
#  define XTM_LOCKABLE_INIT(expr) 
#endif

// ignored
typedef union stm_tx_attr {
  struct {
    unsigned int id : 16;
    unsigned int read_only : 1;
    unsigned int visible_reads : 1;
    unsigned int no_retry : 1;
    unsigned int no_extend : 1;
  };
  int32_t attrs;
} stm_tx_attr_t;

void *stm_malloc(size_t size);
void stm_free(void *addr, size_t size);
void mod_mem_init(int gc);
void mod_stats_init(void);
// global_nb_commits
// global_nb_aborts
// global_max_retries
int stm_get_global_stats(const char *name, unsigned long *val);

void stm_init(void);
void stm_exit(void);
void stm_abort(int abort_reason);
int stm_commit(void);
struct stm_tx *stm_init_thread(void);
void stm_exit_thread(void);
sigjmp_buf *stm_start(stm_tx_attr_t attr);

stm_word_t stm_load(volatile stm_word_t *addr);
float stm_load_float(volatile float *addr);
void *stm_load_ptr(volatile void **addr);

void stm_store(volatile stm_word_t *addr, stm_word_t value);
void stm_store_float(volatile float *addr, float value);
void stm_store_ptr(volatile void **addr, void *value);

#ifdef __cplusplus
}
#endif

#endif
