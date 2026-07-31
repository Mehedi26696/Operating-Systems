/*
 * Bar synchronization solution for ASST1.
 */

#include <types.h>
#include <lib.h>
#include <synch.h>
#include "bar.h"
#include "bar_driver.h"

#define BAR_ORDER_QUEUE_SIZE 32

static struct bar_order *bar_orders[BAR_ORDER_QUEUE_SIZE];
static unsigned bar_head;
static unsigned bar_tail;
static struct semaphore *bar_empty;
static struct semaphore *bar_full;
static struct semaphore *bar_mutex;
static struct semaphore *bottle_mutex[BAR_NBOTTLES + 1];

static bool valid_bottle(unsigned bottle)
{
	return bottle >= 1 && bottle <= BAR_NBOTTLES;
}

static void sort_unique_bottles(unsigned *bottles, unsigned *count)
{
	unsigned i, j, tmp;

	for (i = 0; i < *count; i++) {
		for (j = i + 1; j < *count; j++) {
			if (bottles[j] < bottles[i]) {
				tmp = bottles[i];
				bottles[i] = bottles[j];
				bottles[j] = tmp;
			}
		}
	}

	j = 0;
	for (i = 0; i < *count; i++) {
		if (i == 0 || bottles[i] != bottles[i - 1]) {
			bottles[j++] = bottles[i];
		}
	}
	*count = j;
}

void bar_open(void)
{
	unsigned i;
	char name[24];

	bar_head = 0;
	bar_tail = 0;

	bar_empty = sem_create("bar empty", BAR_ORDER_QUEUE_SIZE);
	if (bar_empty == NULL) {
		panic("bar_open: sem_create failed\n");
	}

	bar_full = sem_create("bar full", 0);
	if (bar_full == NULL) {
		panic("bar_open: sem_create failed\n");
	}

	bar_mutex = sem_create("bar mutex", 1);
	if (bar_mutex == NULL) {
		panic("bar_open: sem_create failed\n");
	}

	for (i = 1; i <= BAR_NBOTTLES; i++) {
		snprintf(name, sizeof(name), "bottle %u", i);
		bottle_mutex[i] = sem_create(name, 1);
		if (bottle_mutex[i] == NULL) {
			panic("bar_open: sem_create failed\n");
		}
	}
}

void bar_close(void)
{
	unsigned i;

	for (i = 1; i <= BAR_NBOTTLES; i++) {
		sem_destroy(bottle_mutex[i]);
		bottle_mutex[i] = NULL;
	}

	sem_destroy(bar_mutex);
	sem_destroy(bar_full);
	sem_destroy(bar_empty);
	bar_mutex = NULL;
	bar_full = NULL;
	bar_empty = NULL;
}

void bar_place_order(struct bar_order *order)
{
	P(bar_empty);
	P(bar_mutex);

	bar_orders[bar_tail] = order;
	bar_tail = (bar_tail + 1) % BAR_ORDER_QUEUE_SIZE;

	V(bar_mutex);
	V(bar_full);
}

struct bar_order *bar_get_order(void)
{
	struct bar_order *order;

	P(bar_full);
	P(bar_mutex);

	order = bar_orders[bar_head];
	bar_head = (bar_head + 1) % BAR_ORDER_QUEUE_SIZE;

	V(bar_mutex);
	V(bar_empty);

	return order;
}

void mix(struct bar_order *order)

{
	unsigned bottles[BAR_MAX_INGREDIENTS];
	unsigned nbottles;
	unsigned i, bottle;

	nbottles = 0;
	for (i = 0; i < BAR_MAX_INGREDIENTS; i++) {
		bottle = order->requested[i];
		order->glass.contents[i] = BAR_NO_INGREDIENT;
		if (valid_bottle(bottle)) {
			bottles[nbottles++] = bottle;
		}
	}

	sort_unique_bottles(bottles, &nbottles);

	for (i = 0; i < nbottles; i++) {
		P(bottle_mutex[bottles[i]]);
	}

	for (i = 0; i < BAR_MAX_INGREDIENTS; i++) {
		bottle = order->requested[i];
		if (valid_bottle(bottle)) {
			bottle_record_dose(bottle);
			order->glass.contents[i] = bottle;
		}
	}

	for (i = nbottles; i > 0; i--) {
		V(bottle_mutex[bottles[i - 1]]);
	}
}

void bar_finish_order(struct bar_order *order)
{
	V(order->served);
}