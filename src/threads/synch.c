/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
// #include <stdlib.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/malloc.h"
/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
     decrement it.

   - up or "V": increment the value (and wake up one waiting
     thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value)
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. */
void
sema_down (struct semaphore *sema)
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0)
    {
      // printf("    71 Inside sema _down. running_thread priority %d\n",thread_current()->priority );
      // list_insert_ordered (&sema->waiters, &thread_current ()->elem, &compare_priority, NULL);
      list_push_back (&sema->waiters, &thread_current ()->elem);
      thread_block ();
      // printf("    74 Thread unblocked in sema down. current thread priority %d\n",thread_current()->priority );
      // printf("46 sema value %d\n", sema->value);
    }
    // printf("77 sema value non 0 .\n" );

  sema->value--;
  intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema)
{
  enum intr_level old_level;
  bool success;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (sema->value > 0)
    {
      sema->value--;
      success = true;
    }
  else
    success = false;
  intr_set_level (old_level);

  return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema)
{
  // printf("Inside sema up 116\n");
  enum intr_level old_level;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  sema->value++;
  if (!list_empty (&sema->waiters))
  {
    //the threads' priorities can change due to donation even if we added them in order. Thus call sort
    list_sort(&sema->waiters,&compare_priority,NULL);
    struct thread *t=list_entry (list_pop_front (&sema->waiters), struct thread, elem);
    // printf("synch.c 121 priority of unblocked thred: %d\n", t->priority);
    // printf("synch.c 122 priority of current thred: %d\n", thread_current()->priority);
    thread_unblock (t);


  }

  intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void)
{
  struct semaphore sema[2];
  int i;

  printf ("Testing semaphores...");
  sema_init (&sema[0], 0);
  sema_init (&sema[1], 0);
  thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++)
    {
      sema_up (&sema[0]);
      sema_down (&sema[1]);
    }
  printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_)
{
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++)
    {
      sema_down (&sema[0]);
      sema_up (&sema[1]);
    }
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);

  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock)
{
  // printf("202 Inside lock_acquire.\n" );
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  if (lock->holder !=NULL)
  {
    //lock owned by some other thread. add the lock in the waiting list of crrent thread
    struct waiting_locks_elem *w=malloc(sizeof ( struct waiting_locks_elem));
    w->lock=lock;
    list_push_back(&thread_current()->waiting_locks,&w->elem);

    //priority donation
    if ( (lock->holder)->priority<thread_current()->priority)
    {
      //priority will change now
      //store the priority with the lock. ONLY IF AN ENTRY FOR THIS LOCK IN THE HOLDER NOT THERE. UPDATE OTHERWISE. when the lock is realeased its entry is deleted and the max of the prorities in locksAndPriorities is taken as new priority. when all finished, the original priority is taken
      int exist=0;
      struct list_elem *e;
      for ( e = list_begin (&lock->holder->locksAndPriorities); e != list_end (&lock->holder->locksAndPriorities);  e = list_next (e))
      {
        struct locksAndPriorities_elem *s=list_entry(e,struct locksAndPriorities_elem, elem);
        if (s->lock==lock)
        {
          //update the entry with the larger priority
          s->priority=thread_current()->priority;
          exist=1;
          break;
          /* code */
        }
      }
      if (exist==0)
      {
        struct locksAndPriorities_elem *s= malloc (sizeof (struct locksAndPriorities_elem) );
        s->lock=lock;
        s->priority=thread_current()->priority;//new value of priority
        // s->priority=(lock->holder)->priority;//old value of priority
        list_push_back(&(lock->holder)->locksAndPriorities,&(s->elem));
        /* code */
      }
      // (lock->holder)->priority=thread_current()->priority;
      //implement nested donoations
      struct list queue;
      list_init(&queue);
      struct thread_lock_list_elem *st=malloc(sizeof(struct thread_lock_list_elem));
      st->thread=lock->holder;
      st->lock=lock;
      list_push_back(&queue, &st->elem);
      while (!list_empty(&queue))
      {
        struct thread_lock_list_elem *t=list_entry(list_pop_front(&queue), struct thread_lock_list_elem, elem);
        if (t->thread->priority<thread_current()->priority)
        {
          t->thread->priority=thread_current()->priority;
          //update the priority of the lock in locksAndPriorities of the thread as Well
          struct list_elem *el;
          for ( el = list_begin (&t->thread->locksAndPriorities); el != list_end (&t->thread->waiting_locks);  el = list_next (el))
          {
            struct locksAndPriorities_elem *lp=list_entry(el, struct locksAndPriorities_elem,elem);
            if (lp->lock==t->lock)
            {
              lp->priority=thread_current()->priority;
              break;
              /* code */
            }
          }

          /* code */
        }
        //now add all the threads which are the owners of the locks thread t is waiting on.
        struct list_elem *e;
        for ( e = list_begin (&t->thread->waiting_locks); e != list_end (&t->thread->waiting_locks);  e = list_next (e))
        {
          struct waiting_locks_elem *ws=list_entry(e,struct waiting_locks_elem, elem);
          struct thread_lock_list_elem *st=malloc(sizeof(struct thread_lock_list_elem));
          st->thread=ws->lock->holder;
          st->lock=ws->lock;
          list_push_back(&queue, &st->elem);
        }
        free(t);
      }
      // list_sort(&ready_list,&compare_priority,NULL);
    }
    /* code */
  }
  sema_down (&lock->semaphore);
  //now the lock has been finally acquired by the current thread. remove it from waiting list.
  struct list_elem *e;
  for ( e = list_begin (&thread_current()->waiting_locks); e != list_end (&thread_current()->waiting_locks);  e = list_next (e))
  {
    struct waiting_locks_elem *w=list_entry(e,struct waiting_locks_elem, elem);
    if (w->lock==lock)
    {
      //remove this
      list_remove(e);
      free(w);
      break;
    }
  }

  lock->holder = thread_current ();
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock)
{
  bool success;

  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  success = sema_try_down (&lock->semaphore);
  if (success)
    lock->holder = thread_current ();
  return success;
}

/* Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  //priority restoration
  struct list_elem *e;
  for ( e = list_begin (&lock->holder->locksAndPriorities); e != list_end (&lock->holder->locksAndPriorities);  e = list_next (e))
  {
    struct locksAndPriorities_elem *s=list_entry (e, struct locksAndPriorities_elem, elem);
    if (s->lock==lock)
    {
      list_remove(e);
      free(s);
      if (list_empty(&lock->holder->locksAndPriorities))
      {
        lock->holder->priority=lock->holder->first_priority;
      }
      else
      {
        struct list_elem *e=(list_max(&lock->holder->locksAndPriorities,&not_compare_priority, NULL));
        struct locksAndPriorities_elem *sl=list_entry(e,struct locksAndPriorities_elem,elem);
        lock->holder->priority=sl->priority;
      }

      break;
    }
  }
  lock->holder = NULL;
  sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock)
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}

/* Reader-writer lock implementation - simple. For the
   file system assignment implementing synchronization.
 */
void rwlock_init (struct rwlock *l) {
   ASSERT(l != NULL);

   l->read_count = 0;
   lock_init(&l->read_lock);
   lock_init(&l->write_lock);
}

void rwlock_read_acquire (struct rwlock *l) {
   ASSERT(l != NULL);

   lock_acquire(&l->read_lock);
   if(l->read_count == 0)
         lock_acquire(&l->write_lock);
   l->read_count++;
   lock_release(&l->read_lock);
}

void rwlock_read_release (struct rwlock *l) {
   ASSERT(l != NULL);

   lock_acquire(&l->read_lock);
   l->read_count--;
   if(l->read_count == 0)
         lock_release(&l->write_lock);
   lock_release(&l->read_lock);
}

void rwlock_write_acquire (struct rwlock *l) {
   ASSERT(l != NULL);
   lock_acquire(&l->write_lock);
}

void rwlock_write_release (struct rwlock *l) {
   ASSERT(l != NULL);
   lock_release(&l->write_lock);
}


/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock)
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  sema_init (&waiter.semaphore, 0);
  // list_insert_ordered (&cond->waiters, &waiter.elem, &compare_priority, NULL);
  list_push_back (&cond->waiters, &waiter.elem);
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED)
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));
  list_sort(&cond->waiters,&compare_condvar_priority,NULL);
  if (!list_empty (&cond->waiters))
    sema_up (&list_entry (list_pop_front (&cond->waiters),
                          struct semaphore_elem, elem)->semaphore);
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock)
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}
