# Bar Synchronization Explanation

This note explains these ASST1 files:

- `src/kern/asst1/bar.c`
- `src/kern/asst1/bar.h`
- `src/kern/asst1/bar_driver.c`
- `src/kern/asst1/bar_driver.h`

Together, they implement and test a concurrent bar simulation using OS/161
kernel threads and semaphores.

The test appears in the OS/161 kernel menu as:

```text
1c  Bar synchronization problem
```

That menu command calls:

```c
runbar(int nargs, char **args)
```

## Problem Being Solved

The bar simulation has:

- customers that place drink orders
- bartenders that take orders and mix drinks
- bottles that must be used safely by multiple bartenders
- an order queue shared between customers and bartenders

The synchronization problem has two main parts:

1. Customers and bartenders share a bounded order queue.
2. Bartenders must avoid conflicting access to shared bottles while mixing.

## `bar.h`

This header defines the core bar data structures and function declarations.

```c
#define BAR_MAX_INGREDIENTS 3
#define BAR_NBOTTLES 10
#define BAR_NO_INGREDIENT 0
```

`BAR_MAX_INGREDIENTS` means each drink can request up to 3 ingredients.

`BAR_NBOTTLES` means there are 10 available bottles, numbered from `1` to `10`.

`BAR_NO_INGREDIENT` means a drink slot is empty.

### Glass

```c
struct bar_glass {
    unsigned contents[BAR_MAX_INGREDIENTS];
};
```

A `bar_glass` records what ingredients were actually poured into the drink.

### Order

```c
struct bar_order {
    bool valid;
    unsigned requested[BAR_MAX_INGREDIENTS];
    struct bar_glass glass;
    struct semaphore *served;
};
```

`valid` tells bartenders whether this is a real order or a shutdown signal.

`requested` stores the ingredients the customer wants.

`glass` stores the ingredients the bartender actually mixed.

`served` is a per-customer semaphore. The customer waits on it until the
bartender finishes the order.

The header declares the public bar functions:

```c
void bar_open(void);
void bar_close(void);
void bar_place_order(struct bar_order *order);
struct bar_order *bar_get_order(void);
void bar_finish_order(struct bar_order *order);
void mix(struct bar_order *order);
```

## `bar_driver.h`

This header defines the driver constants and bottle-statistics functions.

```c
#define BAR_NCUSTOMERS 10
#define BAR_NBARTENDERS 3
#define BAR_DRINKS_PER_CUSTOMER 10
```

With these values:

```text
10 customers
3 bartenders
10 drinks per customer
100 total customer drinks
```

The driver also declares:

```c
void bottle_reset_stats(void);
void bottle_record_dose(unsigned bottle);
unsigned bottle_get_doses(unsigned bottle);
int runbar(int nargs, char **args);
```

The `bottle_*` functions track how many times each bottle was used.

## `bar.c`

This file contains the synchronization logic for the bar itself.

### Order Queue State

```c
#define BAR_ORDER_QUEUE_SIZE 32
```

The bar has a bounded order queue with 32 slots.

```c
static struct bar_order *bar_orders[BAR_ORDER_QUEUE_SIZE];
static unsigned bar_head;
static unsigned bar_tail;
static struct semaphore *bar_empty;
static struct semaphore *bar_full;
static struct semaphore *bar_mutex;
```

`bar_orders` is the circular order queue.

`bar_head` is the index where the next bartender reads an order.

`bar_tail` is the index where the next customer places an order.

`bar_empty` counts empty queue slots.

`bar_full` counts filled queue slots.

`bar_mutex` protects the queue, `bar_head`, and `bar_tail`.

This is the same bounded-buffer pattern as producer/consumer:

- customers are producers of orders
- bartenders are consumers of orders

### Bottle Locks

```c
static struct semaphore *bottle_mutex[BAR_NBOTTLES + 1];
```

Each bottle has its own semaphore. The array has `BAR_NBOTTLES + 1` entries so
that bottle numbers can be used directly as indexes from `1` to `10`.

Index `0` is unused because `0` means `BAR_NO_INGREDIENT`.

### Valid Bottle Check

```c
static bool valid_bottle(unsigned bottle)
{
    return bottle >= 1 && bottle <= BAR_NBOTTLES;
}
```

This returns true only for real bottle numbers.

### Sorting and Removing Duplicate Bottles

```c
static void sort_unique_bottles(unsigned *bottles, unsigned *count)
```

This helper sorts the requested bottle numbers and removes duplicates.

This matters because a drink might request the same bottle more than once.
Without removing duplicates, a bartender could try to lock the same bottle
twice and block on their own lock.

Sorting also gives every bartender the same bottle-lock acquisition order.
That helps avoid deadlock.

For example, if two bartenders need bottles `2` and `5`, both will lock bottle
`2` before bottle `5`. They will not lock the same pair in opposite orders.

