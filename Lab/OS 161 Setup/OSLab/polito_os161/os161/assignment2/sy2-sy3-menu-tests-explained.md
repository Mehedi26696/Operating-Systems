# The `sy2` and `sy3` Menu Commands — How They Work (In Detail)

This document explains, from the ground up, what happens when you type **`sy2`**
or **`sy3`** at the OS/161 kernel menu prompt:

```
OS/161 kernel [? for menu]: sy2
OS/161 kernel [? for menu]: sy3
```

- **`sy2`** runs the **lock test** (`locktest`) — it stress-tests your
  `struct lock` implementation.
- **`sy3`** runs the **condition-variable test** (`cvtest`) — it stress-tests
  your `struct cv` (condition variable) + lock implementation.

Both are entries in the **Tests menu** (`?t`) and are marked `(1)` meaning
"*these tests will fail until you finish the synchronization assignment*".

Everything below traces the exact code path, file by file.

---

## 1. The files involved

| File | Role |
|------|------|
| [src/kern/main/menu.c](../src/kern/main/menu.c) | The in-kernel menu: reads your input, looks up the command, calls the test function, times it. |
| [src/kern/test/synchtest.c](../src/kern/test/synchtest.c) | The actual test code: `locktest()` (sy2) and `cvtest()` (sy3). |
| [src/kern/include/test.h](../src/kern/include/test.h) | Declares `locktest`, `cvtest`, etc. so `menu.c` can call them. |
| `src/kern/synch/synch.c` | Your `lock_*` and `cv_*` implementations that the tests exercise. |

---

## 2. How the menu finds and runs `sy2` / `sy3`

### 2.1 The command table

In [menu.c](../src/kern/main/menu.c), every menu command is one row of a table
that maps a **string name** to a **function pointer**:

```c
static struct {
	const char *name;
	int (*func)(int nargs, char **args);
} cmdtable[] = {
	...
	/* synchronization assignment tests */
	{ "sy2",	locktest },
	{ "sy3",	cvtest },
	{ "sy4",	cvtest2 },
	...
	{ NULL, NULL }
};
```

So:
- `"sy2"` → the function `locktest`
- `"sy3"` → the function `cvtest`

`locktest` and `cvtest` themselves live in `synchtest.c`. The table only knows
them because `test.h` declares them (`int locktest(int, char **);` etc.).

### 2.2 The menu listing (what you see with `?t`)

The Tests menu text is a separate array of strings, `testmenu[]`, printed by
`cmd_testmenu`:

```c
static const char *testmenu[] = {
	...
	"[sy1] Semaphore test                ",
	"[sy2] Lock test             (1)     ",
	"[sy3] CV test               (1)     ",
	"[sy4] CV test #2            (1)     ",
	...
	NULL
};
```

