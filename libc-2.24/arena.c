/* Malloc implementation for multiple threads without lock contention.
   Copyright (C) 2001-2016 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Wolfram Gloger <wg@malloc.de>, 2001.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public License as
   published by the Free Software Foundation; either version 2.1 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If
   not, see <http://www.gnu.org/licenses/>.  */

#include <stdbool.h>

/* Compile-time constants.  */

#define mutex_init(M) pthread_mutex_init(M, NULL)
#define mutex_lock(M) pthread_mutex_lock(M)
#define mutex_unlock(M) pthread_mutex_unlock(M)

#define HEAP_MIN_SIZE (32 * 1024)
#ifndef HEAP_MAX_SIZE
# ifdef DEFAULT_MMAP_THRESHOLD_MAX
#  define HEAP_MAX_SIZE (2 * DEFAULT_MMAP_THRESHOLD_MAX)
# else
#  define HEAP_MAX_SIZE (1024 * 1024) /* must be a power of two */
# endif
#endif

/* HEAP_MIN_SIZE and HEAP_MAX_SIZE limit the size of mmap()ed heaps
   that are dynamically created for multi-threaded programs.  The
   maximum size must be a power of two, for fast determination of
   which heap belongs to a chunk.  It should be much larger than the
   mmap threshold, so that requests with a size just below that
   threshold can be fulfilled without creating too many heaps.  */

/***************************************************************************/

#define top(ar_ptr) ((ar_ptr)->top)

/* A heap is a single contiguous memory region holding (coalesceable)
   malloc_chunks.  It is allocated with mmap() and always starts at an
   address aligned to HEAP_MAX_SIZE.  */

typedef struct _heap_info
{
  mstate ar_ptr; /* Arena for this heap. */
  struct _heap_info *prev; /* Previous heap. */
  size_t size;   /* Current size in bytes. */
  size_t mprotect_size; /* Size in bytes that has been mprotected
                           PROT_READ|PROT_WRITE.  */
  /* Make sure the following data is properly aligned, particularly
     that sizeof (heap_info) + 2 * SIZE_SZ is a multiple of
     MALLOC_ALIGNMENT. */
  char pad[-6 * SIZE_SZ & MALLOC_ALIGN_MASK];
} heap_info;

/* Get a compile-time error if the heap_info padding is not correct
   to make alignment work as expected in sYSMALLOc.  */
extern int sanity_check_heap_info_alignment[(sizeof (heap_info)
                                             + 2 * SIZE_SZ) % MALLOC_ALIGNMENT
                                            ? -1 : 1];

/* Thread specific data.  */

static __thread mstate thread_arena attribute_tls_model_ie;

/* Arena free list.  free_list_lock synchronizes access to the
   free_list variable below, and the next_free and attached_threads
   members of struct malloc_state objects.  No other locks must be
   acquired after free_list_lock has been acquired.  */

static mutex_t free_list_lock = _LIBC_LOCK_INITIALIZER;
static size_t narenas = 1;
static mstate free_list;

/* list_lock prevents concurrent writes to the next member of struct
   malloc_state objects.

   Read access to the next member is supposed to synchronize with the
   atomic_write_barrier and the write to the next member in
   _int_new_arena.  This suffers from data races; see the FIXME
   comments in _int_new_arena and reused_arena.

   list_lock also prevents concurrent forks.  At the time list_lock is
   acquired, no arena lock must have been acquired, but it is
   permitted to acquire arena locks subsequently, while list_lock is
   acquired.  */
static mutex_t list_lock = _LIBC_LOCK_INITIALIZER;

/* Already initialized? */
int __malloc_initialized = -1;

/**************************************************************************/


/* arena_get() acquires an arena and locks the corresponding mutex.
   First, try the one last locked successfully by this thread.  (This
   is the common case and handled with a macro for speed.)  Then, loop
   once over the circularly linked list of arenas.  If no arena is
   readily available, create a new one.  In this latter case, `size'
   is just a hint as to how much memory will be required immediately
   in the new arena. */

#define arena_get(ptr, size) do { \
      ptr = thread_arena;						      \
      arena_lock (ptr, size);						      \
  } while (0)

