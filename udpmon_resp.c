/*
     udpmon_resp.c     R. Hughes-Jones  The University of Manchester

*/

/*
   Copyright (c) 2002,2003,2004,2005,2006,2007,2008,2009,2010,2012,2013,2014,2015,2016,2017,2018,2019,2020 Richard Hughes-Jones, University of Manchester
   All rights reserved.

   Redistribution and use in source and binary forms, with or
   without modification, are permitted provided that the following
   conditions are met:

     o Redistributions of source code must retain the above
       copyright notice, this list of conditions and the following
       disclaimer. 
     o Redistributions in binary form must reproduce the above
       copyright notice, this list of conditions and the following
       disclaimer in the documentation and/or other materials
       provided with the distribution. 

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
   CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
   MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
   BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
   TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
   ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
   OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
   OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
   POSSIBILITY OF SUCH DAMAGE.
*/



#define INSTANTIATE true
#include "version.h"                             /* common inlcude file */
#include "net_test.h"                            /* common inlcude file */

#include <sys/stat.h>				             /* required for open() */
#include <fcntl.h>				                 /* required for open() */
#include <linux/net_tstamp.h>                    /* for HW timestamps */

#define UDP_DATA_MAX 128000
    unsigned char udp_data[UDP_DATA_MAX];        /* ethernet frame to send back */
    unsigned char udp_data_recv[UDP_DATA_MAX];   /* ethernet frame received */
    int *int_data_ptr;                           /* int pointer to data */

#define ERROR_MSG_SIZE 256
    char error_msg[ERROR_MSG_SIZE];              /* buffer for error messages */

/* Time in sec to keep the  Lock to reject new test requests when a test is in progress */
#define LOCK_TIME   60
    struct sockaddr *lock_ip_address;            /* IP address of remote host OR =0 if no test in progress */
    int ipfamily_addr_len;                       /* length of soc_recv_address - depends on IP family IPv4, IPv6*/

/* parameters */
    int dest_udp_port;
    int pkt_len = 64;                            /* length of request packet */
    int resp_len;                                /* length of response frame */
    int soc_buf_size =65535;                     /* send & recv buffer size bytes */
    int precidence_bits=0;                       /* precidence bits for the TOS field of the IP header IPTOS_TOS_MASK = 0x1E */
    int tos_bits=0;                              /* tos bits for the TOS field of the IP header IPTOS_PREC_MASK = 0xE0 */
    int tos_set = 0;                             /* flag =1 if precidence or tos bits set */
    int dscp_bits=0;                             /* difserv code point bits for the TOS field of the IP header */
    int dscp_set = 0;                            /* flag =1 if dscp bits set - alternative to precidence or tos bits set */
    int verbose =0;                  		     /* set to 1 for printout (-v) */
    int use_IPv6 =0;                             /* set to 1 to use the IPv6 address family */
    int is_daemon =0;				             /* set to 1 to run as daemon */
    int quiet = 0;                               /* set =1 for just printout of results - monitor mode */
    int64 n_to_skip=0;                           /* number of packets to skip before recording data for -G option */
    int log_lost=0;                              /* =1 to log LOST packets only -L option */
    long cpu_affinity_mask;                      /* cpu affinity mask set bitwise cpu_no 3 2 1 0 in hex */
    char *interface_name=NULL;                   /* name of the interface e.g. eth0 */
    int use_hwTstamps =0;                        /* set to 1 to use HW Time Stamp on NIC */

/* for command line options */
extern char *optarg;

/* statistics */
    struct HIST hist[10];

/* forward declarations */
static void parse_command_line (int argc, char **argv);
static void sig_alrm(int signo);
static void make_daemon(void);
char * sock_ntop(const struct sockaddr *sa);

static void enable_timestamp(int soc)
/* --------------------------------------------------------------------- */
{
		int enable = 0;
		int error;
		int enable_len = sizeof(enable);

		/*
		printf("Select hardware timestamping.\n");
		enable = SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RAW_HARDWARE
				| SOF_TIMESTAMPING_SYS_HARDWARE | SOF_TIMESTAMPING_SOFTWARE;
		int timestampOn =
				SOF_TIMESTAMPING_RX_SOFTWARE | SOF_TIMESTAMPING_TX_SOFTWARE |
				SOF_TIMESTAMPING_SOFTWARE | SOF_TIMESTAMPING_RX_HARDWARE |
				SOF_TIMESTAMPING_TX_HARDWARE | SOF_TIMESTAMPING_RAW_HARDWARE |
		// SOF_TIMESTAMPING_OPT_TSONLY |
				0;
		*/
	  
		enable =   
			SOF_TIMESTAMPING_RX_HARDWARE  |
			SOF_TIMESTAMPING_RX_SOFTWARE  |
			SOF_TIMESTAMPING_RAW_HARDWARE |
			SOF_TIMESTAMPING_SYS_HARDWARE |
			SOF_TIMESTAMPING_SOFTWARE ;

		error = setsockopt(soc, SOL_SOCKET, SO_TIMESTAMPING, &enable, sizeof(enable));
		if(error <0 ) {
			perror("Error on recvfrom request packet :" );
			exit(-1);
		}
		if(verbose){
			/* read back what is set for SO_TIMESTAMPING */
			enable =0;
			error = getsockopt(soc, SOL_SOCKET, SO_TIMESTAMPING, &enable, (socklen_t *)&enable_len);
			printf("getsockopt() enable %08x \n", enable);
		}
	
}

