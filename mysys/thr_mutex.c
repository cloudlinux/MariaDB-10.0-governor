/*
   Copyright (c) 2000, 2011, Oracle and/or its affiliates.
   Copyright (c) 2010, 2011, Monty Program Ab

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

/* This makes a wrapper for mutex handling to make it easier to debug mutex */

#include <my_global.h>
#if defined(TARGET_OS_LINUX) && !defined (__USE_UNIX98)
#define __USE_UNIX98			/* To get rw locks under Linux */
#endif

#ifdef SAFE_MUTEX
#define SAFE_MUTEX_DEFINED
#undef SAFE_MUTEX                       /* Avoid safe_mutex redefinitions */
#endif

#include <linux/unistd.h>

#include "mysys_priv.h"
#include "my_static.h"
#include <m_string.h>
#include <hash.h>

#include <execinfo.h>
#include <syslog.h>

#ifndef DO_NOT_REMOVE_THREAD_WRAPPERS
/* Remove wrappers */
#undef pthread_mutex_t
#undef pthread_mutex_init
#undef pthread_mutex_lock
#undef pthread_mutex_unlock
#undef pthread_mutex_trylock
#undef pthread_mutex_destroy
#undef pthread_cond_wait
#undef pthread_cond_timedwait
#undef safe_mutex_free_deadlock_data
#endif /* DO_NOT_REMOVE_THREAD_WRAPPERS */

#ifdef PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP
pthread_mutexattr_t my_fast_mutexattr;
#endif
#ifdef PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP
pthread_mutexattr_t my_errorcheck_mutexattr;
#endif

#ifdef SAFE_MUTEX_DEFINED

void * (*governor_load_lve_library)() = NULL;
int (*governor_init_lve)() = NULL;
void (*governor_destroy_lve)() = NULL;
int (*governor_enter_lve)(uint32_t *, char *) = NULL;
void (*governor_lve_exit)(uint32_t *) = NULL;
int (*governor_enter_lve_light)(uint32_t *) = NULL;
void (*governor_lve_exit_null)() = NULL;
int (*governor_lve_enter_pid)(pid_t) = NULL;
int (*governor_is_in_lve)() = NULL;
 
void governor_setlve_mysql_thread_info(pid_t thread_id) {
       (void)(thread_id);
       return;
}

__attribute__((noinline)) int put_in_lve(char *user) {
       (void)(user);
       return 0;
}

__attribute__((noinline)) void lve_thr_exit() {
       return;
}

void governor_detroy_mysql_thread_info(){
       return;
}

__attribute__((noinline)) void my_release_slot(){
    return;
}

__attribute__((noinline)) void my_reserve_slot(){
    return;
}

void create_core_dump_lvedebug_info(){
    return;
}
void release_thread_chanks_lvedebug_info(){
    return;
}
void make_snapshot_lvedebug_info(long number_of_in, long numbers_of_out,
	int real_lve, void *mysql_lve_mutex_governor_ptr_n, char *fname,
	void *mtx){
    (void)(number_of_in);
    (void)(numbers_of_out);
    (void)(real_lve);
    (void)(mysql_lve_mutex_governor_ptr_n);
    (void)(fname);
    (void)(mtx);
    return;
}
void init_data_lvedebug_info(char *sql, char *user_name){
    (void)(sql);
    (void)(user_name);
    return;
}
void free_lvedebug_info(){
    return;
}
int initialize_lvedebug_info(){
    return 0;
}

int send_to_client_debug_data_lvedebug_info(char *buffer, int max_size) {
	(void)(buffer);
	(void)(max_size);
	return 0;
}

long get_memusage_lvedebug_info() {
	return 0;
}

void *init_info_retarray_lvedbug_info(int size){
	(void)(size);
	return NULL;
}

void release_info_retarray_lvedbug_info(void *ptr){
	(void)(ptr);
}

void retinfo_info_retarray_lvedbug_info(char *buffer, int field, void *data, int index, int buf_len){
	(void)(buffer);
	(void)(field);
	(void)(data);
	(void)(index);
	(void)(buf_len);
}

static pthread_mutex_t THR_LOCK_mutex;
static ulong safe_mutex_count= 0;		/* Number of mutexes created */
static ulong safe_mutex_id= 0;
my_bool safe_mutex_deadlock_detector= 1;        /* On by default */

#ifdef SAFE_MUTEX_DETECT_DESTROY
static struct st_safe_mutex_create_info_t *safe_mutex_create_root= NULL;
#endif

static my_bool add_used_to_locked_mutex(safe_mutex_t *used_mutex,
                                        safe_mutex_deadlock_t *locked_mutex);
static my_bool add_to_locked_mutex(safe_mutex_deadlock_t *locked_mutex,
                                   safe_mutex_t *current_mutex);
static my_bool remove_from_locked_mutex(safe_mutex_t *mp,
                                        safe_mutex_t *delete_mutex);
static my_bool remove_from_used_mutex(safe_mutex_deadlock_t *locked_mutex,
                                      safe_mutex_t *mutex);
static void print_deadlock_warning(safe_mutex_t *new_mutex,
                                   safe_mutex_t *conflicting_mutex);
#endif


/* Initialize all mutex handling */

void my_mutex_init()
{
  /* Initialize mutex attributes */
#ifdef PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP
  /*
    Set mutex type to "fast" a.k.a "adaptive"

    In this case the thread may steal the mutex from some other thread
    that is waiting for the same mutex.  This will save us some
    context switches but may cause a thread to 'starve forever' while
    waiting for the mutex (not likely if the code within the mutex is
    short).
  */
  pthread_mutexattr_init(&my_fast_mutexattr);
  pthread_mutexattr_settype(&my_fast_mutexattr,
                            PTHREAD_MUTEX_ADAPTIVE_NP);
#endif
#ifdef PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP
  /*
    Set mutex type to "errorcheck"
  */
  pthread_mutexattr_init(&my_errorcheck_mutexattr);
  pthread_mutexattr_settype(&my_errorcheck_mutexattr,
                            PTHREAD_MUTEX_ERRORCHECK);
#endif

#if defined(SAFE_MUTEX_DEFINED)
  safe_mutex_global_init();
#elif defined(MY_PTHREAD_FASTMUTEX)
  fastmutex_global_init();
#endif
}

void my_mutex_end()
{
#ifdef PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP
  pthread_mutexattr_destroy(&my_fast_mutexattr);
#endif
#ifdef PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP
  pthread_mutexattr_destroy(&my_errorcheck_mutexattr);
#endif
}


/* Initialize safe_mutex handling */

#ifdef SAFE_MUTEX_DEFINED
void safe_mutex_global_init(void)
{
  pthread_mutex_init(&THR_LOCK_mutex,MY_MUTEX_INIT_FAST);
  safe_mutex_id= safe_mutex_count= 0;
  safe_mutex_deadlock_detector= 1;

#ifdef SAFE_MUTEX_DETECT_DESTROY
  safe_mutex_create_root= 0;
#endif /* SAFE_MUTEX_DETECT_DESTROY */
}

static inline void remove_from_active_list(safe_mutex_t *mp)
{
  if (!(mp->active_flags & (MYF_NO_DEADLOCK_DETECTION | MYF_TRY_LOCK)))
  {
    /* Remove mutex from active mutex linked list */
    if (mp->next)
      mp->next->prev= mp->prev;
    if (mp->prev)
      mp->prev->next= mp->next;
    else
      *my_thread_var_mutex_in_use()= mp->next;
  }
  mp->prev= mp->next= 0;
}

/*
  We initialise the hashes for deadlock detection lazily.
  This greatly helps with performance when lots of mutexes are initiased but
  only a few of them are actually used (eg. XtraDB).
*/
static int safe_mutex_lazy_init_deadlock_detection(safe_mutex_t *mp)
{
  if (!my_multi_malloc(MY_FAE | MY_WME,
                       &mp->locked_mutex, sizeof(*mp->locked_mutex),
                       &mp->used_mutex, sizeof(*mp->used_mutex), NullS))
  {
    /* Disable deadlock handling for this mutex */
    mp->create_flags|= MYF_NO_DEADLOCK_DETECTION;
    mp->active_flags|= MYF_NO_DEADLOCK_DETECTION;
    return 1;                                   /* Error */
  }

  pthread_mutex_lock(&THR_LOCK_mutex);
  mp->id= ++safe_mutex_id;
  pthread_mutex_unlock(&THR_LOCK_mutex);
  my_hash_init2(mp->locked_mutex, 64, &my_charset_bin,
             128,
             offsetof(safe_mutex_deadlock_t, id),
             sizeof(mp->id),
             0, 0, 0, HASH_UNIQUE);
  my_hash_init2(mp->used_mutex, 64, &my_charset_bin,
             128,
             offsetof(safe_mutex_t, id),
             sizeof(mp->id),
             0, 0, 0, HASH_UNIQUE);
  return 0;
}