#define arena_lock(ptr, size) do {					      \
      if (ptr && !arena_is_corrupt (ptr))				      \
        (void) mutex_lock (&ptr->mutex);				      \
      else								      \
        ptr = arena_get2 ((size), NULL);				      \
  } while (0)

/* find the heap and corresponding arena for a given ptr */

#define heap_for_ptr(ptr) \
  ((heap_info *) ((unsigned long) (ptr) & ~(HEAP_MAX_SIZE - 1)))
#define arena_for_chunk(ptr) \
  (chunk_non_main_arena (ptr) ? heap_for_ptr (ptr)->ar_ptr : &main_arena)


/**************************************************************************/

/* atfork support.  */

/* The following three functions are called around fork from a
   multi-threaded process.  We do not use the general fork handler
   mechanism to make sure that our handlers are the last ones being
   called, so that other fork handlers can use the malloc
   subsystem.  */

void
internal_function
__malloc_fork_lock_parent (void)
{
  if (__malloc_initialized < 1)
    return;

  /* We do not acquire free_list_lock here because we completely
     reconstruct free_list in __malloc_fork_unlock_child.  */

  (void) mutex_lock (&list_lock);

  for (mstate ar_ptr = &main_arena;; )
    {
      (void) mutex_lock (&ar_ptr->mutex);
      ar_ptr = ar_ptr->next;
      if (ar_ptr == &main_arena)
        break;
    }
}

void
internal_function
__malloc_fork_unlock_parent (void)
{
  if (__malloc_initialized < 1)
    return;

  for (mstate ar_ptr = &main_arena;; )
    {
      (void) mutex_unlock (&ar_ptr->mutex);
      ar_ptr = ar_ptr->next;
      if (ar_ptr == &main_arena)
        break;
    }
  (void) mutex_unlock (&list_lock);
}

void
internal_function
__malloc_fork_unlock_child (void)
{
  if (__malloc_initialized < 1)
    return;

  /* Push all arenas to the free list, except thread_arena, which is
     attached to the current thread.  */
  mutex_init (&free_list_lock);
  if (thread_arena != NULL)
    thread_arena->attached_threads = 1;
  free_list = NULL;
  for (mstate ar_ptr = &main_arena;; )
    {
      mutex_init (&ar_ptr->mutex);
      if (ar_ptr != thread_arena)
        {
	  /* This arena is no longer attached to any thread.  */
	  ar_ptr->attached_threads = 0;
          ar_ptr->next_free = free_list;
          free_list = ar_ptr;
        }
      ar_ptr = ar_ptr->next;
      if (ar_ptr == &main_arena)
        break;
    }

  mutex_init (&list_lock);
}

/* Initialization routine. */
#include <string.h>
extern char **_environ;

static char *
internal_function
next_env_entry (char ***position)
{
  char **current = *position;
  char *result = NULL;

  while (*current != NULL)
    {
      if (__builtin_expect ((*current)[0] == 'M', 0)
          && (*current)[1] == 'A'
          && (*current)[2] == 'L'
          && (*current)[3] == 'L'
          && (*current)[4] == 'O'
          && (*current)[5] == 'C'
          && (*current)[6] == '_')
        {
          result = &(*current)[7];

          /* Save current position for next visit.  */
          *position = ++current;

          break;
        }

      ++current;
    }

  return result;
}


#ifdef SHARED
static void *
__failing_morecore (ptrdiff_t d)
{
  return (void *) MORECORE_FAILURE;
}

extern struct dl_open_hook *_dl_open_hook;
#endif

