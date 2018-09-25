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

/** 
 * \file rtpsession.h
 * \brief The RtpSession api
 *
 * The RtpSession objects represent a RTP session: once it is configured with 
 * local and remote network addresses and a payload type is given, it let you send
 * and recv a media stream.
**/


#ifndef RTPSESSION_H
#define RTPSESSION_H


#include <ortp/port.h>
#include <ortp/rtp.h>
#include <ortp/payloadtype.h>
#include <ortp/rtpprofile.h>
#include <ortp/sessionset.h>
#include <ortp/rtcp.h>
#include <ortp/str_utils.h>
#include <ortp/rtpsignaltable.h>
#include <ortp/event.h>



typedef enum {
	RTP_SESSION_RECVONLY,
	RTP_SESSION_SENDONLY,
	RTP_SESSION_SENDRECV
} RtpSessionMode;


/*! Jitter buffer parameters
*/
typedef struct _JBParameters{
	int min_size; /**< in milliseconds*/
	int nom_size; /**< idem */
	int max_size; /**< idem */
	bool_t adaptive;
	bool_t pad[3];
	int max_packets; /**< max number of packets allowed to be queued in the jitter buffer */
} JBParameters;

typedef struct _JitterControl
{
	unsigned int count;
	int jitt_comp;   /* the user jitt_comp in miliseconds*/
	int jitt_comp_ts; /* the jitt_comp converted in rtp time (same unit as timestamp) */
	int adapt_jitt_comp_ts;
	int64_t slide;
	int64_t prev_slide;
	float jitter;
	int olddiff;
	float inter_jitter;	/* interarrival jitter as defined in the RFC */
	int corrective_step;
	int corrective_slide;
	uint64_t cum_jitter_buffer_size; /*in timestamp units*/
	unsigned int cum_jitter_buffer_count; /*used for computation of jitter buffer size*/
	int clock_rate;
	bool_t adaptive;
	bool_t enabled;
} JitterControl;

// 用来记录等待信息的结构
typedef struct _WaitPoint
{
	ortp_mutex_t lock;	// 互斥锁
	ortp_cond_t  cond;	// 用来唤醒阻塞状态的条件变量
	uint32_t time;		// 用来记录等待的时间点的时间(毫秒)，此值记录的时间是和RtpScheduler的time_成员比较的。 
	bool_t wakeup;		// 标志是否在等待唤醒的标志
} WaitPoint;

typedef struct _RtpTransport
{
	void *data;
	ortp_socket_t (*t_getsocket)(struct _RtpTransport *t);
	int  (*t_sendto)(struct _RtpTransport *t, mblk_t *msg , int flags, const struct sockaddr *to, socklen_t tolen);
	int  (*t_recvfrom)(struct _RtpTransport *t, mblk_t *msg, int flags, struct sockaddr *from, socklen_t *fromlen);
	struct _RtpSession *session;//<back pointer to the owning session, set by oRTP
	void  (*t_close)(struct _RtpTransport *transport, void *userData);
}  RtpTransport;

typedef struct _OrtpNetworkSimulatorParams{
	int enabled;
	float max_bandwidth; /*IP bandwidth, in bit/s*/
	float loss_rate;
}OrtpNetworkSimulatorParams;

typedef struct _OrtpNetworkSimulatorCtx{
	OrtpNetworkSimulatorParams params;
	int bit_budget;
	int qsize;
	queue_t q;
	struct timeval last_check;
}OrtpNetworkSimulatorCtx;

