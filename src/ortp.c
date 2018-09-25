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


#ifdef _MSC_VER
#include "ortp-config-win32.h"
#elif HAVE_CONFIG_H
#include "ortp-config.h"
#endif
#include "ortp/ortp.h"
#include "scheduler.h"

rtp_stats_t ortp_global_stats;

#ifdef ENABLE_MEMCHECK
int ortp_allocations=0;
#endif


#ifdef HAVE_SRTP
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#include <srtp/srtp.h>
#endif

// 是一个全局变量调度器。说明全局只有这么一个唯一的调度器。
// 如果要使用调度器功能必须调用一次这个函数，ortp库不会
// 自动调用这个函数，必须用户手动调用，否则就无法使用调度器功能。
RtpScheduler *__ortp_scheduler;

extern void av_profile_init(RtpProfile *profile);

// 初始化随机数发生器(通过时间来获取随机数)
static void init_random_number_generator(){
	struct timeval t;
	// 第二个参数一般都为空，因为我们一般都只是为了获得当前时间，而不用获取时区的数值
	gettimeofday(&t,NULL);
	// 在这个时间内(也就是这么多数)获取一个随机数
	srandom(t.tv_usec+t.tv_sec);
}


#ifdef WIN32
static bool_t win32_init_sockets(void){
	WORD wVersionRequested;
	WSADATA wsaData;
	int i;
	
	wVersionRequested = MAKEWORD(2,0);
	if( (i = WSAStartup(wVersionRequested,  &wsaData))!=0)
	{
		ortp_error("Unable to initialize windows socket api, reason: %d (%s)",i,getWinSocketError(i));
		return FALSE;
	}
	return TRUE;
}
#endif

static int ortp_initialized=0;

/**
 *	Initialize the oRTP library. You should call this function first before using
 *	oRTP API.
**/
void ortp_init()
{
	// ortp_initialized全局变量如果是1则表示初始化好了，如果是0则表示需要初始化；
	// 作用是保证ortp_init函数重复调用不会出现问题；
	// ortp_init函数第一次被调用会初始化，第二次被调用就不会初始化了；
	if (ortp_initialized) 
		return;
	ortp_initialized++;

#ifdef WIN32
	win32_init_sockets();
#endif

	// 记录当前系统能够识别的profile格式
	av_profile_init(&av_profile);
	// 全局变量清零
	ortp_global_stats_reset();
	// 初始化随机数发生器
	init_random_number_generator();

#ifdef HAVE_SRTP
	if (srtp_init() != err_status_ok) {
		ortp_fatal("Couldn't initialize SRTP library.");
	}
	err_reporting_init("oRTP");
#endif

	ortp_message("oRTP-" ORTP_VERSION " initialized.");
}


/**
 *	Initialize the oRTP scheduler. You only have to do that if you intend to use the
 *	scheduled mode of the #RtpSession in your application.
 *	
**/
// 初始化调度器(这里就是初始化定时器)，就是仲裁机构，这段代码的运行决定把一些时间交给谁；
// Linux操作系统中的调度器的本质就是一段代码；
// 在ORTP库中调度器相当于一个定时器，定时器不断循环执行，时间到了，去调度一个程序来执行；
// 这个调度器用来调度任务中的多个会话的，从而决定哪个会话工作；
void ortp_scheduler_init()
{
	static bool_t initialized = FALSE;
	if (initialized) 
		return;
	initialized = TRUE;
#ifdef __hpux
	/* on hpux, we must block sigalrm on the main process, because signal delivery
	is ?random?, well, sometimes the SIGALRM goes to both the main thread and the 
	scheduler thread */
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set,SIGALRM);
	sigprocmask(SIG_BLOCK,&set,NULL);
#endif /* __hpux */

	// 创建一个调度
	__ortp_scheduler = rtp_scheduler_new();
	// 启动调度任务
	rtp_scheduler_start(__ortp_scheduler);
	//sleep(1);
}


/**
 * Gracefully uninitialize the library, including shutdowning the scheduler if it was started.
 *	
**/
void ortp_exit()
{
	ortp_initialized--;
	if (ortp_initialized==0){
		if (__ortp_scheduler!=NULL)
		{
			rtp_scheduler_destroy(__ortp_scheduler);
			__ortp_scheduler=NULL;
		}
#ifdef HAVE_SRTP_SHUTDOWN
		srtp_shutdown();
#endif
	}
}