static void
ptmalloc_init (void)
{
  if (__malloc_initialized >= 0)
    return;

  __malloc_initialized = 0;

#ifdef SHARED
  /* In case this libc copy is in a non-default namespace, never use brk.
     Likewise if dlopened from statically linked program.  */
  Dl_info di;
  struct link_map *l;

  if (_dl_open_hook != NULL
      || (_dl_addr (ptmalloc_init, &di, &l, NULL) != 0
          && l->l_ns != LM_ID_BASE))
    __morecore = __failing_morecore;
#endif

  thread_arena = &main_arena;
  const char *s = NULL;
  if (__glibc_likely (_environ != NULL))
    {
      char **runp = _environ;
      char *envline;

      while (__builtin_expect ((envline = next_env_entry (&runp)) != NULL,
                               0))
        {
          size_t len = strcspn (envline, "=");

          if (envline[len] != '=')
            /* This is a "MALLOC_" variable at the end of the string
               without a '=' character.  Ignore it since otherwise we
               will access invalid memory below.  */
            continue;

          switch (len)
            {
            case 6:
              if (memcmp (envline, "CHECK_", 6) == 0)
                s = &envline[7];
              break;
            case 8:
              if (!__builtin_expect (__libc_enable_secure, 0))
                {
                  if (memcmp (envline, "TOP_PAD_", 8) == 0)
                    __libc_mallopt (M_TOP_PAD, atoi (&envline[9]));
                  else if (memcmp (envline, "PERTURB_", 8) == 0)
                    __libc_mallopt (M_PERTURB, atoi (&envline[9]));
                }
              break;
            case 9:
              if (!__builtin_expect (__libc_enable_secure, 0))
                {
                  if (memcmp (envline, "MMAP_MAX_", 9) == 0)
                    __libc_mallopt (M_MMAP_MAX, atoi (&envline[10]));
                  else if (memcmp (envline, "ARENA_MAX", 9) == 0)
                    __libc_mallopt (M_ARENA_MAX, atoi (&envline[10]));
                }
              break;
            case 10:
              if (!__builtin_expect (__libc_enable_secure, 0))
                {
                  if (memcmp (envline, "ARENA_TEST", 10) == 0)
                    __libc_mallopt (M_ARENA_TEST, atoi (&envline[11]));
                }
              break;
            case 15:
              if (!__builtin_expect (__libc_enable_secure, 0))
                {
                  if (memcmp (envline, "TRIM_THRESHOLD_", 15) == 0)
                    __libc_mallopt (M_TRIM_THRESHOLD, atoi (&envline[16]));
                  else if (memcmp (envline, "MMAP_THRESHOLD_", 15) == 0)
                    __libc_mallopt (M_MMAP_THRESHOLD, atoi (&envline[16]));
                }
              break;
            default:
              break;
            }
        }
    }
  if (s && s[0])
    {
      __libc_mallopt (M_CHECK_ACTION, (int) (s[0] - '0'));
      if (check_action != 0)
        __malloc_check_init ();
    }
#if HAVE_MALLOC_INIT_HOOK
  void (*hook) (void) = atomic_forced_read (__malloc_initialize_hook);
  if (hook != NULL)
    (*hook)();
#endif
  __malloc_initialized = 1;
}

/* Managing heaps and arenas (for concurrent threads) */

#if MALLOC_DEBUG > 1

/* Print the complete contents of a single heap to stderr. */

static void
dump_heap (heap_info *heap)
{
  char *ptr;
  mchunkptr p;

  fprintf (stderr, "Heap %p, size %10lx:\n", heap, (long) heap->size);
  ptr = (heap->ar_ptr != (mstate) (heap + 1)) ?
        (char *) (heap + 1) : (char *) (heap + 1) + sizeof (struct malloc_state);
  p = (mchunkptr) (((unsigned long) ptr + MALLOC_ALIGN_MASK) &
                   ~MALLOC_ALIGN_MASK);
  for (;; )
    {
      fprintf (stderr, "chunk %p size %10lx", p, (long) p->size);
      if (p == top (heap->ar_ptr))
        {
          fprintf (stderr, " (top)\n");
          break;
        }
      else if (p->size == (0 | PREV_INUSE))
        {
          fprintf (stderr, " (fence)\n");
          break;
        }
      fprintf (stderr, "\n");
      p = next_chunk (p);
    }
}
#endif /* MALLOC_DEBUG > 1 */

/* If consecutive mmap (0, HEAP_MAX_SIZE << 1, ...) calls return decreasing
   addresses as opposed to increasing, new_heap would badly fragment the
   address space.  In that case remember the second HEAP_MAX_SIZE part
   aligned to HEAP_MAX_SIZE from last mmap (0, HEAP_MAX_SIZE << 1, ...)
   call (if it is already aligned) and try to reuse it next time.  We need
   no locking for it, as kernel ensures the atomicity for us - worst case
   we'll call mmap (addr, HEAP_MAX_SIZE, ...) for some value of addr in
   multiple threads, but only one will succeed.  */
static char *aligned_heap_area;

/* Create a new heap.  size is automatically rounded up to a multiple
   of the page size. */

