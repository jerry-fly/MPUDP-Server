/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the terms found in the LICENSE file in the root of this source tree.
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
//#include <bits/siginfo.h>


//#include "bstrlib.h"

#include "assertions.h"
//#include "dynamic_memory_check.h"
//#include "intertask_interface.h"
//#include "log.h"
#include "queue.h"
#include "timer.h"
using namespace std;
int timer_handle_signal(siginfo_t *info);
static sigset_t set;

struct timer_elm_s {
  task_id_t task_id;  ///< Task ID which has requested the timer
  int32_t instance;   ///< Instance of the task which has requested the timer
  timer_t timer;      ///< Unique timer id
  timer_type_t type;  ///< Timer type
  void  *timer_arg;  ///< Optional argument that will be passed when timer expires
  STAILQ_ENTRY(timer_elm_s) entries;  ///< Pointer to next element
};

typedef struct timer_desc_s {
  STAILQ_HEAD(timer_list_head, timer_elm_s) timer_queue;
  pthread_mutex_t timer_list_mutex;
  struct timespec timeout;
} timer_desc_t;

static timer_desc_t timer_desc;



typedef struct timer_arg_s {
  timer_callback_t timer_callback;
  void* timer_callback_arg;
} timer_arg_t;

#define TIMER_SEARCH(vAR, tIMERfIELD, tIMERvALUE, tIMERqUEUE) \
  do {                                                        \
    STAILQ_FOREACH(vAR, tIMERqUEUE, entries) {                \
      if (((vAR)->tIMERfIELD == tIMERvALUE)) break;           \
    }                                                         \
  } while (0)

//------------------------------------------------------------------------------
void free_wrapper(void **ptr) {
  // for debug only
  AssertFatal(ptr, "Trying to free NULL *ptr, ptr=%p", ptr);
  if (ptr) {
    // for debug only
    AssertFatal(*ptr, "Trying to free NULL *ptr, ptr=%p", ptr);
    free(*ptr);
    *ptr = NULL;
  }
}

void usSleep(unsigned int us){
  struct timeval tval;
  tval.tv_sec = us/1000000;
  tval.tv_usec = us%1000000;
  select(0, NULL, NULL, NULL, &tval);
  
}

