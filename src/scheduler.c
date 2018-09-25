/*
  The oRTP library is an RTP (Realtime Transport Protocol - rfc1889) stack.
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

#include <ortp/ortp.h>
#include "utils.h"
#include "scheduler.h"
#include "rtpsession_priv.h"

// To avoid warning during compile
extern void rtp_session_process (RtpSession * session, uint32_t time, RtpScheduler *sched);

// 初始化调度器，此函数主要是将调度器结构内部的成员置为初始状态；
void rtp_scheduler_init(RtpScheduler *sched)
{
	sched->list = 0;
	sched->time_ = 0;
	/* default to the posix timer */
	// 把RTP定时器的时间设置为调度器的时间，也就是将二者时间保持一致；
	rtp_scheduler_set_timer(sched, &posix_timer);
	// 封装Linux内核提供的线程互斥锁API，用于对关键段进行保护
	ortp_mutex_init(&sched->lock, NULL);
	// 初始化条件变量
	ortp_cond_init(&sched->unblock_select_cond, NULL);
	// max_sessions的值是SessionSet的空间乘8，因为每个字节占8bit空间。
	// 而上文可得到SessionSet的结构占据32个字节，max_sessions就是1024.
	// 说明调度器最多管理1024个rtpsession。
	sched->max_sessions=sizeof(SessionSet) * 8;

	// 初始化，默认下面的变量都是0
	session_set_init(&sched->all_sessions);
	sched->all_max=0;
	
	session_set_init(&sched->r_sessions);
	sched->r_max=0;
	
	session_set_init(&sched->w_sessions);
	sched->w_max=0;
	
	session_set_init(&sched->e_sessions);
	sched->e_max=0;
}

// 新建调度器，简单的分配内存空间，然后置空后调用rtp_scheduler_init初始化。
RtpScheduler * rtp_scheduler_new()
{
	// 初始化调度模块结构体
	RtpScheduler *sched=(RtpScheduler *) ortp_malloc(sizeof(RtpScheduler));
	memset(sched,0,sizeof(RtpScheduler));
	// 初始化结构体，也就是对结构体进行填充；
	rtp_scheduler_init(sched);
	return sched;
}

// 设置调度器的定时器，同时将调度器的时间间隔和定时器的时间间隔统一；
void rtp_scheduler_set_timer(RtpScheduler *sched, RtpTimer *timer)
{
	// thread_running不为0，表示线程正在运行
	if (sched->thread_running){
		ortp_warning("Cannot change timer while the scheduler is running !!");
		return;
	}
	sched->timer = timer;
	/* report the timer increment */
	sched->timer_inc = (timer->interval.tv_usec/1000) + (timer->interval.tv_sec*1000);
}

// 启动调度器，设置线程运行状态后启动调度器的工作线程，线程入口函数为rtp_scheduler_schedule
void rtp_scheduler_start(RtpScheduler *sched)
{
	if (sched->thread_running==0){
		sched->thread_running=1;
		// 线程上锁
		ortp_mutex_lock(&sched->lock);
		// ortp_thread_create函数就是对Linux的线程API进行封装得到的属于ORTP库的创建线程函数；
		// 第三个传参就是线程的线程函数，操作系统调度线程的时候，其实就是调度这个函数；
		ortp_thread_create(&sched->thread, NULL, rtp_scheduler_schedule, (void*)sched);
		// 线程一直阻塞
		ortp_cond_wait(&sched->unblock_select_cond, &sched->lock);
		// 线程解锁
		ortp_mutex_unlock(&sched->lock);
	}
	else 
		ortp_warning("Scheduler thread already running.");

}
void rtp_scheduler_stop(RtpScheduler *sched)
{
	if (sched->thread_running==1)
	{
		sched->thread_running=0;
		ortp_thread_join(sched->thread, NULL);
	}
	else ortp_warning("Scheduler thread is not running.");
}

void rtp_scheduler_destroy(RtpScheduler *sched)
{
	if (sched->thread_running) rtp_scheduler_stop(sched);
	ortp_mutex_destroy(&sched->lock);
	//g_mutex_free(sched->unblock_select_mutex);
	ortp_cond_destroy(&sched->unblock_select_cond);
	ortp_free(sched);
}

// 这个函数是调度器的工作函数，其主要任务就是检查是否有rtpsession到了需要唤醒的时间并更新相应
// session集合的状态。首先启动定时器，然后遍历调度器上存储rtpsession的链表，对每个rtpsession
// 调用rtp_session_process这个处理函数，然后用条件变量唤醒所有可能在等待的select。最后调用
// 定时器的等待函数睡眠并更新运行时间。
void * rtp_scheduler_schedule(void * psched)
{
	RtpScheduler *sched = (RtpScheduler*) psched;
	RtpTimer *timer = sched->timer;		// 初始化调度器的时候已经赋值了
	RtpSession *current;

	/* take this lock to prevent the thread to start until g_thread_create() returns
		because we need sched->thread to be initialized */
	ortp_mutex_lock(&sched->lock);
	// 激活一个线程
	ortp_cond_signal(&sched->unblock_select_cond);	/* unblock the starting thread */
	ortp_mutex_unlock(&sched->lock);
	timer->timer_init();
	// sched->thread_running在上个函数中已经置为1了
	while(sched->thread_running)
	{
		/* do the processing here: */
		ortp_mutex_lock(&sched->lock);

		// 把调度器中的列表赋值给current；sched->list之前赋值为了0，经过一个循环后，激活很多线程，此值不为0了；
		// 也就是说当前有会话需要去处理了；
		current = sched->list;
		/* processing all scheduled rtp sessions */
		// 如果当前的current不等于NULL，表示需要调度器管理
		while (current != NULL)
		{
			ortp_debug("scheduler: processing session=0x%p.\n", current);
			// 进行会话处理
			rtp_session_process(current, sched->time_, sched);
			current = current->next;
		}
		/* wake up all the threads that are sleeping in _select()  */
		// 激活很多线程
		ortp_cond_broadcast(&sched->unblock_select_cond);
		ortp_mutex_unlock(&sched->lock);
		
		/* now while the scheduler is going to sleep, the other threads can compute their
		result mask and see if they have to leave, or to wait for next tick*/
		//ortp_message("scheduler: sleeping.");
		timer->timer_do();
		// sched->timer_inc之前已经赋值过了
		sched->time_ += sched->timer_inc;
	}
	/* when leaving the thread, stop the timer */
	timer->timer_uninit();
	return NULL;
}

