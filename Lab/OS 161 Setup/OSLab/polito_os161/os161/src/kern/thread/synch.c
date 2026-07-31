/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Synchronization primitives.
 * The specifications of the functions are in synch.h.
 */

#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <wchan.h>
#include <thread.h>
#include <current.h>
#include <synch.h>

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *name, unsigned initial_count)
{
        struct semaphore *sem;

        sem = kmalloc(sizeof(*sem));
        if (sem == NULL) {
                return NULL;
        }

        sem->sem_name = kstrdup(name);
        if (sem->sem_name == NULL) {
                kfree(sem);
                return NULL;
        }

	sem->sem_wchan = wchan_create(sem->sem_name);
	if (sem->sem_wchan == NULL) {
		kfree(sem->sem_name);
		kfree(sem);
		return NULL;
	}

	spinlock_init(&sem->sem_lock);
        sem->sem_count = initial_count;

        return sem;
}

void
sem_destroy(struct semaphore *sem)
{
        KASSERT(sem != NULL);

	/* wchan_cleanup will assert if anyone's waiting on it */
	spinlock_cleanup(&sem->sem_lock);
	wchan_destroy(sem->sem_wchan);
        kfree(sem->sem_name);
        kfree(sem);
}

void
P(struct semaphore *sem)
{
        KASSERT(sem != NULL);

        /*
         * May not block in an interrupt handler.
         *
         * For robustness, always check, even if we can actually
         * complete the P without blocking.
         */
        KASSERT(curthread->t_in_interrupt == false);

	/* Use the semaphore spinlock to protect the wchan as well. */
	spinlock_acquire(&sem->sem_lock);
        while (sem->sem_count == 0) {
		/*
		 *
		 * Note that we don't maintain strict FIFO ordering of
		 * threads going through the semaphore; that is, we
		 * might "get" it on the first try even if other
		 * threads are waiting. Apparently according to some
		 * textbooks semaphores must for some reason have
		 * strict ordering. Too bad. :-)
		 *
		 * Exercise: how would you implement strict FIFO
		 * ordering?
		 */
		wchan_sleep(sem->sem_wchan, &sem->sem_lock);
        }
        KASSERT(sem->sem_count > 0);
        sem->sem_count--;
	spinlock_release(&sem->sem_lock);
}

void
V(struct semaphore *sem)
{
        KASSERT(sem != NULL);

	spinlock_acquire(&sem->sem_lock);

        sem->sem_count++;
        KASSERT(sem->sem_count > 0);
	wchan_wakeone(sem->sem_wchan, &sem->sem_lock);

	spinlock_release(&sem->sem_lock);
}

////////////////////////////////////////////////////////////
//
// Lock.

struct lock *
lock_create(const char *name)
{
        struct lock *lock;

        lock = kmalloc(sizeof(*lock));
        if (lock == NULL) {
                return NULL;
        }

        lock->lk_name = kstrdup(name);
        if (lock->lk_name == NULL) {
                kfree(lock);
                return NULL;
        }

	HANGMAN_LOCKABLEINIT(&lock->lk_hangman, lock->lk_name);

	/* Initialize lock implementation state. */
	lock->lk_wchan = wchan_create(lock->lk_name);
	if (lock->lk_wchan == NULL) {
		kfree(lock->lk_name);
		kfree(lock);
		return NULL;
	}
	spinlock_init(&lock->lk_lock);
	lock->lk_held = false;
	lock->lk_owner = NULL;

        return lock;
}

void
lock_destroy(struct lock *lock)
{
        KASSERT(lock != NULL);

        /* Make sure no one is using the lock. */
        KASSERT(!lock->lk_held);

	spinlock_cleanup(&lock->lk_lock);
	wchan_destroy(lock->lk_wchan);

        kfree(lock->lk_name);
        kfree(lock);
}

void
lock_acquire(struct lock *lock)
{
	/* Call this (atomically) before waiting for a lock */
	//HANGMAN_WAIT(&curthread->t_hangman, &lock->lk_hangman);

        KASSERT(lock != NULL);
        KASSERT(curthread->t_in_interrupt == false);

	/* Spin on the lock's internal spinlock to keep wchan + state atomic. */
	spinlock_acquire(&lock->lk_lock);

	/* Sleep while another thread holds the lock. */
	while (lock->lk_held) {
		wchan_sleep(lock->lk_wchan, &lock->lk_lock);
	}

	lock->lk_held = true;
	lock->lk_owner = curthread;

	spinlock_release(&lock->lk_lock);

	/* Call this (atomically) once the lock is acquired */
	//HANGMAN_ACQUIRE(&curthread->t_hangman, &lock->lk_hangman);
}

