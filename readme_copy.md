# COS417 Spring 2026 Assignment 3: Copy-on-Write

## Table of Contents

1. [Introduction](#introduction)
2. [Objectives](#objectives)
3. [Task 1: `va2pa` System Call](#task-1-va2pa-system-call)
4. [Task 2: Copy-on-Write](#task-2-copy-on-write)
    - [Background: Xv6 Memory Layout & Current Fork Behavior](#background-xv6-memory-layout--current-fork-behavior)
    - [Approach: The Copy-on-Write Technique](#approach-the-copy-on-write-technique)
    - [Task 2a: Reference Counting](#task-2a-reference-counting)
    - [Task 2b: Modifying Fork Procedure](#task-2b-modifying-fork-procedure)
    - [Task 2c: Copy-on-Write Fault Handler](#task-2c-copy-on-write-fault-handler)
    - [Task 2d: Copy-on-Write For Kernel Writes](#task-2d-copy-on-write-for-kernel-writes)

## Quick References

1. [Xv6 memory layout (Section 3.6)](https://pdos.csail.mit.edu/6.828/2025/xv6/book-riscv-rev5.pdf)
2. [39-bit Page-Based Virtual-Memory System](https://riscv.github.io/riscv-isa-manual/snapshot/privileged/#sv39)
3. [RISC-V trap codes (Table 16)](https://riscv.github.io/riscv-isa-manual/snapshot/privileged/#scause)

## Introduction

In this project, you will learn more about the memory
management subsystem in xv6 by improving the performance of existing mechanisms
and implementing Copy-on-Write (CoW) behavior that is common in other, full-fledged
operating systems.

Copy-on-Write improves the performance of forking processes by eliminating the need
to copy all process memory for each child during every `fork` syscall. Instead, CoW
allows children to share process memory with parents until absolutely necessary: a
write to memory. Only then does the OS make a copy of the memory that is being
written to for the writing process to use.

## Objectives

- To understand the xv6 memory layout
- To understand the relation between virtual and physical addresses
- To understand the use of the page fault handler for memory management
- To understand the Copy-on-Write technique for forking processes

## Task 1: `va2pa` System Call

```c
    uint64 va2pa(uint64 va);
```

As a warmup task, you will implement a new syscall called `va2pa`. Writing this
syscall requires that you familiarize yourself with the concept of virtual
memory and specifically the difference between physical and virtual addresses.

When you launch `xv6`, it has access to a
predetermined amount of physical memory that the `xv6` kernel can directly
read and write.
However, userspace processes do not directly access physical memory. Instead,
they access memory
through virtual addresses from within their (virtual) process address space.
These virtual addresses must somehow be translated into their corresponding
physical address when executing reads and writes.
The `xv6` kernel is responsible for translating between virtual to physical
addresses by creating per-process mappings. The mappings are stored in a
per-process "page table." As indicated by the name, to save on space, the
kernel manages memory and address translations at page granularity, a page
being 4 KB on RISC-V (the CPU architecture that `xv6` runs on).

A consequence of virtual memory is that user processes normally have no way of
determining what physical memory they are actually accessing during a read or
write with a virtual memory address.

In this task, your new `va2pa` system call (_virtual address
to physical address_) will allow a process to translate a virtual address
 into the underlying physical memory address, using its own page table for translation.

Hints:

- You should look at the file `kernel/vm.c` and carefully read through the `walk*`
functions that already translate virtual to physical addresses for you.

- Processes have different page tables, and can map the same virtual addresses
  to different physical addresses. Be sure that you use the page table of the
  currently running process!

- Be careful to incorporate the correct offset into the returned physical memory address
  so that you are not only returning the physical address for the surrounding page.

- If the virtual address doesn't translate to a valid physical address, return the
  maximum value for this unsigned, 64-bit integer (`0xFFFFFFFF_FFFFFFFF`).

## Task 2: Copy-on-Write

### Background: Xv6 Memory Layout & Current Fork Behavior

For the remaining parts of this assignment, you will need to understand how
`xv6` manages memory, and tweak how it handles forking a process. For more details, we
recommend that you look at the
[official `xv6` manual](https://pdos.csail.mit.edu/6.828/2025/xv6/book-riscv-rev5.pdf),
and more, specifically Section 3.6 "Process address space".

When a process forks, both the parent process and the new child process need to
continue running at the same location in the code (i.e., same program counter
register), with the same register values, and an identical copy of their
memory. However, simply copying all possible virtual addresses back-and-forth is
much too expensive: for a 64-bit Operating System like `xv6` on RISC-V, we'd
need 18.4 Exabyte (2^64 bytes) to store a single copy of our process!

Instead, `xv6` uses what's called a _sparse address space_, where only parts of
memory in an address space are actually backed by physical memory---the rest is
unmapped, and inaccessible to userspace. This drastically reduces the amount of
memory we need to copy from the parent process into the child process during a fork.

Since the process address spaces are sparse, now `xv6` only needs to copy _allocated_
physical memory from the parent process for a new forked child process. It does so
by "walking" the parent process' page table, which stores all virtual to physical
address mappings, and allocating a new page for each physical page that
the parent has mapped. It then copies the data in the newly allocated pages and adds
the pages to the child's page table so that its _virtual_ address space looks
identical to its parent's despite being backed by different _physical pages_:

```
          Parent Process                        Child Process
         +-----------------------------+       +-----------------------------+
         |                             |       |                             |
         |   Virt. Addr. 0xDEADBEEF    |       |   Virt. Addr. 0xDEADBEEF    |
         |   +---------------------+   |       |   +---------------------+   |
         |   | Hi this is a page!  |   |       |   | Hi this is a page!  |   |
         |   +---------------------+   |       |   +---------------------+   |
         |     |                    |  |       | ^   |                       |
         +-----+--------------------+--+       +-+---+-----------------------+
    Virtual    |                    |    FORK!   |   |
      Memory   |  Parent            +------------+   | Child
               |  Pagetable                          | Pagetable
               |                                     |
    +----------+-------------------------------------+------------------------------+
    |          |                                     v                              |
    |          v                                 +---------------------+            |
    |        +---------------------+             | Hi this is a page!  |            |
    |        | Hi this is a page!  |             +---------------------+            |
    |        +---------------------+             Phys. Addr. 0xF0CACC1A             |
    |        Phys. Addr. 0x8BADFOOD                                                 |
    |                                                                               |
    +-------------------------------------------------------------------------------+
     Physical Memory
```

Compared to allocating all physical memory for a single process, virtual memory
with sparse address spaces allows us to store memory for multiple processes in
physical memory at the same time! Additionally, for forked processes, we can switch
between parent and child processes without those processes having to change their
(virtual) memory addresses, since the OS now keeps record of where each processes'
physical memory resides. When switching between parent and child contexts, we just
need to switch the page table that the OS uses to translate addresses.

But, we can still make this more efficient!

### Approach: The Copy-on-Write Technique

Even though `xv6`'s pagetable-based approach avoids us having to swap all
physical memory when switching between parent and child processes, it
still requires us to create copies of physical memory for each child created.

Theoretically, this makes sense since parent and children are now independent
processes. If one process modifies its own memory, those changes must not be visible
in any other process' memory. However, this wastes a significant amount of memory,
particularly for memory that will never be modified. For instance, imagine a process
that uses 1 GB of memory forks itself several times. Each time it forks, another
GB is consumed, making it likely the system will run out of memory soon.

Copy-on-Write (CoW) is a technique that attempts to solve this issue by having forked
processes start by sharing memory.  This works when processes only read
shared memory, but becomes a problem as soon as one process wants to _write_ to
memory. Thus, copy-on-write does exactly that: it copies memory into a new physical
page only when a process tries to write to the memory.
With CoW, our previous example now looks like this:

```
          Parent Process                        Child Process
         +-----------------------------+       +-----------------------------+
         |                             |       |                             |
         |   Virt. Addr. 0xDEADBEEF    |       |   Virt. Addr. 0xDEADBEEF    |
         |   +---------------------+   |       |   +---------------------+   |
         |   | Hi this is a page!  |   |       |   | Modified page       |   |
         |   +---------------------+   |       |   +---------------------+   |
         |     |                    |  |       | ^   |                       |
         +-----+--------------------+--+       +-+---+-----------------------+
     Virtual   |                    |    FORK!   |   |
       Memory  | Parent             +------------+   | Child
               | Pagetable                           | Pagetable
               |                                     |
               |                                     |
               |  +----------------------------------+
               |  |  Before                          | After
               |  |  Write                           | Write
    +----------+--+----------------------------------+------------------------------+
    |          |  |                                  v                              |
    |          v  v                                +---------------------+          |
    |        +---------------------+               | Modified page       |          |
    |        | Hi this is a page!  |               +---------------------+          |
    |        +---------------------+               Phys. Addr. 0xF0CACC1A           |
    |        Phys. Addr. 0x8BADFOOD                                                 |
    |                                                                               |
    +-------------------------------------------------------------------------------+
     Physical Memory
```

To implement this logic, we have broken this task into 3 subtasks:

1. Add [reference counts](#task-2a-reference-counting) to physical pages to keep track
   of processes sharing this memory.

2. [Modify the fork procedure](#task-2b-modifying-fork-procedure)
 so that page tables are duplicated and all shared memory is marked read-only.

3. Implement the [copy-on-write mechanic](#task-2c-copy-on-write-fault-handler) for when
a process writes to read-only shared memory.

### Task 2a: Reference Counting

In a simple fork example, there are only two processes, the parent and the child.
However, what if the child or the parent forks itself multiple times?
With CoW, it is possible for a single physical page to be shared by an arbitrary
number of processes.

If any one of these processes were to try to free a shared page of memory, the page
should only be freed if no other processes reference the page. Therefore, you will
need to keep track of a reference count of each physical page so that a page is not freed
prematurely.

**Reference count data structure**

How should you store reference counts? You cannot store them
in the pages themselves, as those pages might be overwritten by user processes.
You might be tempted to allocate a separate page of memory specifically for storing a
reference count of the page that follows it in the physical address space,
but this would be very wasteful.

Luckily, the system `xv6` runs on systems with only limited RAM, and it knows
ahead of time how much memory it can at most use: 128 MB (see
`kernel/memlayout.h`). We can use this fact to determine the maximum number of
physical pages we need to keep track of with the following macro, defined in
`kernel/kalloc.c`:

```c
   #define MAX_PHYSPAGES ((PHYSTOP - KERNBASE) / PGSIZE)
```

The physical memory pages accessible to `xv6` start at address `KERNBASE` and go
up until and excluding address `PHYSTOP`. You can use those offsets to, for
example, derive an array index from a given physical page.

For this assignment you can assume that you have at most 255 processes
referencing a given page, which allows you to store the reference count for each
page in an 8-bit integer. You can create a static global array of `MAX_PHYSPAGES`
number of elements in the `kernel/kalloc.c`
file to keep track of all page references.

We have provided an additional macro in `kernel/kalloc.c` to help you convert a
page-aligned address into an index into a reference count array:

```c
   #define PA2INDEX(p) (((uint64)p - KERNBASE )/ PGSIZE)
```

**Modifying kernel allocator to count references**

The `xv6` file `kernel/kalloc.c` implements an allocator for physical memory pages.
Read through this file and familiarize yourself with its logic.
The `kalloc()` function allows the kernel to obtain a new, unused page
of physical memory from its free list of pages. The `kfree()` takes in an address and
frees the physical page at that address, making the page
available for subsequent calls to `kalloc()` by adding it back to the free page list.

After creating a reference count array, modify the `kalloc()` function so that
new pages have a reference count of `1`.
Be sure that your new code takes into account the case where `kalloc()` runs out of
memory to allocate.
Unlike with user-space applications, with operating systems, we generally want the system to
run as long as possible without crashing, even in out-of-memory scenarios. Make sure your
new code does not cause the system to panic at all.

Next, you should modify `kfree` to decrement
the reference count of the physical page with the passed-in address `pa`. Only if
this reference count reaches `0` should you go through with the rest of the `kfree()`
function.

Last, to prepare for CoW, you should extend the `kernel/kalloc.c` file with a new
function, `kdup()`:

```C
   void* kdup(void *pa);
```

`kdup` takes an already allocated physical page at address `pa` and increments
its reference count by one. It should return the same passed-in physical
page address (to be able to use the `new_page_ref = kdup(old_page_ref)` idiom),
or `0` in case a new reference to this page could not be created.

At this point, `xv6` should work exactly as it did at the start of this
assignment. If your kernel now has errors, then you have implemented reference counting
incorrectly. Also make sure that `kdup` works by creating
multiple references to a given page, verifying that you only deallocate them
after you've freed the last reference.

### Task 2b: Modifying Fork Procedure

With reference-counting implemented, you can modify the `fork`
procedure. For this, look at the `fork` implementation in `kernel/proc.c` and
identify the function responsible for copying process memory. Create a similar
function that - instead of copying physical memory - initializes a new process
page table with the same mappings as the parent's.

This function will also need to a) increment the reference count for each referenced
physical page and b) make sure that all parent and child memory is
marked as read-only in their respective page table entries (PTEs).
This will cause any attempted write by either process to
trigger a trap into the kernel. Look at
`kernel/riscv.h` to learn how you can access and modify the various PTE
flags. For more information about `xv6`'s page table system, you can read about
RISC-V's `Sv39` [39-bit Page-Based Virtual-Memory System](https://riscv.github.io/riscv-isa-manual/snapshot/privileged/#sv39).

**One complication**

As mentioned above, you need to mark pages in both the parent and child
process as read-only. Some pages are already read-only before issuing
`fork`, however, and when the program tries to write to them it is expected to
trigger a page fault and
crash. That raises the question: how can you distinguish a _good_ Copy-on-Write
fault from a _bad_ fault, because the program tried to write to a page that is
supposed to be read-only?

It turns out, the architects behind RISC-V prepared for this scenario.
When you look at the layout of Page Table Entries (PTEs) on RISC-V, you will
find the following fields, which correspond to the `PTE_` defines in `riscv.h`:

| Bit(s) | 63 | 62-61 | 60-54    | 53-10 | 9-8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|--------|----|-------|----------|-------|-----|---|---|---|---|---|---|---|---|
| Field  | N  | PBMT  | Reserved | PPN   | RSW | D | A | G | U | X | W | R | V |

Most fields can be ignored, but one is particularly
interesting: RSW. This field is reserved for "supervisor use", which in our case
means the `xv6` kernel! This is an excellent place to _remember_ whether a page
used to be writeable (what you might call a "shadow bit") before you mark it as
read-only during a fork.
You can then use this field to distinguish CoW pages from read-only ones.

### Task 2c: Copy-on-Write Fault Handler

When you try to run your `xv6` kernel after completing the previous task, you
will find that it probably throws some nasty-looking errors when running any
programs, a possible example being:

```sh
   $ cat
   usertrap(): unexpected scause 0xf pid=3
               sepc=0x870 stval=0x4f88
   usertrap(): unexpected scause 0xf pid=2
               sepc=0x2 stval=0x4f88
   usertrap(): unexpected scause 0xf pid=1
               sepc=0x7c2 stval=0x3f68
   panic: init exiting
```

This is because we have yet to handle when forked process write to the memory
we previously marked as read-only.

While it is expected that we trap into the kernel when a process tries to write
to a CoW page, we don't want this to crash the system. Instead, we should check
if this is a CoW write fault and perform the copy-on-write.

To do so, we need to modify the trap handler to actually allocate new physical
pages on such a "CoW fault".
When we search (with a tool like `grep`) for where the above error is generated,
we find the function `usertrap()` in `kernel/trap.c`. You can see that this
function already calls into a handler for traps on
lazily-allocated pages, called `vmfault()`. But this function is different than
the function we need to create: `vmfault()` maps _new_, _fresh_ pages into a
process when it accesses a legal page that has never been accessed before.

You should implement a similar function, called `cowfault()`, and invoke it from
within the `usertrap()` handler only when a write fault code is encountered
(see [RISCV trap codes Table 37](https://riscv.github.io/riscv-isa-manual/snapshot/privileged/#scause)).
This function should do the following:

- Create a copy of the original page
- Modify the page table mapping in the current process's page table
    so that the same virtual address references the new page's physical address.
- Restore the shadow bit to the PTE flags write bit, allowing the process to
write to the page
- Decrease the reference count for the original page

Note that this algorithm implies that a physical page could be both a CoW page and
also only referenced by one remaining process. This will still cause a page fault
when this _only_ process writes to the page (a good exercise is thinking about
why it would be difficult to avoid this situation).

After this change, you should be able to run programs in `xv6`
again. Try executing basic commmands like `ls`, `cat README`, and
`forktest`. These should not cause any visible errors.

### Task 2d: Copy-on-Write For Kernel Writes

Although `xv6` appears to work after completing Task 2c, there is still a case
we have not handled. You can see the issue if you run the `usertests forktest`
command:

```
$ usertests forktest
usertests starting
test forktest: OK
FAILED -- lost some free pages 32383 (out of 32390)
```

This error is misleading, as it indicates a possible memory leak, but
is caused by the following sequence of events:
1. `usertests` internally runs `fork()`, which marks all of its writeable pages
   as read-only for both the parent and the child.
2. The parent then runs `wait(int *wstatus)`. This will instruct the kernel to
   wait for a child to exit, and write its status to the supplied `wstatus`
   pointer.
3. The kernel will wait for the child to exit.
4. The child exits. The kernel will now attempt to write its status to the
   parent, before cleaning up the child's resources, using the `copyout()` function.
5. However, `copyout` will see that the page to write the status to is
   read-only---it's a CoW-page! It will refuse to do this write, and will return
   immediately to the parent, even before the child's resources are cleaned up.

So, the root cause of the issue is that we modified our kernel to handle
_processes_ writing to a CoW-page (with our `cowfault()` handler), but we do not
handle the case where the kernel itself writes to CoW-pages!

Luckily, the fix should be relatively small.
To complete this assignment, you need to change `copyout()` so that it ensures
any CoW-pages are writeable by the kernel on behalf of the process.
You should be able to call your `cowfault()` function directly for this purpose
(see how `copyout()` already calls the `vmfault()` handler to handle missing pages).

However, the following order of checks/operations must be respected! As implemented,
`copyout()` calls `walkaddr()` to check if the destination virtual address is mapped
in the process, but this will return a read-only page for shared CoW-pages.
Therefore, you must call `cowfault()` before the call to `walkaddr` so
that you handle copying the CoW pages so that `copyout` can then write to the _new_
copied and writeable page.

Once this is implemented, the `usertests forktest` command should no longer
report any errors.

## Testing

Since copy-on-write is a performance optimization and not a functional change,
you should be able to load into your `xv6` system and run the following basic
commands without error: `ls`, `cat README`, and `forktest`.

`xv6` includes an extensive set of regression tests with the `usertests`
program. When run without any arguments, this program executes many different
tests, some of which deliberately cause error messages to be printed. This is
fine and expected; it should show `ALL TESTS PASSED` at the end, which indicates
that your changes to the OS did not break any functionality.

In case you have any failures, try debugging by running individual failed tests
with `usertests <failed_test>`. You can find the full list of tests by looking
at the `usertests.c` source code, on line 2751.

## Submission/Deliverables

You must do three things to turn in this assignment.

1) Fill in your name, netid, and AI disclosure statement at the top of the
`vm.c` file.
2) Submit the `xv6` directory (which should contain all of your code changes) using the `handin` command.
Usage: `handin cow [path to the xv6 directory]`
3) Submit a `git diff` file with your code changes to Gradescope. This file should be created automatically by the `handin` script; it will print out the absolute path at which you can find this file.
Upload this file to Gradescope under the A3 CoW assignment by first copying
the file to your local computer:

```sh
scp <netid>@courselab.cs.princeton.edu:<path printed by handin> .
```

The period at the end of this command means copy the file and store it in the
current directory. You can change the destination period to whatever path you wish.

**Note that the `handin` command from Step 2 of the above process will
be used as the official, archival version of your handed-in
assignment! It is *most* important that this step completes
successfully.**
