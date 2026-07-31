# Bar Synchronization Solution

## Goal

The `1c` bar problem models customers and bartenders running concurrently.

Required behavior:

- customers place drink orders and wait until the drink is served;
- bartenders take orders, mix the requested drink, and wake the customer;
- orders must not be lost or mixed up;
- bottle usage statistics must match the drinks mixed;
- bartenders must go home after all customers are finished;
- bartenders should be able to mix in parallel when they do not need the same bottle.

## Important Note About This Tree

The assignment files mentioned in the prompt were missing from this checkout, so I added local versions:

```text
src/kern/asst1/bar.c
src/kern/asst1/bar.h
src/kern/asst1/bar_driver.c
src/kern/asst1/bar_driver.h
```

The main synchronization solution is in `bar.c`. The local driver/header exist so `1c` can be built and tested in this checkout.

## Core Design

The bar has two shared-resource groups:

1. The order queue shared by customers and bartenders.
2. The drink bottles shared by bartenders.

The order queue is a bounded FIFO circular buffer:

```c
static struct bar_order *bar_orders[BAR_ORDER_QUEUE_SIZE];
static unsigned bar_head;
static unsigned bar_tail;
```

The queue uses three semaphores:

```c
static struct semaphore *bar_empty;
static struct semaphore *bar_full;
static struct semaphore *bar_mutex;
```

Meaning:

- `bar_empty` counts free queue slots.
- `bar_full` counts pending orders.
- `bar_mutex` protects `bar_head`, `bar_tail`, and `bar_orders[]`.

Each bottle has its own semaphore:

```c
static struct semaphore *bottle_mutex[BAR_NBOTTLES + 1];
```

Why: two bartenders can mix in parallel when their drinks use different bottles, but they serialize when they need the same bottle.

## File 1: `src/kern/asst1/bar.c`

### Opening the Bar

Added:

```c
void
bar_open(void)
{
        bar_head = 0;
        bar_tail = 0;

        bar_empty = sem_create("bar empty", BAR_ORDER_QUEUE_SIZE);
        bar_full = sem_create("bar full", 0);
        bar_mutex = sem_create("bar mutex", 1);

        for (i = 1; i <= BAR_NBOTTLES; i++) {
                bottle_mutex[i] = sem_create(name, 1);
        }
}
```

Why:

- reset the order queue;
- let customers enqueue until the queue is full;
- make bartenders block when there are no orders;
- create one mutex per bottle.

### Placing an Order

Added:

```c
void
bar_place_order(struct bar_order *order)
{
        P(bar_empty);
        P(bar_mutex);

        bar_orders[bar_tail] = order;
        bar_tail = (bar_tail + 1) % BAR_ORDER_QUEUE_SIZE;

        V(bar_mutex);
        V(bar_full);
}
```

Why:

- customers block if the order queue is full;
- queue insertion is protected by `bar_mutex`;
- `bar_full` wakes a bartender waiting for work.

### Taking an Order

Added:

```c
struct bar_order *
bar_get_order(void)
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
```

Why:

- bartenders block if no orders exist;
- FIFO order is preserved;
- removing an order frees one queue slot.

### Mixing a Drink

Added:

```c
void
mix(struct bar_order *order)
{
        unsigned bottles[BAR_MAX_INGREDIENTS];
        unsigned nbottles;

        ...

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
```

Why:

- each required bottle is locked while the dose is poured;
- different bottles allow parallel mixing;
- bottles are acquired in sorted order to prevent deadlock;
- duplicate ingredients from the same bottle only acquire that bottle once;
- `glass.contents[]` is filled to match `requested[]`.

### Serving the Customer

Added:

```c
void
bar_finish_order(struct bar_order *order)
{
        V(order->served);
}
```

Why: the customer blocks on its order semaphore and wakes only after the bartender has finished mixing the drink.

### Closing the Bar

Added:

```c
void
bar_close(void)
{
        for (i = 1; i <= BAR_NBOTTLES; i++) {
                sem_destroy(bottle_mutex[i]);
        }

        sem_destroy(bar_mutex);
        sem_destroy(bar_full);
        sem_destroy(bar_empty);
}
```

Why: clean up all synchronization primitives after the driver confirms customers and bartenders are done.

## File 2: `src/kern/asst1/bar.h`

Added local data structures:

