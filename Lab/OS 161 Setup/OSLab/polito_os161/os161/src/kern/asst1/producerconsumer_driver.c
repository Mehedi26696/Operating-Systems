/*
 * Local producer/consumer driver for ASST1 command 1b.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <synch.h>
#include <thread.h>
#include <test.h>
#include "producerconsumer_driver.h"

#define TOTAL_ITEMS (NPRODUCERS * ITEMS_PER_PRODUCER)

static struct semaphore *pc_producer_done;
static struct semaphore *pc_consumer_done;
static struct semaphore *pc_ticket_mutex;
static struct semaphore *pc_print_mutex;
static unsigned pc_next_ticket;
static unsigned pc_consumed;

static void pc_status(const char *msg)
{
	P(pc_print_mutex);
	kprintf("%s\n", msg);
	V(pc_print_mutex);
}

static void producer_thread(void *junk, unsigned long num)
{
	unsigned i;
	struct pc_data data;

	(void)junk;

	pc_status("Producer started");
	for (i = 0; i < ITEMS_PER_PRODUCER; i++) {
		data.producer = num;
		data.item = i;
		producer_produce(data);
		thread_yield();
	}
	pc_status("Producer finished");

	V(pc_producer_done);
	thread_exit();
}

static void consumer_thread(void *junk, unsigned long num)
{
	struct pc_data data;
	bool should_consume;

	(void)junk;
	(void)num;

	pc_status("Consumer started");
	while (1) {
		P(pc_ticket_mutex);
		should_consume = pc_next_ticket < TOTAL_ITEMS;
		if (should_consume) {
			pc_next_ticket++;
		}
		V(pc_ticket_mutex);

		if (!should_consume) {
			break;
		}

		data = consumer_consume();
		(void)data;

		P(pc_ticket_mutex);
		pc_consumed++;
		V(pc_ticket_mutex);

		thread_yield();
	}

	pc_status("Consumer finished normally");
	V(pc_consumer_done);
	thread_exit();
}

int run_producerconsumer(int nargs, char **args)
{
	int i, result;

	(void)nargs;
	(void)args;

	kprintf("run_producerconsumer: starting up\n");

	pc_producer_done = sem_create("pc producer done", 0);
	if (pc_producer_done == NULL) {
		return ENOMEM;
	}

	pc_consumer_done = sem_create("pc consumer done", 0);
	if (pc_consumer_done == NULL) {
		sem_destroy(pc_producer_done);
		return ENOMEM;
	}

	pc_ticket_mutex = sem_create("pc ticket mutex", 1);
	if (pc_ticket_mutex == NULL) {
		sem_destroy(pc_consumer_done);
		sem_destroy(pc_producer_done);
		return ENOMEM;
	}

	pc_print_mutex = sem_create("pc print mutex", 1);
	if (pc_print_mutex == NULL) {
		sem_destroy(pc_ticket_mutex);
		sem_destroy(pc_consumer_done);
		sem_destroy(pc_producer_done);
		return ENOMEM;
	}

	pc_next_ticket = 0;
	pc_consumed = 0;
	producerconsumer_startup();

	for (i = 0; i < NCONSUMERS; i++) {
		result = thread_fork("consumer", NULL, consumer_thread, NULL, i);
		if (result) {
			panic("run_producerconsumer: thread_fork failed: %s\n",
			      strerror(result));
		}
	}

	for (i = 0; i < NPRODUCERS; i++) {
		result = thread_fork("producer", NULL, producer_thread, NULL, i);
		if (result) {
			panic("run_producerconsumer: thread_fork failed: %s\n",
			      strerror(result));
		}
	}

	pc_status("Waiting for producer threads to exit...");
	for (i = 0; i < NPRODUCERS; i++) {
		P(pc_producer_done);
	}
	pc_status("All producer threads have exited.");

	for (i = 0; i < NCONSUMERS; i++) {
		P(pc_consumer_done);
	}

	KASSERT(pc_consumed == TOTAL_ITEMS);

	producerconsumer_shutdown();
	sem_destroy(pc_print_mutex);
	sem_destroy(pc_ticket_mutex);
	sem_destroy(pc_consumer_done);
	sem_destroy(pc_producer_done);
	pc_print_mutex = NULL;
	pc_ticket_mutex = NULL;
	pc_consumer_done = NULL;
	pc_producer_done = NULL;

	return 0;
}