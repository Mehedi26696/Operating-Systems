# `math.c` Explanation

`math.c` is an OS/161 ASST1 concurrency test. It creates multiple kernel
threads that all increment one shared counter. The purpose is to show why
synchronization is needed when several threads access shared data.

This test appears in the OS/161 kernel menu as:

```text
1a  Concurrent math problem
```

## Constants

```c
#define NADDERS 10
#define ADD_LIMIT 10000
```

`NADDERS` is the number of worker threads created by the test.

`ADD_LIMIT` is the maximum value of the shared counter. Once the counter reaches
`10000`, all worker threads stop.

## Shared State

```c
static volatile unsigned math_counter;
static unsigned math_stats[NADDERS];
static struct semaphore *math_mutex;
static struct semaphore *math_done;
```

`math_counter` is the shared counter incremented by all adder threads.

`math_stats[i]` stores how many increments adder thread `i` performed.

`math_mutex` is a semaphore used like a lock. It protects `math_counter` and
`math_stats` so only one thread updates them at a time.

`math_done` is a semaphore used to let the main test wait until all adder
threads have finished.

## `P()` and `V()`

OS/161 uses semaphore operations named `P()` and `V()`.

```c
P(sem);
```

means wait for or acquire the semaphore. If the semaphore is not available, the
current thread sleeps.

```c
V(sem);
```

means signal or release the semaphore. If another thread is waiting, one waiting
thread may be woken up.

In this file, `math_mutex` is created with initial value `1`, so it behaves like
a mutex:

```c
math_mutex = sem_create("math mutex", 1);
```

Only one thread can pass `P(math_mutex)` at a time. The thread must later call
`V(math_mutex)` to let another thread enter.

`math_done` is created with initial value `0`:

```c
math_done = sem_create("math done", 0);
```

This makes the main thread block until adder threads signal completion with
`V(math_done)`.

## The `adder` Function

```c
static void adder(void *junk, unsigned long num)
```

Each worker thread runs `adder`. The `num` argument is the thread number from
`0` to `9`.

```c
(void)junk;
```

The `junk` argument is unused. This line avoids a compiler warning.

```c
KASSERT(num < NADDERS);
```

This checks that the thread number is valid.

The main loop repeatedly tries to increment the shared counter:

```c
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
```

The important part is between `P(math_mutex)` and `V(math_mutex)`. This is the
critical section.

Inside the critical section, the thread:

1. Checks whether `math_counter` has reached `ADD_LIMIT`.
2. Increments the shared `math_counter`.
3. Increments its own entry in `math_stats`.

The mutex is needed because `math_counter++` is not an atomic operation. Without
the mutex, two threads could read the same old value and overwrite each other's
updates.

After releasing the mutex, the thread calls:

```c
thread_yield();
```

This voluntarily gives another thread a chance to run. It increases thread
interleaving, which makes synchronization problems easier to expose.

When the thread is done, it runs:

```c
V(math_done);
thread_exit();
```

`V(math_done)` signals that one adder thread has finished. `thread_exit()` ends
the current kernel thread.

## The `math` Function

```c
int math(int nargs, char **args)
```

This is the entry point called from the OS/161 menu.

The arguments are unused:

```c
(void)nargs;
(void)args;
```

The function first resets the shared state:

```c
math_counter = 0;
for (i = 0; i < NADDERS; i++) {
    math_stats[i] = 0;
}
```

Then it creates the semaphores:

```c
math_mutex = sem_create("math mutex", 1);
math_done = sem_create("math done", 0);
```

If a semaphore cannot be created, the function returns `ENOMEM`, meaning there
was not enough memory.

Next it creates the adder threads:

```c
for (i = 0; i < NADDERS; i++) {
    result = thread_fork("adder", NULL, adder, NULL, i);
    if (result) {
        panic("math: thread_fork failed: %s\n", strerror(result));
    }
}
```

Each thread runs `adder`, and each one receives a different thread number `i`.

Then the main thread waits for all adder threads:

```c
for (i = 0; i < NADDERS; i++) {
    P(math_done);
}
```

Because there are 10 adder threads, the main thread waits for 10 completion
signals.

Finally, the function prints the results:

```c
kprintf("Adder threads performed %u adds\n", math_counter);
```

It also prints how many increments each individual thread performed and sums
those values into `total`.

At the end, it destroys the semaphores:

```c
sem_destroy(math_done);
sem_destroy(math_mutex);
math_done = NULL;
math_mutex = NULL;
```

Then it returns `0`, meaning the test completed successfully.

## Expected Result

The final value of `math_counter` should be:

```text
10000
```

The sum of all values in `math_stats` should also be:

```text
10000
```

The exact distribution between the 10 adder threads may be different each time.
That is normal, because the scheduler decides which thread runs when.

## Why This File Matters

This file demonstrates:

1. Creating kernel threads with `thread_fork`.
2. Protecting shared data with a semaphore used as a mutex.
3. Waiting for worker threads using a completion semaphore.
4. How thread scheduling affects the amount of work each thread performs.

The main lesson is that shared data must be protected. Without `math_mutex`, the
counter updates could race and the final result could be wrong.