This array is **only for display**. The real wiring is the `cmdtable` above.
(That's why the label and the dispatch are kept consistent by hand.)

### 2.3 Dispatch: from keystrokes to a function call

The main loop is at the bottom of `menu.c`:

```c
void
menu(char *args)
{
	char buf[64];

	menu_execute(args, 1);          /* run any boot-time kernel args first */

	while (1) {
		kprintf("OS/161 kernel [? for menu]: ");
		kgets(buf, sizeof(buf));    /* read a line from the console      */
		menu_execute(buf, 0);       /* execute it                        */
	}
}
```

`menu_execute` splits the line on `;` (multiple commands) and hands each command
to `cmd_dispatch`, which does the real work:

```c
static int
cmd_dispatch(char *cmd)
{
	struct timespec before, after, duration;
	char *args[MAXMENUARGS];
	int nargs = 0;
	char *word, *context;
	int i, result;

	/* 1. Tokenize the line into words on spaces/tabs. */
	for (word = strtok_r(cmd, " \t", &context);
	     word != NULL;
	     word = strtok_r(NULL, " \t", &context)) {
		if (nargs >= MAXMENUARGS) {
			kprintf("Command line has too many words\n");
			return E2BIG;
		}
		args[nargs++] = word;
	}
	if (nargs == 0) {
		return 0;               /* empty line: do nothing */
	}

	/* 2. Linear search of the command table for args[0]. */
	for (i = 0; cmdtable[i].name; i++) {
		if (*cmdtable[i].name && !strcmp(args[0], cmdtable[i].name)) {
			KASSERT(cmdtable[i].func != NULL);

			/* 3. Time the command. */
			gettime(&before);
			result = cmdtable[i].func(nargs, args);   /* <-- calls locktest / cvtest */
			gettime(&after);
			timespec_sub(&after, &before, &duration);

			kprintf("Operation took %llu.%09lu seconds\n",
				(unsigned long long) duration.tv_sec,
				(unsigned long) duration.tv_nsec);
			return result;
		}
	}

	kprintf("%s: Command not found\n", args[0]);
	return EINVAL;
}
```

Step by step for `sy2`:

1. The line `"sy2"` is tokenized: `args[0] = "sy2"`, `nargs = 1`.
2. The loop compares `"sy2"` against each `cmdtable[i].name` with `strcmp`; it
   matches the row `{ "sy2", locktest }`.
3. It records the start time, calls `locktest(1, {"sy2"})`, records the end time,
   and prints how long it took.

`sy3` is identical except it matches `{ "sy3", cvtest }` and calls `cvtest`.

> **Note:** the test functions ignore their `nargs`/`args` (they cast them to
> `(void)`), so typing extra words after `sy2`/`sy3` has no effect.

---

## 3. Shared test scaffolding (`synchtest.c`)

Both tests share a set of global objects and an initializer.

```c
#define NSEMLOOPS     63
#define NLOCKLOOPS    120
#define NCVLOOPS      5
#define NTHREADS      32

static volatile unsigned long testval1;
static volatile unsigned long testval2;
static volatile unsigned long testval3;
static struct semaphore *testsem;
static struct lock *testlock;
static struct cv *testcv;
static struct semaphore *donesem;
```

- `NTHREADS = 32` — every test forks 32 worker threads.
- `testlock`, `testcv` — the objects **under test**.
- `donesem` — a semaphore used as a **"join" / barrier**: each worker calls
  `V(donesem)` when finished, and the main test thread calls `P(donesem)` 32
  times so it can wait for all workers to complete before printing "done".
- `testval1/2/3` — shared variables used to *detect* whether mutual exclusion
  actually held (explained below).

`inititems()` lazily creates all four objects the first time any test runs, and
reuses them afterward (so you can run `sy1`/`sy2`/`sy3` repeatedly):

```c
static void
inititems(void)
{
	if (testsem == NULL) {
		testsem = sem_create("testsem", 2);
		if (testsem == NULL) panic("synchtest: sem_create failed\n");
	}
	if (testlock == NULL) {
		testlock = lock_create("testlock");
		if (testlock == NULL) panic("synchtest: lock_create failed\n");
	}
	if (testcv == NULL) {
		testcv = cv_create("testlock");
		if (testcv == NULL) panic("synchtest: cv_create failed\n");
	}
	if (donesem == NULL) {
		donesem = sem_create("donesem", 0);
		if (donesem == NULL) panic("synchtest: sem_create failed\n");
	}
}
```

`donesem` is created with an initial count of **0**. That is what makes it work
as a barrier: `P(donesem)` blocks until some worker has done a matching
`V(donesem)`.

---

## 4. `sy2` — the Lock Test (`locktest`) in detail

### 4.1 The idea

A lock's whole job is **mutual exclusion**: only one thread may hold `testlock`
at a time. The test verifies this *indirectly* by having every thread, while
holding the lock, write a set of related values into shared globals and then
immediately re-check that those values are still consistent with each other. If
the lock is broken (two threads inside the critical section at once), one
thread's writes will be interleaved with another's and the consistency checks
will fail.

### 4.2 The entry point

```c
int
locktest(int nargs, char **args)
{
	int i, result;

	(void)nargs;
	(void)args;

	inititems();
	kprintf("Starting lock test...\n");

	/* Fork NTHREADS (=32) worker threads. */
	for (i = 0; i < NTHREADS; i++) {
		result = thread_fork("synchtest", NULL, locktestthread, NULL, i);
		if (result) {
			panic("locktest: thread_fork failed: %s\n",
			      strerror(result));
		}
	}

	/* Wait for all 32 workers to finish. */
	for (i = 0; i < NTHREADS; i++) {
		P(donesem);
	}

	kprintf("Lock test done.\n");
	return 0;
}
```

- `thread_fork(name, proc, func, data, num)` creates a new kernel thread that
  starts in `locktestthread(data=NULL, num=i)`. The 32 threads are numbered
  `0..31` via the `num` argument.
- After forking, the main thread calls `P(donesem)` 32 times. Because `donesem`
  started at 0, each `P` blocks until a worker posts a `V`. So this loop does
  **not** return until all 32 workers have finished — a clean join.

### 4.3 The worker thread

```c
#define NLOCKLOOPS 120

static void
locktestthread(void *junk, unsigned long num)
{
	int i;
	(void)junk;

	for (i = 0; i < NLOCKLOOPS; i++) {       /* 120 iterations each */
		lock_acquire(testlock);          /* ENTER critical section */

		testval1 = num;                  /* write related values   */
		testval2 = num*num;
		testval3 = num%3;

		/* Now re-verify they are all mutually consistent.        */
		if (testval2 != testval1*testval1)      fail(num, "testval2/testval1");
		if (testval2%3 != (testval3*testval3)%3) fail(num, "testval2/testval3");
		if (testval3 != testval1%3)             fail(num, "testval3/testval1");
		if (testval1 != num)                    fail(num, "testval1/num");
		if (testval2 != num*num)                fail(num, "testval2/num");
		if (testval3 != num%3)                  fail(num, "testval3/num");

		lock_release(testlock);          /* LEAVE critical section */
	}
	V(donesem);                              /* signal "I'm done"     */
}
```

**Why this detects broken locks:** inside the critical section the invariants
`testval2 == testval1*testval1` and `testval3 == testval1%3` etc. must hold
*for this thread's own `num`*. If mutual exclusion works, no other thread can
touch `testval1/2/3` between the writes and the checks, so every check passes.
If two threads are ever inside at once, thread A writes `testval1 = A`, thread B
overwrites `testval1 = B`, and A's check `testval1 != num` (A) fires. Each
worker loops 120 times to make such a race overwhelmingly likely if the lock is
wrong.

### 4.4 The failure path

```c
static void
fail(unsigned long num, const char *msg)
{
	kprintf("thread %lu: Mismatch on %s\n", num, msg);
	kprintf("Test failed\n");

	lock_release(testlock);   /* we still hold it — release so we don't wedge */
	V(donesem);               /* still post done, or the main join hangs      */
	thread_exit();            /* kill this worker                             */
}
```

Note it **releases the lock** and **posts `donesem`** before exiting — otherwise
the main thread's `P(donesem)` join loop would hang forever and you'd never see
the failure.

### 4.5 Expected output

```
Starting lock test...
Lock test done.
Operation took X.XXXXXXXXX seconds
```

No "Mismatch"/"Test failed" lines means the lock provided correct mutual
exclusion. Until you implement `struct lock` correctly, you will instead see
mismatch messages (hence the `(1)` marker in the menu).

---

## 5. `sy3` — the Condition Variable Test (`cvtest`) in detail

### 5.1 The idea

A condition variable lets threads **wait for a condition and be woken in an
orderly way**, always used together with a lock. This test makes 32 threads
take strict turns: thread 31 goes first, then 30, then 29, … down to 0. Each
thread waits on the CV until it is "its turn" (`testval1 == num`), prints its
number, decrements the turn counter, and broadcasts to wake the others. If the
CV works, the numbers print in **reverse order** with no missed wakeups.

### 5.2 The entry point

```c
int
cvtest(int nargs, char **args)
{
	int i, result;

	(void)nargs;
	(void)args;

	inititems();
	kprintf("Starting CV test...\n");
	kprintf("Threads should print out in reverse order.\n");

	testval1 = NTHREADS - 1;                 /* first turn belongs to thread 31 */

	for (i = 0; i < NTHREADS; i++) {
		result = thread_fork("synchtest", NULL, cvtestthread, NULL, i);
		if (result) {
			panic("cvtest: thread_fork failed: %s\n", strerror(result));
		}
	}
	for (i = 0; i < NTHREADS; i++) {
		P(donesem);                      /* join all 32 */
	}

	kprintf("CV test done\n");
	return 0;
}
```

Here `testval1` is repurposed as the **"whose turn is it" counter**, initialized
to 31.

### 5.3 The worker thread

```c
#define NCVLOOPS 5

static void
cvtestthread(void *junk, unsigned long num)
{
	int i;
	volatile int j;
	struct timespec ts1, ts2;
	(void)junk;

	for (i = 0; i < NCVLOOPS; i++) {
		lock_acquire(testlock);

		/* Wait until it is this thread's turn. */
		while (testval1 != num) {
			gettime(&ts1);
			cv_wait(testcv, testlock);   /* atomically release lock + sleep */
			gettime(&ts2);

			/* ts2 = ts2 - ts1 (how long we actually slept)                */
			timespec_sub(&ts2, &ts1, &ts2);

			/* Sanity: a real cv_wait must actually block, not spin.       */
			if (ts2.tv_sec == 0 && ts2.tv_nsec < 40*2000) {
				kprintf("cv_wait took only %u ns\n", ts2.tv_nsec);
				kprintf("That's too fast... you must be busy-looping\n");
				V(donesem);
				thread_exit();
			}
		}

		kprintf("Thread %lu\n", num);

		/* Hand the turn to the next-lower thread (wrapping mod 32). */
		testval1 = (testval1 + NTHREADS - 1) % NTHREADS;

		/* Burn a little time so the wait above is measurable. */
		for (j = 0; j < 3000; j++);

		cv_broadcast(testcv, testlock);      /* wake everyone to re-check */
		lock_release(testlock);
	}
	V(donesem);
}
```

Key mechanics:

1. **`while (testval1 != num)` — not `if`.** A thread woken by a broadcast must
   re-check the condition, because *all* waiters wake but only one has the
   matching turn. This is the standard "wait in a loop" discipline.
2. **`cv_wait(testcv, testlock)`** must **atomically** release `testlock` and put
   the thread to sleep, then re-acquire `testlock` before returning. If your
   implementation isn't atomic, a wakeup can slip through between the check and
   the sleep (the "lost wakeup" bug), and threads deadlock.
3. **The timing check** guards against a fake `cv_wait` that just spins/returns
   immediately: it measures wall-clock time spent in `cv_wait` and fails the test
   if it was implausibly short (< 80 µs on the simulated 25 MHz CPU), because a
   real sleep-and-context-switch takes longer than a busy spin.
4. **`cv_broadcast`** wakes all waiters after the turn counter is decremented;
   exactly one of them will now satisfy `testval1 == num` and proceed.

### 5.4 Expected output

```
Starting CV test...
Threads should print out in reverse order.
Thread 31
Thread 30
Thread 29
...
Thread 0
Thread 31          <- because NCVLOOPS = 5, the whole 31..0 sweep repeats
...
CV test done
Operation took X.XXXXXXXXX seconds
```

Because each thread loops `NCVLOOPS = 5` times and the counter wraps mod 32, the
reverse-order sweep from 31 down to 0 happens repeatedly. **Correct = strictly
descending numbers.** Out-of-order numbers, a hang, or the "busy-looping"
message all indicate a bug in `cv_wait`/`cv_signal`/`cv_broadcast` or the lock.

---

## 6. `sy4` for contrast (`cvtest2`)

`sy4` maps to `cvtest2`, a harder test that specifically hunts for **lost
wakeups**: it rotates through 250 lock/CV pairs with one `sleepthread` and one
`wakethread`, using `V(gatesem)`/`P(gatesem)` to guarantee the sleeper is inside
`cv_wait` before the waker signals. If a single wakeup is ever missed, the
sleeper doesn't loop enough times and the test wedges. It's listed here only so
you can see `sy2`/`sy3`/`sy4` form a progression of increasingly strict checks
on the same primitives.

---

## 7. One-paragraph summary

Typing `sy2` or `sy3` at the prompt makes `menu()` → `menu_execute()` →
`cmd_dispatch()` tokenize the line, `strcmp` it against `cmdtable`, and call the
matching function pointer — `locktest` for `sy2`, `cvtest` for `sy3` — timing the
call. `locktest` forks 32 threads that each grab `testlock`, scribble
inter-dependent values into shared globals, and re-check them, proving mutual
exclusion. `cvtest` forks 32 threads that take strict reverse-order turns using
`cv_wait`/`cv_broadcast` on `testcv`+`testlock`, proving correct, atomic
condition-variable signalling. Both use a `donesem` semaphore (initial count 0)
as a barrier so the menu thread waits for every worker before printing "done".
They are marked `(1)` because they only pass once your `lock` and `cv`
implementations in `synch.c` are correct.