static heap_info *
internal_function
new_heap (size_t size, size_t top_pad)
{
  size_t pagesize = getpagesize();
  char *p1, *p2;
  unsigned long ul;
  heap_info *h;

  if (size + top_pad < HEAP_MIN_SIZE)
    size = HEAP_MIN_SIZE;
  else if (size + top_pad <= HEAP_MAX_SIZE)
    size += top_pad;
  else if (size > HEAP_MAX_SIZE)
    return 0;
  else
    size = HEAP_MAX_SIZE;
  size = ALIGN_UP (size, pagesize);

  /* A memory region aligned to a multiple of HEAP_MAX_SIZE is needed.
     No swap space needs to be reserved for the following large
     mapping (on Linux, this is the case for all non-writable mappings
     anyway). */
  p2 = MAP_FAILED;
  if (aligned_heap_area)
    {
      p2 = (char *) MMAP (aligned_heap_area, HEAP_MAX_SIZE, PROT_NONE,
                          MAP_NORESERVE);
      aligned_heap_area = NULL;
      if (p2 != MAP_FAILED && ((unsigned long) p2 & (HEAP_MAX_SIZE - 1)))
        {
          munmap (p2, HEAP_MAX_SIZE);
          p2 = MAP_FAILED;
        }
    }
  if (p2 == MAP_FAILED)
    {
      p1 = (char *) MMAP (0, HEAP_MAX_SIZE << 1, PROT_NONE, MAP_NORESERVE);
      if (p1 != MAP_FAILED)
        {
          p2 = (char *) (((unsigned long) p1 + (HEAP_MAX_SIZE - 1))
                         & ~(HEAP_MAX_SIZE - 1));
          ul = p2 - p1;
          if (ul)
            munmap (p1, ul);
          else
            aligned_heap_area = p2 + HEAP_MAX_SIZE;
          munmap (p2 + HEAP_MAX_SIZE, HEAP_MAX_SIZE - ul);
        }
      else
        {
          /* Try to take the chance that an allocation of only HEAP_MAX_SIZE
             is already aligned. */
          p2 = (char *) MMAP (0, HEAP_MAX_SIZE, PROT_NONE, MAP_NORESERVE);
          if (p2 == MAP_FAILED)
            return 0;

          if ((unsigned long) p2 & (HEAP_MAX_SIZE - 1))
            {
              munmap (p2, HEAP_MAX_SIZE);
              return 0;
            }
        }
    }
  if (mprotect (p2, size, PROT_READ | PROT_WRITE) != 0)
    {
      munmap (p2, HEAP_MAX_SIZE);
      return 0;
    }
  h = (heap_info *) p2;
  h->size = size;
  h->mprotect_size = size;
  LIBC_PROBE (memory_heap_new, 2, h, h->size);
  return h;
}

/* Grow a heap.  size is automatically rounded up to a
   multiple of the page size. */

static int
grow_heap (heap_info *h, long diff)
{
  size_t pagesize = getpagesize();
  long new_size;

  diff = ALIGN_UP (diff, pagesize);
  new_size = (long) h->size + diff;

  if ((unsigned long) new_size > (unsigned long) HEAP_MAX_SIZE)
    return -1;

  if ((unsigned long) new_size > h->mprotect_size)
    {
      if (mprotect ((char *) h + h->mprotect_size,
                      (unsigned long) new_size - h->mprotect_size,
                      PROT_READ | PROT_WRITE) != 0)
        return -2;

      h->mprotect_size = new_size;
    }

  h->size = new_size;
  LIBC_PROBE (memory_heap_more, 2, h, h->size);
  return 0;
}

/* Shrink a heap.  */

static int
shrink_heap (heap_info *h, long diff)
{
  long new_size;

  new_size = (long) h->size - diff;
  if (new_size < (long) sizeof (*h))
    return -1;

  /* Try to re-map the extra heap space freshly to save memory, and make it
     inaccessible.  See malloc-sysdep.h to know when this is true.  */
  if (__glibc_unlikely (check_may_shrink_heap ()))
    {
      if ((char *) MMAP ((char *) h + new_size, diff, PROT_NONE,
                         MAP_FIXED) == (char *) MAP_FAILED)
        return -2;

      h->mprotect_size = new_size;
    }
  else
    madvise ((char *) h + new_size, diff, MADV_DONTNEED);
  /*fprintf(stderr, "shrink %p %08lx\n", h, new_size);*/

  h->size = new_size;
  LIBC_PROBE (memory_heap_less, 2, h, h->size);
  return 0;
}

