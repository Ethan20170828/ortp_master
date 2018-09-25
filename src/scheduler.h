/*
  The oRTP library is an RTP (Realtime Transport Protocol - rfc3550) stack.
  Copyright (C) 2001  Simon MORLAT simon.morlat@linphone.org

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "ortp/rtpsession.h"
#include "ortp/sessionset.h"
#include "rtptimer.h"

// 该结构体是调度模块的数据结构
struct _RtpScheduler {
 	// list保存了所有要处理的会话，r/w/e的意义类似于select，在这里分别代表接收、发送以及异常；
 	// select函数是外部表现为阻塞式，内部表现为非阻塞式(内部进行轮询方式)；
	RtpSession *list;	/* list of scheduled sessions*/
	// 调度器管理的所有的rtpsession的session集合
	SessionSet	all_sessions;  /* mask of scheduled sessions */
	// 调度器曾经管理的rtpsession的最大的数量，也就是all_sessions集合中最多使用了多少
	// bit位，检查标志位时只要检查不超过这个数的bit位就行。
	int		all_max;		/* the highest pos in the all mask */
	// 调度器管理的所有的用来接收数据的rtpsession的session集合
	SessionSet  r_sessions;		/* mask of sessions that have a recv event */
	int		r_max;
	// 调度器管理的所有的用来发送数据的rtpsession的session集合
	SessionSet	w_sessions;		/* mask of sessions that have a send event */
	int 		w_max;
	// 本意应该是调度器管理的发生错误的rtpsession的session集合，但是实际上并没有使用
	SessionSet	e_sessions;	/* mask of session that have error event */
	int		e_max;
	// 调度器管理的rtpsession数量的上限
	int max_sessions;		/* the number of position in the masks */
    /* GMutex  *unblock_select_mutex; */
    // 调度器更新了标识量后用来通知select函数继续轮询的条件变量
	ortp_cond_t   unblock_select_cond;
	// 操作调度器的互斥锁 
	ortp_mutex_t	lock;
	// 用来不断检查时间是否到期后更新标识量的线程
	ortp_thread_t thread;
	// thread线程是否需要继续循环的标识量
	int thread_running;
	// 调度器的定时器
	struct _RtpTimer *timer;
	// 调度器的计时器，记录了从调度器开始运行线程后过去的时间（毫秒） 
	uint32_t time_;       /*number of miliseconds elapsed since the start of the thread */
	// 定时器timer的时间间隔（毫秒）
	uint32_t timer_inc;	/* the timer increment in milisec */
};

typedef struct _RtpScheduler RtpScheduler;
	
RtpScheduler * rtp_scheduler_new(void);
void rtp_scheduler_set_timer(RtpScheduler *sched,RtpTimer *timer);
void rtp_scheduler_start(RtpScheduler *sched);
void rtp_scheduler_stop(RtpScheduler *sched);
void rtp_scheduler_destroy(RtpScheduler *sched);

void rtp_scheduler_add_session(RtpScheduler *sched, RtpSession *session);
void rtp_scheduler_remove_session(RtpScheduler *sched, RtpSession *session);

void * rtp_scheduler_schedule(void * sched);

#define rtp_scheduler_lock(sched)	ortp_mutex_lock(&(sched)->lock)
#define rtp_scheduler_unlock(sched)	ortp_mutex_unlock(&(sched)->lock)

/* void rtp_scheduler_add_set(RtpScheduler *sched, SessionSet *set); */

ORTP_PUBLIC RtpScheduler * ortp_get_scheduler(void);
#endif