// 这个函数是用来获取全局唯一的调度器的，当调用过ortp_scheduler_init之后，
// 就可以调用此函数来获取可用的调度器了。在ortp中使用的调度器只能通过这个函数
// 来获取，不能自己新建调度器，因为调度器中使用了一些全局唯一的资源，
// 如果有多个调度器运行就会造成冲突。
RtpScheduler * ortp_get_scheduler()
{
	if (__ortp_scheduler==NULL) ortp_error("Cannot use the scheduled mode: the scheduler is not "
									"started. Call ortp_scheduler_init() at the begginning of the application.");
	return __ortp_scheduler;
}


/**
 * Display global statistics (cumulative for all RtpSession)
**/
void ortp_global_stats_display()
{
	rtp_stats_display(&ortp_global_stats,"Global statistics");
#ifdef ENABLE_MEMCHECK	
	printf("Unfreed allocations: %i\n",ortp_allocations);
#endif
}

/**
 * Print RTP statistics.
**/
void rtp_stats_display(const rtp_stats_t *stats, const char *header)
{
#ifndef WIN32
  ortp_log(ORTP_MESSAGE,
	   "oRTP-stats:\n   %s :",
	   header);
  ortp_log(ORTP_MESSAGE,
	   " number of rtp packet sent=%lld",
	   (long long)stats->packet_sent);
  ortp_log(ORTP_MESSAGE,
	   " number of rtp bytes sent=%lld bytes",
	   (long long)stats->sent);
  ortp_log(ORTP_MESSAGE,
	   " number of rtp packet received=%lld",
	   (long long)stats->packet_recv);
  ortp_log(ORTP_MESSAGE,
	   " number of rtp bytes received=%lld bytes",
	   (long long)stats->hw_recv);
  ortp_log(ORTP_MESSAGE,
	   " number of incoming rtp bytes successfully delivered to the application=%lld ",
	   (long long)stats->recv);
  ortp_log(ORTP_MESSAGE,
	   " number of rtp packet lost=%lld",
	   (long long) stats->cum_packet_loss);
  ortp_log(ORTP_MESSAGE,
	   " number of rtp packets received too late=%lld",
	   (long long)stats->outoftime);
  ortp_log(ORTP_MESSAGE,
	   " number of bad formatted rtp packets=%lld",
	   (long long)stats->bad);
  ortp_log(ORTP_MESSAGE,
	   " number of packet discarded because of queue overflow=%lld",
	   (long long)stats->discarded);
#else
  ortp_log(ORTP_MESSAGE,
	   "oRTP-stats:\n   %s :",
	   header);
  ortp_log(ORTP_MESSAGE,
	   " number of rtp packet sent=%I64d",
	   (uint64_t)stats->packet_sent);
  ortp_log(ORTP_MESSAGE,
	   " number of rtp bytes sent=%I64d bytes",
	   (uint64_t)stats->sent);
  ortp_log(ORTP_MESSAGE,
	   " number of rtp packet received=%I64d",
	   (uint64_t)stats->packet_recv);
  ortp_log(ORTP_MESSAGE,
	   " number of rtp bytes received=%I64d bytes",
	   (uint64_t)stats->hw_recv);
  ortp_log(ORTP_MESSAGE,
	   " number of incoming rtp bytes successfully delivered to the application=%I64d ",
	   (uint64_t)stats->recv);
  ortp_log(ORTP_MESSAGE,
	   " number of rtp packet lost=%I64d",
	   (uint64_t) stats->cum_packet_loss);
  ortp_log(ORTP_MESSAGE,
	   " number of rtp packets received too late=%I64d",
	   (uint64_t)stats->outoftime);
  ortp_log(ORTP_MESSAGE,
		 " number of bad formatted rtp packets=%I64d",
	   (uint64_t)stats->bad);
  ortp_log(ORTP_MESSAGE,
	   " number of packet discarded because of queue overflow=%I64d",
	   (uint64_t)stats->discarded);
#endif
}

// 函数作用是用来给表示状态的全局变量来赋值初值，这里初值是0
void ortp_global_stats_reset(){
	memset(&ortp_global_stats,0,sizeof(rtp_stats_t));
}

rtp_stats_t *ortp_get_global_stats(){
	return &ortp_global_stats;
}

void rtp_stats_reset(rtp_stats_t *stats){
	memset((void*)stats,0,sizeof(rtp_stats_t));
}


/**
 * This function give the opportunity to programs to check if the libortp they link to
 * has the minimum version number they need.
 *
 * Returns: true if ortp has a version number greater or equal than the required one.
**/
bool_t ortp_min_version_required(int major, int minor, int micro){
	return ((major*1000000) + (minor*1000) + micro) <= 
		   ((ORTP_MAJOR_VERSION*1000000) + (ORTP_MINOR_VERSION*1000) + ORTP_MICRO_VERSION);
}
