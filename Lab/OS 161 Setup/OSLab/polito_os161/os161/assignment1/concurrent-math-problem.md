# Concurrent Mathematics Problem Solution

## Goal

The `1a` test starts several kernel threads that increment one shared counter until the counter reaches `10000`.

Without synchronization, this operation is not atomic:

```c
counter = counter + 1;
```

It is really a read, an add, and a write. Two threads can read the same old value and both write back the same new value, causing lost increments and incorrect output such as:

```text
345 + 1 = 352
```

The fix is to protect the shared counter and the per-thread statistics with mutual exclusion.

## Important Note About This Tree

The requested path `src/kern/asst1/math.c` did not exist in this checkout, so I added it and wired it into the kernel menu as command `1a`.

## Files Changed

### 1. `src/kern/asst1/math.c`

Added the concurrent math test implementation.

Important shared state:

```c
#define NADDERS 10
#define ADD_LIMIT 10000

static volatile unsigned math_counter;
static unsigned math_stats[NADDERS];
static struct semaphore *math_mutex;
static struct semaphore *math_done;
```

Why:

- `math_counter` is the shared value all adder threads increment.
- `math_stats[]` records how many increments each thread performed.
- `math_mutex` is a binary semaphore used as a mutex.
- `math_done` is a counting semaphore used by the main `math()` thread to wait until all adders finish.

The critical section is:

```c
P(math_mutex);
if (math_counter >= ADD_LIMIT) {
        V(math_mutex);
        break;
}

math_counter++;
math_stats[num]++;
V(math_mutex);
```

Why this works:

- Only one adder thread can enter this block at a time.
- The limit check and increment happen atomically with respect to other adders.
- The per-thread statistic is updated together with the shared counter, so the final statistics match the final counter.

Each adder signals completion before exiting:

```c
V(math_done);
thread_exit();
```

The main thread waits for every adder thread:

```c
for (i = 0; i < NADDERS; i++) {
        P(math_done);
}
```

Why: this prevents `math()` from printing or destroying semaphores before all worker threads are finished.

After all workers finish, `math()` prints both the shared counter and the sum of the per-thread statistics:

```c
kprintf("Adder threads performed %u adds\n", math_counter);

total = 0;
for (i = 0; i < NADDERS; i++) {
        kprintf("Adder %d performed %u increments.\n", i, math_stats[i]);
        total += math_stats[i];
}
kprintf("The adders performed %u increments overall\n", total);
```

Expected invariant:

```text
math_counter == sum(math_stats[0..9]) == 10000
```

### 2. `src/kern/include/test.h`

Added the prototype:

```c
int math(int, char **);
```

Why: `menu.c` calls `math`, so the test header needs to declare it.

### 3. `src/kern/main/menu.c`

Added the `1a` test menu label:

```c
"[1a]  Concurrent math problem       ",
```

Added the command table entry:

```c
{ "1a",         math },
```

Why: this lets you run the test from the OS/161 kernel prompt with:

```text
1a
```

or directly from System/161 with:

```sh
sys161 kernel "1a;q"
```

### 4. `src/kern/conf/conf.kern`

Added:

```text
file            asst1/math.c
```

Why: the kernel config system must know that `math.c` should be compiled into the kernel.

### 5. `src/kern/compile/DUMBVM/files.mk`

Added the generated build entry too:

```make
SRCS+=$(KTOP)/asst1/math.c
```

Why: this lets the current DUMBVM compile directory see the new source immediately. Running `./config DUMBVM` will regenerate this file from `conf.kern`.

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

Interactive test:

```sh
cd "/mnt/d/Depression/Academic Subjects/OS Lab/polito_os161/os161/root"
../tools/bin/sys161 kernel-DUMBVM
```

At the kernel prompt:

```text
1a
```

Direct test:

```sh
cd "/mnt/d/Depression/Academic Subjects/OS Lab/polito_os161/os161/root"
../tools/bin/sys161 kernel-DUMBVM "1a;q"
```

## Expected Output Shape

The exact per-thread numbers will vary because scheduling varies, but these two totals must match:

```text
Starting 10 adder threads
Adder threads performed 10000 adds
Adder 0 performed ... increments.
Adder 1 performed ... increments.
...
Adder 9 performed ... increments.
The adders performed 10000 increments overall
```

If both totals are `10000`, the mutual exclusion problem is solved.