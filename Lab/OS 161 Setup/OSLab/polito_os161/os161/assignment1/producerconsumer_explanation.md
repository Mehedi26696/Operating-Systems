# Producer/Consumer Explanation

This note explains these three ASST1 files:

- `src/kern/asst1/producerconsumer.c`
- `src/kern/asst1/producerconsumer_driver.c`
- `src/kern/asst1/producerconsumer_driver.h`

Together, they implement and test the bounded-buffer producer/consumer problem
using OS/161 kernel threads and semaphores.

The test appears in the OS/161 kernel menu as:

```text
1b  Producer/consumer problem
```

That menu command calls:

```c
run_producerconsumer(int nargs, char **args)
```

## Problem Being Solved

The producer/consumer problem has two kinds of threads:

- Producers create items and put them into a shared buffer.
- Consumers remove items from that shared buffer.

The buffer has limited space, so producers must wait when the buffer is full.
Consumers must wait when the buffer is empty.

This implementation uses semaphores to enforce those rules safely.

## `producerconsumer_driver.h`

The header file defines the shared constants, data type, and function
declarations used by both `.c` files.

```c
#define BUFFER_SIZE 8
#define NPRODUCERS 2
#define NCONSUMERS 5
#define ITEMS_PER_PRODUCER 32
```

`BUFFER_SIZE` is the number of slots in the bounded buffer.

`NPRODUCERS` is the number of producer threads.

`NCONSUMERS` is the number of consumer threads.

`ITEMS_PER_PRODUCER` is how many items each producer creates.

The total number of produced items is:

```c
NPRODUCERS * ITEMS_PER_PRODUCER
```

With the current values:

```text
2 * 32 = 64 total items
```

The item type is:

```c
struct pc_data {
    unsigned producer;
    unsigned item;
};
```

Each item records:

- which producer created it
- which item number it is for that producer

The header also declares the functions implemented in the `.c` files:

```c
void producerconsumer_startup(void);
void producerconsumer_shutdown(void);
void producer_produce(struct pc_data data);
struct pc_data consumer_consume(void);
int run_producerconsumer(int nargs, char **args);
```

## `producerconsumer.c`

This file contains the actual bounded-buffer implementation.

### Shared Buffer State

```c
static struct pc_data pc_buffer[BUFFER_SIZE];
static unsigned pc_head;
static unsigned pc_tail;
static struct semaphore *pc_empty;
static struct semaphore *pc_full;
static struct semaphore *pc_mutex;
```

`pc_buffer` is the circular buffer.

`pc_head` is the index where the next consumer reads.

`pc_tail` is the index where the next producer writes.

`pc_empty` counts how many empty buffer slots are available.

`pc_full` counts how many filled buffer slots are available.

`pc_mutex` protects the buffer, `pc_head`, and `pc_tail` so only one thread
modifies them at a time.

### Startup

```c
void producerconsumer_startup(void)
```

This initializes the bounded buffer:

```c
pc_head = 0;
pc_tail = 0;
```

Both circular-buffer indexes start at zero.

Then it creates three semaphores:

```c
pc_empty = sem_create("pc empty", BUFFER_SIZE);
pc_full = sem_create("pc full", 0);
pc_mutex = sem_create("pc mutex", 1);
```

`pc_empty` starts at `BUFFER_SIZE` because all buffer slots are initially empty.

`pc_full` starts at `0` because no items are initially available to consume.

`pc_mutex` starts at `1`, so it behaves like a lock.

If any semaphore creation fails, the kernel panics.

### Shutdown

```c
void producerconsumer_shutdown(void)
```

This destroys the three semaphores and sets their pointers to `NULL`.

It should be called only after all producer and consumer threads have finished
using the buffer.

### Producing an Item

```c
void producer_produce(struct pc_data data)
```

This function inserts one item into the bounded buffer.

```c
P(pc_empty);
```

The producer waits for an empty slot. If the buffer is full, this blocks.

```c
P(pc_mutex);
```

The producer enters the critical section that modifies the shared buffer.

```c
pc_buffer[pc_tail] = data;
pc_tail = (pc_tail + 1) % BUFFER_SIZE;
```