int timer_handle_signal(siginfo_t *info) {
  struct timer_elm_s *timer_p;
  // MessageDef *message_p;
  // timer_has_expired_t *timer_expired_p;
  task_id_t task_id;
  // int32_t instance;

  /*
   * Get back pointer to timer list element
   */
  timer_p = (struct timer_elm_s *)info->si_ptr;
  // LG: To many traces for msc timer:
  // TMR_DEBUG("Timer with id 0x%lx has expired", (long)timer_p->timer);
  task_id = timer_p->task_id;
  //instance = timer_p->instance;
  //message_p = itti_alloc_new_message(TASK_TIMER, TIMER_HAS_EXPIRED);
  //timer_expired_p = &message_p->ittiMsg.timer_has_expired;
  //timer_expired_p->timer_id = (long)timer_p->timer;
  //timer_expired_p->arg = timer_p->timer_arg;
  timer_arg_t *timer_arg = (timer_arg_t *)timer_p->timer_arg;
  timer_arg->timer_callback(timer_arg->timer_callback_arg);
  /*
   * Timer is a one shot timer, remove it
   */
  if (timer_p->type == TIMER_ONE_SHOT) {
    //         if (timer_delete(timer_p->timer) < 0) {
    //             TMR_DEBUG("Failed to delete timer 0x%lx",
    //             (long)timer_p->timer);
    //         }
    //         TMR_DEBUG("Removed timer 0x%lx", (long)timer_p->timer);
    //         pthread_mutex_lock(&timer_desc.timer_list_mutex);
    //         STAILQ_REMOVE(&timer_desc.timer_queue, timer_p, timer_elm_s,
    //         entries); pthread_mutex_unlock(&timer_desc.timer_list_mutex);
    //         free_wrapper(timer_p);
    //         timer_p = NULL;

    
    if (timer_remove((long)timer_p->timer, NULL) != 0) {
      printf( "Failed to delete timer 0x%lx\n",
                   (long)timer_p->timer);
    }
  }

  /*
   * Notify task of timer expiry
   */
  printf("timer 0x%lx timeout\n", (long)timer_p->timer);
  // if (abs(task_id) > TASK_MAX) {
  //   printf(
        
  //       " TIMER OBJECT %x (with inner timer %x) task_id %d is invalid. \n",
  //       timer_p, timer_p->timer, timer_p->task_id);
  //   //itti_free(TASK_TIMER, message_p);
  //   return -1;
  // }
  // if (itti_send_msg_to_task(task_id, instance, message_p) < 0) {
  //   printf( "Failed to send msg TIMER_HAS_EXPIRED to task %u\n",
  //                task_id);
  //   itti_free(TASK_TIMER, message_p);
  //   return -1;
  // }

  return 0;
}
int signal_handle(int *end) {
  int ret;
  siginfo_t info;

  sigemptyset(&set);
  sigaddset(&set, SIGTIMER);
  sigaddset(&set, SIGUSR1);
  sigaddset(&set, SIGABRT);
  sigaddset(&set, SIGSEGV);
  sigaddset(&set, SIGINT);

  if (sigprocmask(SIG_BLOCK, &set, NULL) < 0) {
    perror("sigprocmask");
    return -1;
  }

  /*
   * Block till a signal is received.
   * * * NOTE: The signals defined by set are required to be blocked at the time
   * * * of the call to sigwait() otherwise sigwait() is not successful.
   */
  if ((ret = sigwaitinfo(&set, &info)) == -1) {
    perror("sigwait");
    return ret;
  }
  // printf("Received signal %d\n", info.si_signo);

  /*
   * Real-time signals are non constant and are therefore not suitable for
   * * * use in switch.
   */
  if (info.si_signo == SIGTIMER) {
    timer_handle_signal(&info);
  } else {
    /*
     * Dispatch the signal to sub-handlers
     */
    switch (info.si_signo) {
      case SIGUSR1:
        printf("Received SIGUSR1\n");
        *end = 1;
        break;

      case SIGSEGV: /* Fall through */
      case SIGABRT:
        printf("Received SIGABORT\n");
        //backtrace_handle_signal(&info);
        break;

      case SIGINT:
        printf("Received SIGINT\n");
        printf("Closing all tasks\n");
        //itti_send_terminate_message(TASK_UNKNOWN);
        *end = 1;
        break;

      default:
        printf("Received unknown signal %d\n", info.si_signo);
        break;
    }
  }

  return 0;
}

int timer_setup(uint32_t interval_sec, uint32_t interval_us, task_id_t task_id,
                int32_t instance, timer_type_t type, void *timer_arg,
                long *timer_id) {
  struct sigevent se;
  struct itimerspec its;
  struct timer_elm_s *timer_p;
  timer_t timer;

  if (timer_id == NULL) {
    return -1;
  }

  AssertFatal(type < TIMER_TYPE_MAX, "Invalid timer type (%d/%d)!\n", type,
              TIMER_TYPE_MAX);
  /*
   * Allocate new timer list element
   */
  timer_p = (struct timer_elm_s *)malloc(sizeof(struct timer_elm_s));

  if (timer_p == NULL) {
    printf( "Failed to create new timer element\n");
    return -1;
  }

  memset(&timer, 0, sizeof(timer_t));
  memset(&se, 0, sizeof(struct sigevent));
  timer_p->task_id = task_id;
  timer_p->instance = instance;
  timer_p->type = type;
  timer_p->timer_arg = timer_arg;
  /*
   * Setting up alarm
   */
  /*
   * Set and enable alarm
   */
  se.sigev_notify = SIGEV_SIGNAL;
  se.sigev_signo = SIGTIMER;
  se.sigev_value.sival_ptr = timer_p;

  /*
   * At the timer creation, the timer structure will be filled in with timer_id,
   * * * which is unique for this process. This id is allocated by kernel and
   * the
   * * * value might be used to distinguish timers.
   */
  if (timer_create(CLOCK_REALTIME, &se, &timer) < 0) {
    printf( "Failed to create timer: (%s:%d)\n", strerror(errno),
                 errno);
    free_wrapper((void **)&timer_p);
    return -1;
  }

  /*
   * Fill in the first expiration value.
   */
  its.it_value.tv_sec = interval_sec;
  its.it_value.tv_nsec = interval_us * 1000;

  if (type == TIMER_PERIODIC) {
    /*
     * Asked for periodic timer. We set the interval time
     */
    its.it_interval.tv_sec = interval_sec;
    its.it_interval.tv_nsec = interval_us * 1000;
  } else {
    /*
     * Asked for one-shot timer. Do not set the interval field
     */
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 0;
  }

  timer_settime(timer, 0, &its, NULL);
  /*
   * Simply set the timer_id argument. so it can be used by caller
   */
  *timer_id = (long)timer;
  printf("Requesting new %s timer with id 0x%lx that expires within "
               "%d sec and %d usec\n",
               type == TIMER_PERIODIC ? "periodic" : "single shot", *timer_id,
               interval_sec, interval_us);
  timer_p->timer = timer;
  /*
   * Lock the queue and insert the timer at the tail
   */
  pthread_mutex_lock(&timer_desc.timer_list_mutex);
  STAILQ_INSERT_TAIL(&timer_desc.timer_queue, timer_p, entries);
  pthread_mutex_unlock(&timer_desc.timer_list_mutex);
  return 0;
}

