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

// ��ʼ�����������˺�����Ҫ�ǽ��������ṹ�ڲ��ĳ�Ա��Ϊ��ʼ״̬��
void rtp_scheduler_init(RtpScheduler *sched)
{
	sched->list = 0;
	sched->time_ = 0;
	/* default to the posix timer */
	// ��RTP��ʱ����ʱ������Ϊ��������ʱ�䣬Ҳ���ǽ�����ʱ�䱣��һ�£�
	rtp_scheduler_set_timer(sched, &posix_timer);
	// ��װLinux�ں��ṩ���̻߳�����API�����ڶԹؼ��ν��б���
	ortp_mutex_init(&sched->lock, NULL);
	// ��ʼ����������
	ortp_cond_init(&sched->unblock_select_cond, NULL);
	// max_sessions��ֵ��SessionSet�Ŀռ��8����Ϊÿ���ֽ�ռ8bit�ռ䡣
	// �����Ŀɵõ�SessionSet�Ľṹռ��32���ֽڣ�max_sessions����1024.
	// ˵��������������1024��rtpsession��
	sched->max_sessions=sizeof(SessionSet) * 8;

	// ��ʼ����Ĭ������ı�������0
	session_set_init(&sched->all_sessions);
	sched->all_max=0;
	
	session_set_init(&sched->r_sessions);
	sched->r_max=0;
	
	session_set_init(&sched->w_sessions);
	sched->w_max=0;
	
	session_set_init(&sched->e_sessions);
	sched->e_max=0;
}

// �½����������򵥵ķ����ڴ�ռ䣬Ȼ���ÿպ����rtp_scheduler_init��ʼ����
RtpScheduler * rtp_scheduler_new()
{
	// ��ʼ������ģ��ṹ��
	RtpScheduler *sched=(RtpScheduler *) ortp_malloc(sizeof(RtpScheduler));
	memset(sched,0,sizeof(RtpScheduler));
	// ��ʼ���ṹ�壬Ҳ���ǶԽṹ�������䣻
	rtp_scheduler_init(sched);
	return sched;
}

// ���õ������Ķ�ʱ����ͬʱ����������ʱ�����Ͷ�ʱ����ʱ����ͳһ��
void rtp_scheduler_set_timer(RtpScheduler *sched, RtpTimer *timer)
{
	// thread_running��Ϊ0����ʾ�߳���������
	if (sched->thread_running){
		ortp_warning("Cannot change timer while the scheduler is running !!");
		return;
	}
	sched->timer = timer;
	/* report the timer increment */
	sched->timer_inc = (timer->interval.tv_usec/1000) + (timer->interval.tv_sec*1000);
}