// ORTP完成对rtp数据的流控功能，所有这些变量都定义在这个结构体中
typedef struct _RtpStream
{
	ortp_socket_t socket;
	struct _RtpTransport *tr; 
	int sockfamily;
	int max_rq_size;
	int time_jump;
	uint32_t ts_jump;
	queue_t rq;
	queue_t tev_rq;
	mblk_t *cached_mp;
	int loc_port;
#ifdef ORTP_INET6
	struct sockaddr_storage rem_addr;
#else
	struct sockaddr_in rem_addr;
#endif
	int rem_addrlen;
	void *QoSHandle;
	unsigned long QoSFlowID;
	JitterControl jittctl;
	// 应用程序发送其第一个时间戳的调度器时间；
	uint32_t snd_time_offset;/*the scheduler time when the application send its first timestamp*/
	// 被应用程序发送的第一个应用程序时间戳；
	uint32_t snd_ts_offset;	/* the first application timestamp sent by the application */
	// 添加到用户offset上的一个随机数，用来产生流的时间戳；
	uint32_t snd_rand_offset;	/* a random number added to the user offset to make the stream timestamp*/
	// 流上最后发送的时间戳；
	uint32_t snd_last_ts;	/* the last stream timestamp sended */
	// 前三个时间变量是offset结尾的，分别标记了第一个时间戳，包括调度器的时间偏移，在应用开始发送
	// 数据时，应用发送数据的时间偏移，也即是自己的时间戳，还有一个随机数用来添加到偏移上的，
	// 而第四个才是真正标记流里面当前最新发送的数据的时间戳。

	// 应用程序询问其第一个时间戳时的调度时间，这里询问意指获取接收到的数据包，应该指开始接收
	// 数据时的调度时间；
	uint32_t rcv_time_offset; /*the scheduler time when the application ask for its first timestamp*/
	// 第一个流的时间戳----此应该指第一个rtp包到来时其流上带的时间戳值；
	uint32_t rcv_ts_offset;  /* the first stream timestamp */
	// 被应用程序询问的第一个user时间戳----此应该指应用接收数据流时的时间；
	uint32_t rcv_query_ts_offset;	/* the first user timestamp asked by the application */
	// 应用程序得到的流的最后一个时间戳----此应该指应用程序收到的最后一个rtp包的时间戳,
	// 是包里的时间戳值, 而非应用自己的时间。
	uint32_t rcv_last_ts;	/* the last stream timestamp got by the application */
	// 被应用程序询问的最后一个应用程序时间戳----此处应该指应用收最后一个包时的应用时间,
	// 是应用按照payload类型及其采样率增长的时间戳记录,不是系统时间,也不是包里的时间。
	uint32_t rcv_last_app_ts; /* the last application timestamp asked by the application */	
	// 最后一个返回的采样的时间戳,仅仅对于连续的音频而言接收相对于发送来讲存在一个问题,
	// 就是接收数据包时当前系统有个时间,数据包里面也有时间戳记录的时间,调度器也有记录时间。
	// 而对于发送,当前应用的时间就是给包的时间戳时间,这两个值对于发送来讲是一样的。
	uint32_t rcv_last_ret_ts; /* the timestamp of the last sample returned (only for continuous audio)*/
	// 在socket上最后接收的扩展的序列号
	uint32_t hwrcv_extseq; /* last received on socket extended sequence number */
	// 每次发送报告包后,该变量更新为hwrcv_extseq,因此是最近发送rtcp报告包时的最高扩展序列号。
	uint32_t hwrcv_seq_at_last_SR;
	// 每收到一个rtp包,该变量加1,在 rtcp报告报构造好后,
	// 该变量就清为零,因此说明这个变量计数的是从上一个报告包以来接收的rtp包数目。
	uint32_t hwrcv_since_last_SR;
	// 前三个变量就是计算出丢包率。首先，最近一次丢失包数(就是自从上一次sr或rr发送以来)通过
	// hwrcv_extseq->hwrcv_seq_at_last_SR->hwrcv_since_last_SR计算得到

	// 最后一个接收到的sr的NTP时间戳,取的是中间的32bit。这个值也是报告包中上LSR值的来源。
	uint32_t last_rcv_SR_ts;     /* NTP timestamp (middle 32 bits) of last received SR */
	// 最后一个sr 接收到的时间,这个时间是用系统当前的时间来表示的。
	// 这个值记录了接收到最后一个SR时的系统时间,再发送当前报告包时,
	// 再次获取系统当前时间,然后二者相减,得到的值乘以65536得到以1/65536为单位的时间值。
	struct timeval last_rcv_SR_time;   /* time at which last SR was received  */
	// 发送序列号。累加变量,保存会话的序列号的增长。
	uint16_t snd_seq; /* send sequence number */
	// 在最后 发送的一个rtcp sr包中记录的发送者发送的 rtp 包总数。这个变量把这个值记录了下来。
	// 记录这个值是为了实现协议中规定的:如果之前的rtcp 包发送之后到当前再次发送 rtcp 包, 
	// 这期间如果发送了rtp 包,则发送rtcp SR 报告包,否则只需发送 rtcp RR 包就可以了。
	uint32_t last_rtcp_packet_count; /*the sender's octet count in the last sent RTCP SR*/
	// 用于rtcp 发送者报告的 payload 字节数,数据来源。
	// 这个变量保存了从开始发送到发送这个 rtcp 报告包时发送的字 节总数,但不包括头部和填充。
	uint32_t sent_payload_bytes; /*used for RTCP sender reports*/
	// 上面这些时间相关都是用于rtcp包的。

	// 用于带宽评估；
	unsigned int sent_bytes; /* used for bandwidth estimation */
	//  用于带宽评估
	struct timeval send_bw_start; /* used for bandwidth estimation */
	// 上面两个变量用于计算发送带宽,start记录的开始时间,sent_bytes 记录了发送的字节数,
	// 该值没调用 rtp 接口发送数据后都会进行累加更新。记录一次带宽值后,清为零,之后进行；


	// 下一次带宽估计的计算。
	unsigned int recv_bytes; /* used for bandwidth estimation */
	// 下一次带宽估计的计算。
	struct timeval recv_bw_start; /* used for bandwidth estimation */
	// 作用和处理逻辑都同上面发送部分；


	rtp_stats_t stats;
	int recv_errno;
	int send_errno;
	int snd_socket_size;
	int rcv_socket_size;
	int ssrc_changed_thres;
	jitter_stats_t jitter_stats;
}RtpStream;