## Opening the Bar

```c
void bar_open(void)
```

This initializes the order queue:

```c
bar_head = 0;
bar_tail = 0;
```

Then it creates the order queue semaphores:

```c
bar_empty = sem_create("bar empty", BAR_ORDER_QUEUE_SIZE);
bar_full = sem_create("bar full", 0);
bar_mutex = sem_create("bar mutex", 1);
```

`bar_empty` starts at `32` because the queue starts empty.

`bar_full` starts at `0` because no orders have been placed yet.

`bar_mutex` starts at `1`, so it behaves like a lock.

Then it creates one mutex semaphore per bottle:

```c
for (i = 1; i <= BAR_NBOTTLES; i++) {
    snprintf(name, sizeof(name), "bottle %u", i);
    bottle_mutex[i] = sem_create(name, 1);
}
```

Each bottle can be used by only one bartender at a time.

## Closing the Bar

```c
void bar_close(void)
```

This destroys all bottle semaphores, then destroys the order queue semaphores.

It should run only after all customers and bartenders are finished.

## Placing an Order

```c
void bar_place_order(struct bar_order *order)
```

Customers use this function to put an order into the queue.

```c
P(bar_empty);
```

Wait for an empty queue slot. If the queue is full, the customer blocks.

```c
P(bar_mutex);
```

Enter the critical section for the order queue.

```c
bar_orders[bar_tail] = order;
bar_tail = (bar_tail + 1) % BAR_ORDER_QUEUE_SIZE;
```

Store the order pointer at the tail and advance the tail index. The modulo
operation makes the queue circular.

```c
V(bar_mutex);
V(bar_full);
```

Leave the critical section and signal that one more order is available.

## Getting an Order

```c
struct bar_order *bar_get_order(void)
```

Bartenders use this function to remove an order from the queue.

```c
P(bar_full);
```

Wait for an available order. If the queue is empty, the bartender blocks.

```c
P(bar_mutex);
```

Enter the queue critical section.

```c
order = bar_orders[bar_head];
bar_head = (bar_head + 1) % BAR_ORDER_QUEUE_SIZE;
```

Read the next order and advance the head index.

```c
V(bar_mutex);
V(bar_empty);
```

Leave the critical section and signal that one more empty queue slot is
available.

The function returns the order pointer to the bartender.

## Mixing a Drink

```c
void mix(struct bar_order *order)
```

This function pours the requested ingredients into the order's glass while
locking the needed bottles.

First, it scans the requested ingredients:

```c
for (i = 0; i < BAR_MAX_INGREDIENTS; i++) {
    bottle = order->requested[i];
    order->glass.contents[i] = BAR_NO_INGREDIENT;
    if (valid_bottle(bottle)) {
        bottles[nbottles++] = bottle;
    }
}
```

It clears the glass contents and collects only valid bottle numbers.

Then:

```c
sort_unique_bottles(bottles, &nbottles);
```

This sorts the bottle list and removes duplicates.

Next, it locks all needed bottles:

```c
for (i = 0; i < nbottles; i++) {
    P(bottle_mutex[bottles[i]]);
}
```

Because the bottles were sorted first, every bartender locks bottles in the same
global order. This prevents deadlock between bartenders who need overlapping
bottle sets.

Then it pours the drink:

```c
for (i = 0; i < BAR_MAX_INGREDIENTS; i++) {
    bottle = order->requested[i];
    if (valid_bottle(bottle)) {
        bottle_record_dose(bottle);
        order->glass.contents[i] = bottle;
    }
}
```

For each valid requested bottle, the code records one dose and writes the
bottle number into the glass contents.

Finally, it releases the bottle locks in reverse order:

```c
for (i = nbottles; i > 0; i--) {
    V(bottle_mutex[bottles[i - 1]]);
}
```

Reverse release is not strictly required for correctness here, but it is a
common pattern after acquiring multiple locks in sorted order.

## Finishing an Order

```c
void bar_finish_order(struct bar_order *order)
{
    V(order->served);
}
```

This signals the customer that their drink has been served.

The customer placed a semaphore pointer in `order->served`, then waited on it.
The bartender calls `V(order->served)` when the drink is ready.

## `bar_driver.c`

This file creates the customer and bartender threads and checks the simulation.

### Driver State

```c
static unsigned bottle_doses[BAR_NBOTTLES + 1];
static struct semaphore *bottle_stats_mutex;
static struct semaphore *bar_done;
static struct semaphore *bar_print_mutex;
```

`bottle_doses[i]` records how many doses were poured from bottle `i`.

`bottle_stats_mutex` protects the `bottle_doses` array.

`bar_done` lets the main test wait for customer and bartender threads to exit.

`bar_print_mutex` prevents status messages from overlapping.

