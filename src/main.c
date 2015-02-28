#include <stdio.h>
#include <err.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include <time.h>
#include <bsd/stdlib.h>

#define MAX 64

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"
#define COLOR(color_macro, string) color_macro string ANSI_COLOR_RESET
#define RED(string) COLOR(ANSI_COLOR_RED, string)
#define GREEN(string) COLOR(ANSI_COLOR_GREEN, string)
#define YELLOW(string) COLOR(ANSI_COLOR_YELLOW, string)
#define BLUE(string) COLOR(ANSI_COLOR_BLUE, string)
#define MAGENTA(string) COLOR(ANSI_COLOR_MAGENTA, string)
#define CYAN(string) COLOR(ANSI_COLOR_CYAN, string)

#define MSG_ARR "customer " RED("%d") " arrives: arrival time (" GREEN("%d") \
  "), service time (" YELLOW("%d") "), priority (" MAGENTA("%d") "). \n"

#define MSG_SERV_END "The clerk finishes the service to customer " RED("%d")  \
  " at time " CYAN( "%s" )
#define MSG_SERV_START "The clerk starts serving customer " RED("%d") " at time " CYAN("%s")
#define MSG_WAIT "customer " RED("%d") " waits for the finish of customer " RED("%d") "\n"

typedef struct {
  pthread_t thread_id;
  int id;
  int arrival;
  int running;
  int priority;
} Customer;

pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER,
                mutex2 = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t idle = PTHREAD_COND_INITIALIZER;
int is_idle = 1, current_id;

Customer *waiting_customer[MAX];
int no_of_waiting_customer = 0;

void customer_init (char * str, Customer *customer) {
  customer->id = strtonum(strtok(str, ":"), 1, INT_MAX, NULL);
  customer->arrival = strtonum(strtok(NULL, ","), 1, INT_MAX, NULL);
  customer->running = strtonum(strtok(NULL, ","), 1, INT_MAX, NULL);
  customer->priority = strtonum(str, 1, INT_MAX, NULL);
}

#define grt(ATTR, NEXT_COND) \
  do {\
    if (customer_a->ATTR < customer_b->ATTR)       { return -1; }\
    else if (customer_a->ATTR > customer_b->ATTR)  { return 1;  }\
    else if (customer_a->ATTR == customer_b->ATTR) { NEXT_COND; }\
  } while (0)
#define lst(ATTR, NEXT_COND) \
  do {\
    if (customer_a->ATTR > customer_b->ATTR)       { return -1; }\
    else if (customer_a->ATTR < customer_b->ATTR)  { return 1;  }\
    else if (customer_a->ATTR == customer_b->ATTR) { NEXT_COND; }\
  } while (0)

int customer_compare (const Customer * customer_a, const Customer * customer_b) {
  grt( priority,
      lst( arrival,
        lst( running,
          lst( id,
            return 0 ))));
  return 0;
}

int customer_compare_w (const void * a, const void * b) {
  return customer_compare ((Customer *) a, (Customer *) b);
}

/* This function will allow only one active thread to obtain
   the service; the current thread will not be able to run the rest code
   until it obtains the right for service */
static void request_service (Customer *self) {
  // lock mutex1;
  // if (clerk idle & no_of_waiting_customer ==0)
  //   {set clerk to busy; unlock mutex1; return;}
  pthread_mutex_lock (&mutex1);
  if (is_idle == 1 && no_of_waiting_customer == 0) {
    is_idle = 0;
    pthread_mutex_unlock (&mutex1);
    return;
  }
  //lock mutex2;
  pthread_mutex_lock (&mutex2);
  //add customer to the queue of waiting customers and sort it by priority rules;
  /*Assume that after sorting, the first customer in the queue is the next one
   * for service */
  waiting_customer[no_of_waiting_customer] = self;
  no_of_waiting_customer++;

  qsort (waiting_customer, no_of_waiting_customer, sizeof (Customer),
      customer_compare_w);
  //unlock mutex 2;
  pthread_mutex_unlock (&mutex2);

  //while(clerk is busy or the current customer is not the first one in the sorted queue)
  //  idle.wait( );
  while (is_idle == 0 || customer_compare (waiting_customer[0], self) != 0) {
    printf(MSG_WAIT, self->id, current_id);
    pthread_cond_wait (&idle, &mutex1);
  }

  /* Note that the pthread_cond_wait routine will automatically and atomically
   * unlock mutex1 while it waits. */
  //take current customer from queue and execute and re-sort the queue;

  waiting_customer[0] = waiting_customer[no_of_waiting_customer - 1];

  qsort (waiting_customer, no_of_waiting_customer, sizeof (Customer),
      customer_compare_w);

  no_of_waiting_customer--;
  is_idle = 0;
  pthread_mutex_unlock (&mutex1);
}

/* This function will make the clerk available again and wake up the waiting
 * threads to again compete for the service */
void release_service () {
  //set clerk to idle;
  //idle.signal( );
  pthread_mutex_lock (&mutex1);
  is_idle = 1;
  pthread_mutex_unlock (&mutex1);
  pthread_cond_broadcast (&idle);
}

void thread_control (Customer *customer) {
  //sleep until arrival time;
  struct timespec arr, serv;
  struct timespec rem;
  time_t rawtime;

  arr.tv_nsec = customer->arrival * 1000 * 1000; // x 10 milliseconds
  serv.tv_nsec = customer->running * 1000 * 1000; // x 10 milliseconds

  nanosleep(&arr, &rem);

  printf(MSG_ARR, customer->id, customer->arrival, customer->running,
      customer->priority);
  //populate thread structures;
  request_service (customer);
  current_id = customer->id;

  //sleep for the service time;
  time (&rawtime);
  printf(MSG_SERV_START, customer->id, ctime (&rawtime) );

  nanosleep(&serv, &rem);

  time (&rawtime);
  printf(MSG_SERV_END, customer->id, ctime (&rawtime));

  release_service();
}

void * thread_control_w (void *customer) {
  thread_control((Customer *) customer);
  return 0;
}

//for each thread in the file, create thread to execute thread_control;
int main (int argc, char *argv[]) {
  FILE *file;
  char *line;
  size_t len = 0;
  int i;
  Customer * customer;

  if (argc < 2) {
    errx(1, "usage: sim [data]");
  }

  file = fopen (argv[1], "r");
  getline (&line, &len, file);

  for (i=0; getline (&line, &len, file) != -1; i++) {
    customer = (Customer *) malloc (sizeof (customer));
    customer_init (line, customer);
    pthread_create (&customer->thread_id, NULL, thread_control_w,
      customer);
  }

  free (line);
  fclose (file);
  pthread_exit(0);
  return 0;
}