typedef struct _RtcpStream
{
	ortp_socket_t socket;
	int sockfamily;
	struct _RtpTransport *tr; 
	mblk_t *cached_mp;
	int loc_port;
#ifdef ORTP_INET6
	struct sockaddr_storage rem_addr;
#else
	struct sockaddr_in rem_addr;
#endif
	int rem_addrlen;
	int interval;
	uint32_t last_rtcp_report_snt_r;	/* the time of the last rtcp report sent, in recv timestamp unit */
	uint32_t last_rtcp_report_snt_s;	/* the time of the last rtcp report sent, in send timestamp unit */
	uint32_t rtcp_report_snt_interval_r; /* the interval in timestamp unit for receive path between rtcp report sent */
	uint32_t rtcp_report_snt_interval_s; /* the interval in timestamp unit for send path between rtcp report sent */
	bool_t enabled; /*tells whether we can send RTCP packets */
} RtcpStream;

typedef struct _RtpSession RtpSession;


/**
 * An object representing a bi-directional RTP session.
 * It holds sockets, jitter buffer, various counters (timestamp, sequence numbers...)
 * Applications SHOULD NOT try to read things within the RtpSession object but use
 * instead its public API (the rtp_session_* methods) where RtpSession is used as a 
 * pointer.
 * rtp_session_new() allocates and initialize a RtpSession.
**/
// 该结构体即是一个会话的抽象，与会话相关的各种信息都定义在该结构体上或者能够通过该结构体找到。
// 要使用ORTP进行媒体数据的传输，需要先创建一个会话，之后所有数据的传输都在会话上完成或者基于会话完成。
struct _RtpSession
{
	// 用来构建链表，调度器中的RtpSession链表就使用了这个成员来构建链表，
	// 如果不用调度器管理，那么置NULL。
	RtpSession *next;	/* next RtpSession, when the session are enqueued by the scheduler */
	// 记录了此RtpSession在调度器中的session集合中的位置，及Session集合中第几个bit位代表这个
	// RtpSession。如果不用调度器管理，那么这个成员无用。
	int mask_pos;	/* the position in the scheduler mask of RtpSession : do not move this field: it is part of the ABI since the session_set macros use it*/
	// 存储发送和接受关键数据的一个结构体，其中我们主要关心结构体的wp。
	// wp成员是前文介绍过的_WaitPoint结构。
	struct {
		RtpProfile *profile;
		int pt;
		unsigned int ssrc;
		WaitPoint wp;
		int telephone_events_pt;	/* the payload type used for telephony events */
	} snd,rcv;
	unsigned int inc_ssrc_candidate;
	int inc_same_ssrc_count;
	int hw_recv_pt; /* recv payload type before jitter buffer */
	int recv_buf_size;
	RtpSignalTable on_ssrc_changed;
	RtpSignalTable on_payload_type_changed;
	RtpSignalTable on_telephone_event_packet;
	RtpSignalTable on_telephone_event;
	RtpSignalTable on_timestamp_jump;
	RtpSignalTable on_network_error;
	RtpSignalTable on_rtcp_bye;
	struct _OList *signal_tables;
	struct _OList *eventqs;
	msgb_allocator_t allocator;
	RtpStream rtp;
	RtcpStream rtcp;
	RtpSessionMode mode;
	// 此RtpSession属于的调度器，如果不属于调度器管理则为NULL。
	struct _RtpScheduler *sched;
	// 一个标识量，其中每一位bit都代表一个状态。
	uint32_t flags;
	int dscp;
	int multicast_ttl;
	int multicast_loopback;
	void * user_data;
	/* FIXME: Should be a table for all session participants. */
	struct timeval last_recv_time; /* Time of receiving the RTP/RTCP packet. */
	mblk_t *pending;
	/* telephony events extension */
	mblk_t *current_tev;		/* the pending telephony events */
	mblk_t *sd;
	queue_t contributing_sources;
	unsigned int lost_packets_test_vector;
	unsigned int interarrival_jitter_test_vector;
	unsigned int delay_test_vector;
	float rtt;/*last round trip delay calculated*/
	OrtpNetworkSimulatorCtx *net_sim_ctx;
	bool_t symmetric_rtp;
	bool_t permissive; /*use the permissive algorithm*/
	bool_t use_connect; /* use connect() on the socket */
	bool_t ssrc_set;
};
	