int safe_mutex_init(safe_mutex_t *mp,
		    const pthread_mutexattr_t *attr __attribute__((unused)),
                    const char *name, const char *file, uint line)
{
  DBUG_ENTER("safe_mutex_init");
  DBUG_PRINT("enter",("mutex: 0x%lx  name: %s", (ulong) mp, name));
  bzero((char*) mp,sizeof(*mp));
  pthread_mutex_init(&mp->global,MY_MUTEX_INIT_ERRCHK);
  pthread_mutex_init(&mp->mutex,attr);
  /* Mark that mutex is initialized */
  mp->file= file;
  mp->line= line;
  /* Skip the very common '&' prefix from the autogenerated name */
  mp->name= name[0] == '&' ? name + 1 : name;

  /* Deadlock detection is initialised only lazily, on first use. */

  mp->create_flags= safe_mutex_deadlock_detector ? 0 : MYF_NO_DEADLOCK_DETECTION;

#ifdef SAFE_MUTEX_DETECT_DESTROY
  /*
    Monitor the freeing of mutexes.  This code depends on single thread init
    and destroy
  */
  if ((mp->info= (safe_mutex_info_t *) malloc(sizeof(safe_mutex_info_t))))
  {
    struct st_safe_mutex_info_t *info= mp->info;

    info->init_file= file;
    info->init_line= line;
    info->prev= NULL;
    info->next= NULL;

    pthread_mutex_lock(&THR_LOCK_mutex);
    if ((info->next= safe_mutex_create_root))
      safe_mutex_create_root->prev= info;
    safe_mutex_create_root= info;
    safe_mutex_count++;
    pthread_mutex_unlock(&THR_LOCK_mutex);
  }
#else
  pthread_mutex_lock(&THR_LOCK_mutex);
  safe_mutex_count++;
  pthread_mutex_unlock(&THR_LOCK_mutex);
#endif /* SAFE_MUTEX_DETECT_DESTROY */
  DBUG_RETURN(0);
}


int safe_mutex_lock(safe_mutex_t *mp, myf my_flags, const char *file,
                    uint line)
{
  int error;
  DBUG_PRINT("mutex", ("%s (0x%lx) locking", mp->name ? mp->name : "Null",
                       (ulong) mp));
  if (!mp->file)
  {
    fprintf(stderr,
	    "safe_mutex: Trying to lock unitialized mutex at %s, line %d\n",
	    file, line);
    fflush(stderr);
    abort();
  }

  pthread_mutex_lock(&mp->global);
  if (mp->count > 0)
  {
    /*
      Check that we are not trying to lock mutex twice. This is an error
      even if we are using 'try_lock' as it's not portably what happens
      if you lock the mutex many times and this is in any case bad
      behaviour that should not be encouraged
    */
    if (pthread_equal(pthread_self(),mp->thread))
    {
      fprintf(stderr,
              "safe_mutex: Trying to lock mutex at %s, line %d, when the"
              " mutex was already locked at %s, line %d in thread %s\n",
              file,line,mp->file, mp->line, my_thread_name());
      fflush(stderr);
      abort();
    }
  }
  pthread_mutex_unlock(&mp->global);

  /*
    If we are imitating trylock(), we need to take special
    precautions.

    - We cannot use pthread_mutex_lock() only since another thread can
      overtake this thread and take the lock before this thread
      causing pthread_mutex_trylock() to hang. In this case, we should
      just return EBUSY. Hence, we use pthread_mutex_trylock() to be
      able to return immediately.

    - We cannot just use trylock() and continue execution below, since
      this would generate an error and abort execution if the thread
      was overtaken and trylock() returned EBUSY . In this case, we
      instead just return EBUSY, since this is the expected behaviour
      of trylock().
   */
  if (my_flags & MYF_TRY_LOCK)
  {
    error= pthread_mutex_trylock(&mp->mutex);
    if (error == EBUSY)
      return error;
  }
  else
    error= pthread_mutex_lock(&mp->mutex);

  if (error || (error=pthread_mutex_lock(&mp->global)))
  {
    fprintf(stderr,"Got error %d when trying to lock mutex %s at %s, line %d\n",
	    error, mp->name, file, line);
    fflush(stderr);
    abort();
  }
  mp->thread= pthread_self();
  if (mp->count++)
  {
    fprintf(stderr,"safe_mutex: Error in thread libray: Got mutex %s at %s, "
            "line %d more than 1 time\n", mp->name, file,line);
    fflush(stderr);
    abort();
  }
  mp->file= file;
  mp->line= line;
  mp->active_flags= mp->create_flags | my_flags;
  pthread_mutex_unlock(&mp->global);

  /* Deadlock detection */

  mp->prev= mp->next= 0;
  if (!(mp->active_flags & (MYF_TRY_LOCK | MYF_NO_DEADLOCK_DETECTION)) &&
      (mp->used_mutex != NULL || !safe_mutex_lazy_init_deadlock_detection(mp)))
  {
    safe_mutex_t **mutex_in_use= my_thread_var_mutex_in_use();

    if (!mutex_in_use)
    {
      /* thread has not called my_thread_init() */
      mp->active_flags|= MYF_NO_DEADLOCK_DETECTION;
    }
    else
    {
      safe_mutex_t *mutex_root;
      if ((mutex_root= *mutex_in_use))   /* If not first locked */
      {
        /*
          Protect locked_mutex against changes if a mutex is deleted
        */
        pthread_mutex_lock(&THR_LOCK_mutex);

        if (!my_hash_search(mutex_root->locked_mutex, (uchar*) &mp->id, 0))
        {
          safe_mutex_deadlock_t *deadlock;
          safe_mutex_t *mutex;

          /* Create object to store mutex info */
          if (!(deadlock= my_malloc(sizeof(*deadlock),
                                    MYF(MY_ZEROFILL | MY_WME | MY_FAE))))
            goto abort_loop;
          deadlock->name= mp->name;
          deadlock->id= mp->id;
          deadlock->mutex= mp;
          /* The following is useful for debugging wrong mutex usage */
          deadlock->file= file;
          deadlock->line= line;

          /* Check if potential deadlock */
          mutex= mutex_root;
          do
          {
            if (my_hash_search(mp->locked_mutex, (uchar*) &mutex->id, 0))
            {
              print_deadlock_warning(mp, mutex);
              /* Mark wrong usage to avoid future warnings for same error */
              deadlock->warning_only= 1;
              add_to_locked_mutex(deadlock, mutex_root);
              DBUG_ASSERT(deadlock->count > 0);
              goto abort_loop;
            }
          }
          while ((mutex= mutex->next));

          /*
            Copy current mutex and all mutex that has been locked
            after current mutex (mp->locked_mutex) to all mutex that
            was locked before previous mutex (mutex_root->used_mutex)

            For example if A->B would have been done before and we
            are now locking (C) in B->C, then we would add C into
            B->locked_mutex and A->locked_mutex
          */
          my_hash_iterate(mutex_root->used_mutex,
                          (my_hash_walk_action) add_used_to_locked_mutex,
                          deadlock);

          /*
            Copy all current mutex and all mutex locked after current one
            into the prev mutex
          */
          add_used_to_locked_mutex(mutex_root, deadlock);
          DBUG_ASSERT(deadlock->count > 0);
        }
  abort_loop:
        pthread_mutex_unlock(&THR_LOCK_mutex);
      }
      /* Link mutex into mutex_in_use list */
      if ((mp->next= *mutex_in_use))
        (*mutex_in_use)->prev= mp;
      *mutex_in_use= mp;
    }
  }

  DBUG_PRINT("mutex", ("%s (0x%lx) locked", mp->name, (ulong) mp));
  return error;
}


int safe_mutex_unlock(safe_mutex_t *mp,const char *file, uint line)
{
  int error;
  DBUG_PRINT("mutex", ("%s (0x%lx) unlocking", mp->name, (ulong) mp));
  pthread_mutex_lock(&mp->global);
  if (mp->count == 0)
  {
    fprintf(stderr,
            "safe_mutex: Trying to unlock mutex %s that wasn't locked at "
            "%s, line %d\n"
            "Last used at %s, line: %d\n",
	    mp->name ? mp->name : "Null", file, line,
            mp->file ? mp->file : "Null", mp->line);
    fflush(stderr);
    abort();
  }
  if (!pthread_equal(pthread_self(),mp->thread))
  {
    fprintf(stderr,
            "safe_mutex: Trying to unlock mutex %s at %s, line %d that was "
            "locked by "
            "another thread at: %s, line: %d\n",
	    mp->name, file, line, mp->file, mp->line);
    fflush(stderr);
    abort();
  }
  mp->thread= 0;
  mp->count--;

  remove_from_active_list(mp);

#ifdef __WIN__
  pthread_mutex_unlock(&mp->mutex);
  error=0;
#else
  error=pthread_mutex_unlock(&mp->mutex);
  if (error)
  {
    fprintf(stderr,
            "safe_mutex: Got error: %d (%d) when trying to unlock mutex "
            "%s at %s, line %d\n", error, errno, mp->name, file, line);
    fflush(stderr);
    abort();
  }
#endif /* __WIN__ */
  pthread_mutex_unlock(&mp->global);
  return error;
}