int timer_remove(long timer_id, void **arg) {
  int rc = 0;
  struct timer_elm_s *timer_p;

  //printf( "Removing timer 0x%lx\n", timer_id);
  pthread_mutex_lock(&timer_desc.timer_list_mutex);
  TIMER_SEARCH(timer_p, timer, ((timer_t)timer_id), &timer_desc.timer_queue);

  /*
   * We didn't find the timer in list
   */
  if (timer_p == NULL) {
    pthread_mutex_unlock(&timer_desc.timer_list_mutex);
    if (arg) *arg = NULL;
    printf( "Didn't find timer 0x%lx in list\n", timer_id);
    return -1;
  }

  STAILQ_REMOVE(&timer_desc.timer_queue, timer_p, timer_elm_s, entries);
  pthread_mutex_unlock(&timer_desc.timer_list_mutex);

  // let user of API get back arg that can be an allocated memory (memory leak).

  if (arg) *arg = timer_p->timer_arg;
  if (timer_delete(timer_p->timer) < 0) {
    printf( "Failed to delete timer 0x%lx\n",
                 (long)timer_p->timer);
    rc = -1;
  }

  printf("REMOVED TIMER OBJECT %p (inner timer 0x%lx) with task_id %d. \n",
               timer_p, (long)timer_p->timer, timer_p->task_id);

  free_wrapper((void **)&timer_p);
  return rc;
}

int timer_init(void) {
  printf( "Initializing TIMER task interface\n");
  memset(&timer_desc, 0, sizeof(timer_desc_t));
  STAILQ_INIT(&timer_desc.timer_queue);
  pthread_mutex_init(&timer_desc.timer_list_mutex, NULL);
  printf( "Initializing TIMER task interface: DONE\n");
  return 0;
}


//------------------------------------------------------------------------------
long int timer_start(long sec, long usec, long *timerID, timer_type_t type,
                                timer_callback_t timer_callback,
                                void *timer_callback_args) {
  int ret;
  long timer_id;
  timer_arg_t *timer_arg = NULL;
  /*
   * Do not start null timer
   */
  if ((sec == 0) && (usec == 0)) {
    return (-1);
  }

  timer_arg = (timer_arg_t*)calloc(1, sizeof(timer_arg_t));
  timer_arg->timer_callback = timer_callback;
  timer_arg->timer_callback_arg = timer_callback_args;

  ret = timer_setup(sec, usec, TASK_TIMER,
                    -1, type, timer_arg,
                    &timer_id);

  if (ret == -1) {
    free_wrapper((void **)&timer_arg);
    return (long)-1;
  }
  *timerID = timer_id;
  return (timer_id);
}

// int main(int argc, const char *argv[]){
//   int rc;
//   //CHECK_INIT_RETURN(signal_mask());
//   int end=0;
//   long time_id[200];
//   CHECK_INIT_RETURN(timer_init());

//   for(int i=0;i<200;i++){
//     time_id[i]=timer_start(3, 0, true, NULL, NULL);
//   }
  

//   while (end == 0) {
//     signal_handle(&end);
//   }
//   //printf("Closing all tasks");
//   return 0;
// }