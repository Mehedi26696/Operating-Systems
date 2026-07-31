/*
 * Bounded-buffer producer/consumer solution for ASST1.
 */

#include <types.h>
#include <lib.h>
#include <synch.h>
#include "producerconsumer_driver.h"

static struct pc_data pc_buffer[BUFFER_SIZE];
static unsigned pc_head;
static unsigned pc_tail;
static struct semaphore *pc_empty;
static struct semaphore *pc_full;
static struct semaphore *pc_mutex;

void producerconsumer_startup(void)
{
	pc_head = 0;
	pc_tail = 0;

	pc_empty = sem_create("pc empty", BUFFER_SIZE);
	if (pc_empty == NULL) {
		panic("producerconsumer_startup: sem_create failed\n");
	}

	pc_full = sem_create("pc full", 0);
	if (pc_full == NULL) {
		panic("producerconsumer_startup: sem_create failed\n");
	}

	pc_mutex = sem_create("pc mutex", 1);
	if (pc_mutex == NULL) {
		panic("producerconsumer_startup: sem_create failed\n");
	}
}

void producerconsumer_shutdown(void)
{
	sem_destroy(pc_mutex);
	sem_destroy(pc_full);
	sem_destroy(pc_empty);

	pc_mutex = NULL;
	pc_full = NULL;
	pc_empty = NULL;
}

void producer_produce(struct pc_data data)
{
	P(pc_empty);
	P(pc_mutex);

	pc_buffer[pc_tail] = data;
	pc_tail = (pc_tail + 1) % BUFFER_SIZE;

	V(pc_mutex);
	V(pc_full);
}

struct pc_data consumer_consume(void)
{
	struct pc_data data;

	P(pc_full);
	P(pc_mutex);

	data = pc_buffer[pc_head];
	pc_head = (pc_head + 1) % BUFFER_SIZE;

	V(pc_mutex);
	V(pc_empty);

	return data;
}