static void print_time(struct timespec* ts)
/* --------------------------------------------------------------------- */
{
     /* Hardware timestamping provides three timestamps -
     *   system (software)
     *   transformed (hw converted to sw)
     *   raw (hardware)
     * in that order - though depending on socket option, you may have 0 in
     * some of them.
     */
	 /* Seconds.nanoseconds format */

	if( ts != NULL ) {
		printf("timestamps \n");
		printf("system (software)                %lu.%lu s\n", (uint64_t)ts[0].tv_sec, (uint64_t)ts[0].tv_nsec);
		printf("transformed (hw converted to sw) %lu.%lu s\n", (uint64_t)ts[1].tv_sec, (uint64_t)ts[1].tv_nsec);
		printf("raw (hardware)                   %lu.%lu s\n", (uint64_t)ts[2].tv_sec, (uint64_t)ts[2].tv_nsec);
	} 
	else {
		printf( "no timestamp\n" );
	}
}

struct timespec* extract_timestamp(struct msghdr* msg)
/* --------------------------------------------------------------------- */
{
	struct timespec* ts = NULL;
	struct cmsghdr* cmsg;
	
	//printf("msg->msg_controllen %ld msg->msg_flags %08x\n", msg->msg_controllen, msg->msg_flags);

	for( cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg,cmsg) ) {
		if(verbose) printf("cmsg_level %d cmsg_type %d\n", cmsg->cmsg_level, cmsg->cmsg_type);
		if( cmsg->cmsg_level != SOL_SOCKET ) continue;

		switch( cmsg->cmsg_type ) {
			case SO_TIMESTAMPNS:
				if(verbose) printf("cmsg type %d SO_TIMESTAMPNS\n", cmsg->cmsg_type);
				ts = (struct timespec*) CMSG_DATA(cmsg);
				break;
			case SO_TIMESTAMPING:
				if(verbose) printf("cmsg type %d SO_TIMESTAMPING\n", cmsg->cmsg_type);
				ts = (struct timespec*) CMSG_DATA(cmsg);
				break;
			default:
				/* Ignore other cmsg options */
				if(verbose) printf("cmsg type %d\n", cmsg->cmsg_type);
				break;
		}
	}
	if(verbose) {
		print_time(ts);
		printf("done print_time ------------------ \n");
	}
	return (ts);
}

