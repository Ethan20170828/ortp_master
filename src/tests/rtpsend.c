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

#include <ortp/ortp.h>
#include <signal.h>
#include <stdlib.h>

// ���û����_WIN32����꣬˵������Linux�����±���ģ�����Ҫ��������ͷ�ļ�����֮��Ȼ��
#ifndef _WIN32 
#include <sys/types.h>
#include <sys/time.h>
#include <stdio.h>
#endif

int runcond=1;

void stophandler(int signum)
{
	runcond=0;
}

// rtpsend	��������
// filename	ͨ��ORTP�����ļ�������
// dest_ip4addr	Ŀ���ַ
// dest_port	Ŀ��˿�
static const char *help="usage: rtpsend	filename dest_ip4addr dest_port [ --with-clockslide <value> ] [ --with-jitter <milliseconds>]\n";

int main(int argc, char *argv[])
{
	RtpSession *session;		// ����һ��RTP�Ự
	unsigned char buffer[160];	// ����
	int i;
	FILE *infile;				// �ļ�ָ��
	char *ssrc;					// �ź�ͬ��Դָ��
	uint32_t user_ts=0;			// ʱ���
	int clockslide=0;
	int jitter=0;

	// �жϴ���
	if (argc<4){
		printf("%s", help);
		return -1;
	}
	for(i=4;i<argc;i++){
		// ��4������������--with-clockslide����--with-jitter
		if (strcmp(argv[i],"--with-clockslide")==0){
			i++;
			if (i>=argc) {
				printf("%s", help);
				return -1;
			}
			// atoi�������ܽ��ַ���strת����һ�����������ؽ��������str�����ֿ�ͷ��
			// ��������str�ж����������ַ������ת������������ء�
			clockslide=atoi(argv[i]);
			ortp_message("Using clockslide of %i milisecond every 50 packets.",clockslide);
		}else if (strcmp(argv[i],"--with-jitter")==0){
			ortp_message("Jitter will be added to outgoing stream.");
			i++;
			if (i>=argc) {
				printf("%s", help);
				return -1;
			}
			jitter=atoi(argv[i]);
		}
	}

	// ���������payload��ע�᣻
	ortp_init();
	// ʹ��ORTP�������ݴ���ʱ��������һ����������ɶ���Ự���Ľ��պͷ��͡�
	// �������ORTP�е���ģ���֧�֡�Ҫʹ�õ���ģ�飬Ӧ����Ҫ�ڽ���ORTP�ĳ�ʼ��ʱ
	// �Ե��Ƚ��г�ʼ��������Ҫ���ȹ���ĻỰע�ᵽ����ģ���С����������н��պͷ���
	// ����ʱ���������ѯ�ʵ�ǰ�Ự�Ƿ���Խ��з��ͺͽ��գ�������ܽ����շ���������
	// ������һ���Ự��
	ortp_scheduler_init();
	ortp_set_log_level_mask(ORTP_MESSAGE | ORTP_WARNING | ORTP_ERROR);
	// ����һ���µĻỰ
	session = rtp_session_new(RTP_SESSION_SENDONLY);	

	// ���ûỰ������
	rtp_session_set_scheduling_mode(session,1);		// ���ûỰ����ģʽ(���Ự��ӵ��������й���)
	rtp_session_set_blocking_mode(session,1);		// ���ûỰ����ģʽ
	rtp_session_set_connected_mode(session,TRUE);	// ���ûỰ����ģʽ
	rtp_session_set_remote_addr(session, argv[2], atoi(argv[3]));	// ���ûỰԶ�̵�ַ�����������õ���connect���ȴ��ͻ��˹�������
	rtp_session_set_payload_type(session, 0);		// ���ûỰ��payload��ʽ

	// ϵͳ����SSRC���������������rtp_session_set_ssrc������
	// SSRC�������Ϊ��һ���Ự��ʶ���룬ͨ�����ʶ�����֪������Ự����˭�ģ�
	// ��������ʶ���룬��ô�Ժ��Ҿͻ�֪������Ự����˭��
	ssrc=getenv("SSRC");	// ��ȡ��������SSRC�����ݣ����ص���ָ������ݵ�ָ��
	if (ssrc!=NULL) {
		printf("using SSRC=%i.\n",atoi(ssrc));
		// atoi�����ַ���ת��Ϊ����
		rtp_session_set_ssrc(session,atoi(ssrc));
	}

	#ifndef _WIN32
		infile=fopen(argv[1],"r");
	#else
	// ����Ҫ���͵����ݷŵ�һ���ļ��У������������Ҫ������ļ���
	// ������sample��Ҫ���͵��ļ����Ǹձ��������ͼ��
		infile=fopen(argv[1],"rb");
	#endif

	if (infile==NULL) {
		perror("Cannot open file");
		return -1;
	}

/***********************************************************************************************
 ������������ݰ�������Ѿ�����ˣ���Ҫ�����ݶ���������ˣ�����ľ���׼�������������ݰ���
 ***********************************************************************************************/

	// signal�������ݲ���ָ�����źű�������ø��źŵĴ�������
	// ��ָ�����źŵ���ʱ����ת��������ȥִ�С�
	signal(SIGINT, stophandler);
	// ���ļ����������뻺�棬������ɹ�������ļ�β�򷵻�0������ѭ����
	// fread���ǰ�infile�ļ��е�����д��buffer��
	while( ((i = fread(buffer, 1, 160, infile)) > 0) && (runcond) )
	{
		// rtp�����ͺ���(���Ĵ�����: �ӻ������ⷢ������)
		rtp_session_send_with_ts(session, buffer, i, user_ts);
		// ���ͷ���ʱ��
		user_ts+=160;	// ����+160��������ʵ��������Ӧ��+3600
		if (clockslide!=0 && user_ts%(160*50)==0){
			ortp_message("Clock sliding of %i miliseconds now",clockslide);
			rtp_session_make_time_distorsion(session,clockslide);	// ����ʱ��ƫ��
		}
		/*this will simulate a burst of late packets */
		// ���½���ģ��һ���������ӳٰ�
		if (jitter && (user_ts%(8000)==0)) {
			// timespec�ṹ������������Ա��һ�����룬һ��������
			struct timespec pausetime, remtime;
			ortp_message("Simulating late packets now (%i milliseconds)",jitter);
			pausetime.tv_sec=jitter/1000;
			pausetime.tv_nsec=(jitter%1000)*1000000;
			// nanosleep������Linux��ϵͳ���ã���������ͣĳ������ֱ����涨��ʱ���ָ���
			// ���δ�ȵ��涨��ʱ������أ�����-1��
			while(nanosleep(&pausetime,&remtime)==-1 && errno==EINTR){
				pausetime=remtime;
			}
		}
	}

	fclose(infile);
	rtp_session_destroy(session);
	ortp_exit();
	ortp_global_stats_display();

	return 0;
}