The item is written at `pc_tail`. Then `pc_tail` advances to the next slot.

The modulo operation wraps the index back to zero at the end of the buffer,
which makes the array behave like a circular buffer.

```c
V(pc_mutex);
V(pc_full);
```

The producer leaves the critical section, then signals that one more full slot
is available.

The order is important:

1. Wait for space.
2. Lock the buffer.
3. Insert the item.
4. Unlock the buffer.
5. Signal that an item is available.

### Consuming an Item

```c
struct pc_data consumer_consume(void)
```

This function removes and returns one item from the bounded buffer.

```c
P(pc_full);
```

The consumer waits for an available item. If the buffer is empty, this blocks.

```c
P(pc_mutex);
```

The consumer enters the critical section that modifies the shared buffer.

```c
data = pc_buffer[pc_head];
pc_head = (pc_head + 1) % BUFFER_SIZE;
```

The item is read from `pc_head`. Then `pc_head` advances to the next slot,
wrapping around with modulo arithmetic.

```c
V(pc_mutex);
V(pc_empty);
```

The consumer leaves the critical section, then signals that one more empty slot
is available.

Finally:

```c
return data;
```

The consumed item is returned to the caller.

## `producerconsumer_driver.c`

This file is the test driver. It creates producer and consumer threads, waits
for them to finish, and checks that the expected number of items were consumed.

### Total Items

```c
#define TOTAL_ITEMS (NPRODUCERS * ITEMS_PER_PRODUCER)
```

This computes the total number of items the consumers should consume.

With the current constants:

```text
TOTAL_ITEMS = 64
```

### Driver State

```c
static struct semaphore *pc_producer_done;
static struct semaphore *pc_consumer_done;
static struct semaphore *pc_started;
static struct semaphore *pc_ticket_mutex;
static struct semaphore *pc_print_mutex;
static unsigned pc_next_ticket;
static unsigned pc_consumed;
```

`pc_producer_done` lets the main test wait for producer threads to exit.

`pc_consumer_done` lets the main test wait for consumer threads to exit.

`pc_started` lets the main test wait until at least one worker thread has
printed its start message before printing `Waiting for producer threads to
exit...`.

`pc_ticket_mutex` protects `pc_next_ticket` and `pc_consumed`.

`pc_print_mutex` prevents status messages from different threads from
interleaving badly.

`pc_next_ticket` controls how many consume operations have been assigned to
consumer threads.

`pc_consumed` counts how many items were actually consumed.

### Status Printing

```c
static void pc_status(const char *msg)
```

This prints one status message while holding `pc_print_mutex`:

```c
P(pc_print_mutex);
kprintf("%s\n", msg);
V(pc_print_mutex);
```

The lock keeps messages from multiple threads from overlapping.

### Producer Thread

```c
static void producer_thread(void *junk, unsigned long num)
```

Each producer thread runs this function.

`num` is the producer number.

For each item:

```c
data.producer = num;
data.item = i;
producer_produce(data);
thread_yield();
```

The producer creates a `pc_data` item, puts it into the buffer, then yields to
encourage more thread interleaving.

Right after printing `Producer started`, the producer also signals:

```c
V(pc_started);
```

This tells the main thread that at least one worker has actually begun running.

After producing all items:

```c
V(pc_producer_done);
thread_exit();
```

The producer signals completion and exits.

### Consumer Thread

```c
static void consumer_thread(void *junk, unsigned long num)
```

Each consumer thread repeatedly consumes items until all expected consume
operations have been assigned.

The important control logic is:

```c
P(pc_ticket_mutex);
should_consume = pc_next_ticket < TOTAL_ITEMS;
if (should_consume) {
    pc_next_ticket++;
}
V(pc_ticket_mutex);
```

This gives a consumer permission to consume one item if fewer than
`TOTAL_ITEMS` consume operations have already been assigned.

`pc_ticket_mutex` is needed because all consumer threads share
`pc_next_ticket`. Without the mutex, two consumers could claim the same ticket
or the driver could assign too many consume operations.

