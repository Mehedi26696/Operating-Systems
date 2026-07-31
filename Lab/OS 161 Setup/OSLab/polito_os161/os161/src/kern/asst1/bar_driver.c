/*
 * Local bar simulation driver for ASST1 command 1c.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <synch.h>
#include <thread.h>
#include <test.h>
#include "bar.h"
#include "bar_driver.h"

static unsigned bottle_doses[BAR_NBOTTLES + 1];
static struct semaphore *bottle_stats_mutex;
static struct semaphore *bar_done;
static struct semaphore *bar_print_mutex;

static void bar_status(const char *msg)
{
	P(bar_print_mutex);
	kprintf("%s\n", msg);
	V(bar_print_mutex);
}

void bottle_reset_stats(void)
{
	unsigned i;

	for (i = 1; i <= BAR_NBOTTLES; i++) {
		bottle_doses[i] = 0;
	}
}

void bottle_record_dose(unsigned bottle)
{
	KASSERT(bottle >= 1 && bottle <= BAR_NBOTTLES);

	P(bottle_stats_mutex);
	bottle_doses[bottle]++;
	V(bottle_stats_mutex);
}

unsigned bottle_get_doses(unsigned bottle)
{
	unsigned doses;

	KASSERT(bottle >= 1 && bottle <= BAR_NBOTTLES);

	P(bottle_stats_mutex);
	doses = bottle_doses[bottle];
	V(bottle_stats_mutex);
	return doses;
}

static void customer(void *junk, unsigned long num)
{
	struct bar_order order;
	char semname[24];
	unsigned i, j;

	(void)junk;

	snprintf(semname, sizeof(semname), "customer %lu", num);
	order.served = sem_create(semname, 0);
	if (order.served == NULL) {
		panic("customer: sem_create failed\n");
	}

	for (i = 0; i < BAR_DRINKS_PER_CUSTOMER; i++) {
		order.valid = true;
		order.requested[0] = 1;
		order.requested[1] = BAR_NO_INGREDIENT;
		order.requested[2] = BAR_NO_INGREDIENT;

		bar_place_order(&order);
		P(order.served);

		for (j = 0; j < BAR_MAX_INGREDIENTS; j++) {
			if (order.glass.contents[j] != order.requested[j]) {
				panic("customer: wrong drink served\n");
			}
		}

		thread_yield();
	}

	sem_destroy(order.served);
	V(bar_done);
	thread_exit();
}

static void bartender(void *junk, unsigned long num)
{
	struct bar_order *order;
	unsigned mixed;

	(void)junk;

	mixed = 0;
	while (1) {
		order = bar_get_order();
		if (!order->valid) {
			break;
		}

		mix(order);
		mixed++;
		bar_finish_order(order);
		thread_yield();
	}

	P(bar_print_mutex);
	kprintf("S %lu going home after mixing %u drinks\n", num, mixed);
	V(bar_print_mutex);

	V(bar_done);
	thread_exit();
}

int runbar(int nargs, char **args)
{
	struct bar_order close_orders[BAR_NBARTENDERS];
	int i, result;

	(void)nargs;
	(void)args;

	bottle_stats_mutex = sem_create("bottle stats", 1);
	if (bottle_stats_mutex == NULL) {
		return ENOMEM;
	}

	bar_done = sem_create("bar done", 0);
	if (bar_done == NULL) {
		sem_destroy(bottle_stats_mutex);
		return ENOMEM;
	}

	bar_print_mutex = sem_create("bar print", 1);
	if (bar_print_mutex == NULL) {
		sem_destroy(bar_done);
		sem_destroy(bottle_stats_mutex);
		return ENOMEM;
	}

	bottle_reset_stats();
	bar_open();

	for (i = 0; i < BAR_NBARTENDERS; i++) {
		result = thread_fork("bartender", NULL, bartender, NULL, i);
		if (result) {
			panic("runbar: thread_fork failed: %s\n",
			      strerror(result));
		}
	}

	for (i = 0; i < BAR_NCUSTOMERS; i++) {
		result = thread_fork("customer", NULL, customer, NULL, i);
		if (result) {
			panic("runbar: thread_fork failed: %s\n",
			      strerror(result));
		}
	}

	for (i = 0; i < BAR_NCUSTOMERS; i++) {
		P(bar_done);
	}

	for (i = 0; i < BAR_NBARTENDERS; i++) {
		close_orders[i].valid = false;
		close_orders[i].served = NULL;
		close_orders[i].requested[0] = BAR_NO_INGREDIENT;
		close_orders[i].requested[1] = BAR_NO_INGREDIENT;
		close_orders[i].requested[2] = BAR_NO_INGREDIENT;
		bar_place_order(&close_orders[i]);
	}

	for (i = 0; i < BAR_NBARTENDERS; i++) {
		P(bar_done);
	}

	for (i = 1; i <= BAR_NBOTTLES; i++) {
		P(bar_print_mutex);
		kprintf("Bottle %d used for %u doses\n", i, bottle_get_doses(i));
		V(bar_print_mutex);
	}

	bar_close();
	bar_status("The bar is closed, bye!!!");

	sem_destroy(bar_print_mutex);
	sem_destroy(bar_done);
	sem_destroy(bottle_stats_mutex);
	bar_print_mutex = NULL;
	bar_done = NULL;
	bottle_stats_mutex = NULL;

	return 0;
}