#ifdef __cplusplus
extern "C"
{
#endif

/* public API */
ORTP_PUBLIC RtpSession *rtp_session_new(int mode);
ORTP_PUBLIC void rtp_session_set_scheduling_mode(RtpSession *session, int yesno);
ORTP_PUBLIC void rtp_session_set_blocking_mode(RtpSession *session, int yesno);
ORTP_PUBLIC void rtp_session_set_profile(RtpSession *session, RtpProfile *profile);
ORTP_PUBLIC void rtp_session_set_send_profile(RtpSession *session,RtpProfile *profile);
ORTP_PUBLIC void rtp_session_set_recv_profile(RtpSession *session,RtpProfile *profile);
ORTP_PUBLIC RtpProfile *rtp_session_get_profile(RtpSession *session);
ORTP_PUBLIC RtpProfile *rtp_session_get_send_profile(RtpSession *session);
ORTP_PUBLIC RtpProfile *rtp_session_get_recv_profile(RtpSession *session);
ORTP_PUBLIC int rtp_session_signal_connect(RtpSession *session,const char *signal_name, RtpCallback cb, unsigned long user_data);
ORTP_PUBLIC int rtp_session_signal_disconnect_by_callback(RtpSession *session,const char *signal_name, RtpCallback cb);
ORTP_PUBLIC void rtp_session_set_ssrc(RtpSession *session, uint32_t ssrc);
ORTP_PUBLIC uint32_t rtp_session_get_send_ssrc(RtpSession* session);
ORTP_PUBLIC uint32_t rtp_session_get_recv_ssrc(RtpSession *session);
ORTP_PUBLIC void rtp_session_set_seq_number(RtpSession *session, uint16_t seq);
ORTP_PUBLIC uint16_t rtp_session_get_seq_number(RtpSession *session);

ORTP_PUBLIC void rtp_session_enable_jitter_buffer(RtpSession *session , bool_t enabled);
ORTP_PUBLIC bool_t rtp_session_jitter_buffer_enabled(const RtpSession *session);
ORTP_PUBLIC void rtp_session_set_jitter_buffer_params(RtpSession *session, const JBParameters *par);
ORTP_PUBLIC void rtp_session_get_jitter_buffer_params(RtpSession *session, JBParameters *par);

/*deprecated jitter control functions*/
ORTP_PUBLIC void rtp_session_set_jitter_compensation(RtpSession *session, int milisec);
ORTP_PUBLIC void rtp_session_enable_adaptive_jitter_compensation(RtpSession *session, bool_t val);
ORTP_PUBLIC bool_t rtp_session_adaptive_jitter_compensation_enabled(RtpSession *session);

ORTP_PUBLIC void rtp_session_set_time_jump_limit(RtpSession *session, int miliseconds);
ORTP_PUBLIC int rtp_session_set_local_addr(RtpSession *session,const char *addr, int rtp_port, int rtcp_port);
ORTP_PUBLIC int rtp_session_get_local_port(const RtpSession *session);

ORTP_PUBLIC int
rtp_session_set_remote_addr_full (RtpSession * session, const char * rtp_addr, int rtp_port, const char * rtcp_addr, int rtcp_port);
/*same as previous function, old name:*/
ORTP_PUBLIC int rtp_session_set_remote_addr_and_port (RtpSession * session, const char * addr, int rtp_port, int rtcp_port);
ORTP_PUBLIC int rtp_session_set_remote_addr(RtpSession *session,const char *addr, int port);
/* alternatively to the set_remote_addr() and set_local_addr(), an application can give
a valid socket (potentially connect()ed )to be used by the RtpSession */
ORTP_PUBLIC void rtp_session_set_sockets(RtpSession *session, int rtpfd, int rtcpfd);
ORTP_PUBLIC void rtp_session_set_transports(RtpSession *session, RtpTransport *rtptr, RtpTransport *rtcptr);

/*those methods are provided for people who wants to send non-RTP messages using the RTP/RTCP sockets */
ORTP_PUBLIC ortp_socket_t rtp_session_get_rtp_socket(const RtpSession *session);
ORTP_PUBLIC ortp_socket_t rtp_session_get_rtcp_socket(const RtpSession *session);


/* QOS / DSCP */
ORTP_PUBLIC int rtp_session_set_dscp(RtpSession *session, int dscp);
ORTP_PUBLIC int rtp_session_get_dscp(const RtpSession *session);


/* Packet info */
ORTP_PUBLIC int rtp_session_set_pktinfo(RtpSession *session, int activate);

/* Multicast methods */
ORTP_PUBLIC int rtp_session_set_multicast_ttl(RtpSession *session, int ttl);
ORTP_PUBLIC int rtp_session_get_multicast_ttl(RtpSession *session);

ORTP_PUBLIC int rtp_session_set_multicast_loopback(RtpSession *session, int yesno);
ORTP_PUBLIC int rtp_session_get_multicast_loopback(RtpSession *session);



ORTP_PUBLIC int rtp_session_set_send_payload_type(RtpSession *session, int paytype);
ORTP_PUBLIC int rtp_session_get_send_payload_type(const RtpSession *session);

ORTP_PUBLIC int rtp_session_get_recv_payload_type(const RtpSession *session);
ORTP_PUBLIC int rtp_session_set_recv_payload_type(RtpSession *session, int pt);

ORTP_PUBLIC int rtp_session_set_payload_type(RtpSession *session, int pt);

ORTP_PUBLIC void rtp_session_set_symmetric_rtp (RtpSession * session, bool_t yesno);

ORTP_PUBLIC void rtp_session_set_connected_mode(RtpSession *session, bool_t yesno);

ORTP_PUBLIC void rtp_session_enable_rtcp(RtpSession *session, bool_t yesno);

ORTP_PUBLIC void rtp_session_set_rtcp_report_interval(RtpSession *session, int value_ms);

ORTP_PUBLIC void rtp_session_set_ssrc_changed_threshold(RtpSession *session, int numpackets);

/*low level recv and send functions */
ORTP_PUBLIC mblk_t * rtp_session_recvm_with_ts (RtpSession * session, uint32_t user_ts);
ORTP_PUBLIC mblk_t * rtp_session_create_packet(RtpSession *session,int header_size, const uint8_t *payload, int payload_size);
ORTP_PUBLIC mblk_t * rtp_session_create_packet_with_data(RtpSession *session, uint8_t *payload, int payload_size, void (*freefn)(void*));
ORTP_PUBLIC mblk_t * rtp_session_create_packet_in_place(RtpSession *session,uint8_t *buffer, int size, void (*freefn)(void*) );
ORTP_PUBLIC int rtp_session_sendm_with_ts (RtpSession * session, mblk_t *mp, uint32_t userts);
/* high level recv and send functions */
ORTP_PUBLIC int rtp_session_recv_with_ts(RtpSession *session, uint8_t *buffer, int len, uint32_t ts, int *have_more);
ORTP_PUBLIC int rtp_session_send_with_ts(RtpSession *session, const uint8_t *buffer, int len, uint32_t userts);

/* event API*/
ORTP_PUBLIC void rtp_session_register_event_queue(RtpSession *session, OrtpEvQueue *q);
ORTP_PUBLIC void rtp_session_unregister_event_queue(RtpSession *session, OrtpEvQueue *q);


/* IP bandwidth usage estimation functions, returning bits/s*/
ORTP_PUBLIC float rtp_session_compute_send_bandwidth(RtpSession *session);
ORTP_PUBLIC float rtp_session_compute_recv_bandwidth(RtpSession *session);

ORTP_PUBLIC void rtp_session_send_rtcp_APP(RtpSession *session, uint8_t subtype, const char *name, const uint8_t *data, int datalen);

ORTP_PUBLIC uint32_t rtp_session_get_current_send_ts(RtpSession *session);
ORTP_PUBLIC uint32_t rtp_session_get_current_recv_ts(RtpSession *session);
ORTP_PUBLIC void rtp_session_flush_sockets(RtpSession *session);
ORTP_PUBLIC void rtp_session_release_sockets(RtpSession *session);
ORTP_PUBLIC void rtp_session_resync(RtpSession *session);
ORTP_PUBLIC void rtp_session_reset(RtpSession *session);
ORTP_PUBLIC void rtp_session_destroy(RtpSession *session);

ORTP_PUBLIC const rtp_stats_t * rtp_session_get_stats(const RtpSession *session);
ORTP_PUBLIC const jitter_stats_t * rtp_session_get_jitter_stats( const RtpSession *session );
ORTP_PUBLIC void rtp_session_reset_stats(RtpSession *session);

ORTP_PUBLIC void rtp_session_set_data(RtpSession *session, void *data);
ORTP_PUBLIC void *rtp_session_get_data(const RtpSession *session);

ORTP_PUBLIC void rtp_session_set_recv_buf_size(RtpSession *session, int bufsize);
ORTP_PUBLIC void rtp_session_set_rtp_socket_send_buffer_size(RtpSession * session, unsigned int size);
ORTP_PUBLIC void rtp_session_set_rtp_socket_recv_buffer_size(RtpSession * session, unsigned int size);

/* in use with the scheduler to convert a timestamp in scheduler time unit (ms) */
ORTP_PUBLIC uint32_t rtp_session_ts_to_time(RtpSession *session,uint32_t timestamp);
ORTP_PUBLIC uint32_t rtp_session_time_to_ts(RtpSession *session, int millisecs);
/* this function aims at simulating senders with "imprecise" clocks, resulting in 
rtp packets sent with timestamp uncorrelated with the system clock .
This is only availlable to sessions working with the oRTP scheduler */
ORTP_PUBLIC void rtp_session_make_time_distorsion(RtpSession *session, int milisec);

/*RTCP functions */
ORTP_PUBLIC void rtp_session_set_source_description(RtpSession *session, const char *cname,
	const char *name, const char *email, const char *phone, 
    const char *loc, const char *tool, const char *note);
ORTP_PUBLIC void rtp_session_add_contributing_source(RtpSession *session, uint32_t csrc, 
    const char *cname, const char *name, const char *email, const char *phone, 
    const char *loc, const char *tool, const char *note);
ORTP_PUBLIC void rtp_session_remove_contributing_sources(RtpSession *session, uint32_t csrc);
ORTP_PUBLIC mblk_t* rtp_session_create_rtcp_sdes_packet(RtpSession *session);

ORTP_PUBLIC void rtp_session_get_last_recv_time(RtpSession *session, struct timeval *tv);
ORTP_PUBLIC int rtp_session_bye(RtpSession *session, const char *reason);

ORTP_PUBLIC int rtp_session_get_last_send_error_code(RtpSession *session);
ORTP_PUBLIC void rtp_session_clear_send_error_code(RtpSession *session);
ORTP_PUBLIC int rtp_session_get_last_recv_error_code(RtpSession *session);
ORTP_PUBLIC void rtp_session_clear_recv_error_code(RtpSession *session);


ORTP_PUBLIC float rtp_session_get_round_trip_propagation(RtpSession *session);


ORTP_PUBLIC void rtp_session_enable_network_simulation(RtpSession *session, const OrtpNetworkSimulatorParams *params);
ORTP_PUBLIC void rtp_session_rtcp_set_lost_packet_value( RtpSession *session, const unsigned int value );
ORTP_PUBLIC void rtp_session_rtcp_set_jitter_value(RtpSession *session, const unsigned int value );
ORTP_PUBLIC void rtp_session_rtcp_set_delay_value(RtpSession *session, const unsigned int value );
ORTP_PUBLIC mblk_t * rtp_session_pick_with_cseq (RtpSession * session, const uint16_t sequence_number);
/*private */
ORTP_PUBLIC void rtp_session_init(RtpSession *session, int mode);
#define rtp_session_set_flag(session,flag) (session)->flags|=(flag)
#define rtp_session_unset_flag(session,flag) (session)->flags&=~(flag)
ORTP_PUBLIC void rtp_session_uninit(RtpSession *session);
ORTP_PUBLIC void rtp_session_dispatch_event(RtpSession *session, OrtpEvent *ev);

#ifdef __cplusplus
}
#endif

#endif
