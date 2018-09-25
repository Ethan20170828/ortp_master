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

// 如果没定义_WIN32这个宏，说明是在Linux环境下编译的，则需要添加下面的头文件，反之亦然；
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

// rtpsend	程序名字
// filename	通过ORTP发送文件的名字
// dest_ip4addr	目标地址
// dest_port	目标端口
static const char *help="usage: rtpsend	filename dest_ip4addr dest_port [ --with-clockslide <value> ] [ --with-jitter <milliseconds>]\n";

int main(int argc, char *argv[])
{
	RtpSession *session;		// 定义一个RTP会话
	unsigned char buffer[160];	// 缓存
	int i;
	FILE *infile;				// 文件指针
	char *ssrc;					// 信号同步源指针
	uint32_t user_ts=0;			// 时间戳
	int clockslide=0;
	int jitter=0;

	// 判断传参
	if (argc<4){
		printf("%s", help);
		return -1;
	}
	for(i=4;i<argc;i++){
		// 第4个参数可能是--with-clockslide或者--with-jitter
		if (strcmp(argv[i],"--with-clockslide")==0){
			i++;
			if (i>=argc) {
				printf("%s", help);
				return -1;
			}
			// atoi函数功能将字符串str转换成一个整数并返回结果。参数str以数字开头，
			// 当函数从str中读到非数字字符则结束转换并将结果返回。
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

	// 函数完成了payload的注册；
	ortp_init();
	// 使用ORTP进行数据传输时，可以在一个任务上完成多个会话流的接收和发送。
	// 这得益于ORTP中调度模块的支持。要使用调度模块，应用需要在进行ORTP的初始化时
	// 对调度进行初始化，将需要调度管理的会话注册到调度模块中。这样当进行接收和发送
	// 操作时，先向调度询问当前会话是否可以进行发送和接收，如果不能进行收发操作，则
	// 处理下一个会话。
	ortp_scheduler_init();
	ortp_set_log_level_mask(ORTP_MESSAGE | ORTP_WARNING | ORTP_ERROR);
	// 创建一个新的会话
	session = rtp_session_new(RTP_SESSION_SENDONLY);	

	// 设置会话的属性
	rtp_session_set_scheduling_mode(session,1);		// 设置会话调度模式(将会话添加到调度器中管理)
	rtp_session_set_blocking_mode(session,1);		// 设置会话阻塞模式
	rtp_session_set_connected_mode(session,TRUE);	// 设置会话连接模式
	rtp_session_set_remote_addr(session, argv[2], atoi(argv[3]));	// 设置会话远程地址，里面最后调用的是connect，等待客户端过来连接
	rtp_session_set_payload_type(session, 0);		// 设置会话的payload格式

	// 系统存在SSRC环境变量，则调用rtp_session_set_ssrc函数；
	// SSRC可以理解为是一个会话的识别码，通过这个识别码就知道这个会话属于谁的；
	// 如果有这个识别码，那么以后我就会知道这个会话属于谁；
	ssrc=getenv("SSRC");	// 获取环境变量SSRC的内容，返回的是指向该内容的指针
	if (ssrc!=NULL) {
		printf("using SSRC=%i.\n",atoi(ssrc));
		// atoi，由字符串转换为整数
		rtp_session_set_ssrc(session,atoi(ssrc));
	}

	#ifndef _WIN32
		infile=fopen(argv[1],"r");
	#else
	// 事先要发送的内容放到一个文件中，我们这里就是要打开这个文件；
	// 在我们sample中要发送的文件就是刚编码出来的图像；
		infile=fopen(argv[1],"rb");
	#endif

	if (infile==NULL) {
		perror("Cannot open file");
		return -1;
	}

/***********************************************************************************************
 上面的网络数据包的填充已经完成了，需要的数据都被填充完了，下面的就是准备发送网络数据包了
 ***********************************************************************************************/

	// signal函数根据参数指定的信号编号来设置该信号的处理函数。
	// 当指定的信号到达时会跳转到处理函数去执行。
	signal(SIGINT, stophandler);
	// 将文件数据流读入缓存，如果不成功或读到文件尾则返回0，结束循环。
	// fread就是把infile文件中的内容写到buffer中
	while( ((i = fread(buffer, 1, 160, infile)) > 0) && (runcond) )
	{
		// rtp包发送函数(核心处理环节: 从缓存向外发送数据)
		rtp_session_send_with_ts(session, buffer, i, user_ts);
		// 发送方的时间
		user_ts+=160;	// 这里+160，但是在实际中这里应该+3600
		if (clockslide!=0 && user_ts%(160*50)==0){
			ortp_message("Clock sliding of %i miliseconds now",clockslide);
			rtp_session_make_time_distorsion(session,clockslide);	// 设置时间偏移
		}
		/*this will simulate a burst of late packets */
		// 以下将会模拟一个爆发的延迟包
		if (jitter && (user_ts%(8000)==0)) {
			// timespec结构体中有两个成员，一个是秒，一个是纳秒
			struct timespec pausetime, remtime;
			ortp_message("Simulating late packets now (%i milliseconds)",jitter);
			pausetime.tv_sec=jitter/1000;
			pausetime.tv_nsec=(jitter%1000)*1000000;
			// nanosleep函数是Linux的系统调用，功能是暂停某个进程直到你规定的时间后恢复，
			// 如果未等到规定的时间而返回，返回-1。
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