If the consumer gets a ticket, it consumes one item:

```c
data = consumer_consume();
(void)data;
```

The consumed data is ignored in this test. The test only checks that the right
number of items are consumed.

Then it updates the consumed count:

```c
P(pc_ticket_mutex);
pc_consumed++;
V(pc_ticket_mutex);
```

After that, it yields:

```c
thread_yield();
```

Like the producer, the consumer signals `pc_started` right after printing
`Consumer started`.

When no more consume tickets remain, the consumer exits the loop:

```c
V(pc_consumer_done);
thread_exit();
```

### Test Entry Point

```c
int run_producerconsumer(int nargs, char **args)
```

This is called by the OS/161 menu command `1b`.

The arguments are unused:

```c
(void)nargs;
(void)args;
```

The function creates driver semaphores:

```c
pc_producer_done = sem_create("pc producer done", 0);
pc_consumer_done = sem_create("pc consumer done", 0);
pc_started = sem_create("pc started", 0);
pc_ticket_mutex = sem_create("pc ticket mutex", 1);
pc_print_mutex = sem_create("pc print mutex", 1);
```

The done semaphores start at `0` because the main thread must wait until worker
threads signal completion.

`pc_started` also starts at `0`. The main thread waits on it once so that the
`Waiting for producer threads to exit...` message is printed after at least one
producer or consumer start message.

The mutex semaphores start at `1` because they are used as locks.

Then it resets counters:

```c
pc_next_ticket = 0;
pc_consumed = 0;
```

Then it initializes the bounded buffer:

```c
producerconsumer_startup();
```

Next it starts consumer threads first:

```c
for (i = 0; i < NCONSUMERS; i++) {
    thread_fork("consumer", NULL, consumer_thread, NULL, i);
}
```

Starting consumers first is fine because consumers will block on `pc_full` until
producers put items into the buffer.

Then it starts producer threads:

```c
for (i = 0; i < NPRODUCERS; i++) {
    thread_fork("producer", NULL, producer_thread, NULL, i);
}
```

The main thread waits for all producers:

```c
for (i = 0; i < NPRODUCERS; i++) {
    P(pc_producer_done);
}
```

Then it waits for all consumers:

```c
for (i = 0; i < NCONSUMERS; i++) {
    P(pc_consumer_done);
}
```

After all threads finish, it verifies:

```c
KASSERT(pc_consumed == TOTAL_ITEMS);
```

This checks that exactly the expected number of items were consumed.

Finally, it shuts down the bounded buffer and destroys the driver semaphores.

## Why Three Semaphores Are Needed in the Buffer

The bounded buffer uses:

```c
pc_empty
pc_full
pc_mutex
```

They solve different problems.

`pc_empty` prevents producers from writing into a full buffer.

`pc_full` prevents consumers from reading from an empty buffer.

`pc_mutex` prevents multiple threads from modifying `pc_head`, `pc_tail`, and
`pc_buffer` at the same time.

Using only `pc_mutex` would protect the buffer from races, but it would not tell
producers when the buffer is full or consumers when the buffer is empty.

Using only `pc_empty` and `pc_full` would manage capacity, but two producers or
two consumers could still corrupt `pc_head` or `pc_tail` by updating them at the
same time.

## Expected Behavior

With the current constants:

```text
2 producers
5 consumers
32 items per producer
8 buffer slots
64 total items
```

The program should:

1. Start 5 consumer threads.
2. Start 2 producer threads.
3. Produce 64 total items.
4. Consume 64 total items.
5. Exit all producer and consumer threads.
6. Pass `KASSERT(pc_consumed == TOTAL_ITEMS)`.

The exact order of producer and consumer messages may change between runs. That
is normal because the scheduler decides which thread runs at each moment.

## Main Lesson

This code demonstrates the standard bounded-buffer solution:

- use one semaphore for empty slots
- use one semaphore for full slots
- use one mutex to protect shared buffer state
- use completion semaphores so the main thread can wait for workers

The result is that producers and consumers can run concurrently without
overflowing the buffer, reading from an empty buffer, or corrupting shared
indexes.
