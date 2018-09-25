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

// �ýṹ���ǵ���ģ������ݽṹ
struct _RtpScheduler {
 	// list����������Ҫ����ĻỰ��r/w/e������������select��������ֱ������ա������Լ��쳣��
 	// select�������ⲿ����Ϊ����ʽ���ڲ�����Ϊ������ʽ(�ڲ�������ѯ��ʽ)��
	RtpSession *list;	/* list of scheduled sessions*/
	// ��������������е�rtpsession��session����
	SessionSet	all_sessions;  /* mask of scheduled sessions */
	// ���������������rtpsession������������Ҳ����all_sessions���������ʹ���˶���
	// bitλ������־λʱֻҪ��鲻�����������bitλ���С�
	int		all_max;		/* the highest pos in the all mask */
	// ��������������е������������ݵ�rtpsession��session����
	SessionSet  r_sessions;		/* mask of sessions that have a recv event */
	int		r_max;
	// ��������������е������������ݵ�rtpsession��session����
	SessionSet	w_sessions;		/* mask of sessions that have a send event */
	int 		w_max;
	// ����Ӧ���ǵ���������ķ��������rtpsession��session���ϣ�����ʵ���ϲ�û��ʹ��
	SessionSet	e_sessions;	/* mask of session that have error event */
	int		e_max;
	// �����������rtpsession����������
	int max_sessions;		/* the number of position in the masks */
    /* GMutex  *unblock_select_mutex; */
    // �����������˱�ʶ��������֪ͨselect����������ѯ����������
	ortp_cond_t   unblock_select_cond;
	// �����������Ļ����� 
	ortp_mutex_t	lock;
	// �������ϼ��ʱ���Ƿ��ں���±�ʶ�����߳�
	ortp_thread_t thread;
	// thread�߳��Ƿ���Ҫ����ѭ���ı�ʶ��
	int thread_running;
	// �������Ķ�ʱ��
	struct _RtpTimer *timer;
	// �������ļ�ʱ������¼�˴ӵ�������ʼ�����̺߳��ȥ��ʱ�䣨���룩 
	uint32_t time_;       /*number of miliseconds elapsed since the start of the thread */
	// ��ʱ��timer��ʱ���������룩
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