### Bottle Statistics

```c
void bottle_reset_stats(void)
```

This resets the dose count for every bottle.

```c
void bottle_record_dose(unsigned bottle)
```

This increments the dose count for one bottle. It uses `bottle_stats_mutex`
because multiple bartenders can record doses concurrently.

```c
unsigned bottle_get_doses(unsigned bottle)
```

This returns the dose count for one bottle while holding the same mutex.

## Customer Thread

```c
static void customer(void *junk, unsigned long num)
```

Each customer creates one private semaphore:

```c
order.served = sem_create(semname, 0);
```

The semaphore starts at `0` because the customer should block until a bartender
serves the order.

Each customer orders `BAR_DRINKS_PER_CUSTOMER` drinks.

In the current driver, every drink requests only bottle `1`:

```c
order.valid = true;
order.requested[0] = 1;
order.requested[1] = BAR_NO_INGREDIENT;
order.requested[2] = BAR_NO_INGREDIENT;
```

Then the customer places the order:

```c
bar_place_order(&order);
```

And waits until a bartender finishes it:

```c
P(order.served);
```

After being served, the customer checks that the glass contents match the
request:

```c
if (order.glass.contents[j] != order.requested[j]) {
    panic("customer: wrong drink served\n");
}
```

When all drinks are done, the customer destroys its private semaphore, signals
`bar_done`, and exits.

## Bartender Thread

```c
static void bartender(void *junk, unsigned long num)
```

Each bartender repeatedly gets orders from the queue:

```c
order = bar_get_order();
```

If the order is not valid, it is a shutdown order:

```c
if (!order->valid) {
    break;
}
```

Otherwise, the bartender mixes and serves it:

```c
mix(order);
mixed++;
bar_finish_order(order);
thread_yield();
```

`mixed` counts how many real drinks this bartender made.

When a shutdown order is received, the bartender prints how many drinks it mixed,
signals `bar_done`, and exits.

## Test Entry Point

```c
int runbar(int nargs, char **args)
```

This is the function called by the menu command `1c`.

It creates driver semaphores:

```c
bottle_stats_mutex = sem_create("bottle stats", 1);
bar_done = sem_create("bar done", 0);
bar_print_mutex = sem_create("bar print", 1);
```

`bottle_stats_mutex` and `bar_print_mutex` start at `1` because they are locks.

`bar_done` starts at `0` because the main thread waits for worker threads to
signal completion.

Then it resets bottle statistics and opens the bar:

```c
bottle_reset_stats();
bar_open();
```

Next it starts bartender threads:

```c
for (i = 0; i < BAR_NBARTENDERS; i++) {
    thread_fork("bartender", NULL, bartender, NULL, i);
}
```

Then it starts customer threads:

```c
for (i = 0; i < BAR_NCUSTOMERS; i++) {
    thread_fork("customer", NULL, customer, NULL, i);
}
```

The main thread waits for all customers to finish:

```c
for (i = 0; i < BAR_NCUSTOMERS; i++) {
    P(bar_done);
}
```

After all customers are done, the driver sends one invalid order per bartender:

```c
for (i = 0; i < BAR_NBARTENDERS; i++) {
    close_orders[i].valid = false;
    close_orders[i].served = NULL;
    ...
    bar_place_order(&close_orders[i]);
}
```

These are shutdown orders. Each bartender consumes one invalid order and exits.

Then the main thread waits for all bartenders:

```c
for (i = 0; i < BAR_NBARTENDERS; i++) {
    P(bar_done);
}
```

Finally, the driver prints bottle usage, closes the bar, destroys driver
semaphores, and returns `0`.

## Expected Behavior

With the current constants:

```text
10 customers
3 bartenders
10 drinks per customer
100 total drinks
```

Because every requested drink uses bottle `1`, bottle `1` should be used for
`100` doses. The other bottles should be used for `0` doses.

The exact distribution of drinks among bartenders may change between runs
because the scheduler decides which bartender runs when.

## Main Synchronization Ideas

The order queue uses the bounded-buffer pattern:

- `bar_empty` waits for empty queue slots
- `bar_full` waits for available orders
- `bar_mutex` protects queue indexes and storage

Each bottle has its own lock:

- two bartenders cannot use the same bottle at the same time
- different bartenders can use different bottles concurrently

The `mix` function sorts bottle numbers before locking them:

- this prevents deadlock when drinks need multiple bottles
- duplicate bottle requests are collapsed so a bartender does not lock the same
  bottle twice

Each customer has a private `served` semaphore:

- the customer places an order
- the customer waits on `P(order.served)`
- the bartender calls `V(order.served)` when the drink is ready

The main lesson is that this problem combines multiple synchronization patterns:
bounded buffer, per-resource locking, completion signaling, and deadlock
avoidance through consistent lock ordering.
