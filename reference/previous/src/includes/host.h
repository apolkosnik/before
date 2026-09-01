/*
  Previous - host.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#pragma once

#ifndef PREV_HOST_H
#define PREV_HOST_H

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* These types must be provided through host or cross-platform API. */
typedef SDL_AtomicInt      atomic_int;
typedef SDL_SpinLock       lock_t;
typedef SDL_Thread         thread_t;
typedef SDL_ThreadFunction thread_func_t;
typedef SDL_Semaphore      semaphore_t;
typedef SDL_Mutex          mutex_t;

/* These functions must be provided through host or cross-platform API. */
extern void         host_lock(lock_t* lock);
extern int          host_trylock(lock_t* lock);
extern void         host_unlock(lock_t* lock);
extern int          host_atomic_set(atomic_int* a, int newValue);
extern int          host_atomic_get(atomic_int* a);
extern int          host_atomic_add(atomic_int* a, int value);
extern int          host_atomic_cas(atomic_int* a, int oldValue, int newValue);
extern thread_t*    host_thread_create(thread_func_t, const char* name, void* data);
extern void         host_thread_priority(int priority);
extern int          host_thread_wait(thread_t* thread);
extern semaphore_t* host_semaphore_create(uint32_t value);
extern void         host_semaphore_signal(semaphore_t* semaphore);
extern int          host_semaphore_wait_timeout(semaphore_t* semaphore, int32_t ms);
extern void         host_semaphore_destroy(semaphore_t* semaphore);
extern mutex_t*     host_mutex_create(void);
extern void         host_mutex_lock(mutex_t* mutex);
extern void         host_mutex_unlock(mutex_t* mutex);
extern void         host_mutex_destroy(mutex_t* mutex);
extern void         host_sleep_ms(uint32_t ms);
extern void         host_sleep_us(uint64_t us);
extern uint64_t     host_get_counter(void);
extern uint64_t     host_get_counter_frequency(void);
extern int          host_num_cpus(void);

#ifdef __cplusplus
}
#endif

#endif /* PREV_HOST_H */
