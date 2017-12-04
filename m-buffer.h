/*
 * M*LIB - Fixed size BUFFER & STACK interface
 *
 * Copyright (c) 2017, Patrick Pelissier
 * All rights reserved.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * + Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * + Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS AND CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#ifndef __M_BUFFER_H
#define __M_BUFFER_H

#include "m-core.h"
#include "m-mutex.h"
#include "m-atomic.h"

/* Define the different kind of policy a buffer can have. */
typedef enum {
  BUFFER_QUEUE = 0,    BUFFER_STACK = 1,
  BUFFER_BLOCKING_PUSH = 0, BUFFER_UNBLOCKING_PUSH = 2,
  BUFFER_BLOCKING_POP = 0, BUFFER_UNBLOCKING_POP = 4,
  BUFFER_BLOCKING = 0, BUFFER_UNBLOCKING = 6,
  BUFFER_THREAD_SAFE = 0, BUFFER_THREAD_UNSAFE = 8,
  BUFFER_PUSH_INIT_POP_MOVE = 16,
  BUFFER_PUSH_OVERWRITE = 32,
  BUFFER_DEFERRED_POP = 64
} buffer_policy_e;

/* Define a buffer.
   USAGE: BUFFER_DEF(name, type, size_of_buffer_or_0, policy[, oplist]) */
#define BUFFER_DEF(name, type, m_size, ... )                            \
  BUFFERI_DEF(M_IF_NARGS_EQ1(__VA_ARGS__)                               \
              ((name, type, m_size,__VA_ARGS__, M_GLOBAL_OPLIST_OR_DEF(__VA_ARGS__), M_C(name,_t)), \
               (name, type, m_size,__VA_ARGS__,                                      M_C(name,_t))))

/* Define a queue for Many Producer Many Consummer
   Much faster than queue of BUFFER_DEF in heavy communication scenario
   Size of created queue can only a power of 2.
*/
#define QUEUE_MPMC_DEF(name, type, ...)					\
  QUEUEI_MPMC_DEF(M_IF_NARGS_EQ1(__VA_ARGS__)                           \
                  ((name, type, __VA_ARGS__, M_GLOBAL_OPLIST_OR_DEF(__VA_ARGS__), M_C(name,_t)), \
                   (name, type, __VA_ARGS__,                                      M_C(name,_t))))



/********************************** INTERNAL ************************************/

/* Define the exclusion size so that 2 atomic variables can be in
   separate cache line. This prevent false sharing to occur within the
   CPU cache. */
#if defined(_M_X64) || defined(_M_AMD64) || defined(__x86_64__)
# define BUFFERI_ALIGN_FOR_CACHELINE_EXCLUSION 128
#else
# define BUFFERI_ALIGN_FOR_CACHELINE_EXCLUSION 64
#endif

#define BUFFERI_IF_CTE_SIZE(m_size)  M_IF(M_BOOL(m_size))
#define BUFFERI_SIZE(m_size)         BUFFERI_IF_CTE_SIZE(m_size) (m_size, v->size)

#define BUFFERI_POLICY_P(policy, val)                                   \
  (((policy) & (val)) != 0)

#define BUFFERI_CONTRACT(buffer, size)	do {		\
    assert (buffer != NULL);				\
    assert (buffer->data != NULL);			\
  }while (0)

#define BUFFERI_PROTECTED_CONTRACT(buffer, size) do {	\
    assert (buffer->number[0] <= BUFFERI_SIZE(size));	\
    assert (buffer->idx_prod <= BUFFERI_SIZE(size));	\
    assert (buffer->idx_cons <= BUFFERI_SIZE(size));	\
  } while (0)

#define BUFFERI_DEF(arg) BUFFERI_DEF2 arg