/* Delete a heap. */

#define delete_heap(heap) \
  do {									      \
      if ((char *) (heap) + HEAP_MAX_SIZE == aligned_heap_area)		      \
        aligned_heap_area = NULL;					      \
      munmap ((char *) (heap), HEAP_MAX_SIZE);			      \
    } while (0)

static int
internal_function
heap_trim (heap_info *heap, size_t pad)
{
  mstate ar_ptr = heap->ar_ptr;
  unsigned long pagesz = getpagesize();
  mchunkptr top_chunk = top (ar_ptr), p, bck, fwd;
  heap_info *prev_heap;
  long new_size, top_size, top_area, extra, prev_size, misalign;

  /* Can this heap go away completely? */
  while (top_chunk == chunk_at_offset (heap, sizeof (*heap)))
    {
      prev_heap = heap->prev;
      prev_size = prev_heap->size - (MINSIZE - 2 * SIZE_SZ);
      p = chunk_at_offset (prev_heap, prev_size);
      /* fencepost must be properly aligned.  */
      misalign = ((long) p) & MALLOC_ALIGN_MASK;
      p = chunk_at_offset (prev_heap, prev_size - misalign);
      assert (p->size == (0 | PREV_INUSE)); /* must be fencepost */
      p = prev_chunk (p);
      new_size = chunksize (p) + (MINSIZE - 2 * SIZE_SZ) + misalign;
      assert (new_size > 0 && new_size < (long) (2 * MINSIZE));
      if (!prev_inuse (p))
        new_size += p->prev_size;
      assert (new_size > 0 && new_size < HEAP_MAX_SIZE);
      if (new_size + (HEAP_MAX_SIZE - prev_heap->size) < pad + MINSIZE + pagesz)
        break;
      ar_ptr->system_mem -= heap->size;
      LIBC_PROBE (memory_heap_free, 2, heap, heap->size);
      delete_heap (heap);
      heap = prev_heap;
      if (!prev_inuse (p)) /* consolidate backward */
        {
          p = prev_chunk (p);
          unlink (ar_ptr, p, bck, fwd);
        }
      assert (((unsigned long) ((char *) p + new_size) & (pagesz - 1)) == 0);
      assert (((char *) p + new_size) == ((char *) heap + heap->size));
      top (ar_ptr) = top_chunk = p;
      set_head (top_chunk, new_size | PREV_INUSE);
      /*check_chunk(ar_ptr, top_chunk);*/
    }

  /* Uses similar logic for per-thread arenas as the main arena with systrim
     and _int_free by preserving the top pad and rounding down to the nearest
     page.  */
  top_size = chunksize (top_chunk);
  if ((unsigned long)(top_size) <
      (unsigned long)(mp_.trim_threshold)) {
    return 0;
	}

  top_area = top_size - MINSIZE - 1;
  if (top_area < 0 || (size_t) top_area <= pad) {
    return 0;
	}

  /* Release in pagesize units and round down to the nearest page.  */
  extra = ALIGN_DOWN(top_area - pad, pagesz);
  if (extra == 0) {
    return 0;
	}

  /* Try to shrink. */
  if (shrink_heap (heap, extra) != 0) {
    return 0;
	}

  ar_ptr->system_mem -= extra;

  /* Success. Adjust top accordingly. */
  set_head (top_chunk, (top_size - extra) | PREV_INUSE);
  /*check_chunk(ar_ptr, top_chunk);*/
  return 1;
}

/* Create a new arena with initial size "size".  */

/* If REPLACED_ARENA is not NULL, detach it from this thread.  Must be
   called while free_list_lock is held.  */
static void
detach_arena (mstate replaced_arena)
{
  if (replaced_arena != NULL)
    {
      assert (replaced_arena->attached_threads > 0);
      /* The current implementation only detaches from main_arena in
	 case of allocation failure.  This means that it is likely not
	 beneficial to put the arena on free_list even if the
	 reference count reaches zero.  */
      --replaced_arena->attached_threads;
    }
}

