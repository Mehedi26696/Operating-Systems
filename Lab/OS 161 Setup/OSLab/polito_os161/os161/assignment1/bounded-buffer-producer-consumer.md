# Bounded Buffer Producer/Consumer Solution

## Goal

The `1b` assignment test starts producer threads and consumer threads that communicate through a fixed-size shared buffer.

The required behavior is:

- producers call `producer_produce(data)` to insert one `struct pc_data` item;
- consumers call `consumer_consume()` to remove one item;
- the buffer must hold at least `BUFFER_SIZE` items;
- the buffer must be FIFO;
- producing into a full buffer must block;
- consuming from an empty buffer must block;
- concurrent producers and consumers must not corrupt the buffer.

## Important Note About This Tree

The files mentioned in the assignment were missing from this checkout, so I added local versions:

```text
src/kern/asst1/producerconsumer.c
src/kern/asst1/producerconsumer_driver.c
src/kern/asst1/producerconsumer_driver.h
```

The real grading setup may overwrite the driver and header. The main solution is in `producerconsumer.c`.

## Core Design

The implementation uses a circular FIFO buffer:

```c
static struct pc_data pc_buffer[BUFFER_SIZE];
static unsigned pc_head;
static unsigned pc_tail;
```

Meaning:

- `pc_head` points to the next item consumers remove.
- `pc_tail` points to the next empty slot producers fill.
- both wrap around using modulo arithmetic.

The synchronization primitives are semaphores:

```c
static struct semaphore *pc_empty;
static struct semaphore *pc_full;
static struct semaphore *pc_mutex;
```

Meaning:

- `pc_empty` counts empty slots. It starts at `BUFFER_SIZE`.
- `pc_full` counts full slots. It starts at `0`.
- `pc_mutex` is a binary semaphore that protects `pc_head`, `pc_tail`, and `pc_buffer`.

## File 1: `src/kern/asst1/producerconsumer.c`

### Startup

Added:

```c
void
producerconsumer_startup(void)
{
        pc_head = 0;
        pc_tail = 0;

        pc_empty = sem_create("pc empty", BUFFER_SIZE);
        pc_full = sem_create("pc full", 0);
        pc_mutex = sem_create("pc mutex", 1);
}
```

Why:

- reset the circular buffer indices before each run;
- allow exactly `BUFFER_SIZE` producers to insert without blocking;
- make consumers block until at least one item exists;
- protect shared buffer state with one mutex.

### Producer

Added:

```c
void
producer_produce(struct pc_data data)
{
        P(pc_empty);
        P(pc_mutex);

        pc_buffer[pc_tail] = data;
        pc_tail = (pc_tail + 1) % BUFFER_SIZE;

        V(pc_mutex);
        V(pc_full);
}
```

Why this works:

1. `P(pc_empty)` waits until there is at least one empty slot.
2. `P(pc_mutex)` enters the critical section.
3. The producer writes at `pc_tail`.
4. `pc_tail` advances circularly.
5. `V(pc_mutex)` leaves the critical section.
6. `V(pc_full)` wakes a consumer because one more item is available.

This prevents overwriting existing items.

### Consumer

Added:

```c
struct pc_data
consumer_consume(void)
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
```

Why this works:

1. `P(pc_full)` waits until there is at least one item.
2. `P(pc_mutex)` enters the critical section.
3. The consumer reads from `pc_head`.
4. `pc_head` advances circularly.
5. `V(pc_mutex)` leaves the critical section.
6. `V(pc_empty)` wakes a producer because one more slot is free.

This preserves FIFO order.

### Shutdown

Added:

```c
void
producerconsumer_shutdown(void)
{
        sem_destroy(pc_mutex);
        sem_destroy(pc_full);
        sem_destroy(pc_empty);

        pc_mutex = NULL;
        pc_full = NULL;
        pc_empty = NULL;
}
```

Why: clean up synchronization primitives after the test finishes.

## File 2: `src/kern/asst1/producerconsumer_driver.h`