void
lock_release(struct lock *lock)
{
	/* Call this (atomically) when the lock is released */
	//HANGMAN_RELEASE(&curthread->t_hangman, &lock->lk_hangman);

        KASSERT(lock != NULL);
        KASSERT(lock_do_i_hold(lock));

	spinlock_acquire(&lock->lk_lock);

	lock->lk_held = false;
	lock->lk_owner = NULL;

	/* Wake exactly one waiter, if any. */
	wchan_wakeone(lock->lk_wchan, &lock->lk_lock);

	spinlock_release(&lock->lk_lock);
}

bool
lock_do_i_hold(struct lock *lock)
{
        KASSERT(lock != NULL);

        /*
         * Read lk_owner under the spinlock so we get a consistent view of
         * "is curthread the owner right now".
         */
	spinlock_acquire(&lock->lk_lock);
	bool held = lock->lk_held && (lock->lk_owner == curthread);
	spinlock_release(&lock->lk_lock);

        return held;
}

////////////////////////////////////////////////////////////
//
// CV


struct cv *
cv_create(const char *name)
{
        struct cv *cv;

        cv = kmalloc(sizeof(*cv));
        if (cv == NULL) {
                return NULL;
        }

        cv->cv_name = kstrdup(name);
        if (cv->cv_name==NULL) {
                kfree(cv);
                return NULL;
        }

	/* Initialize CV implementation state. */
	cv->cv_wchan = wchan_create(cv->cv_name);
	if (cv->cv_wchan == NULL) {
		kfree(cv->cv_name);
		kfree(cv);
		return NULL;
	}
	spinlock_init(&cv->cv_lock);

        return cv;
}

void
cv_destroy(struct cv *cv)
{
        KASSERT(cv != NULL);

        /* Wait channel must be empty before destruction. */
        spinlock_acquire(&cv->cv_lock);
        KASSERT(wchan_isempty(cv->cv_wchan, &cv->cv_lock));
        spinlock_release(&cv->cv_lock);

	spinlock_cleanup(&cv->cv_lock);
	wchan_destroy(cv->cv_wchan);

        kfree(cv->cv_name);
        kfree(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
        KASSERT(cv != NULL);
        KASSERT(lock != NULL);
        KASSERT(lock_do_i_hold(lock));
        KASSERT(curthread->t_in_interrupt == false);

	/*
	 * Atomically:
	 *   1. Acquire the CV spinlock (so we can safely sleep on its wchan).
	 *   2. Release the caller's lock.
	 *   3. Go to sleep on the CV wchan (wchan_sleep releases the CV
	 *      spinlock during the switch, then re-acquires it on return).
	 *   4. After waking, release the CV spinlock.
	 *   5. Re-acquire the caller's lock before returning.
	 *
	 * Step 2 has to happen *after* step 1, because lock_release
	 * briefly takes its own internal spinlock; we must not be
	 * holding any spinlock when we call it (see
	 * curcpu->c_spinlocks check in wchan_sleep).
	 *
	 * Wait -- the rule is "must not hold *other* spinlocks". We can
	 * release the caller's lock after grabbing cv_lock: we don't
	 * hold the lock's lk_lock at that point (lock_acquire/release
	 * only briefly take it internally).
	 */
	spinlock_acquire(&cv->cv_lock);
	lock_release(lock);

	wchan_sleep(cv->cv_wchan, &cv->cv_lock);
	/* wchan_sleep returned with cv_lock held; release it now. */
	spinlock_release(&cv->cv_lock);

	lock_acquire(lock);
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
        KASSERT(cv != NULL);
        KASSERT(lock != NULL);
        KASSERT(lock_do_i_hold(lock));

	(void)lock; /* lock is required by the API, not used here directly */

	/* wchan_wakeone requires the wchan spinlock to be held. */
	spinlock_acquire(&cv->cv_lock);
	wchan_wakeone(cv->cv_wchan, &cv->cv_lock);
	spinlock_release(&cv->cv_lock);
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
        KASSERT(cv != NULL);
        KASSERT(lock != NULL);
        KASSERT(lock_do_i_hold(lock));

	(void)lock; /* lock is required by the API, not used here directly */

	/* wchan_wakeall requires the wchan spinlock to be held. */
	spinlock_acquire(&cv->cv_lock);
	wchan_wakeall(cv->cv_wchan, &cv->cv_lock);
	spinlock_release(&cv->cv_lock);
}