int main (int argc, char **argv)
/* --------------------------------------------------------------------- */
{
/* 
Ethernet frame:
           | MAC link |  data                  | CRC |
len bytes:         14    46-1500                  4      
   ethernet packet:
                      |                        |
Ethernet frame for RAW IP:
           | MAC link | IP | data              | CRC |
len bytes:         14   20   46->(1500-20)        4      
   ethernet packet for RAW IP:
                      |                        |
Ethernet frame for IP:
           | MAC link | IP | UDP/TCP | data             | CRC |
len bytes:         14   20    8 / 20   46->(1500-28/40)    4      
   ethernet packet for IP:
                      |                                 |
Ethernet MTU 1500 bytes
Ethernet type 0-1500     packet length
              5101-65535 packet type
              2048       = IP
*/
    struct sockaddr *soc_address;
    struct sockaddr *soc_recv_address;
    unsigned int flags = 0;          	        /* flags for sendto() */
    int recvbuf_size = UDP_DATA_MAX;
    SOC_INFO soc_info;
    struct addrinfo hint_options, *result;      /* for getaddrinfo() */
	int recvmsg_flags =0;		                // for recvmsg() 0 for now

/* parameters */
    struct param *params;
    int ack_len = 46;                           /* length of ack to send */
/* timing */
    double time;                                /* time for one message loop - StopWatch us */
    double relative_time;                       /* time between curent packet and first seen - StopWatch us */
    long hist_time;
    int64 *recv_time_ptr;
    int64 *recv_time_start;
    int64 *recv_time_end;
    int64 *recv_temp_ptr;
    int64 int64_time;

	struct msghdr recv_msg;                     /* for getmsg() */
	struct iovec io_vec;                        /* scatter gather vector for getmsg() */
	char control[1024];                         /* buffer for ancilary data from getmsg() */
	struct timespec* time_stamp = NULL;

	int64 time_hwsystem = 0;                    /* for NIC HW TimeStamps  */ 
	int64 time_hwraw = 0;                       /* for NIC HW TimeStamps  */ 
	int64 last_time_hwsystem = 0;
	int64 last_time_hwraw = 0;

/* timers */
    StopWatch ipg_sw;                           /* time between packets */
    StopWatch relative_sw;                      /* time between curent packet and first seen */
    StopWatch first_last_sw;                    /* time between first and last packet seen */

/* statistics */
    CPUStat cpu_stats;
    NET_SNMPStat net_snmp_stats;
    NETIFinfo net_if_info[NET_SNMP_MAX_IF];
    SNMPinfo snmp_info;
    CPUinfo cpuinfo[NET_SNMP_MAX_CPU+1];
    NIC_Stat nic_stats;
    NICinfo  nic_info;

/* local variables */
    int error=0;
    int soc;                         		    /* handle for socket */
    int cmd;
    int i,j;
    int64 inc;                                  /* number by which the frame number increments */
    int64 frame_num;                            /* number of frame just received */ 
    int64 old_frame_num = -1;                   /* number of previous frame received */  
    int64 old_frame_recv_time =0;               /* time previous frame was received 0.1 us */
    int64 old_frame_send_time =0;               /* time previous frame was sent 0.1 us */
    int num_recv=0;                     	    /* number of udp packet received */
    int num_lost=0;                     	    /* number of udp packet lost */
    int num_badorder=0;                     	/* number of udp packet out of order */
    int ret;
    char port_str[128];
    int first=1;                                /* flag to indicate that the next frame read will be the first in test */
    double delay;

    int send_ack=0;                             /* set to n to tell remote end to send ACK aster n packets */
    int data_index;                             /* offset in requesting histo and info data to be sent back */
	int hist_num;
	
/* set the signal handler for SIGALRM */
    signal (SIGALRM, sig_alrm);

    printf(" Command line: ");
    for(i=0; i<argc; i++){
        printf(" %s", argv[i]);
    }
    printf(" \n");

/* book histograms */ 
/*     h_book(struct HIST *hist_ptr, int32 id, int32 low_lim, int32 bin_width, 
		int32 n_bins, char *char_ptr)  */ 
    h_book( &hist[0], 0, 0, 1, 150, "Time between frames 0.1us");
    h_book( &hist[1], 1, 0, 1, 10, "Lost-frame sequence");
    h_book( &hist[2], 2, 0, 1, 10, "Out-of-order");
    h_book( &hist[3], 3, 0, 250, 150, "hwsystem Time between frames ns");
    h_book( &hist[4], 4, 0, 250, 150, "hwraw Time between frames ns");


/* setup defaults for parameters */
    resp_len = sizeof(struct param);
    recv_time_start = NULL;
    recv_time_ptr = NULL;
    recv_time_end = NULL;
    recv_temp_ptr = NULL;
    /* these are initialised on CMD_ZEROSTATS */
    relative_sw.t1 = 0; 
    relative_sw.t2 = 0;
    ipg_sw.t1 = 0;
    ipg_sw.t2 = 0;
    first_last_sw.t1 = 0;
    first_last_sw.t2 = 0;

/* Set the default IP Ethernet */
/* IP protocol number ICMP=1 IGMP=2 TCP=6 UDP=17 */

    dest_udp_port = 0x3799;           		/* The default RAW/UDP port number (14233 base10) */

/* get the input parameters */
    parse_command_line ( argc, argv);

/* set the CPU affinity of this process*/
    set_cpu_affinity (cpu_affinity_mask, quiet);

/* initalise and calibrate the time measurement system */
    ret = RealTime_Initialise(1);
    if (ret) exit(EXIT_FAILURE);
    ret = StopWatch_Initialise(1);
    if (ret) exit(EXIT_FAILURE);

/* initalise CPUStats */
    CPUStat_Init();
	
/* initialise snmp stats */
	if(use_IPv6 == 1 ){
		net_snmp_Init( &net_snmp_stats, &snmp_info, SNMP_V6 );
	}
	else {
		net_snmp_Init( &net_snmp_stats, &snmp_info, SNMP_V4 );
	}
	
    printf(" The UDP port is   %d %x\n", dest_udp_port, dest_udp_port);

/* create the socket address with the correct IP family for listening to UDP packets */
    sprintf(port_str, "%d", dest_udp_port);
/* clear then load the hints */
    bzero(&hint_options, sizeof(struct addrinfo) );
    hint_options.ai_family = AF_INET;
#ifdef	IPV6
    if(use_IPv6 == 1) hint_options.ai_family = AF_INET6;
#endif
    hint_options.ai_socktype = SOCK_DGRAM;
    hint_options.ai_flags = AI_PASSIVE; /* for server */
    error = getaddrinfo(NULL, port_str, &hint_options, &result);   
    if(error){
        snprintf(error_msg, ERROR_MSG_SIZE,
		 "Error: Could not use address family %s", gai_strerror(error) );
	perror(error_msg );
        exit(-1);
    }
    /* get the length of the sock address struct */
   ipfamily_addr_len = result->ai_addrlen;
   soc_address = result->ai_addr;
    
/* Open the UDP IP socket. */
    soc_info.soc_buf_size = soc_buf_size;
    soc_info.precidence_bits = precidence_bits;
    soc_info.tos_bits = tos_bits;
    soc_info.tos_set = tos_set;
    soc_info.dscp_bits = dscp_bits;
    soc_info.dscp_set = dscp_set;
    soc_info.quiet = quiet;
    sock_create_udp_socket(&soc, &soc_info, result->ai_family);
 	
/* assign a protocol address (UDP port) to that UDP socket */
    error = bind(soc, soc_address, ipfamily_addr_len );
	if (error) {
        perror("Bind of port to UDP IP socket failed :" );
        exit(-1);
    }

/* initalise NIC Stats */
   nic_stats_Init( &nic_stats, soc, interface_name);

    /* allocate space for recv address - size depends on IPv4 or IPv6 */
   soc_recv_address = malloc(ipfamily_addr_len);

   /* allocate space for lock address - size depends on IPv4 or IPv6 */
   lock_ip_address = malloc(ipfamily_addr_len);
   bzero(lock_ip_address,  ipfamily_addr_len);

/* Here we build the Ethernet packet(s), ready for sending.
   Fill with random values to prevent any compression on the network 
   The same frame is sent, we just make sure that the right size is reported
*/
    for(int_data_ptr= (int *)&udp_data; 
        int_data_ptr < (int *)&udp_data + UDP_DATA_MAX/4; int_data_ptr++){
        *int_data_ptr =rand();
    }

/* continue as daemon, when selected */
   if (is_daemon)
       make_daemon();

/* set up the scatter/gather array */
	io_vec.iov_base = &udp_data_recv;
	io_vec.iov_len = recvbuf_size;
	/* make the msghdr structure for recvmsg() */
	memset(&recv_msg, 0, sizeof(recv_msg));
	recv_msg.msg_name = (caddr_t)soc_recv_address;
	recv_msg.msg_namelen = (socklen_t )sizeof(ipfamily_addr_len);
	recv_msg.msg_iov = &io_vec; 
	recv_msg.msg_iovlen = 1;
	recv_msg.msg_control = control;           // ancillary data
	recv_msg.msg_controllen = sizeof(control);

	
/* loop for ever over the tests */
    for(;;){
    READ_INPUT:
/* Read the request from the remote host. */
		if(use_hwTstamps){
			// select scatter/gather for HW TimeStamps
			recv_msg.msg_controllen = sizeof(control);  // need to specify each time
			recvmsg_flags =0;		// 0 for now
			error = recvmsg(soc, &recv_msg, recvmsg_flags);
			if(error <0 ) {
				perror("Error on recvfrom request packet :" );
				goto READ_INPUT;
			}
			time_stamp = extract_timestamp(&recv_msg);
		}
		else{
			error = recvfrom(soc, &udp_data_recv, recvbuf_size, flags, soc_recv_address, (socklen_t *)&ipfamily_addr_len );
			if(error <0 ) {
				perror("Error on recvfrom request packet :" );
				goto READ_INPUT;
			}
		}
	
/* what do we have to do */
	params = (struct param *) udp_data_recv;
	cmd = i4swap(params->cmd);
	frame_num = i8swap(params->frame_num);

	if(verbose) {
		printf(" From host: %s\n", sock_ntop(soc_recv_address));
	    printf("Packet: \n");
	    for(j=0; j<64; j++){
	      printf(" %x", udp_data_recv[j]);
	    }
	    printf(" \n");
	    printf("Command : %d Frame num %" LONG_FORMAT "d old frame num %" LONG_FORMAT "d\n", cmd, frame_num, old_frame_num);
	}

	switch (cmd){

	case CMD_DATA:
		/* record the time between the frames and histogram */
		StopWatch_Stop(&ipg_sw);
		time = StopWatch_TimeDiff(&ipg_sw);
		relative_sw.t2 = ipg_sw.t2;
		StopWatch_Start(&ipg_sw);

		/* record time of first frame seen */
		if(first){
		    first = 0;
		    first_last_sw.t1 =ipg_sw.t2;
		}
		/* histogram ipg with 0.1us  bins*/
		hist_time = (long) 10*(time);
		h_fill1( &hist[0], hist_time);

		/* hardware Time Stamps check if available */
		if(use_hwTstamps && (time_stamp != NULL)){
			time_hwsystem = (long) (time_stamp[0].tv_sec*1e9 + time_stamp[0].tv_nsec);
			time_hwraw = (long) (time_stamp[2].tv_sec*1e9 + time_stamp[2].tv_nsec);
			//printf("time_hwsystem %ld time_hwraw %ld \n", time_hwsystem, time_hwraw);
			if(last_time_hwraw !=0){
				hist_time = time_hwsystem  - last_time_hwsystem;
				h_fill1( &hist[3], hist_time);
				hist_time = time_hwraw - last_time_hwraw;
				h_fill1( &hist[4], hist_time);
			}
			last_time_hwsystem = time_hwsystem; 
			last_time_hwraw = time_hwraw;
		}
		
		num_recv ++;
		/* check increment of frame number */
		inc = frame_num - old_frame_num;
                if(inc == 1){
		}
		if(inc > 1) {
		     num_lost = num_lost +inc -1;
		     hist_time = (long) (inc-1);
			 h_fill1( &hist[1], hist_time);
		     if((frame_num >= n_to_skip ) && (log_lost == 0)){
		         /* increment the pointer for timing info for this frame - *4 as record 4 words per frame include HW TimeStamps */
		         recv_time_ptr = recv_time_ptr + (inc-1)*4;
		     }
		     else if((frame_num >= n_to_skip ) && (log_lost == 1) && 
				(recv_time_ptr != NULL ) && (recv_time_ptr < recv_time_end ) ){
				/* record the arrival time of the last packet received in sequence and the packe number of the lost packet */
				for (i=1; i<inc; i++){
					if( (recv_time_ptr != NULL ) && (recv_time_ptr < recv_time_end ) ){
						*recv_time_ptr=  old_frame_recv_time;
						recv_time_ptr++;
						*recv_time_ptr=  old_frame_send_time;
						recv_time_ptr++;
						*recv_time_ptr= old_frame_num + (int64)i;
						recv_time_ptr++;
					}
				} /* end of loop over lost packets */  
		     }

		}
		if(inc <= 0) {
		    num_badorder++; 
		     hist_time = (long) (inc*-1);
			h_fill1( &hist[2], hist_time);
		    /* calc a temp pointer */
		    recv_temp_ptr = recv_time_ptr +(inc-1)*4;
		    if( (frame_num >= n_to_skip ) && (log_lost == 0) && (recv_temp_ptr > recv_time_start ) && (recv_temp_ptr < recv_time_end) ) { 
				/* record the time this frame was seen relative to the time of the zerostats command - in us */
				relative_time = StopWatch_TimeDiff(&relative_sw);
				*recv_temp_ptr= (int64) (relative_time*(double)10.0);
				recv_temp_ptr++;
				/* record the time the frame was sent - in us */
				*recv_temp_ptr= (int64) i8swap(params->send_time);
				recv_temp_ptr++;
				/* record the HW timestamps */
				*recv_time_ptr= (int64) i8swap(time_hwsystem);
				recv_time_ptr++;
				*recv_time_ptr= (int64) i8swap(time_hwraw);
				recv_time_ptr++;
		    }
		}
		if(inc >0){
		    old_frame_num = frame_num;
		    /* record the time this frame was seen relative to the time of the zerostats command - in 0.1us */
		    relative_time = StopWatch_TimeDiff(&relative_sw);
		    old_frame_recv_time = (int64)((double)10.0*relative_time);
		    old_frame_send_time = (int64) i8swap(params->send_time);
		    if( (frame_num >= n_to_skip) && (log_lost == 0) && recv_time_ptr < recv_time_end) { 
				*recv_time_ptr= (int64) ((double)10*relative_time);
				recv_time_ptr++;
				/* record the time the frame was sent - in us */
				*recv_time_ptr= (int64) i8swap(params->send_time);
				recv_time_ptr++;
				/* record the HW timestamps */
				*recv_time_ptr= (int64) i8swap(time_hwsystem);
				recv_time_ptr++;
				*recv_time_ptr= (int64) i8swap(time_hwraw);
				recv_time_ptr++;
		    }
		}

		/* For all recv frames, check if have to send an ACK */
		if(send_ack >0){
		    params = (struct param *) udp_data;
		    params->cmd = i4swap(CMD_ACK);
		    params->protocol_version = i4swap(PROTOCOL_VERSION);
		    error = sendto(soc, &udp_data, ack_len, flags, (struct sockaddr*)&soc_recv_address, sizeof(soc_recv_address));
		    if(error != ack_len) {
		        printf("ACK Send error: sent %d bytes not %d\n", error, ack_len);
		    }
		}
	break;


	case CMD_REQ_RESP:
/* send the response frame(s) using the remote address */
		resp_len = i4swap(params->resp_len);
	        params->cmd = i4swap(CMD_RESPONSE);
		params->protocol_version = i4swap(PROTOCOL_VERSION);
/* record the time this frame was seen relative to the time of the zerostats command - in us */
	        StopWatch_Stop(&relative_sw);
		relative_time = StopWatch_TimeDiff(&relative_sw);
/* Timestamp the frame - in us */
		int64_time = (int64) relative_time;
		params->resp_time = i8swap(int64_time);
		if(verbose) {
                     printf("req_resp : %d Frame num %"LONG_FORMAT"d  resp_len %d resp_time %"LONG_FORMAT"d\n", 
			    cmd, frame_num, resp_len, i8swap(params->resp_time) );
		}

		error = sendto(soc, &udp_data_recv, resp_len, flags, soc_recv_address, ipfamily_addr_len);
		if(error != resp_len) {
		    printf("request_response Send error: sent %d bytes not %d\n", error, resp_len);
		}
	        break;

	    case CMD_ZEROSTATS:
			if(verbose)	printf("zerostats\n");

/* only allow a new test request or existing repeat request - zero the stats */
			if(sock_cmp_addr(lock_ip_address, soc_recv_address ) == 0) {
				if(verbose) {
					printf("zerostats : reject - INUSE " );
					printf(" The requesting IP address is %s\n", sock_ntop(soc_recv_address ) );
				}
	
				/* send the INUSE response frame(s) using the remote address */
				params->cmd = i4swap(CMD_INUSE);
				params->protocol_version = i4swap(PROTOCOL_VERSION);
				resp_len = sizeof(struct param);
				error = sendto(soc, &udp_data_recv, resp_len, flags, soc_recv_address, ipfamily_addr_len);
				if(error != resp_len) {
					printf("zerostats INUSE Send error: sent %d bytes not %d\n", error, resp_len);
				}
				goto READ_INPUT;
			}
			/* note the time to act as time-zero for relative times (ie inter-packet and 1 way times) */
			StopWatch_Stop(&ipg_sw);
			relative_sw.t1 = ipg_sw.t2;

	    case CMD_START:
/* new test request or existing repeat request - zero the stats and start */
	        num_recv =0;
			num_lost =0;
			num_badorder =0;
			old_frame_num = - 1;
			send_ack = i4swap(params->send_ack);
			use_hwTstamps = i4swap(params->use_hwTstamps);
			/* enable hardware timestamping */
			if(use_hwTstamps){
				enable_timestamp(soc);
			}
			i= i4swap(params->bin_width);
			hist[0].bin_width = (int32)(i);
			//RHJ expt
			hist[3].bin_width = (int32)(i) *250;
			hist[4].bin_width = (int32)(i) *250;
			i = i4swap(params->low_lim);
			hist[0].low_lim = (int32)(i);
			//RHJ expt
			hist[3].low_lim = (int32)(i) *250;
			hist[4].low_lim = (int32)(i) *250;
			h_clear( &hist[0] ); 
			h_clear( &hist[1] ); 
			h_clear( &hist[2] ); 
			h_clear( &hist[3] ); 
			h_clear( &hist[4] ); 

			/* record initial interface & snmp info */
			net_snmp_Start(  &net_snmp_stats);
			/* record initial CPU and interrupt info */
			CPUStat_Start(  &cpu_stats);
			/* record initial info from NIC */
			nic_stats_Start( &nic_stats);
		
		    /* record the IP address of the remote node */
			memcpy(lock_ip_address, soc_recv_address, ipfamily_addr_len);
			if(verbose) {
		        printf("start : %d Frame num %" LONG_FORMAT "d = old frame num %" LONG_FORMAT "d\n", cmd, frame_num, old_frame_num);
				printf(" bin_width %d low lim %d -G resp_len %d\n", i4swap(params->bin_width), i4swap(params->low_lim),i4swap(params->resp_len) );
				printf(" The requesting IP address is %s\n", sock_ntop(soc_recv_address ));
			}
			/* allocate memory for -G or -L option - free first - params->resp_len is no of BYTES required for ALL the data */
			if(recv_time_start != NULL) {
				free(recv_time_start);
				recv_time_start = NULL;
			}
			recv_time_ptr = NULL;
			recv_time_end = NULL;
			resp_len = i4swap(params->resp_len);
			if(resp_len >0) {
		        recv_time_start = (int64 *)malloc(resp_len);
      			recv_time_end = recv_time_start + resp_len/8 -1;
				for(recv_time_ptr=recv_time_start; recv_time_ptr<= recv_time_end; 
					recv_time_ptr++) *recv_time_ptr = 0;
				recv_time_ptr = recv_time_start;
			}
			n_to_skip = (int64) i4swap(params->n_to_skip);
			log_lost = i4swap(params->log_lost);
			/* set the lock timer */
			alarm(LOCK_TIME);
			/* send the OK response frame(s) using the remote address */
			params->cmd = i4swap(CMD_OK);
			params->protocol_version = i4swap(PROTOCOL_VERSION);
			resp_len = sizeof(struct param);
			error = sendto(soc, &udp_data_recv, resp_len, flags, soc_recv_address, ipfamily_addr_len);
			if(error != resp_len) {
		        printf("zerostats OK Send error: sent %d bytes not %d\n", error, resp_len);
			}
			/* start the timer on first data frame */
			first = 1;
		break;

	    case CMD_GETSTATS:
/* record time of first frame seen */
		first_last_sw.t2 =ipg_sw.t2;
                delay = StopWatch_TimeDiff(&first_last_sw);
/* record final CPU and interrupt info */
		CPUStat_Stop( &cpu_stats);
/* record final interface & snmp info */
		net_snmp_Stop(  &net_snmp_stats);
/* record final info from the NIC */
		nic_stats_Stop( &nic_stats);
		nic_stats_Info( &nic_stats, &nic_info);

		if(verbose){
		    net_snmp_Info(  &net_snmp_stats, net_if_info, &snmp_info);

		    printf("L if;");
		    for (i=0; i<5; i++){
		      if(net_if_info[i].name[0] != 0){
			printf( "-%d-%s: ; %" LONG_FORMAT "d; %" LONG_FORMAT "d; %" LONG_FORMAT "d; %" LONG_FORMAT "d;", 
				i,&net_if_info[i].name[0], net_if_info[i].pktsin, net_if_info[i].bytesin,
				net_if_info[i].pktsout, net_if_info[i].bytesout);
		      }
		    }
		    printf(" L snmp;");
		    printf( " %" LONG_FORMAT "d; %" LONG_FORMAT "d; %" LONG_FORMAT "d; %" LONG_FORMAT "d; ", 
			    snmp_info.InReceives, snmp_info.InDiscards,
			    snmp_info.OutRequests, snmp_info.OutDiscards);
		    printf("\n");
		}

		params = (struct param *) udp_data;
/* sort byte swapping */
		params->num_recv = i4swap(num_recv);
		params->num_lost = i4swap(num_lost);
		params->num_badorder = i4swap(num_badorder);
		int64_time = (int64)(delay*(double)10.0);
		params->first_last_time = i8swap(int64_time);

		CPUStat_Info(  &cpu_stats, cpuinfo, params->inter_info);

		if(verbose) {
		    printf("num_recv: %d\n", num_recv);
		    printf("num_lost: %d\n",num_lost);
		    printf("num_badorder: %d\n", num_badorder);
		    printf("time us: %g\n", delay);
		    printf( "CPU: %"LONG_FORMAT"d; %"LONG_FORMAT"d; %"LONG_FORMAT"d; %"LONG_FORMAT"d;",   
			    cpuinfo[0].user, 
			    cpuinfo[0].lo_pri, 
			    cpuinfo[0].system, 
			    cpuinfo[0].idle  );
		    printf("  \n" );
		}
/* byte swap if need be */
#ifdef BYTE_SWAP_ON
		for (i=0; i<5; i++){
		    params->inter_info[i].count = i8swap(params->inter_info[i].count);
		}
#endif    /* ifdef BYTE_SWAP_ON */
/* send the response frame(s)  */
		resp_len = sizeof(struct param);
		error = sendto(soc, &udp_data, resp_len, flags, soc_recv_address, ipfamily_addr_len);
		if(error != resp_len) {
		   printf("Send error: sent %d bytes not %d\n", error, resp_len);
		}
		break;

	    case CMD_GETHIST0:
			hist_num = i4swap(params->low_lim);
			resp_len = i4swap(params->resp_len);
			data_index = i4swap(params->data_index);
			if(verbose) {
				printf("get hist %d\n", hist_num);
				printf("params->resp_len %d  params->data_index %d\n", resp_len, data_index );
				printf(" The requesting IP address is %s\n", sock_ntop(soc_recv_address ));
			}

			/* copy histogram to allow for byte swapping */
			if(data_index == 0) h_copy(&hist[hist_num], (struct HIST *)udp_data_recv);
/* send the response frame(s)  */		
			error = sendto(soc, ((unsigned char *)&udp_data_recv[0])+data_index , resp_len, flags, soc_recv_address, ipfamily_addr_len);
			if(error != resp_len) {
				sprintf(error_msg, 
						"Error: on data sent GETHIST0: sent %d bytes not %d ", 
						error, resp_len );
				perror(error_msg );
			}
		break;

	    case CMD_GETCPULOAD:
	        resp_len = i4swap(params->resp_len);
		data_index = i4swap(params->data_index);
		if(verbose) {
		    printf("get cpuload \n");
		    printf("params->resp_len %d  params->data_index %d\n", resp_len, data_index );
		}
/* should allow for byte swapping */

/* send the response frame(s)  */		
		error = sendto(soc, ((unsigned char *)&cpuinfo[0])+data_index , resp_len, flags, soc_recv_address, ipfamily_addr_len);
		if(error != resp_len) {
		    sprintf(error_msg, 
			    "Error: on data sent GETCPULOAD: sent %d bytes not %d ", 
			    error, resp_len );
		    perror(error_msg );
		}
		break;

	    case CMD_GETINFO1:
	        resp_len = i4swap(params->resp_len);
		data_index = i4swap(params->data_index);
		if(verbose) {
		    printf("get info 1\n");
		    printf("params->resp_len %d  params->data_index %d\n", resp_len, data_index );
		}
/* byte swap - but only once */
#ifdef BYTE_SWAP_ON
		if(data_index == 0){
		    if(recv_time_start != NULL) {		
			for(recv_time_ptr=recv_time_start; recv_time_ptr<= recv_time_end; 
			    recv_time_ptr++) *recv_time_ptr = i8swap(*recv_time_ptr);
		    }
		}
#endif    /* ifdef BYTE_SWAP_ON */
/* send the response frame(s)  */
			error = sendto(soc, recv_time_start+data_index , resp_len, flags, soc_recv_address, ipfamily_addr_len);
			if(error != resp_len) {
				sprintf(error_msg, 
						"Error: on data sent GETINFO1: sent %d bytes not %d ", 
						error, resp_len );
				perror(error_msg );
			}
		break;

	    case CMD_GETNETSNMP:
	        resp_len = i4swap(params->resp_len);
			data_index = i4swap(params->data_index);
			if(verbose) {
				printf("get net_snmp\n");
				printf("params->resp_len %d  params->data_index %d\n", resp_len, data_index );
			}
			/* should byte swap - but only once */

/* send the response frame(s)  */		
			error = sendto(soc, ((unsigned char *)&net_snmp_stats)+data_index , resp_len, flags, soc_recv_address, ipfamily_addr_len);
			if(error != resp_len) {
				sprintf(error_msg, 
						"Error: on data sent GETNETSNMP: sent %d bytes not %d ", 
						error, resp_len );
				perror(error_msg );
			}
		break;

	    case CMD_GETNICSTATS:
			resp_len = i4swap(params->resp_len);
			data_index = i4swap(params->data_index);
			if(verbose) {
				printf("get nic_stats\n");
				printf("params->resp_len %d  params->data_index %d\n", resp_len, data_index );
				nic_stats_print_info(&nic_info, 1, 'L');
				printf("  --- nic_info.num_stats_keep %d\n", nic_info.num_stats_keep);
				nic_stats_print_info(&nic_info, 2, 'L');
				printf("\n");
			}
			/* should byte swap - but only once */

/* send the response frame(s)  */
			error = sendto(soc, ((unsigned char *)&nic_info)+data_index , resp_len, flags, soc_recv_address, ipfamily_addr_len);
			if(error != resp_len) {
				sprintf(error_msg,
						"Error: on data sent GETNICSTATA: sent %d bytes not %d ",
						error, resp_len );
				perror(error_msg );
			}
		break;

	    case CMD_TESTEND:
			if(verbose) {
				printf("testend 0\n");
			}
			bzero(lock_ip_address,  ipfamily_addr_len);

			/* unset the lock timer */
			alarm(0);
			/* send the OK response frame(s) using the remote address */
			params->cmd = i4swap(CMD_OK);
			params->protocol_version = i4swap(PROTOCOL_VERSION);
			error = sendto(soc, &udp_data_recv, resp_len, flags, soc_recv_address, ipfamily_addr_len);
			if(error != resp_len) {
				printf("testend OK Send error: sent %d bytes not %d\n", error, pkt_len);
			}
		break;

	    default:
		break;

	}   /* end of switch() */


    }    /* end of for ever looping over requests */

    close(soc);
    
}