Added a local header so this checkout can compile and run `1b`:

```c
#define BUFFER_SIZE 8
#define NPRODUCERS 2
#define NCONSUMERS 5
#define ITEMS_PER_PRODUCER 32

struct pc_data {
        unsigned producer;
        unsigned item;
};
```

Why: the assignment files were missing locally. In grading, this file may be replaced by the official one.

## File 3: `src/kern/asst1/producerconsumer_driver.c`

Added a local simulation driver that:

- starts consumer threads;
- starts producer threads;
- waits for producer threads to exit;
- waits for consumer threads to finish;
- checks that all produced items were consumed.

The driver is for local testing. The bounded-buffer solution itself is in `producerconsumer.c`.
### Driver Output Serialization

The first successful run produced unreadable mixed text because several threads called `kprintf` at the same time. That was not a bounded-buffer failure; it was only concurrent console output interleaving character-by-character.

Added a local driver print mutex:

```c
static struct semaphore *pc_print_mutex;
```

and a helper:

```c
static
void
pc_status(const char *msg)
{
        P(pc_print_mutex);
        kprintf("%s\n", msg);
        V(pc_print_mutex);
}
```

Why: each status message now prints as one complete line. This affects only local test output readability; the bounded-buffer solution remains in `producerconsumer.c`.
### Driver Completion Semaphores

The local driver originally used one completion semaphore for both producers and consumers. That made the output misleading: the main thread could count a consumer completion while it was waiting for producers.

The driver now uses two separate semaphores:

```c
static struct semaphore *pc_producer_done;
static struct semaphore *pc_consumer_done;
```

Producer threads signal:

```c
V(pc_producer_done);
```

Consumer threads signal:

```c
V(pc_consumer_done);
```

The main driver waits separately:

```c
for (i = 0; i < NPRODUCERS; i++) {
        P(pc_producer_done);
}

for (i = 0; i < NCONSUMERS; i++) {
        P(pc_consumer_done);
}
```

Why: the message `All producer threads have exited.` is now printed only after actual producer completions, not after unrelated consumer completions.

## File 4: `src/kern/include/test.h`

Added:

```c
int run_producerconsumer(int, char **);
```

Why: `menu.c` calls this function for the `1b` command.

## File 5: `src/kern/main/menu.c`

Added the menu label:

```c
"[1b]  Producer/consumer problem     ",
```

Added the command table entry:

```c
{ "1b",         run_producerconsumer },
```

Why: this lets you run the assignment test from the OS/161 kernel prompt with:

```text
1b
```

## File 6: `src/kern/conf/conf.kern`

Added:

```text
file            asst1/producerconsumer.c
file            asst1/producerconsumer_driver.c
```

Why: these files must be compiled into the kernel.

## File 7: `src/kern/compile/DUMBVM/files.mk`

Added generated build entries:

```make
SRCS+=$(KTOP)/asst1/producerconsumer.c
SRCS+=$(KTOP)/asst1/producerconsumer_driver.c
```

Why: this makes the current DUMBVM compile directory aware of the files immediately. Running `./config DUMBVM` regenerates this from `conf.kern`.

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

Interactive:

```sh
cd "/mnt/d/Depression/Academic Subjects/OS Lab/polito_os161/os161/root"
sys161 kernel
```

Then at the kernel prompt:

```text
1b
```

Direct:

```sh
cd "/mnt/d/Depression/Academic Subjects/OS Lab/polito_os161/os161/root"
sys161 kernel "1b;q"
```

## Expected Output Shape

Thread order can vary because scheduling is nondeterministic, but a correct run should look like:

```text
run_producerconsumer: starting up
Consumer started
Consumer started
Producer started
Producer started
Waiting for producer threads to exit...
Producer finished
Producer finished
All producer threads have exited.
Consumer finished normally
Consumer finished normally
Consumer finished normally
Consumer finished normally
Consumer finished normally
```

No consumer should print a boredom/error message, and the test should return to the kernel prompt.