int safe_cond_wait(pthread_cond_t *cond, safe_mutex_t *mp, const char *file,
		   uint line)
{
  int error;
  safe_mutex_t save_state;

  pthread_mutex_lock(&mp->global);
  if (mp->count == 0)
  {
    fprintf(stderr,
            "safe_mutex: Trying to cond_wait on a unlocked mutex %s at %s, "
            "line %d\n",
            mp->name ? mp->name : "Null", file, line);
    fflush(stderr);
    abort();
  }
  if (!pthread_equal(pthread_self(),mp->thread))
  {
    fprintf(stderr,
            "safe_mutex: Trying to cond_wait on a mutex %s at %s, line %d "
            "that was locked by another thread at: %s, line: %d\n",
	    mp->name, file, line, mp->file, mp->line);
    fflush(stderr);
    abort();
  }

  if (mp->count-- != 1)
  {
    fprintf(stderr,
            "safe_mutex:  Count was %d on locked mutex %s at %s, line %d\n",
	    mp->count+1, mp->name, file, line);
    fflush(stderr);
    abort();
  }
  save_state= *mp;
  remove_from_active_list(mp);
  pthread_mutex_unlock(&mp->global);
  error=pthread_cond_wait(cond,&mp->mutex);
  pthread_mutex_lock(&mp->global);

  if (error)
  {
    fprintf(stderr,
            "safe_mutex: Got error: %d (%d) when doing a safe_mutex_wait on "
            "%s at %s, line %d\n", error, errno, mp->name, file, line);
    fflush(stderr);
    abort();
  }
  /* Restore state as it was before */
  mp->thread=       save_state.thread;
  mp->active_flags= save_state.active_flags;
  mp->next=         save_state.next;
  mp->prev=         save_state.prev;

  if (mp->count++)
  {
    fprintf(stderr,
	    "safe_mutex:  Count was %d in thread 0x%lx when locking mutex %s "
            "at %s, line %d\n",
	    mp->count-1, my_thread_dbug_id(), mp->name, file, line);
    fflush(stderr);
    abort();
  }
  mp->file= file;
  mp->line=line;
  pthread_mutex_unlock(&mp->global);
  return error;
}


int safe_cond_timedwait(pthread_cond_t *cond, safe_mutex_t *mp,
			const struct timespec *abstime,
			const char *file, uint line)
{
  int error;
  safe_mutex_t save_state;

  pthread_mutex_lock(&mp->global);
  if (mp->count != 1 || !pthread_equal(pthread_self(),mp->thread))
  {
    fprintf(stderr,
            "safe_mutex: Trying to cond_wait at %s, line %d on a not hold "
            "mutex %s\n",
            file, line, mp->name ? mp->name : "Null");
    fflush(stderr);
    abort();
  }
  mp->count--;					/* Mutex will be released */
  save_state= *mp;
  remove_from_active_list(mp);
  pthread_mutex_unlock(&mp->global);
  error=pthread_cond_timedwait(cond,&mp->mutex,abstime);
#ifdef EXTRA_DEBUG
  if (error && (error != EINTR && error != ETIMEDOUT && error != ETIME))
  {
    fprintf(stderr,
            "safe_mutex: Got error: %d (%d) when doing a safe_mutex_timedwait "
            "on %s at %s, line %d\n",
            error, errno, mp->name, file, line);
  }
#endif /* EXTRA_DEBUG */
  pthread_mutex_lock(&mp->global);
  /* Restore state as it was before */
  mp->thread=       save_state.thread;
  mp->active_flags= save_state.active_flags;
  mp->next=         save_state.next;
  mp->prev=         save_state.prev;

  if (mp->count++)
  {
    fprintf(stderr,
	    "safe_mutex:  Count was %d in thread 0x%lx when locking mutex "
            "%s at %s, line %d (error: %d (%d))\n",
	    mp->count-1, my_thread_dbug_id(), mp->name, file, line,
            error, error);
    fflush(stderr);
    abort();
  }
  mp->file= file;
  mp->line=line;
  pthread_mutex_unlock(&mp->global);
  return error;
}


int safe_mutex_destroy(safe_mutex_t *mp, const char *file, uint line)
{
  int error=0;
  DBUG_ENTER("safe_mutex_destroy");
  DBUG_PRINT("enter", ("mutex: 0x%lx  name: %s", (ulong) mp, mp->name));
  if (!mp->file)
  {
    fprintf(stderr,
	    "safe_mutex: Trying to destroy unitialized mutex at %s, line %d\n",
	    file, line);
    fflush(stderr);
    abort();
  }
  if (mp->count != 0)
  {
    fprintf(stderr,
            "safe_mutex: Trying to destroy a mutex %s that was locked at %s, "
            "line %d at %s, line %d\n",
	    mp->name, mp->file, mp->line, file, line);
    fflush(stderr);
    abort();
  }

  /* Free all entries that points to this one */
  safe_mutex_free_deadlock_data(mp);

#ifdef __WIN__ 
  pthread_mutex_destroy(&mp->global);
  pthread_mutex_destroy(&mp->mutex);
#else
  if (pthread_mutex_destroy(&mp->global))
    error=1;
  if (pthread_mutex_destroy(&mp->mutex))
    error=1;
#endif /* __WIN__ */
  mp->file= 0;					/* Mark destroyed */

#ifdef SAFE_MUTEX_DETECT_DESTROY
  if (mp->info)
  {
    struct st_safe_mutex_info_t *info= mp->info;
    pthread_mutex_lock(&THR_LOCK_mutex);

    if (info->prev)
      info->prev->next = info->next;
    else
      safe_mutex_create_root = info->next;
    if (info->next)
      info->next->prev = info->prev;
    safe_mutex_count--;

    pthread_mutex_unlock(&THR_LOCK_mutex);
    free(info);
    mp->info= NULL;				/* Get crash if double free */
  }
#else
  pthread_mutex_lock(&THR_LOCK_mutex);
  safe_mutex_count--;
  pthread_mutex_unlock(&THR_LOCK_mutex);
#endif /* SAFE_MUTEX_DETECT_DESTROY */
  DBUG_RETURN(error);
}


/**
  Free all data related to deadlock detection

  This is also useful together with safemalloc when you don't want to
  have reports of not freed memory for mysys mutexes.
*/

void safe_mutex_free_deadlock_data(safe_mutex_t *mp)
{
  /* Free all entries that points to this one */
  if (!(mp->create_flags & MYF_NO_DEADLOCK_DETECTION) && mp->used_mutex != NULL)
  {
    pthread_mutex_lock(&THR_LOCK_mutex);
    my_hash_iterate(mp->used_mutex,
                    (my_hash_walk_action) remove_from_locked_mutex,
                    mp);
    my_hash_iterate(mp->locked_mutex,
                    (my_hash_walk_action) remove_from_used_mutex,
                    mp);
    pthread_mutex_unlock(&THR_LOCK_mutex);

    my_hash_free(mp->used_mutex);
    my_hash_free(mp->locked_mutex);
    my_free(mp->locked_mutex);
    mp->create_flags|= MYF_NO_DEADLOCK_DETECTION;
  }
}

/*
  Free global resources and check that all mutex has been destroyed

  SYNOPSIS
    safe_mutex_end()
    file		Print errors on this file

  NOTES
    We can't use DBUG_PRINT() here as we have in my_end() disabled
    DBUG handling before calling this function.

   In MySQL one may get one warning for a mutex created in my_thr_init.c
   This is ok, as this thread may not yet have been exited.
*/

void safe_mutex_end(FILE *file __attribute__((unused)))
{
  if (!safe_mutex_count)			/* safetly */
    pthread_mutex_destroy(&THR_LOCK_mutex);
#ifdef SAFE_MUTEX_DETECT_DESTROY
  if (!file)
    return;

  if (safe_mutex_count)
  {
    fprintf(file, "Warning: Not destroyed mutex: %lu\n", safe_mutex_count);
    (void) fflush(file);
  }
  {
    struct st_safe_mutex_info_t *ptr;
    for (ptr= safe_mutex_create_root ; ptr ; ptr= ptr->next)
    {
      fprintf(file, "\tMutex %s initiated at line %4u in '%s'\n",
	      ptr->name, ptr->init_line, ptr->init_file);
      (void) fflush(file);
    }
  }
#endif /* SAFE_MUTEX_DETECT_DESTROY */
}

static my_bool add_used_to_locked_mutex(safe_mutex_t *used_mutex,
                                        safe_mutex_deadlock_t *locked_mutex)
{
  /* Add mutex to all parent of the current mutex */
  if (!locked_mutex->warning_only)
  {
    (void) my_hash_iterate(locked_mutex->mutex->locked_mutex,
                           (my_hash_walk_action) add_to_locked_mutex,
                           used_mutex);
    /* mark that locked_mutex is locked after used_mutex */
    (void) add_to_locked_mutex(locked_mutex, used_mutex);
  }
  return 0;
}


/**
   register that locked_mutex was locked after current_mutex
*/

