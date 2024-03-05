#include <pthread.h>
#include <stdlib.h>
#include <stdbool.h>
#include <setjmp.h>
#include <assert.h>
#include "ec440threads.h"
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h> 
#include <string.h>
#include <signal.h>
#include <sys/time.h>

//givens: 

#define MAX_THREADS 128			/* number of threads you support */
#define THREAD_STACK_SIZE (1<<15)	/* size of stack in bytes */
#define QUANTUM (50 * 1000)		/* quantum in usec */

enum thread_status
{
 TS_EXITED,
 TS_RUNNING,
 TS_READY,
 TS_UNINITIALIZED
};

/*****************************************************
       
  Thread control block
                   
******************************************************/

struct thread_control_block {
	pthread_t id;
	u_int8_t *stack; //stack ptr
	jmp_buf context; 
	enum thread_status state; 
  void* retval; //return value from exit	
};

/*****************************************************
     
  Global Variables
                  
******************************************************/

struct thread_control_block threads[MAX_THREADS];
unsigned int current = 0;
unsigned int active_threads = 0;

/*****************************************************

  Schedulers
                    
******************************************************/

static void schedule(int signal){

  //if main thread is the only active th
  if(active_threads <= 1){
    if(threads[0].state == TS_EXITED){
      threads[current].state = TS_RUNNING; 
    }
    return; 
  }

  int prev = current;
  bool found = false; 

  for(int i = 1; i < MAX_THREADS; i++){
    int next = (current + i) % MAX_THREADS;
    if(threads[next].state == TS_READY){
      current = next;
      found = true; 
      break; 
    }
  }

  if(!found){
    return; 
  }

  if(threads[prev].state != TS_EXITED){
    if(setjmp(threads[prev].context) == 0){
      threads[prev].state = TS_READY;
      threads[current].state = TS_RUNNING; 
      longjmp(threads[current].context,1); 
    }
  }
  else{
    threads[current].state = TS_RUNNING; 
    longjmp(threads[current].context,1); 
    
    free(threads[prev].stack); 
    threads[prev].stack = NULL; 
    active_threads--;
  }
}

static void schedule_wrap(int signal){ schedule(signal); }

static void scheduler_init(){
	
	current = 0; 

  //setup array with basic necessites 
	for(int i = 0; i < MAX_THREADS; i++){
    threads[i].state = TS_UNINITIALIZED; //uninitialize all but main
	}

  threads[0].state = TS_RUNNING; 
  threads[0].stack = NULL;
  threads[0].id = (pthread_t)0;

	active_threads++; 

	struct sigaction sa; 
	memset(&sa, 0, sizeof(sa)); 
	sa.sa_handler = schedule_wrap; 
	sigaction(SIGALRM, &sa, NULL); 

	struct itimerval timer;
	timer.it_interval.tv_sec = 0;
  timer.it_interval.tv_usec = QUANTUM;
	timer.it_value.tv_sec = 0;
  timer.it_value.tv_usec = QUANTUM;
        
	setitimer(ITIMER_REAL, &timer, NULL);	
}

static void schedule(int signal) __attribute__((unused));

/*****************************************************
   
  pthread functions
       
******************************************************/

int pthread_create(
	pthread_t *thread,
	const pthread_attr_t *attr,
	void *(*start_routine) (void *),
	void *arg)
{ 
  static bool is_first_call = true;
  if (is_first_call) {
    is_first_call = false;
    scheduler_init();
  } 

  if(active_threads > MAX_THREADS){
    fprintf(stderr, "ERROR: Too many threads!\n");
    return -1; 
  }

  int found = -1; 
  for(int i = 1; i < MAX_THREADS; i++){
    if(threads[i].state == TS_UNINITIALIZED){
      found = i; 
      break; 
    }
  }

  if(found == -1){
    fprintf(stderr, "ERROR: Failure to find open slot!\n");
    return -1; 
  }

  threads[found].state = TS_READY;
		
  threads[found].stack = malloc(THREAD_STACK_SIZE); 
  if(!threads[found].stack){
    threads[found].state = TS_UNINITIALIZED;
    fprintf(stderr, "ERROR: Failure to allocate stack!\n");
    return -1;
  }
  threads[found].id = (pthread_t)found; 
  *thread = threads[found].id;

  threads[found].retval = NULL;

  void *top = threads[found].stack + THREAD_STACK_SIZE - 8;
  *(uintptr_t*)top = (uintptr_t)pthread_exit;

  //setting up registers for top, program counter, start_routine, and arguments for start_routine
  set_reg(&threads[found].context, JBL_RSP, (unsigned long)top);
  set_reg(&threads[found].context, JBL_PC, (unsigned long)start_thunk);
  set_reg(&threads[found].context, JBL_R12, (unsigned long)start_routine);
  set_reg(&threads[found].context, JBL_R13, (unsigned long)arg);

  active_threads++;
  return 0; 
}

void pthread_exit(void *value_ptr){

	threads[current].state = TS_EXITED; 

	if(value_ptr != NULL){
    threads[current].retval = value_ptr; 
  }

	schedule_wrap(0); 

	__builtin_unreachable(); 
}

pthread_t pthread_self(void){
	return threads[current].id; 
}

int pthread_join(pthread_t thread, void **retval){

  if(thread == pthread_self()){
    return -1; 
  }

  if(thread < 0 || thread >= MAX_THREADS ){
    return -1; 
  }

  bool found = false; 

  for(int i = 0; i < MAX_THREADS; i++){
    if(threads[i].state != TS_UNINITIALIZED && threads[i].id == thread){
      while(threads[i].state != TS_EXITED){
        schedule_wrap(0); 
      }

      if(retval != NULL){
        *retval = threads[i].retval; 
      }

      threads[i].state = TS_EXITED; 
      
      if(threads[i].stack != NULL){
        free(threads[i].stack); 
        threads[i].stack = NULL; 
      }

      found = true; 
      break; 
    }
  }

  if(!found){return -1;}
  return 0; 
}