```c
struct bar_glass {
        unsigned contents[BAR_MAX_INGREDIENTS];
};

struct bar_order {
        bool valid;
        unsigned requested[BAR_MAX_INGREDIENTS];
        struct bar_glass glass;
        struct semaphore *served;
};
```

Why:

- `requested[]` stores what the customer ordered;
- `glass.contents[]` stores what the bartender served;
- `served` lets a customer sleep until its exact order is ready;
- `valid == false` is used as the shutdown order for bartenders.

## File 3: `src/kern/asst1/bar_driver.c`

Added a local test driver for `1c`.

Important behavior:

- starts bartender threads;
- starts customer threads;
- waits for all customers to finish;
- sends one invalid order per bartender;
- waits for all bartenders to go home;
- prints bottle statistics.

Invalid shutdown orders:

```c
close_orders[i].valid = false;
bar_place_order(&close_orders[i]);
```

Why: bartenders use invalid orders as the signal to stop working and go home.

The local driver orders one drink from bottle `1` for each customer order, matching the sample output style where bottle 1 has all doses.

## File 4: `src/kern/asst1/bar_driver.h`

Added local test constants:

```c
#define BAR_NCUSTOMERS 10
#define BAR_NBARTENDERS 3
#define BAR_DRINKS_PER_CUSTOMER 10
```

This produces 100 total drinks in the local test.

## File 5: `src/kern/include/test.h`

Added:

```c
int runbar(int, char **);
```

Why: the kernel menu calls `runbar` for command `1c`.

## File 6: `src/kern/main/menu.c`

Added the menu label:

```c
"[1c]  Bar synchronization          ",
```

Added the command table entry:

```c
{ "1c",         runbar },
```

Why: this lets you run the bar test from the OS/161 kernel prompt.

## File 7: `src/kern/conf/conf.kern`

Added:

```text
file            asst1/bar.c
file            asst1/bar_driver.c
```

Why: the files must be compiled into the kernel.

## File 8: `src/kern/compile/DUMBVM/files.mk`

Added generated build entries:

```make
SRCS+=$(KTOP)/asst1/bar.c
SRCS+=$(KTOP)/asst1/bar_driver.c
```

Why: this lets the current DUMBVM compile directory see the files immediately. Running `./config DUMBVM` regenerates this from `conf.kern`.

## Deadlock Avoidance

Bartenders can need more than one bottle. If bartender A locks bottle 1 then waits for bottle 2, while bartender B locks bottle 2 then waits for bottle 1, the system deadlocks.

To prevent that, `mix()` sorts the needed bottle numbers and always acquires them in increasing order:

```c
sort_unique_bottles(bottles, &nbottles);

for (i = 0; i < nbottles; i++) {
        P(bottle_mutex[bottles[i]]);
}
```

This creates one global lock acquisition order for bottles.

## Parallelism

The solution does not use one giant bar lock for mixing. It locks only the bottles needed for the current drink. Therefore:

- two bartenders using different bottles can mix at the same time;
- two bartenders using the same bottle serialize only on that bottle;
- the order queue lock is held only while inserting/removing queue entries.

## Rebuild Steps

Because `conf.kern` changed, rerun config:

```sh
cd "/mnt/d/Depression/Academic Subjects/OS Lab/polito_os161/os161/src/kern/conf"
./config DUMBVM

cd ../compile/DUMBVM
bmake clean
bmake depend
bmake
bmake install
```

## Test Steps

Direct:

```sh
cd "/mnt/d/Depression/Academic Subjects/OS Lab/polito_os161/os161/root"
sys161 kernel "1c;q"
```

Interactive:

```sh
cd "/mnt/d/Depression/Academic Subjects/OS Lab/polito_os161/os161/root"
sys161 kernel
```

At the kernel prompt:

```text
1c
```

## Expected Output Shape

Exact bartender counts can vary because scheduling varies, but the run should end like:

```text
S 2 going home after mixing ... drinks
S 1 going home after mixing ... drinks
S 0 going home after mixing ... drinks
Bottle 1 used for 100 doses
Bottle 2 used for 0 doses
...
Bottle 10 used for 0 doses
The bar is closed, bye!!!
```

The important correctness checks are:

- all customers receive matching drinks;
- all bartenders go home;
- bottle usage totals match the number of poured doses;
- the test returns to the kernel prompt without hanging.