// 这个函数用来将来rtpsession添加到调度器中受其管理。
// 这个函数通常不会被直接调用，而是通过rtp_session_set_scheduling_mode这个函数将rtpsession加入调度器。
void rtp_scheduler_add_session(RtpScheduler *sched, RtpSession *session)
{
	RtpSession *oldfirst;
	int i;
	// 首先检查这个rtpsession是否已经添加到了调度器里，不能重复添加。
	// 这里可以看出rtpsession的flag是否包含RTP_SESSION_IN_SCHEDULER标识表示是否添加到了调度器。
	if (session->flags & RTP_SESSION_IN_SCHEDULER){
		/* the rtp session is already scheduled, so return silently */
		return;
	}
	rtp_scheduler_lock(sched);
	/* enqueue the session to the list of scheduled sessions */
	// 将这个rtpsession添加到list这个链表里面，list链表里面存储了调度器中已经存在的会话单元，也就是
	// 将所有的会话单元放到调度器中，调度器通过链表来管理这些会话单元；
	oldfirst=sched->list;
	sched->list=session;
	session->next=oldfirst;
	if (sched->max_sessions==0){
		ortp_error("rtp_scheduler_add_session: max_session=0 !");
	}
	/* find a free pos in the session mask */
	// for循环主要的作用是找到all_sessions这个session集合中空闲的位置。
	// 可以看到循环的次数最多是管理rtpsession的最大数量。
	for (i=0;i<sched->max_sessions;i++){
		// 循环从all_sessions的第一位开始测试，找到最靠前的空闲位置，
		if (!ORTP_FD_ISSET(i,&sched->all_sessions.rtpset)){
			// 然后把这个位置序号写入rtpsession的mask_pos成员，这样以后就可以根据这个成员
			// 从session集合找到这个rtpsession。
			session->mask_pos=i;
			// 之后将这个rtpsession加入all_sessions
			session_set_set(&sched->all_sessions,session);
			/* make a new session scheduled not blockable if it has not started*/
			// 根据这个rtpsession是发送类型还是接收类型加你这个rtpsession加入相应的session集合。
			if (session->flags & RTP_SESSION_RECV_NOT_STARTED) 
				session_set_set(&sched->r_sessions,session);
			if (session->flags & RTP_SESSION_SEND_NOT_STARTED) 
				session_set_set(&sched->w_sessions,session);
			if (i>sched->all_max){
				// 更新调度器的管理的rtpsession的最大数量
				sched->all_max=i;
			}
			break;
		}
	}
	// 将rtpsession的flag添加RTP_SESSION_IN_SCHEDULER标识，表示我的这个会话已经在调度器单元中的，
	// 下次在进来的时候，直接退出这个函数，不需要在重新添加到调度器中了；
	rtp_session_set_flag(session,RTP_SESSION_IN_SCHEDULER);
	rtp_scheduler_unlock(sched);
}

// 这个函数用来将rtpsession移出调度器
void rtp_scheduler_remove_session(RtpScheduler *sched, RtpSession *session)
{
	RtpSession *tmp;
	int cond=1;
	// 检查这个rtpsession是否已经添加到了调度器里，如果没添加过是不能删除的。
	return_if_fail(session!=NULL); 
	if (!(session->flags & RTP_SESSION_IN_SCHEDULER)){
		/* the rtp session is not scheduled, so return silently */
		return;
	}

	rtp_scheduler_lock(sched);
	// 检查list链表中的第一个rtpsession是否就是这个要移出的rtpsession
	tmp=sched->list;
	if (tmp==session){
		// 如果是那么很幸运直接让list指向下一个rtpsession从而从链表中删除这个rtpsession；
		sched->list=tmp->next;
		// 清除rtpsession中flag的RTP_SESSION_IN_SCHEDULER标识；
		rtp_session_unset_flag(session,RTP_SESSION_IN_SCHEDULER);
		// 从all_sessions中清除这个rtpsession；
		session_set_clr(&sched->all_sessions,session);
		rtp_scheduler_unlock(sched);
		return;
	}
	/* go the position of session in the list */
	while(cond){
		if (tmp!=NULL){
			if (tmp->next==session){
				tmp->next=tmp->next->next;
				cond=0;
			}
			else tmp=tmp->next;
		}else {
			/* the session was not found ! */
			ortp_warning("rtp_scheduler_remove_session: the session was not found in the scheduler list!");
			cond=0;
		}
	}
	rtp_session_unset_flag(session,RTP_SESSION_IN_SCHEDULER);
	/* delete the bit in the mask */
	session_set_clr(&sched->all_sessions,session);
	rtp_scheduler_unlock(sched);
}