static my_bool add_to_locked_mutex(safe_mutex_deadlock_t *locked_mutex,
                                   safe_mutex_t *current_mutex)
{
  DBUG_ENTER("add_to_locked_mutex");
  DBUG_PRINT("info", ("inserting 0x%lx  into  0x%lx  (id: %lu -> %lu)",
                      (ulong) locked_mutex, (long) current_mutex,
                      locked_mutex->id, current_mutex->id));
  if (my_hash_insert(current_mutex->locked_mutex, (uchar*) locked_mutex))
  {
    /* Got mutex through two paths; ignore */
    DBUG_RETURN(0);
  }
  locked_mutex->count++;
  if (my_hash_insert(locked_mutex->mutex->used_mutex,
                     (uchar*) current_mutex))
  {
    DBUG_ASSERT(0);
  }
  DBUG_RETURN(0);
}


/**
  Remove mutex from the locked mutex hash
  @fn    remove_from_used_mutex()
  @param mp            Mutex that has delete_mutex in it's locked_mutex hash
  @param delete_mutex  Mutex should be removed from the hash

  @notes
    safe_mutex_deadlock_t entries in the locked hash are shared.
    When counter goes to 0, we delete the safe_mutex_deadlock_t entry.
*/

static my_bool remove_from_locked_mutex(safe_mutex_t *mp,
                                        safe_mutex_t *delete_mutex)
{
  safe_mutex_deadlock_t *found;
  DBUG_ENTER("remove_from_locked_mutex");
  DBUG_PRINT("enter", ("delete_mutex: 0x%lx  mutex: 0x%lx  (id: %lu <- %lu)",
                       (ulong) delete_mutex, (ulong) mp, 
                       delete_mutex->id, mp->id));

  found= (safe_mutex_deadlock_t *) my_hash_search(mp->locked_mutex,
                                               (uchar*) &delete_mutex->id, 0);
  DBUG_ASSERT(found);
  if (found)
  {
    if (my_hash_delete(mp->locked_mutex, (uchar*) found))
    {
      DBUG_ASSERT(0);
    }
    if (!--found->count)
      my_free(found);
  }
  DBUG_RETURN(0);
}

static my_bool remove_from_used_mutex(safe_mutex_deadlock_t *locked_mutex,
                                      safe_mutex_t *mutex)
{
  DBUG_ENTER("remove_from_used_mutex");
  DBUG_PRINT("enter", ("delete_mutex: 0x%lx  mutex: 0x%lx  (id: %lu <- %lu)",
                       (ulong) mutex, (ulong) locked_mutex, 
                       mutex->id, locked_mutex->id));
  if (my_hash_delete(locked_mutex->mutex->used_mutex, (uchar*) mutex))
  {
    DBUG_ASSERT(0);
  }
  if (!--locked_mutex->count)
    my_free(locked_mutex);
  DBUG_RETURN(0);
}


static void print_deadlock_warning(safe_mutex_t *new_mutex,
                                   safe_mutex_t *parent_mutex)
{
  safe_mutex_t *mutex_root;
  DBUG_ENTER("print_deadlock_warning");
  DBUG_PRINT("enter", ("mutex: %s  parent: %s",
                       new_mutex->name, parent_mutex->name));

  fprintf(stderr, "safe_mutex: Found wrong usage of mutex "
          "'%s' and '%s'\n",
          parent_mutex->name, new_mutex->name);
  DBUG_PRINT("info", ("safe_mutex: Found wrong usage of mutex "
                      "'%s' and '%s'",
                      parent_mutex->name, new_mutex->name));
  fprintf(stderr, "Mutex currently locked (in reverse order):\n");
  DBUG_PRINT("info", ("Mutex currently locked (in reverse order):"));
  fprintf(stderr, "%-32.32s  %s  line %u\n", new_mutex->name, new_mutex->file,
          new_mutex->line);
  DBUG_PRINT("info", ("%-32.32s  %s  line %u\n", new_mutex->name,
                      new_mutex->file, new_mutex->line));
  for (mutex_root= *my_thread_var_mutex_in_use() ;
       mutex_root;
       mutex_root= mutex_root->next)
  {
    fprintf(stderr, "%-32.32s  %s  line %u\n", mutex_root->name,
            mutex_root->file, mutex_root->line);
    DBUG_PRINT("info", ("%-32.32s  %s  line %u", mutex_root->name,
                        mutex_root->file, mutex_root->line));
  }
  fflush(stderr);
  DBUG_ASSERT(my_assert_on_error == 0);
  DBUG_VOID_RETURN;
}

#elif defined(MY_PTHREAD_FASTMUTEX) /* !SAFE_MUTEX_DEFINED */

static ulong mutex_delay(ulong delayloops)
{
  ulong	i;
  volatile ulong j;

  j = 0;

  for (i = 0; i < delayloops * 50; i++)
    j += i;

  return(j); 
}	

#define MY_PTHREAD_FASTMUTEX_SPINS 8
#define MY_PTHREAD_FASTMUTEX_DELAY 4

static int cpu_count= 0;

int my_pthread_fastmutex_init(my_pthread_fastmutex_t *mp,
                              const pthread_mutexattr_t *attr)
{
  if ((cpu_count > 1) && (attr == MY_MUTEX_INIT_FAST))
    mp->spins= MY_PTHREAD_FASTMUTEX_SPINS; 
  else
    mp->spins= 0;
  mp->rng_state= 1;
  return pthread_mutex_init(&mp->mutex, attr); 
}

/**
  Park-Miller random number generator. A simple linear congruential
  generator that operates in multiplicative group of integers modulo n.

  x_{k+1} = (x_k g) mod n

  Popular pair of parameters: n = 2^32 − 5 = 4294967291 and g = 279470273.
  The period of the generator is about 2^31.
  Largest value that can be returned: 2147483646 (RAND_MAX)

  Reference:

  S. K. Park and K. W. Miller
  "Random number generators: good ones are hard to find"
  Commun. ACM, October 1988, Volume 31, No 10, pages 1192-1201.
*/

static double park_rng(my_pthread_fastmutex_t *mp)
{
  mp->rng_state= ((my_ulonglong)mp->rng_state * 279470273U) % 4294967291U;
  return (mp->rng_state / 2147483647.0);
}

int my_pthread_fastmutex_lock(my_pthread_fastmutex_t *mp)
{
  int   res;
  uint  i;
  uint  maxdelay= MY_PTHREAD_FASTMUTEX_DELAY;

  for (i= 0; i < mp->spins; i++)
  {
    res= pthread_mutex_trylock(&mp->mutex);

    if (res == 0)
      return 0;

    if (res != EBUSY)
      return res;

    mutex_delay(maxdelay);
    maxdelay += park_rng(mp) * MY_PTHREAD_FASTMUTEX_DELAY + 1;
  }
  return pthread_mutex_lock(&mp->mutex);
}

/*
 * List of functions which will be imported from libgovernor
 * to work with LVE
 */
void * (*governor_load_lve_library)() = NULL; //load library by dl and initialize all pointers to fuctions
int (*governor_init_lve)() = NULL; //init_lve wrapper
void (*governor_destroy_lve)() = NULL; //destroy_lve wrapper
int (*governor_enter_lve)(uint32_t *, char *) = NULL; //lve_enter wrapper
void (*governor_lve_exit)(uint32_t *) = NULL; // lve_leave wrapper
int (*governor_enter_lve_light)(uint32_t *) = NULL; // lve_enter with stored pcookie and UID in thread storage
void (*governor_lve_exit_null)() = NULL; //lve_exit with NULL cookie (not used yet)
int (*governor_lve_enter_pid)(pid_t) = NULL; //lve_enter_pid for entering another thread (not used yet)
int (*governor_is_in_lve)() = NULL; //is_in_lve - for checking is thread in LVE, only for debug purpocess

//extern CHARSET_INFO my_charset_latin1_bin;
CHARSET_INFO governor_charset_bin;

//Thread dependent variable for thread cookie storage needs for governor_enter_lve, governor_lve_exit
__thread uint32_t lve_cookie = 0;

//Mutex for checking access to
pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

typedef struct __mysql_mutex {
       pid_t *key; //thread_id
       int is_in_lve; //
       int is_in_mutex; //mutex_lock count
       int put_in_lve; //
       int was_in_lve; //
} mysql_mutex;

static HASH *mysql_lve_mutex_governor = NULL;

__thread mysql_mutex *mysql_lve_mutex_governor_ptr = 0;

pthread_mutex_t mtx_mysql_lve_mutex_governor_ptr = PTHREAD_MUTEX_INITIALIZER;

//Insert debug part begin
#define LVEMUTEX_USER_MAXSIZE 64
#define LVEMUTEX_SQL_MAXSIZE 2048
#define LVEMUTEX_OUT_MAXSIZE 4000
#define LVEMUTEX_OUT_LOG_NAME "debug_info.log"
#define LVEMUTEX_OUT_CONF_NAME "/etc/my.debug.conf"
#define LVEMUTEX_OUT_CORE_NAME "debug_core_file"

typedef long atomic_long;

typedef struct __mysql_mutex_info_runtime_out {
	char chunk_to_out[LVEMUTEX_OUT_MAXSIZE];
	long size;
	time_t tm;
	struct __mysql_mutex_info_runtime_out *next;
} mysql_mutex_info_runtime_out;