static mstate
_int_new_arena (size_t size)
{
  mstate a;
  heap_info *h;
  char *ptr;
  unsigned long misalign;

  h = new_heap (size + (sizeof (*h) + sizeof (*a) + MALLOC_ALIGNMENT),
                mp_.top_pad);
  if (!h)
    {
      /* Maybe size is too large to fit in a single heap.  So, just try
         to create a minimally-sized arena and let _int_malloc() attempt
         to deal with the large request via mmap_chunk().  */
      h = new_heap (sizeof (*h) + sizeof (*a) + MALLOC_ALIGNMENT, mp_.top_pad);
      if (!h)
        return 0;
    }
  a = h->ar_ptr = (mstate) (h + 1);
  malloc_init_state (a);
  a->attached_threads = 1;
  /*a->next = NULL;*/
  a->system_mem = a->max_system_mem = h->size;

  /* Set up the top chunk, with proper alignment. */
  ptr = (char *) (a + 1);
  misalign = (unsigned long) chunk2mem (ptr) & MALLOC_ALIGN_MASK;
  if (misalign > 0)
    ptr += MALLOC_ALIGNMENT - misalign;
  top (a) = (mchunkptr) ptr;
  set_head (top (a), (((char *) h + h->size) - ptr) | PREV_INUSE);

  LIBC_PROBE (memory_arena_new, 2, a, size);
  mstate replaced_arena = thread_arena;
  thread_arena = a;
  mutex_init (&a->mutex);

  (void) mutex_lock (&list_lock);

  /* Add the new arena to the global list.  */
  a->next = main_arena.next;
  /* FIXME: The barrier is an attempt to synchronize with read access
     in reused_arena, which does not acquire list_lock while
     traversing the list.  */
  atomic_write_barrier ();
  main_arena.next = a;

  (void) mutex_unlock (&list_lock);

  (void) mutex_lock (&free_list_lock);
  detach_arena (replaced_arena);
  (void) mutex_unlock (&free_list_lock);

  /* Lock this arena.  NB: Another thread may have been attached to
     this arena because the arena is now accessible from the
     main_arena.next list and could have been picked by reused_arena.
     This can only happen for the last arena created (before the arena
     limit is reached).  At this point, some arena has to be attached
     to two threads.  We could acquire the arena lock before list_lock
     to make it less likely that reused_arena picks this new arena,
     but this could result in a deadlock with
     __malloc_fork_lock_parent.  */

  (void) mutex_lock (&a->mutex);

  return a;
}


/* Remove an arena from free_list.  The arena may be in use because it
   was attached concurrently to a thread by reused_arena below.  */
static mstate
get_free_list (void)
{
  mstate replaced_arena = thread_arena;
  mstate result = free_list;
  if (result != NULL)
    {
      (void) mutex_lock (&free_list_lock);
      result = free_list;
      if (result != NULL)
	{
	  free_list = result->next_free;

	  /* The arena will be attached to this thread.  */
	  ++result->attached_threads;

	  detach_arena (replaced_arena);
	}
      (void) mutex_unlock (&free_list_lock);

      if (result != NULL)
        {
          LIBC_PROBE (memory_arena_reuse_free_list, 1, result);
          (void) mutex_lock (&result->mutex);
	  thread_arena = result;
        }
    }

  return result;
}

/* Lock and return an arena that can be reused for memory allocation.
   Avoid AVOID_ARENA as we have already failed to allocate memory in
   it and it is currently locked.  */