#define BUFFERI_DEF2(name, type, m_size, policy, oplist, buffer_t)      \
                                                                        \
  typedef struct M_C(name, _s) {					\
    m_mutex_t mutex;                                                    \
    m_cond_t there_is_data;                                             \
    m_cond_t there_is_room_for_data;                                    \
    BUFFERI_IF_CTE_SIZE(m_size)( ,size_t size;)                         \
    size_t idx_prod, idx_cons, overwrite;			        \
    size_t number[1 + BUFFERI_POLICY_P(policy, BUFFER_DEFERRED_POP)];   \
    type *data;                                                         \
  } buffer_t[1];                                                        \
                                                                        \
static inline void                                                      \
M_C(name, _init)(buffer_t v, size_t size)                               \
{                                                                       \
  assert( size > 0);                                                    \
  BUFFERI_IF_CTE_SIZE(m_size)(assert(size == m_size), v->size = size);  \
  v->idx_prod = v->idx_cons = v->number[0] = v->overwrite = 0;		\
  if (BUFFERI_POLICY_P(policy, BUFFER_DEFERRED_POP))                    \
    v->number[1] = 0;                                                   \
  if (!BUFFERI_POLICY_P((policy), BUFFER_THREAD_UNSAFE)) {              \
    m_mutex_init(v->mutex);                                             \
    m_cond_init(v->there_is_data);                                      \
    m_cond_init(v->there_is_room_for_data);                             \
  } else                                                                \
    assert(BUFFERI_POLICY_P((policy), BUFFER_UNBLOCKING));              \
  v->data = M_GET_REALLOC oplist (type, NULL, BUFFERI_SIZE(m_size));    \
  if (v->data == NULL) {                                                \
    M_MEMORY_FULL (BUFFERI_SIZE(m_size)*sizeof(type));                  \
    return;                                                             \
  }                                                                     \
  if (!BUFFERI_POLICY_P((policy), BUFFER_PUSH_INIT_POP_MOVE)) {         \
    for(size_t i = 0; i < size; i++) {					\
      M_GET_INIT oplist(v->data[i]);                                    \
    }									\
  }                                                                     \
  BUFFERI_CONTRACT(v,m_size);						\
}									\
                                                                        \
 static inline void                                                     \
 M_C(name, _int_clear_obj)(buffer_t v)					\
 {                                                                      \
   BUFFERI_CONTRACT(v,m_size);						\
   if (!BUFFERI_POLICY_P((policy), BUFFER_PUSH_INIT_POP_MOVE)) {        \
     for(size_t i = 0; i < BUFFERI_SIZE(m_size); i++) {			\
       M_GET_CLEAR oplist(v->data[i]);                                  \
     }									\
   } else {                                                             \
     size_t i = BUFFERI_POLICY_P((policy), BUFFER_STACK) ? 0 : v->idx_cons; \
     while (i != v->idx_prod) {                                         \
       M_GET_CLEAR oplist(v->data[i]);                                  \
       i++;                                                             \
       if (!BUFFERI_POLICY_P((policy), BUFFER_STACK) && i >= BUFFERI_SIZE(m_size)) \
         i = 0;                                                         \
     }                                                                  \
   }                                                                    \
   v->idx_prod = v->idx_cons = v->number[0] = 0;                        \
   if (BUFFERI_POLICY_P(policy, BUFFER_DEFERRED_POP))                   \
     v->number[1] = 0;                                                  \
   BUFFERI_CONTRACT(v,m_size);						\
 }                                                                      \
 									\
 static inline void                                                     \
 M_C(name, _clear)(buffer_t v)						\
 {                                                                      \
   BUFFERI_CONTRACT(v,m_size);						\
   M_C(name, _int_clear_obj)(v);					\
   M_GET_FREE oplist (v->data);                                         \
   v->data = NULL;                                                      \
   v->overwrite = 0;                                                    \
   if (!BUFFERI_POLICY_P((policy), BUFFER_THREAD_UNSAFE)) {             \
     m_mutex_clear(v->mutex);                                           \
     m_cond_clear(v->there_is_data);                                    \
     m_cond_clear(v->there_is_room_for_data);                           \
   }                                                                    \
 }                                                                      \
 									\
 static inline void                                                     \
 M_C(name, _clean)(buffer_t v)						\
 {                                                                      \
   BUFFERI_CONTRACT(v,m_size);						\
   if (!BUFFERI_POLICY_P((policy), BUFFER_THREAD_UNSAFE))               \
     m_mutex_lock(v->mutex);                                            \
   BUFFERI_PROTECTED_CONTRACT(v, m_size);				\
  if (BUFFERI_POLICY_P((policy), BUFFER_PUSH_INIT_POP_MOVE))		\
     M_C(name, _int_clear_obj)(v);					\
   else                                                                 \
     v->idx_prod = v->idx_cons = v->number[0] = 0;                      \
   if (BUFFERI_POLICY_P(policy, BUFFER_DEFERRED_POP))                   \
     v->number[1] = 0;                                                  \
   if (!BUFFERI_POLICY_P((policy), BUFFER_THREAD_UNSAFE)) {             \
     m_cond_signal(v->there_is_room_for_data);                          \
     m_mutex_unlock(v->mutex);                                          \
   }                                                                    \
   BUFFERI_CONTRACT(v,m_size);						\
 }                                                                      \
 									\
 static inline bool                                                     \
 M_C(name, _empty_p)(const buffer_t v)					\
 {                                                                      \
   BUFFERI_CONTRACT(v,m_size);						\
   if (BUFFERI_POLICY_P(policy, BUFFER_DEFERRED_POP))                   \
     return v->number[1] == 0;                                          \
   else                                                                 \
     return v->number[0] == 0;                                          \
 }                                                                      \
 									\
 static inline bool                                                     \
 M_C(name, _full_p)(const buffer_t v)					\
 {                                                                      \
   BUFFERI_CONTRACT(v,m_size);						\
   return v->number[0] == BUFFERI_SIZE(m_size);                         \
 }                                                                      \
 									\
 static inline size_t							\
 M_C(name, _size)(const buffer_t v)					\
 {                                                                      \
   BUFFERI_CONTRACT(v,m_size);						\
   return v->number[0];							\
 }                                                                      \
 									\
 static inline bool                                                     \
 M_C(name, _push)(buffer_t v, type const data)				\
 {                                                                      \
   BUFFERI_CONTRACT(v,m_size);						\
   									\
   /* BUFFER lock */							\
   if (!BUFFERI_POLICY_P((policy), BUFFER_THREAD_UNSAFE)) {             \
     m_mutex_lock(v->mutex);                                            \
     while (!BUFFERI_POLICY_P((policy), BUFFER_PUSH_OVERWRITE)          \
            && M_C(name, _full_p)(v)) {					\
       if (BUFFERI_POLICY_P((policy), BUFFER_UNBLOCKING_PUSH)) {        \
         m_mutex_unlock(v->mutex);                                      \
         return false;                                                  \
       }                                                                \
       m_cond_wait(v->there_is_room_for_data, v->mutex);                \
     }                                                                  \
   } else if (!BUFFERI_POLICY_P((policy), BUFFER_PUSH_OVERWRITE)        \
              && M_C(name, _full_p)(v))					\
     return false;                                                      \
   BUFFERI_PROTECTED_CONTRACT(v, m_size);				\
   									\
   /* INDEX computation if we have to overwrite the last element */	\
   if (M_UNLIKELY (BUFFERI_POLICY_P((policy), BUFFER_PUSH_OVERWRITE)	\
		   && v->number[0] == BUFFERI_SIZE(m_size) )) {		\
     v->overwrite++;							\
     /* Let's clear the last element to overwrite it */                 \
     if (!BUFFERI_POLICY_P((policy), BUFFER_STACK)) {                   \
       v->idx_prod = (v->idx_prod - 1) % BUFFERI_SIZE(m_size);          \
     } else {                                                           \
       (v->idx_prod) --;                                                \
     }                                                                  \
     v->number[0] --;                                                   \
     if (BUFFERI_POLICY_P((policy), BUFFER_DEFERRED_POP))               \
       v->number[1] --;                                                 \
     if (BUFFERI_POLICY_P((policy), BUFFER_PUSH_INIT_POP_MOVE)) {       \
       M_GET_CLEAR oplist(v->data[v->idx_prod]);                        \
     }                                                                  \
   }                                                                    \
                                                                        \
   /* PUSH data */							\
   if (!BUFFERI_POLICY_P((policy), BUFFER_PUSH_INIT_POP_MOVE)) {        \
     M_GET_SET oplist (v->data[v->idx_prod], data);                     \
   } else {                                                             \
     M_GET_INIT_SET oplist(v->data[v->idx_prod], data);                 \
   }                                                                    \
   if (!BUFFERI_POLICY_P((policy), BUFFER_STACK)) {                     \
     v->idx_prod = (v->idx_prod +1) % BUFFERI_SIZE(m_size);             \
   } else {                                                             \
     (v->idx_prod) ++;                                                  \
   }                                                                    \
   v->number[0] ++;                                                     \
   if (BUFFERI_POLICY_P((policy), BUFFER_DEFERRED_POP))                 \
     v->number[1] ++;                                                   \
                                                                        \
   /* BUFFER unlock */							\
   if (!BUFFERI_POLICY_P((policy), BUFFER_THREAD_UNSAFE)) {             \
     m_cond_signal(v->there_is_data);                                   \
     m_mutex_unlock(v->mutex);                                          \
   }                                                                    \
									\
   BUFFERI_CONTRACT(v,m_size);						\
   return true;                                                         \
 }                                                                      \
                                                                        \
 static inline bool                                                     \
 M_C(name, _pop)(type *data, buffer_t v)				\
 {                                                                      \
   BUFFERI_CONTRACT(v,m_size);						\
   assert (data != NULL);						\
									\
   /* BUFFER lock */							\
   if (!BUFFERI_POLICY_P((policy), BUFFER_THREAD_UNSAFE)) {             \
     m_mutex_lock(v->mutex);                                            \
     while (M_C(name, _empty_p)(v)) {					\
       if (BUFFERI_POLICY_P((policy), BUFFER_UNBLOCKING_POP)) {         \
         m_mutex_unlock(v->mutex);                                      \
         return false;                                                  \
       }                                                                \
       m_cond_wait(v->there_is_data, v->mutex);                         \
     }                                                                  \
   } else if (M_C(name, _empty_p)(v))					\
     return false;                                                      \
   BUFFERI_PROTECTED_CONTRACT(v, m_size);				\
   									\
   /* POP data */							\
   if (!BUFFERI_POLICY_P((policy), BUFFER_STACK)) {                     \
     if (!BUFFERI_POLICY_P((policy), BUFFER_PUSH_INIT_POP_MOVE)) {	\
       M_GET_SET oplist (*data, v->data[v->idx_cons]);                  \
     } else {                                                           \
       M_DO_INIT_MOVE (oplist, *data, v->data[v->idx_cons]);            \
     }                                                                  \
     v->idx_cons = (v->idx_cons +1) % BUFFERI_SIZE(m_size);             \
   } else {                                                             \
     v->idx_prod --;                                                    \
     if (!BUFFERI_POLICY_P((policy), BUFFER_PUSH_INIT_POP_MOVE)) {      \
       M_GET_SET oplist (*data, v->data[v->idx_prod]);                  \
     } else {                                                           \
       M_DO_INIT_MOVE (oplist, *data, v->data[v->idx_prod]);            \
     }                                                                  \
   }                                                                    \
   if (!BUFFERI_POLICY_P((policy), BUFFER_DEFERRED_POP))                \
     v->number[0] --;                                                   \
   else                                                                 \
     v->number[1] --;                                                   \
   /* BUFFER unlock */							\
   if (!BUFFERI_POLICY_P((policy), BUFFER_THREAD_UNSAFE)) {             \
     m_cond_signal(v->there_is_room_for_data);                          \
     m_mutex_unlock(v->mutex);                                          \
   }                                                                    \
									\
   BUFFERI_CONTRACT(v,m_size);						\
   return true;                                                         \
 }                                                                      \
									\
 static inline size_t							\
 M_C(name, _overwrite)(const buffer_t v)				\
 {                                                                      \
   return v->overwrite;							\
 }									\
                                                                        \
 static inline size_t                                                   \
 M_C(name, _capacity)(const buffer_t v)                                 \
 {                                                                      \
   (void) v; /* may be unused */                                        \
   return BUFFERI_SIZE(m_size);                                         \
 }                                                                      \
                                                                        \
 static inline void                                                     \
 M_C(name, _pop_release)(buffer_t v)                                    \
 {                                                                      \
   if (BUFFERI_POLICY_P((policy), BUFFER_DEFERRED_POP))                 \
     v->number[0] --;                                                   \
 }                                                                      \