static void parse_command_line (int argc, char **argv)
/* --------------------------------------------------------------------- */
{
/* local variables */
    char c;
    int error;

    error =0;
    char *help ={
"Usage: udpmon_bw_mon -option<parameter> [...]\n\
options:\n\
	-6 = Use IPv6\n\
	-D = run as daemon, all standard streams are redirected to /dev/null\n\
	-I = <interface name for NIC information e.g. enp131s0f1 [NULL]>\n\
	-P = <precidence bits set - in hex - will be shifted left by 9>\n\
	-Q = <DSCP QoS bits set - in hex >\n\
	-S = <size of send and receive socket buffers in bytes>\n\
	-T = <tos bits set - in hex - will be shifted left by 1>\n\
	-V = print version number\n\
	-a = <cpu_mask set bitwise cpu_no 3 2 1 0 in hex>\n\
	-h = print this message\n\
	-u = <raw/udp port no - default 0x3799 ie 14233 decimal>\n\
	-v = turn on debug printout"};

#ifdef IPv6
    while ((c = getopt(argc, argv, "a:u:I:P:Q:S:T:hv6DV")) != (char) EOF) {
#else
    while ((c = getopt(argc, argv, "a:u:I:P:Q:S:T:hvDV")) != (char) EOF) {
#endif	
	switch(c) {

	    case 'a':
		if (optarg != NULL) {
		    sscanf(optarg, "%lx", &cpu_affinity_mask);
		} else {
		    error = 1;
		}
		break;

	    case 'h':
            fprintf (stdout, "%s \n", help);
	        exit(EXIT_SUCCESS);
		break;

	    case 'u':
		if (optarg != NULL) {
		    dest_udp_port =  atoi(optarg); 
		} else {
		    error = 1;
		}
		break;

	    case 'v':
	        verbose = 1;
		break;

	    case '6':
	        use_IPv6 = 1;
		break;

	    case 'D':
		is_daemon = 1;
		break;

            case 'I':
                if (optarg != NULL) {
                    interface_name = optarg;
                } else {
                    error = 1;
                }
                break;

	    case 'P':
		if (optarg != NULL) {
		    sscanf(optarg, "%x", &precidence_bits);
		    tos_set = 1;
		} else {
		    error = 1;
		}
		break;

	    case 'Q':
		if (optarg != NULL) {
		    sscanf(optarg, "%x", &dscp_bits);
		    dscp_set = 1;
		} else {
		    error = 1;
		}
		break;

	    case 'S':
		if (optarg != NULL) {
		    soc_buf_size = (int)atoi(optarg);
		} else {
		    error = 1;
		}

		break;

	    case 'T':
		if (optarg != NULL) {
		    sscanf(optarg, "%x", &tos_bits);
		    tos_set = 1;
		} else {
		    error = 1;
		}
		break;

	    case 'V':
	        printf(" %s \n", UDPMON_VERSION);
	        exit(EXIT_SUCCESS);
		break;

	    default:
		break;
	}   /* end of switch */
    }       /* end of while() */

    if (error) {
	fprintf (stderr, "%s \n", help);
	exit	(EXIT_FAILURE);
    }

}
static void make_daemon( void)
/* --------------------------------------------------------------------- */
{
/* Continue as a daemon. */

    pid_t pid;

    /* Go to the root directory to do not block mounted file systems. */

    if (chdir("/") == -1) {
	perror("Change dir. to root failed :");
	exit(-1);
    }

    /* Redirect stdin from the null device. */

    if (close(0) == -1) {
	perror("Close stdin failed :");
	exit(-1);
    }

    if (open("/dev/null", O_RDONLY) == -1) {		    /* closed stdin   */
	perror("Redirect stdin from /dev/null failed :");   /* descr. is used */
	exit(-1);
    }

    /* Fork the daemon process. */

    pid = fork ();

    if (pid == -1) {
	perror("Fork failed :");
	exit(-1);
    }

    /* Exit with zero exit value at the father process after a diagostic *
     * message have been given.						 */

    if (pid > 0) {
	printf("Continue as daemon; process ID: %d\n", pid);
	exit(EXIT_SUCCESS);
    }

    /* Create a new session for the daemon (child) process. */

    if (setsid() == -1) {
	perror("Create a new session failed :");
	exit(-1);
    }

    /* Redirect stdout to the null device. Do it in the child to allow the *
     * diagnostic message above.					   */

    if (close(1) == -1) {
	perror("Close stdout failed :");
	exit(-1);
    }

    if(open("/dev/null", O_WRONLY) == -1) {		    /* closed stdout  */
	perror("Redirect stdout to /dev/null failed :");    /* descr. is used */
	exit(-1);
    }

    /* Finally redirect stderr to stdout (null device) when there are no more *
     * diagnostic messages to display.					      */

    if (close(2) == -1) {
	perror("Close stderr failed :");
	exit(-1);
    }

    dup(1);    /* No streams to display error messages anymore. */
}

static void sig_alrm( int signo)
/* --------------------------------------------------------------------- */
{
  /* Called when Test in progess Lock timer expires - Just interrupt the recvfrom() */
    if(verbose){ 
        printf("SIGALRM caught\n");
        printf(" The old IP address was %s\n", sock_ntop(lock_ip_address ));
    }
    bzero(lock_ip_address,  ipfamily_addr_len);

    return;
}