typedef struct __mysql_mutex_info_runtime {
	pid_t init_therad_id;
	char sql_req[LVEMUTEX_SQL_MAXSIZE];
	char user_name[LVEMUTEX_USER_MAXSIZE];
	long number_of_in;
	long numbers_of_out;
	long max_mutex_deap;
	int init;
	mysql_mutex_info_runtime_out *chunk_ptr;
	mysql_mutex_info_runtime_out *chunk_ptr_tail;
} mysql_mutex_info_runtime;

typedef struct __mysql_online_statistic_lvedebug_info {
	pid_t pid;
	int is_in_lve;
	int chk_is_in_lve;
	char debug_info[LVEMUTEX_OUT_MAXSIZE];
	char sql[LVEMUTEX_OUT_MAXSIZE];
	pthread_mutex_t flag_m;
	void *ptr;
} mysql_online_statistic_lvedebug_info;

typedef struct __mysql_online_statistic_lvedebug_info_list {
	mysql_online_statistic_lvedebug_info *ptr;
	long size;
	struct __mysql_online_statistic_lvedebug_info_list *next;
	struct __mysql_online_statistic_lvedebug_info_list *prev;
} mysql_online_statistic_lvedebug_info_list;

typedef struct __mysql_mutex_info {
	int debug_level; //0 - disbaled, 1-just in memory, 2 - write to one file, 3 - write to separate files, 4 - write to syslog
	char user_to_debug[LVEMUTEX_USER_MAXSIZE];
	int use_check; //0 - no, 1 - yes(call is_in_lve for flag checking)
	char path_to_save[LVEMUTEX_SQL_MAXSIZE]; //where files will be saved
	FILE *log_fd; //one file mode descriptor
	pid_t pid; //parent pid
	void *core;
	int (*WriteCoreDump)(const char *);
	int frame_deep;
	long max_mem;
	atomic_long current_usage;
	pthread_mutex_t lock;
	mysql_online_statistic_lvedebug_info_list *head;
	mysql_online_statistic_lvedebug_info_list *tail;
} mysql_mutex_info;

typedef struct __mysql_online_statistic_lvedebug_info_ret {
	pid_t pid;
	int is_in_lve;
	int chk_is_in_lve;
	char debug_info[LVEMUTEX_OUT_MAXSIZE];
	char sql[LVEMUTEX_OUT_MAXSIZE];
} mysql_online_statistic_lvedebug_info_ret;

static mysql_mutex_info lve_options_storage = { 0 };

static pthread_mutex_t fd_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t fd_dump = PTHREAD_MUTEX_INITIALIZER;
static __thread mysql_mutex_info_runtime debug_info_storage = { 0 };
static __thread mysql_online_statistic_lvedebug_info current_debug_info = { 0 };

void *init_info_retarray_lvedbug_info(int size){
	mysql_online_statistic_lvedebug_info_ret *ptr = calloc(size, sizeof(mysql_online_statistic_lvedebug_info_ret));
	return (void *)ptr;
}

void release_info_retarray_lvedbug_info(void *ptr){
	if(ptr) free(ptr);
}

void retinfo_info_retarray_lvedbug_info(char *buffer, int field, void *data, int index, int buf_len){
	mysql_online_statistic_lvedebug_info_ret *ptr = (mysql_online_statistic_lvedebug_info_ret *)data;
	if(ptr){
		ptr += index;
		switch(field){
		case 0:
			snprintf(buffer, buf_len, "%d", ptr->pid);
			break;
		case 1:
			snprintf(buffer, buf_len, "%d", ptr->is_in_lve);
			break;
		case 2:
			snprintf(buffer, buf_len, "%d", ptr->chk_is_in_lve);
			break;
		case 3:
			snprintf(buffer, buf_len, "%s", ptr->sql);
			break;
		case 4:
			snprintf(buffer, buf_len, "%s", ptr->debug_info);
			break;
		}
	}

}

static void *try_to_calloc_lvedebug_info(size_t __nmemb, size_t __size) {
	void *result_ptr = NULL;
	long tmp_size = __nmemb * __size;
	if (__sync_sub_and_fetch(&lve_options_storage.current_usage, 0) >= 0) {
		result_ptr = calloc(__nmemb, __size);
		if (result_ptr) {
			__sync_sub_and_fetch(&lve_options_storage.current_usage, tmp_size);
			((mysql_mutex_info_runtime_out *) result_ptr)->size = (__nmemb
					* __size);
		}
	}
	return result_ptr;
}

static void try_to_free_lvedebug_info(void *ptr) {
	mysql_mutex_info_runtime_out *ptr_tmp =
			(mysql_mutex_info_runtime_out *) ptr;
	if(ptr_tmp){
		__sync_add_and_fetch(&lve_options_storage.current_usage, ptr_tmp->size);
		free(ptr_tmp);
	}
}

static void *try_to_calloc_lvedebug_info_(size_t __nmemb, size_t __size) {
	void *result_ptr = NULL;
	long tmp_size = __nmemb * __size;
	if (__sync_sub_and_fetch(&lve_options_storage.current_usage, 0) >= 0) {
		result_ptr = calloc(__nmemb, __size);
		if (result_ptr) {
			__sync_sub_and_fetch(&lve_options_storage.current_usage, tmp_size);
			((mysql_online_statistic_lvedebug_info_list *) result_ptr)->size
					= (__nmemb * __size);
		}
	}
	return result_ptr;
}

static void try_to_free_lvedebug_info_(void *ptr) {
	mysql_online_statistic_lvedebug_info_list *ptr_tmp =
			(mysql_online_statistic_lvedebug_info_list *) ptr;
	if(ptr_tmp){
		__sync_add_and_fetch(&lve_options_storage.current_usage, ptr_tmp->size);
		free(ptr_tmp);
	}
}

static void read_config_lvedebug_info() {
	char read_buff[LVEMUTEX_SQL_MAXSIZE] = { 0 };
	FILE *fp = fopen(LVEMUTEX_OUT_CONF_NAME, "r");
	if (fp) {
		while (fgets(read_buff, LVEMUTEX_SQL_MAXSIZE, fp) != NULL) {
			if (read_buff[strnlen(read_buff, LVEMUTEX_SQL_MAXSIZE) - 1] == 0xA) {
				read_buff[strnlen(read_buff, LVEMUTEX_SQL_MAXSIZE) - 1] = 0;
			}
			if (strstr(read_buff, "debug_level")) {
				char *ptr = strchr(read_buff, '=');
				if (ptr) {
					ptr++;
					lve_options_storage.debug_level = atoi(ptr);
				}
			}
			if (strstr(read_buff, "user_to_debug")) {
				char *ptr = strchr(read_buff, '=');
				if (ptr) {
					ptr++;
					strncpy(lve_options_storage.user_to_debug, ptr, LVEMUTEX_USER_MAXSIZE-1);
				}
			}
			if (strstr(read_buff, "path_to_save")) {
				char *ptr = strchr(read_buff, '=');
				if (ptr) {
					ptr++;
					strncpy(lve_options_storage.path_to_save, ptr, LVEMUTEX_SQL_MAXSIZE-1);
				}
			}
			if (strstr(read_buff, "use_check")) {
				char *ptr = strchr(read_buff, '=');
				if (ptr) {
					ptr++;
					lve_options_storage.use_check = atoi(ptr);
				}
			}
			if (strstr(read_buff, "frame_deep")) {
				char *ptr = strchr(read_buff, '=');
				if (ptr) {
					ptr++;
					lve_options_storage.frame_deep = atoi(ptr);
				}
			}
			if (strstr(read_buff, "max_memory")) {
				char *ptr = strchr(read_buff, '=');
				if (ptr) {
					ptr++;
					lve_options_storage.max_mem = atol(ptr);
				}
			}
		}
		fclose(fp);
	}
}

static pid_t gettid_lvedebug_info(void) {
	return syscall(__NR_gettid);
}

int initialize_lvedebug_info() {
	int error_res = 0;
	char file_path_buffer[LVEMUTEX_OUT_MAXSIZE];
	memset(&lve_options_storage, (int) 0, sizeof(mysql_mutex_info));
	strcpy(lve_options_storage.path_to_save, "/tmp");
	lve_options_storage.frame_deep = 10;
	lve_options_storage.max_mem = 100 * 1024 * 1024;
	lve_options_storage.current_usage = lve_options_storage.max_mem;
	read_config_lvedebug_info();
	pthread_mutex_lock(&fd_mtx);
	if (lve_options_storage.debug_level && !lve_options_storage.log_fd) {
		lve_options_storage.pid = getpid();
		snprintf(file_path_buffer, LVEMUTEX_OUT_MAXSIZE, "%s/%s.%d",
				lve_options_storage.path_to_save, LVEMUTEX_OUT_LOG_NAME,
				lve_options_storage.pid);
		lve_options_storage.log_fd = fopen(file_path_buffer, "a");
		if (lve_options_storage.log_fd == NULL) {
			lve_options_storage.debug_level = 0;
			error_res = 1;
		}
		lve_options_storage.core = dlopen("libcoredumper.so.1",
				RTLD_LOCAL | RTLD_LAZY);
		if (lve_options_storage.core) {
			lve_options_storage.WriteCoreDump = dlsym(lve_options_storage.core,
					"WriteCoreDump");
			if (!lve_options_storage.WriteCoreDump) {
				dlclose(lve_options_storage.core);
				lve_options_storage.core = NULL;
			}
		}
	}
	pthread_mutex_unlock(&fd_mtx);
	if (error_res)
		return -1;
	else
		return 0;
}