/* Definition of a a QUEUE for Many Produccer / Many Consummer
   for high bandwidth scenario:
   * lock-free,
   * wait-free but only because it accepts to fail spuriously,
   * quite fast
   * no blocking calls.
   * only queue (no stack)
   * size of queue is always a power of 2
   * no overwriting.
   */
#define QUEUEI_MPMC_DEF(arg) QUEUEI_MPMC_DEF2 arg

#define QUEUEI_MPMC_DEF2(name, type, policy, oplist, buffer_t)		\
									\
  /* The sequence number of an element will be equal to either		\
     - 2* the index of the production which creates it,			\
     - 1 + 2* the index of the consumption which consummes it		\
  */									\
  typedef struct M_C(name, _el_s) {					\
    atomic_ullong  seq;	/* Can only increase. */			\
    type         x;							\
    char align[BUFFERI_ALIGN_FOR_CACHELINE_EXCLUSION > sizeof(atomic_ullong)+sizeof(type) ? BUFFERI_ALIGN_FOR_CACHELINE_EXCLUSION - sizeof(atomic_ullong)-sizeof(type) : 1]; \
  } M_C(name, _el_t);							\
									\
  typedef struct M_C(name, _s) {					\
    atomic_ullong ProdIdx; /* Can only increase */			\
    char align1[BUFFERI_ALIGN_FOR_CACHELINE_EXCLUSION];			\
    atomic_ullong ConsoIdx; /* can only increase */			\
    char align2[BUFFERI_ALIGN_FOR_CACHELINE_EXCLUSION];			\
    M_C(name, _el_t) *Tab;                                              \
    unsigned int size;							\
  } buffer_t[1];							\
									\
  static inline bool              					\
  M_C(name, _push)(buffer_t table, type x)				\
  {									\
    unsigned long long idx = atomic_load(&table->ProdIdx);              \
    const unsigned int i = idx & (table->size -1);			\
    const unsigned long long seq = atomic_load(&table->Tab[i].seq);	\
    if (M_UNLIKELY (2*(idx - table->size) + 1 != seq))	{		\
      /* Buffer full (or unlikely preemption). Can not push */          \
      return false;							\
    }									\
    if (M_UNLIKELY (!atomic_compare_exchange_strong(&table->ProdIdx, &idx, idx+1))) { \
      /* Thread has been preemptted by another one. */			\
      return false;							\
    }									\
   if (!BUFFERI_POLICY_P((policy), BUFFER_PUSH_INIT_POP_MOVE)) {        \
     M_GET_SET oplist (table->Tab[i].x, x);				\
   } else {                                                             \
     M_GET_INIT_SET oplist (table->Tab[i].x, x);                        \
   }                                                                    \
   atomic_store(&table->Tab[i].seq, 2*idx);				\
   return true;                                                         \
  }									\
									\
  static inline bool							\
  M_C(name, _pop)(type *ptr, buffer_t table)				\
  {									\
    unsigned long long iC = atomic_load(&table->ConsoIdx);              \
    const unsigned int i = (iC & (table->size -1));			\
    const unsigned long long seq = atomic_load(&table->Tab[i].seq);	\
    if (seq != 2 * iC) {						\
      /* Nothing in buffer to consumme (or unlikely preemption) */      \
      return false;							\
    }									\
    if (M_UNLIKELY (!atomic_compare_exchange_strong(&table->ConsoIdx, &iC, iC+1))) { \
      /* Thread has been preempted by another one */			\
      return false;							\
    }									\
   if (!BUFFERI_POLICY_P((policy), BUFFER_PUSH_INIT_POP_MOVE)) {        \
    M_GET_SET oplist (*ptr , table->Tab[i].x);				\
   } else {                                                             \
     M_DO_INIT_MOVE (oplist, *ptr, table->Tab[i].x);                    \
   }                                                                    \
   atomic_store(&table->Tab[i].seq, 2*iC + 1);				\
   return true;                                                         \
  }									\
									\
  static inline void							\
  M_C(name, _init)(buffer_t buffer, size_t size)			\
  {									\
    assert (buffer != NULL);						\
    assert( M_POWEROF2_P(size));					\
    assert (size <= UINT_MAX);                                          \
    assert(((policy) & (BUFFER_STACK|BUFFER_THREAD_UNSAFE|BUFFER_PUSH_OVERWRITE)) == 0); \
    atomic_init(&buffer->ProdIdx, size);				\
    atomic_init(&buffer->ConsoIdx, size);				\
    buffer->size = size;						\
    buffer->Tab = M_GET_REALLOC oplist (M_C(name, _el_t), NULL, size);	\
    if (buffer->Tab == NULL) {						\
      M_MEMORY_FULL (size*sizeof(M_C(name, _el_t) ));			\
      return;								\
    }									\
    for(unsigned int j = 0; j < size; j++) {                            \
      atomic_init(&buffer->Tab[j].seq, 2*j+1);				\
      if (!BUFFERI_POLICY_P((policy), BUFFER_PUSH_INIT_POP_MOVE)) {     \
        M_GET_INIT oplist (buffer->Tab[j].x);				\
      }                                                                 \
    }									\
  }									\
									\
  static inline void							\
  M_C(name, _clear)(buffer_t buffer)					\
  {									\
    if (!BUFFERI_POLICY_P((policy), BUFFER_PUSH_INIT_POP_MOVE)) {       \
      for(unsigned int j = 0; j < buffer->size; j++) {                  \
        M_GET_CLEAR oplist (buffer->Tab[j].x);				\
      }									\
    } else {                                                            \
      unsigned long long iP = atomic_load(&buffer->ProdIdx);            \
      unsigned int i  = iP & (buffer->size -1);                         \
      unsigned long long iC = atomic_load(&buffer->ConsoIdx);           \
      unsigned int j  = iC & (buffer->size -1);                         \
      while (i != j) {                                                  \
        M_GET_CLEAR oplist(buffer->Tab[j].x);                           \
        j++;                                                            \
        if (j >= buffer->size)                                          \
          j = 0;                                                        \
      }                                                                 \
    }                                                                   \
    M_GET_FREE oplist (buffer->Tab);					\
  }									\
									\
  static inline size_t							\
  M_C(name, _size)(buffer_t table)                                      \
  {									\
    const unsigned long long iC = atomic_load(&table->ConsoIdx);	\
    const unsigned long long iP = atomic_load(&table->ProdIdx);		\
    /* We return an approximation as we can't read both iC & iP atomically*/ \
    /* As we read producer index after consummer index,			\
       and they are atomic variables without reordering			\
       producer index is always greater or equal than consummer index */ \
    return iP-iC;							\
  }									\
									\
  static inline bool							\
  M_C(name, _empty_p)(buffer_t v)					\
  {									\
    return M_C(name, _size) (v) == 0;					\
  }									\
  									\
  static inline bool							\
  M_C(name, _full_p)(buffer_t v)					\
  {									\
    return M_C(name, _size)(v) == v->size;                              \
  }									\
  


// NOTE: SET & INIT_SET are deliberatly missing. TBC if it the right way.
// NOTE: There is no oplist defined for this container.

#endif