// �����������������߳�����״̬�������������Ĺ����̣߳��߳���ں���Ϊrtp_scheduler_schedule
void rtp_scheduler_start(RtpScheduler *sched)
{
	if (sched->thread_running==0){
		sched->thread_running=1;
		// �߳�����
		ortp_mutex_lock(&sched->lock);
		// ortp_thread_create�������Ƕ�Linux���߳�API���з�װ�õ�������ORTP��Ĵ����̺߳�����
		// ���������ξ����̵߳��̺߳���������ϵͳ�����̵߳�ʱ����ʵ���ǵ������������
		ortp_thread_create(&sched->thread, NULL, rtp_scheduler_schedule, (void*)sched);
		// �߳�һֱ����
		ortp_cond_wait(&sched->unblock_select_cond, &sched->lock);
		// �߳̽���
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

// ��������ǵ������Ĺ�������������Ҫ������Ǽ���Ƿ���rtpsession������Ҫ���ѵ�ʱ�䲢������Ӧ
// session���ϵ�״̬������������ʱ����Ȼ������������ϴ洢rtpsession��������ÿ��rtpsession
// ����rtp_session_process�����������Ȼ�������������������п����ڵȴ���select��������
// ��ʱ���ĵȴ�����˯�߲���������ʱ�䡣
void * rtp_scheduler_schedule(void * psched)
{
	RtpScheduler *sched = (RtpScheduler*) psched;
	RtpTimer *timer = sched->timer;		// ��ʼ����������ʱ���Ѿ���ֵ��
	RtpSession *current;

	/* take this lock to prevent the thread to start until g_thread_create() returns
		because we need sched->thread to be initialized */
	ortp_mutex_lock(&sched->lock);
	// ����һ���߳�
	ortp_cond_signal(&sched->unblock_select_cond);	/* unblock the starting thread */
	ortp_mutex_unlock(&sched->lock);
	timer->timer_init();
	// sched->thread_running���ϸ��������Ѿ���Ϊ1��
	while(sched->thread_running)
	{
		/* do the processing here: */
		ortp_mutex_lock(&sched->lock);

		// �ѵ������е��б�ֵ��current��sched->list֮ǰ��ֵΪ��0������һ��ѭ���󣬼���ܶ��̣߳���ֵ��Ϊ0�ˣ�
		// Ҳ����˵��ǰ�лỰ��Ҫȥ�����ˣ�
		current = sched->list;
		/* processing all scheduled rtp sessions */
		// �����ǰ��current������NULL����ʾ��Ҫ����������
		while (current != NULL)
		{
			ortp_debug("scheduler: processing session=0x%p.\n", current);
			// ���лỰ����
			rtp_session_process(current, sched->time_, sched);
			current = current->next;
		}
		/* wake up all the threads that are sleeping in _select()  */
		// ����ܶ��߳�
		ortp_cond_broadcast(&sched->unblock_select_cond);
		ortp_mutex_unlock(&sched->lock);
		
		/* now while the scheduler is going to sleep, the other threads can compute their
		result mask and see if they have to leave, or to wait for next tick*/
		//ortp_message("scheduler: sleeping.");
		timer->timer_do();
		// sched->timer_inc֮ǰ�Ѿ���ֵ����
		sched->time_ += sched->timer_inc;
	}
	/* when leaving the thread, stop the timer */
	timer->timer_uninit();
	return NULL;
}

// ���������������rtpsession��ӵ����������������
// �������ͨ�����ᱻֱ�ӵ��ã�����ͨ��rtp_session_set_scheduling_mode���������rtpsession�����������
void rtp_scheduler_add_session(RtpScheduler *sched, RtpSession *session)
{
	RtpSession *oldfirst;
	int i;
	// ���ȼ�����rtpsession�Ƿ��Ѿ���ӵ��˵�����������ظ���ӡ�
	// ������Կ���rtpsession��flag�Ƿ����RTP_SESSION_IN_SCHEDULER��ʶ��ʾ�Ƿ���ӵ��˵�������
	if (session->flags & RTP_SESSION_IN_SCHEDULER){
		/* the rtp session is already scheduled, so return silently */
		return;
	}
	rtp_scheduler_lock(sched);
	/* enqueue the session to the list of scheduled sessions */
	// �����rtpsession��ӵ�list����������棬list��������洢�˵��������Ѿ����ڵĻỰ��Ԫ��Ҳ����
	// �����еĻỰ��Ԫ�ŵ��������У�������ͨ��������������Щ�Ự��Ԫ��
	oldfirst=sched->list;
	sched->list=session;
	session->next=oldfirst;
	if (sched->max_sessions==0){
		ortp_error("rtp_scheduler_add_session: max_session=0 !");
	}
	/* find a free pos in the session mask */
	// forѭ����Ҫ���������ҵ�all_sessions���session�����п��е�λ�á�
	// ���Կ���ѭ���Ĵ�������ǹ���rtpsession�����������
	for (i=0;i<sched->max_sessions;i++){
		// ѭ����all_sessions�ĵ�һλ��ʼ���ԣ��ҵ��ǰ�Ŀ���λ�ã�
		if (!ORTP_FD_ISSET(i,&sched->all_sessions.rtpset)){
			// Ȼ������λ�����д��rtpsession��mask_pos��Ա�������Ժ�Ϳ��Ը��������Ա
			// ��session�����ҵ����rtpsession��
			session->mask_pos=i;
			// ֮�����rtpsession����all_sessions
			session_set_set(&sched->all_sessions,session);
			/* make a new session scheduled not blockable if it has not started*/
			// �������rtpsession�Ƿ������ͻ��ǽ������ͼ������rtpsession������Ӧ��session���ϡ�
			if (session->flags & RTP_SESSION_RECV_NOT_STARTED) 
				session_set_set(&sched->r_sessions,session);
			if (session->flags & RTP_SESSION_SEND_NOT_STARTED) 
				session_set_set(&sched->w_sessions,session);
			if (i>sched->all_max){
				// ���µ������Ĺ����rtpsession���������
				sched->all_max=i;
			}
			break;
		}
	}
	// ��rtpsession��flag���RTP_SESSION_IN_SCHEDULER��ʶ����ʾ�ҵ�����Ự�Ѿ��ڵ�������Ԫ�еģ�
	// �´��ڽ�����ʱ��ֱ���˳��������������Ҫ��������ӵ����������ˣ�
	rtp_session_set_flag(session,RTP_SESSION_IN_SCHEDULER);
	rtp_scheduler_unlock(sched);
}

// �������������rtpsession�Ƴ�������
void rtp_scheduler_remove_session(RtpScheduler *sched, RtpSession *session)
{
	RtpSession *tmp;
	int cond=1;
	// ������rtpsession�Ƿ��Ѿ���ӵ��˵���������û��ӹ��ǲ���ɾ���ġ�
	return_if_fail(session!=NULL); 
	if (!(session->flags & RTP_SESSION_IN_SCHEDULER)){
		/* the rtp session is not scheduled, so return silently */
		return;
	}

	rtp_scheduler_lock(sched);
	// ���list�����еĵ�һ��rtpsession�Ƿ�������Ҫ�Ƴ���rtpsession
	tmp=sched->list;
	if (tmp==session){
		// �������ô������ֱ����listָ����һ��rtpsession�Ӷ���������ɾ�����rtpsession��
		sched->list=tmp->next;
		// ���rtpsession��flag��RTP_SESSION_IN_SCHEDULER��ʶ��
		rtp_session_unset_flag(session,RTP_SESSION_IN_SCHEDULER);
		// ��all_sessions��������rtpsession��
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