static void release_current_info_lvedebug_info() {
	pthread_mutex_lock(&lve_options_storage.lock);
	if (lve_options_storage.head) {
		mysql_online_statistic_lvedebug_info_list *ptr =
				lve_options_storage.head;
		while (ptr) {
			mysql_online_statistic_lvedebug_info_list *ptr_tmp = ptr;
			ptr = ptr->next;
			try_to_free_lvedebug_info_(ptr_tmp);
		}
		lve_options_storage.head = NULL;
		lve_options_storage.tail = NULL;
	}
	pthread_mutex_unlock(&lve_options_storage.lock);
}

void free_lvedebug_info() {
	if (lve_options_storage.debug_level) {
		pthread_mutex_lock(&fd_mtx);
		if (lve_options_storage.log_fd) {
			fflush(lve_options_storage.log_fd);
			fclose(lve_options_storage.log_fd);
			lve_options_storage.pid = 0;
			if (lve_options_storage.core) {
				dlclose(lve_options_storage.core);
				lve_options_storage.core = NULL;
				lve_options_storage.WriteCoreDump = NULL;
			}
		}
		pthread_mutex_unlock(&fd_mtx);
		release_current_info_lvedebug_info();
	}
}

static void set_current_info_lvedebug_info(int first, char *sql,
		char *debug_info, int is_in_lve, int check_is_in_lve) {
	pthread_mutex_lock(&current_debug_info.flag_m);
	if (first) {
		current_debug_info.chk_is_in_lve = check_is_in_lve;
		current_debug_info.is_in_lve = is_in_lve;
		current_debug_info.pid = gettid_lvedebug_info();
		current_debug_info.ptr = NULL;
		strncpy(current_debug_info.debug_info, debug_info,
				sizeof(current_debug_info.debug_info)-1);
		strncpy(current_debug_info.sql, sql, sizeof(current_debug_info.sql)-1);

		mysql_online_statistic_lvedebug_info_list *ptr =
				try_to_calloc_lvedebug_info_(1,
						sizeof(mysql_online_statistic_lvedebug_info_list));
		if (ptr) {
			current_debug_info.ptr = ptr;
			ptr->ptr = &current_debug_info;
			pthread_mutex_lock(&lve_options_storage.lock);
			if (!lve_options_storage.tail) {
				lve_options_storage.tail = ptr;
				lve_options_storage.head = ptr;
			} else {
				lve_options_storage.tail->next = ptr;
				ptr->prev = lve_options_storage.tail;
				lve_options_storage.tail = ptr;
			}
			pthread_mutex_unlock(&lve_options_storage.lock);
		}

	} else {

		current_debug_info.chk_is_in_lve = check_is_in_lve;
		current_debug_info.is_in_lve = is_in_lve;
		strncpy(current_debug_info.debug_info, debug_info,
				sizeof(current_debug_info.debug_info)-1);

	}
	pthread_mutex_unlock(&current_debug_info.flag_m);
}

static void remove_current_info_lvedebug_info() {
	if (current_debug_info.ptr) {
		pthread_mutex_lock(&lve_options_storage.lock);
		mysql_online_statistic_lvedebug_info_list
				*i_ptr =
						(mysql_online_statistic_lvedebug_info_list *) current_debug_info.ptr;
		if (i_ptr->prev) {
			i_ptr->prev->next = i_ptr->next;
		} else {
			lve_options_storage.head = i_ptr->next;
		}
		if (i_ptr->next) {
			i_ptr->next->prev = i_ptr->prev;
		} else {
			lve_options_storage.tail = i_ptr->prev;
		}
		pthread_mutex_unlock(&lve_options_storage.lock);
		try_to_free_lvedebug_info_(current_debug_info.ptr);
		current_debug_info.ptr = NULL;
	}
}

static int save_data_to_file_lvedebug_info(mysql_mutex_info_runtime_out *chanks) {
	char path_to_save[LVEMUTEX_OUT_MAXSIZE] = { 0 };
	char buffer[80];
	FILE *i_fd = NULL;
	if (chanks && (lve_options_storage.debug_level >= 2)) {
		if (lve_options_storage.debug_level == 2) {
			i_fd = lve_options_storage.log_fd;
			pthread_mutex_lock(&fd_mtx);
		} else if (lve_options_storage.debug_level == 4) {
			i_fd = NULL;
		} else {
			snprintf(path_to_save, LVEMUTEX_OUT_MAXSIZE, "%s/%s.%d.%d",
					lve_options_storage.path_to_save, LVEMUTEX_OUT_LOG_NAME,
					lve_options_storage.pid, gettid_lvedebug_info());
			i_fd = fopen(path_to_save, "a");
			if (i_fd == NULL) {
				return -1;
			}
		}

		mysql_mutex_info_runtime_out *ptr = chanks;
		while (ptr) {
			if (i_fd == NULL) {
				openlog ("mysqld_debug", LOG_PID | LOG_NDELAY, LOG_DAEMON);
				syslog (LOG_DEBUG, ptr->chunk_to_out);

				closelog ();
			} else {
				struct tm *info;
				info = localtime(&ptr->tm);
				strftime(buffer, 80, "%c", info);
				fprintf(i_fd, "%s: %s\n", buffer, ptr->chunk_to_out);
			}
			ptr = ptr->next;
		}

		if (lve_options_storage.debug_level == 2) {
			fflush(i_fd);
			pthread_mutex_unlock(&fd_mtx);
		} else if (lve_options_storage.debug_level == 4) {
			i_fd = NULL;
		} else {
			fflush(i_fd);
			fclose(i_fd);
		}
	}
	return 0;
}

void init_data_lvedebug_info(char *sql, char *user_name) {
	if (lve_options_storage.debug_level) {
		memset(&debug_info_storage, 0, sizeof(debug_info_storage));
		strncpy(debug_info_storage.sql_req, sql, LVEMUTEX_SQL_MAXSIZE-1);
		strncpy(debug_info_storage.user_name, user_name, LVEMUTEX_USER_MAXSIZE-1);
		set_current_info_lvedebug_info(1, debug_info_storage.sql_req, "INIT",
				0, -1);
	}
}

void create_core_dump_lvedebug_info() {
	//Don't call in LVE
	char path_to_save[LVEMUTEX_OUT_MAXSIZE] = { 0 };

	snprintf(path_to_save, LVEMUTEX_OUT_MAXSIZE, "%s/%s.%d.%d",
			lve_options_storage.path_to_save, LVEMUTEX_OUT_CORE_NAME,
			lve_options_storage.pid, current_debug_info.pid);
	if (lve_options_storage.WriteCoreDump) {
		pthread_mutex_lock(&fd_dump);
		(*lve_options_storage.WriteCoreDump)(path_to_save);
		pthread_mutex_unlock(&fd_dump);
	}
}

static void flush_data_lvedebug_info() {
	if (debug_info_storage.chunk_ptr) {
		save_data_to_file_lvedebug_info(debug_info_storage.chunk_ptr);
	}
	mysql_mutex_info_runtime_out *ptr = debug_info_storage.chunk_ptr;
	while (ptr) {
		mysql_mutex_info_runtime_out *ptr2 = ptr;
		ptr = ptr->next;
		try_to_free_lvedebug_info(ptr2);
	}
	debug_info_storage.chunk_ptr = NULL;
	debug_info_storage.chunk_ptr_tail = NULL;
}

