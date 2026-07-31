/*
 * Concurrent mathematics test for ASST1.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <synch.h>
#include <thread.h>
#include <test.h>

#define NADDERS 10
#define ADD_LIMIT 10000

static volatile unsigned math_counter;
static unsigned math_stats[NADDERS];
static struct semaphore *math_mutex;
static struct semaphore *math_done;

static void adder(void *junk, unsigned long num)
{
	(void)junk;

	KASSERT(num < NADDERS);

	while (1) {
		P(math_mutex);
		if (math_counter >= ADD_LIMIT) {
			V(math_mutex);
			break;
		}

		math_counter++;
		math_stats[num]++;
		V(math_mutex);

		thread_yield();
	}

	V(math_done);
	thread_exit();
}

int math(int nargs, char **args)
{
	unsigned total;
	int i, result;

	(void)nargs;
	(void)args;

	math_counter = 0;
	for (i = 0; i < NADDERS; i++) {
		math_stats[i] = 0;
	}

	math_mutex = sem_create("math mutex", 1);
	if (math_mutex == NULL) {
		return ENOMEM;
	}

	math_done = sem_create("math done", 0);
	if (math_done == NULL) {
		sem_destroy(math_mutex);
		math_mutex = NULL;
		return ENOMEM;
	}

	kprintf("Starting %d adder threads\n", NADDERS);

	for (i = 0; i < NADDERS; i++) {
		result = thread_fork("adder", NULL, adder, NULL, i);
		if (result) {
			panic("math: thread_fork failed: %s\n", strerror(result));
		}
	}

	for (i = 0; i < NADDERS; i++) {
		P(math_done);
	}

	kprintf("Adder threads performed %u adds\n", math_counter);

	total = 0;
	for (i = 0; i < NADDERS; i++) {
		kprintf("Adder %d performed %u increments.\n", i, math_stats[i]);
		total += math_stats[i];
	}
	kprintf("The adders performed %u increments overall\n", total);

	sem_destroy(math_done);
	sem_destroy(math_mutex);
	math_done = NULL;
	math_mutex = NULL;

	return 0;
}