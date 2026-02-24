// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

// Returns index number of page-aligned address
#define PA2INDEX(pa) (((uint64)pa - KERNBASE )/ PGSIZE)
#define MAXPAGES ((PHYSTOP-KERNBASE) / PGSIZE)
#define MAXREFERENCES 255

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

static struct {
  struct spinlock lock;
  struct run *freelist;
  uint8 page_refcount[MAXPAGES];
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&kmem.lock);

  // Only decrement if there are references 
  if(kmem.page_refcount[PA2INDEX(pa)] > 0){
    --kmem.page_refcount[PA2INDEX(pa)];
  }

  // Only free if no references left
  if(kmem.page_refcount[PA2INDEX(pa)] == 0){
    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    r = (struct run*)pa;

    r->next = kmem.freelist;
    kmem.freelist = r;
  }
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;

  if(r) {
    kmem.page_refcount[PA2INDEX(r)] = 1;
    memset((char*)r, 5, PGSIZE); // fill with junk
  }
  release(&kmem.lock);
  return (void*)r;
}

// Increments an existing physical page reference count by one.
// Returns the same physical page address. 0 in case a new reference
// to this page could not be created.
void*
kdup(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    return 0;

  acquire(&kmem.lock);
  int idx = PA2INDEX(pa);
  // Page already freed or at max references
  if (kmem.page_refcount[idx] <= 0 ||
      kmem.page_refcount[idx] == MAXREFERENCES) {
    release(&kmem.lock);
    return 0;
  }
  ++kmem.page_refcount[PA2INDEX(pa)];
  release(&kmem.lock);
  return pa;
}