void make_snapshot_lvedebug_info(long number_of_in, long numbers_of_out,
		int real_lve, void *mysql_lve_mutex_governor_ptr_n, char *fname,
		void *mtx) {
	int real_lve_i = -1;
	(mysql_mutex *) (mysql_lve_mutex_governor_ptr_n);

	if (mysql_lve_mutex_governor_ptr && lve_options_storage.debug_level) {
		if (lve_options_storage.use_check) {
			if (governor_is_in_lve) {
				real_lve_i = governor_is_in_lve();
			}
		}

		char snapshot_buf_ss[LVEMUTEX_OUT_MAXSIZE] = { 0 };
		char snapshot_buf_ss2[LVEMUTEX_OUT_MAXSIZE] = { 0 };
		debug_info_storage.number_of_in += number_of_in;
		debug_info_storage.numbers_of_out += numbers_of_out;
		if (debug_info_storage.max_mutex_deap
				< mysql_lve_mutex_governor_ptr->is_in_mutex)
			debug_info_storage.max_mutex_deap
					= mysql_lve_mutex_governor_ptr->is_in_mutex;
		void *array[10];
		int size;
		char **strings;
		size_t i;
		size = backtrace(array, lve_options_storage.frame_deep * 3);
		strings = backtrace_symbols(array, size);
		for (i = (((size - lve_options_storage.frame_deep) < 0) ? 0 : (size
				- lve_options_storage.frame_deep)); i < size; i++) {
			snprintf(snapshot_buf_ss2, LVEMUTEX_OUT_MAXSIZE-1, "%s # %s|",
					snapshot_buf_ss, strings[i]);
			strncpy(snapshot_buf_ss, snapshot_buf_ss2, LVEMUTEX_OUT_MAXSIZE-1);
		}
		free(strings);
		set_current_info_lvedebug_info(0, "SQL", snapshot_buf_ss,
				mysql_lve_mutex_governor_ptr->is_in_lve, real_lve_i);
		if (!debug_info_storage.init) {
			debug_info_storage.init = 1;

			mysql_mutex_info_runtime_out *ptr = try_to_calloc_lvedebug_info(1,
					sizeof(mysql_mutex_info_runtime_out));
			if (ptr) {
				ptr->tm = time(NULL);
				snprintf(ptr->chunk_to_out, LVEMUTEX_OUT_MAXSIZE,
						"%d %s SQL %s U:%s", gettid_lvedebug_info(), fname,
						debug_info_storage.sql_req,
						debug_info_storage.user_name);
				if (debug_info_storage.chunk_ptr_tail) {
					debug_info_storage.chunk_ptr_tail->next = ptr;
					debug_info_storage.chunk_ptr_tail = ptr;
				} else {
					debug_info_storage.chunk_ptr = ptr;
					debug_info_storage.chunk_ptr_tail = ptr;
				}
			}

		}

		if (lve_options_storage.debug_level > 1) {
			mysql_mutex_info_runtime_out *ptr = try_to_calloc_lvedebug_info(1,
					sizeof(mysql_mutex_info_runtime_out));
			if (ptr) {
				ptr->tm = time(NULL);
				snprintf(
						ptr->chunk_to_out,
						LVEMUTEX_OUT_MAXSIZE,
						"%d %s IN %ld OUT %ld MD %ld IILR %d FL %d PL %d M:%p %s",
						gettid_lvedebug_info(), fname,
						debug_info_storage.number_of_in,
						debug_info_storage.numbers_of_out,
						debug_info_storage.max_mutex_deap, real_lve_i,
						mysql_lve_mutex_governor_ptr->is_in_lve,
						mysql_lve_mutex_governor_ptr->put_in_lve, mtx,
						snapshot_buf_ss);
				if (debug_info_storage.chunk_ptr_tail) {
					debug_info_storage.chunk_ptr_tail->next = ptr;
					debug_info_storage.chunk_ptr_tail = ptr;
				} else {
					debug_info_storage.chunk_ptr = ptr;
					debug_info_storage.chunk_ptr_tail = ptr;
				}
			}
		}

		if (real_lve && lve_options_storage.use_check) {
			if (((real_lve_i == 1)
					&& (mysql_lve_mutex_governor_ptr->is_in_lve != 1))
					|| ((real_lve_i == 0)
							&& (mysql_lve_mutex_governor_ptr->is_in_lve == 1))) {
				flush_data_lvedebug_info();
				create_core_dump_lvedebug_info();
			}
		}

	}
}

void release_thread_chanks_lvedebug_info() {
	if (lve_options_storage.debug_level) {
		remove_current_info_lvedebug_info();
		if (debug_info_storage.chunk_ptr) {
			save_data_to_file_lvedebug_info(debug_info_storage.chunk_ptr);
		}
		mysql_mutex_info_runtime_out *ptr = debug_info_storage.chunk_ptr;
		while (ptr) {
			mysql_mutex_info_runtime_out *ptr2 = ptr;
			ptr = ptr->next;
			try_to_free_lvedebug_info(ptr2);
		}
		debug_info_storage.chunk_ptr = NULL;
		debug_info_storage.chunk_ptr_tail = NULL;
	}
}

static void get_core_lvedebug_info(void *mysql_lve_mutex_governor_ptr_n) {
	int real_lve_i = -1;
	mysql_mutex *mysql_lve_mutex_governor_ptr_i =
			(mysql_mutex *) mysql_lve_mutex_governor_ptr_n;
	if (mysql_lve_mutex_governor_ptr_i && lve_options_storage.debug_level) {
		if (lve_options_storage.use_check) {
			if (governor_is_in_lve) {
				real_lve_i = governor_is_in_lve();
			}
			if (((real_lve_i == 1)
					&& (mysql_lve_mutex_governor_ptr_i->is_in_lve != 1))
					|| ((real_lve_i == 0)
							&& (mysql_lve_mutex_governor_ptr_i->is_in_lve == 1))) {
				create_core_dump_lvedebug_info();
			}
		}
	}
}

int send_to_client_debug_data_lvedebug_info(void *buffer, int max_size) {
	int counter = max_size;

	mysql_online_statistic_lvedebug_info_ret *buffer_i = (mysql_online_statistic_lvedebug_info_ret *)buffer;
	if(!buffer_i) return 0;
	pthread_mutex_lock(&lve_options_storage.lock);
	if (lve_options_storage.head) {
		mysql_online_statistic_lvedebug_info_list *ptr =
				lve_options_storage.head;
		while (ptr) {
			if (ptr->ptr) {
				pthread_mutex_lock(&ptr->ptr.flag_m);
				(*buffer_i).chk_is_in_lve = ptr->ptr->chk_is_in_lve;
				(*buffer_i).is_in_lve = ptr->ptr->is_in_lve;
				(*buffer_i).pid = ptr->ptr->pid;
				strncpy((*buffer_i).sql, ptr->ptr->sql, LVEMUTEX_OUT_MAXSIZE-1);
				strncpy((*buffer_i).debug_info, ptr->ptr->debug_info, LVEMUTEX_OUT_MAXSIZE-1);
				pthread_mutex_unlock(&ptr->ptr.flag_m);
				buffer_i++;
				counter--;
				if (counter == 0)
					break;
			}
			ptr = ptr->next;
		}
	}
	pthread_mutex_unlock(&lve_options_storage.lock);
	return (max_size - counter);
}

long get_memusage_lvedebug_info() {
	return __sync_add_and_fetch(&lve_options_storage.current_usage, 0);
}
//Insert debug part end

void governor_value_destroyed(mysql_mutex *data) {
       free(data);
}

uchar *governor_get_key_table_mutex(mysql_mutex *table_mutex, size_t *length,
               my_bool not_used __attribute__((unused))) {
       *length = sizeof(table_mutex->key);
       return (uchar*) table_mutex->key;
}

/*
 *   RETURN
 * < 0 s < t
 * 0   s == t
 * > 0 s > t
*/
int governor_my_strnncoll_8bit_bin(CHARSET_INFO * cs __attribute__((unused)), const uchar *s,
               size_t slen, const uchar *t, size_t tlen, my_bool t_is_prefix) {
       int res = 0;
       pid_t s1 = (pid_t)s, t1 = (pid_t)t;
       if (s1 < t1)
               res = -1;
       else if (s1 == t1)
               res = 0;
       else
               res = 1;
       return res;
}

void governor_hash_sort_8bit_bin(CHARSET_INFO *cs __attribute__((unused)),
                           const uchar *key, size_t len,
                           ulong *nr1, ulong *nr2)
{
  return;
}


/*
 * Function for create HASH table where will be stored mysql_mutex items for all
 * threads
 */
HASH *governor_create_hash_table() {
       mysql_lve_mutex_governor = (HASH *) calloc(1, sizeof(HASH));
       if (mysql_lve_mutex_governor) {
               memcpy(&governor_charset_bin, &my_charset_latin1_bin,
                               sizeof(CHARSET_INFO));
               governor_charset_bin.coll->strnncoll = governor_my_strnncoll_8bit_bin;
               governor_charset_bin.coll->hash_sort = governor_hash_sort_8bit_bin;
               if (my_hash_init(mysql_lve_mutex_governor, &governor_charset_bin, 500, 0,
                               0, (my_hash_get_key) governor_get_key_table_mutex,
                               (my_hash_free_key) governor_value_destroyed, 0)) {
                       mysql_lve_mutex_governor = NULL;
               }
       }
       return mysql_lve_mutex_governor;
}

int governor_add_mysql_thread_info() {
       pid_t *buf = NULL;
       pthread_mutex_lock(&mtx_mysql_lve_mutex_governor_ptr);
       mysql_mutex *mm = NULL;
       if (!mysql_lve_mutex_governor) {
               mysql_lve_mutex_governor = governor_create_hash_table();
               if (!mysql_lve_mutex_governor){
            	       pthread_mutex_unlock(&mtx_mysql_lve_mutex_governor_ptr);
                       return -1;
               }
       }
       buf = (pid_t *)syscall(__NR_gettid);
       mm = (mysql_mutex *) my_hash_search(mysql_lve_mutex_governor,
                       (uchar *) buf, sizeof(buf));
       if (!mm) {
               mm = (mysql_mutex *) calloc(1, sizeof(mysql_mutex));
               if (!mm){
            	       pthread_mutex_unlock(&mtx_mysql_lve_mutex_governor_ptr);
                       return -1;
               }
               mm->key = (pid_t *)syscall(__NR_gettid);
               if (my_hash_insert(mysql_lve_mutex_governor, (uchar *) mm)) {
                       free(mm);
                       pthread_mutex_unlock(&mtx_mysql_lve_mutex_governor_ptr);
                       return -1;
               }
       }
       pthread_mutex_unlock(&mtx_mysql_lve_mutex_governor_ptr);
       mysql_lve_mutex_governor_ptr = mm;
       return 0;
}