static mstate
reused_arena (mstate avoid_arena)
{
  mstate result;
  /* FIXME: Access to next_to_use suffers from data races.  */
  static mstate next_to_use;
  if (next_to_use == NULL)
    next_to_use = &main_arena;

  /* Iterate over all arenas (including those linked from
     free_list).  */
  result = next_to_use;
  do
    {
      if (!arena_is_corrupt (result) && !pthread_mutex_trylock (&result->mutex))
        goto out;

      /* FIXME: This is a data race, see _int_new_arena.  */
      result = result->next;
    }
  while (result != next_to_use);

  /* Avoid AVOID_ARENA as we have already failed to allocate memory
     in that arena and it is currently locked.   */
  if (result == avoid_arena)
    result = result->next;

  /* Make sure that the arena we get is not corrupted.  */
  mstate begin = result;
  while (arena_is_corrupt (result) || result == avoid_arena)
    {
      result = result->next;
      if (result == begin)
	/* We looped around the arena list.  We could not find any
	   arena that was either not corrupted or not the one we
	   wanted to avoid.  */
	return NULL;
    }

  /* No arena available without contention.  Wait for the next in line.  */
  LIBC_PROBE (memory_arena_reuse_wait, 3, &result->mutex, result, avoid_arena);
  (void) mutex_lock (&result->mutex);

out:
  /* Attach the arena to the current thread.  Note that we may have
     selected an arena which was on free_list.  */
  {
    /* Update the arena thread attachment counters.   */
    mstate replaced_arena = thread_arena;
    (void) mutex_lock (&free_list_lock);
    detach_arena (replaced_arena);
    ++result->attached_threads;
    (void) mutex_unlock (&free_list_lock);
  }

  LIBC_PROBE (memory_arena_reuse, 2, result, avoid_arena);
  thread_arena = result;
  next_to_use = result->next;

  return result;
}

static mstate
internal_function
arena_get2 (size_t size, mstate avoid_arena)
{
  mstate a;

  static size_t narenas_limit;

  a = get_free_list ();
  if (a == NULL)
    {
      /* Nothing immediately available, so generate a new arena.  */
      if (narenas_limit == 0)
        {
          if (mp_.arena_max != 0)
            narenas_limit = mp_.arena_max;
          else if (narenas > mp_.arena_test)
            {
              int n = get_nprocs ();

              if (n >= 1)
                narenas_limit = NARENAS_FROM_NCORES (n);
              else
                /* We have no information about the system.  Assume two
                   cores.  */
                narenas_limit = NARENAS_FROM_NCORES (2);
            }
        }
    repeat:;
      size_t n = narenas;
      /* NB: the following depends on the fact that (size_t)0 - 1 is a
         very large number and that the underflow is OK.  If arena_max
         is set the value of arena_test is irrelevant.  If arena_test
         is set but narenas is not yet larger or equal to arena_test
         narenas_limit is 0.  There is no possibility for narenas to
         be too big for the test to always fail since there is not
         enough address space to create that many arenas.  */
      if (__glibc_unlikely (n <= narenas_limit - 1))
        {
          if (catomic_compare_and_exchange_bool_acq (&narenas, n + 1, n))
            goto repeat;
          a = _int_new_arena (size);
	  if (__glibc_unlikely (a == NULL))
            catomic_decrement (&narenas);
        }
      else
        a = reused_arena (avoid_arena);
    }
  return a;
}

/* If we don't have the main arena, then maybe the failure is due to running
   out of mmapped areas, so we can try allocating on the main arena.
   Otherwise, it is likely that sbrk() has failed and there is still a chance
   to mmap(), so try one of the other arenas.  */
static mstate
arena_get_retry (mstate ar_ptr, size_t bytes)
{
  LIBC_PROBE (memory_arena_retry, 2, bytes, ar_ptr);
  if (ar_ptr != &main_arena)
    {
      (void) mutex_unlock (&ar_ptr->mutex);
      /* Don't touch the main arena if it is corrupt.  */
      if (arena_is_corrupt (&main_arena))
	return NULL;

      ar_ptr = &main_arena;
      (void) mutex_lock (&ar_ptr->mutex);
    }
  else
    {
      (void) mutex_unlock (&ar_ptr->mutex);
      ar_ptr = arena_get2 (bytes, ar_ptr);
    }

  return ar_ptr;
}

static void __attribute__ ((section ("__libc_thread_freeres_fn")))
arena_thread_freeres (void)
{
  mstate a = thread_arena;
  thread_arena = NULL;

  if (a != NULL)
    {
      (void) mutex_lock (&free_list_lock);
      /* If this was the last attached thread for this arena, put the
	 arena on the free list.  */
      assert (a->attached_threads > 0);
      if (--a->attached_threads == 0)
	{
	  a->next_free = free_list;
	  free_list = a;
	}
      (void) mutex_unlock (&free_list_lock);
    }
}
text_set_element (__libc_thread_subfreeres, arena_thread_freeres);

/*
 * Local variables:
 * c-basic-offset: 2
 * End:
 */