void governor_remove_mysql_thread_info() {
       pid_t *buf = NULL;
       pthread_mutex_lock(&mtx_mysql_lve_mutex_governor_ptr);
       mysql_mutex *mm = NULL;
       if (mysql_lve_mutex_governor) {
               buf = (pid_t *)syscall(__NR_gettid);
               mm = (mysql_mutex *) my_hash_search(mysql_lve_mutex_governor,
                               (uchar *) buf, sizeof(buf));
               if (mm)
                       my_hash_delete(mysql_lve_mutex_governor, (uchar *) mm);
       }
       pthread_mutex_unlock(&mtx_mysql_lve_mutex_governor_ptr);
       mysql_lve_mutex_governor_ptr = NULL;
}

void governor_setlve_mysql_thread_info(pid_t thread_id) {
       pid_t *buf = NULL;
       pthread_mutex_lock(&mtx_mysql_lve_mutex_governor_ptr);
       mysql_mutex *mm = NULL;
       if (mysql_lve_mutex_governor) {
               buf = (pid_t *)thread_id;
               mm = (mysql_mutex *) my_hash_search(mysql_lve_mutex_governor,
                               (uchar *) buf, sizeof(buf));
               if (mm) {
                       if (!mm->is_in_lve) {
                               mm->put_in_lve = 1;
                       }
               }
       }
       pthread_mutex_unlock(&mtx_mysql_lve_mutex_governor_ptr);
}

void governor_detroy_mysql_thread_info() {
       if (mysql_lve_mutex_governor) {
               pthread_mutex_lock(&mtx_mysql_lve_mutex_governor_ptr);
               my_hash_free(mysql_lve_mutex_governor);
               free(mysql_lve_mutex_governor);
               pthread_mutex_unlock(&mtx_mysql_lve_mutex_governor_ptr);
       }
}

__attribute__((noinline)) int put_in_lve(char *user) {
       if (governor_add_mysql_thread_info()<0) return -1;
       if (mysql_lve_mutex_governor_ptr) {
	       make_snapshot_lvedebug_info(0, 0, 0, mysql_lve_mutex_governor_ptr, "put_in_lve(before)", NULL);
               if (!governor_enter_lve(&lve_cookie, user)) {
                       mysql_lve_mutex_governor_ptr->is_in_lve = 1;
               }
               mysql_lve_mutex_governor_ptr->is_in_mutex = 0;
	       make_snapshot_lvedebug_info(0, 0, 0, mysql_lve_mutex_governor_ptr, "put_in_lve", NULL);
       }
	
       return 0;
}

__attribute__((noinline)) void lve_thr_exit() {
       if (mysql_lve_mutex_governor_ptr && mysql_lve_mutex_governor_ptr->is_in_lve
                      == 1) {
	       get_core_lvedebug_info(mysql_lve_mutex_governor_ptr);
	       make_snapshot_lvedebug_info(0, 0, 0, mysql_lve_mutex_governor_ptr, "lve_thr_exit(pre exit)", NULL);
               governor_lve_exit(&lve_cookie);
               mysql_lve_mutex_governor_ptr->is_in_lve = 0;
	       make_snapshot_lvedebug_info(0, 0, 0, mysql_lve_mutex_governor_ptr, "lve_thr_exit", NULL);
       }
       release_thread_chanks_lvedebug_info();
       governor_remove_mysql_thread_info();
}

__attribute__((noinline)) int my_pthread_lvemutex_lock(my_pthread_fastmutex_t *mp) {
       if (mysql_lve_mutex_governor_ptr) {
               if (mysql_lve_mutex_governor_ptr->is_in_lve == 1) {
		       get_core_lvedebug_info(mysql_lve_mutex_governor_ptr);
                       governor_lve_exit(&lve_cookie);
                       mysql_lve_mutex_governor_ptr->is_in_lve = 2;
               } else if (mysql_lve_mutex_governor_ptr->is_in_lve > 1) {
                       mysql_lve_mutex_governor_ptr->is_in_lve++;
               }
               mysql_lve_mutex_governor_ptr->is_in_mutex++;
	       make_snapshot_lvedebug_info(1, 0, 0, mysql_lve_mutex_governor_ptr, "my_pthread_lvemutex_lock", (void *)&mp->mutex);
       }
       return my_pthread_fastmutex_lock(mp);
}

__attribute__((noinline)) int my_pthread_lvemutex_trylock(pthread_mutex_t *mutex) {
       if (mysql_lve_mutex_governor_ptr) {
               if (mysql_lve_mutex_governor_ptr->is_in_lve == 1) {
			get_core_lvedebug_info(mysql_lve_mutex_governor_ptr);
                        governor_lve_exit(&lve_cookie);
               }
       }
       int ret = pthread_mutex_trylock(mutex);
       if (mysql_lve_mutex_governor_ptr) {
           if (ret != EBUSY){
                if (mysql_lve_mutex_governor_ptr->is_in_lve == 1) {
                       mysql_lve_mutex_governor_ptr->is_in_lve = 2;
                } else if (mysql_lve_mutex_governor_ptr->is_in_lve > 1) {
                       mysql_lve_mutex_governor_ptr->is_in_lve++;
                }
                mysql_lve_mutex_governor_ptr->is_in_mutex++;
               } else {
                if (mysql_lve_mutex_governor_ptr->is_in_lve == 1){
                    if (!governor_enter_lve_light(&lve_cookie)) {
                           mysql_lve_mutex_governor_ptr->is_in_lve = 1;
                    } else {
    			   mysql_lve_mutex_governor_ptr->is_in_lve = 0;
                    }
                }
               }
	     make_snapshot_lvedebug_info((ret != EBUSY)?1:0, 0, 0, mysql_lve_mutex_governor_ptr, (ret != EBUSY)?"my_pthread_lvemutex_trylock(success)":"my_pthread_lvemutex_trylock(unsuccess)", (void *)mutex);
       }
       return ret;
}


__attribute__((noinline)) int my_pthread_lvemutex_unlock(
               pthread_mutex_t *mutex) {
       int ret = pthread_mutex_unlock(mutex);
       if (mysql_lve_mutex_governor_ptr) {
               if ((mysql_lve_mutex_governor_ptr->is_in_lve == 2)
                               && governor_enter_lve_light) {
		       if (!governor_enter_lve_light(&lve_cookie)) {
                               mysql_lve_mutex_governor_ptr->is_in_lve = 1;
                       }
               } else if (mysql_lve_mutex_governor_ptr->is_in_lve > 2) {
                       mysql_lve_mutex_governor_ptr->is_in_lve--;
               }
               mysql_lve_mutex_governor_ptr->is_in_mutex--;
	       make_snapshot_lvedebug_info(0, 1, 0, mysql_lve_mutex_governor_ptr, "my_pthread_lvemutex_unlock", (void *)mutex);
       }
       return ret;
}

__attribute__((noinline)) void my_reserve_slot() {
       if (mysql_lve_mutex_governor_ptr) {
               if (mysql_lve_mutex_governor_ptr->is_in_lve == 1) {
		       get_core_lvedebug_info(mysql_lve_mutex_governor_ptr);
                       governor_lve_exit(&lve_cookie);
                       mysql_lve_mutex_governor_ptr->is_in_lve = 2;
               } else if (mysql_lve_mutex_governor_ptr->is_in_lve > 1) {
                       mysql_lve_mutex_governor_ptr->is_in_lve++;
               }
               mysql_lve_mutex_governor_ptr->is_in_mutex++;
	       make_snapshot_lvedebug_info(0, 0, 0, mysql_lve_mutex_governor_ptr, "my_reserve_slot", NULL);
       }
       return;
}

__attribute__((noinline)) void my_release_slot() {
       if (mysql_lve_mutex_governor_ptr) {
               if ((mysql_lve_mutex_governor_ptr->is_in_lve == 2)
                               && governor_enter_lve_light) {
                        if (!governor_enter_lve_light(&lve_cookie)) {
                               mysql_lve_mutex_governor_ptr->is_in_lve = 1;
                       }
               } else if (mysql_lve_mutex_governor_ptr->is_in_lve > 2) {
                       mysql_lve_mutex_governor_ptr->is_in_lve--;
               }
               mysql_lve_mutex_governor_ptr->is_in_mutex--;
	       make_snapshot_lvedebug_info(0, 0, 0, mysql_lve_mutex_governor_ptr, "my_release_slot", NULL);
       }
       return;
}

void fastmutex_global_init(void)
{
#ifdef _SC_NPROCESSORS_CONF
  cpu_count= sysconf(_SC_NPROCESSORS_CONF);
#endif
}

#endif /* defined(MY_PTHREAD_FASTMUTEX) && defined(SAFE_MUTEX_DEFINED) */
