/*
     udpmon_recv.c     R. Hughes-Jones  The University of Manchester

     Aim is to receive a stream of Ethernet packets to allow link debugging
     Use UDP socket to:
	   receive a series of packets from a remote node
     on cntl-C Print local stats

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

/*

     Modifications:
      Version 1.2.0                           
        rich  26 Jul 07 Inital version
*/
#define INSTANTIATE true
#define _GNU_SOURCE
#include "net_test.h"                            /* common inlcude file */
#include <pthread.h>
#include "version.h"                             /* common inlcude file */
#include <linux/net_tstamp.h>                    /* for HW timestamps */

#define UDP_DATA_MAX 128000
    unsigned char udp_data_recv[UDP_DATA_MAX];   /* ethernet frame received */
    int *int_data_ptr;                           /* int pointer to data */

#define ERROR_MSG_SIZE 256
    char error_msg[ERROR_MSG_SIZE];              /* buffer for error messages */

#define ITEMS_PER_G_RECV 4
/* was ITEMS_PER_G_RECV 2 *2 as 2 words recorded per frame
   now *4 as record 4 words per frame to include HW TimeStamps */

/* for print_stats_hist( int mode) */
#define MODE_TABLE   1
#define MODE_HEADER  2
#define MODE_LINE    4
#define MODE_DATA    8

/* parameters */
    char dest_ip_address[HOSTNAME_MAXLEN];       /* actually the IP adddress that sends the packets */
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
    int get_hist =0;            	             /* set to 1 for histograms */
    int get_info =0;            	             /* set to 1 for information = relative packet arrival times */
    int bin_width =1;                            /* bin width of interframe time histo */
    int low_lim =0;                              /* low limit of interframe time histo */
    int info_data_len = 0;                       /* length in bytes of info data to be returned by -G option */
    int timer_interval = 0;                      /* num sec to run the program recving data */
    int timer_prog_lifetime =0;                  /* num sec to run the program recving data */
    int quiet = 0;                               /* set =1 for just printout of results - monitor mode */
    int64 n_to_skip=0;                           /* number of packets to skip before recording data for -G option */
    int log_lost=0;                              /* =1 to log LOST packets only -L option */
    long cpu_affinity_mask;                      /* cpu affinity mask set bitwise cpu_no 3 2 1 0 in hex */
    long cpu_affinity_mask_stats;                /* cpu affinity mask for stats output set bitwise cpu_no 3 2 1 0 in hex */
    char *interface_name=NULL;                   /* name of the interface e.g. eth0 */
    int use_hwTstamps =0;                        /* set to 1 to use HW Time Stamp on NIC */

/* control */
    int first=1;                                 /* flag to indicate that the next frame read will be the first in test */
    int timer_first =1;                          /* flag to indicate that this is the first time the timer fired */
 
/* for command line options */
extern char *optarg;

/* timing */
    struct timeval start;           	         /* time before measurements */
    struct timeval now;            	             /* time after measurements */
    int now_sec_first;                           /* seconds value for the first  gettimeofday(now) */

    int64 *recv_time_ptr = NULL;
    int64 *recv_time_start = NULL;
    int64 *recv_time_end = NULL;

/* timers */
    StopWatch relative_sw;                      /* to measure total time to send data */
    StopWatch relative_last_sw;                 /* to measure time to send last set of data */

/* statistics */
    struct HIST hist[10];
    int64 num_recv=0;                           /* total no. of packets sent */
    int64 num_lost=0;                           /* total no. of packets lost */
    int64 num_badorder=0;                       /* total no. of packets out of order */
    int64 num_recv_last=0;                      /* previous no. of packets sent */
    int64 num_lost_last=0;                      /* previous no. of packets lost */
    int64 num_badorder_last=0;                  /* previous no. of packets out of order */
    int64 bytes_recv;                           /* total bytes received */
    int64 bytes_recv_last;                      /* previous no. of bytes received */
    int64 UDPInErrors_total=0;                  /* total no. of packets lost in the network from start of test */
    int64 rx_out_of_buffer_total;               /* total no. of packets lost no NIC ringbuffer from start of test */

    int num_output=0;                           /* line number for output - num times timer fired */

    CPUStat cpu_stats;
    CPUinfo cpuinfo[NET_SNMP_MAX_CPU+1];
    Interrupt_info  inter_info[NET_SNMP_MAX_IF];  
    NET_SNMPStat net_snmp_stats;
    NETIFinfo net_if_info[NET_SNMP_MAX_IF];
    SNMPinfo snmp_info;

    NIC_Stat nic_stats;
    NICinfo  nic_info;

/* for pthreads */
pthread_mutex_t mutex_start = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t condition_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  condition_cond  = PTHREAD_COND_INITIALIZER;
/* the condition_cond has the variable run_ststus associated with it */

/* forward declarations */
static void parse_command_line (int argc, char **argv);
static void sig_alarm(int signo);
static void cntlc_handler(int signo);
static void cntlz_handler(int signo);
static void print_stats_hist( int mode);
void *stats_output_thread(void *param);

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
    struct addrinfo hint_options, *result;      /* for getaddrinfo() */

    SOC_INFO soc_info;
    int ipfamily_addr_len;                      /* length of soc_recv_address - depends on IP family IPv4, IPv6*/
/* for the "want_IP address" */
    struct sockaddr *soc_want_address = NULL;
    struct addrinfo  *result_want_ip;           /* for getaddrinfo() */

/* timing */
    double relative_time;                       /* time between curent packet and first seen - StopWatch us */
    long hist_time;
    int64 *recv_time_ptr;
    int64 *recv_temp_ptr;

    double ipg_time;
    struct itimerval timer_value;               /* for the interval timer */
    struct itimerval timer_old_value;  
    int timer_val =0;                           /* value to set the interval timer */

	struct msghdr recv_msg;                     /* for getmsg() */
	struct iovec io_vec;                        /* scatter gather vector for getmsg() */
	char control[1024];                         /* buffer for ancilary data from getmsg() */
	struct timespec* time_stamp = NULL;

	int64 time_hwsystem = 0;                    /* for NIC HW TimeStamps */ 
	int64 time_hwraw = 0;                       /* for NIC HW TimeStamps */
	int64 last_time_hwsystem = 0;
	int64 last_time_hwraw = 0;

/* timers */
    StopWatch ipg_sw;                           /* time between packets */

/* statistics */

/* for pthreads */
   int stats_output_thread_ret;                 /* return code from creation */
   pthread_t stats_output_thread_id;            /* pointer to thread struct */
   int stats_output_thread_param = 1 ;          /* application parameter for thread */

   pthread_attr_t tattr;                        /* for storing thread attributes */
   unsigned long cur_mask;
   cpu_set_t new_cpuset;
   cpu_set_t cur_cpuset;
   int cpu;
   int max_cpu = sizeof(cur_mask)*8; // *8 to make in to bits - expect 64 bits

/* local variables */
    int error=0;
    int soc;                         		    /* handle for socket */
    int i;
    int j;
    int64 inc;                                  /* number by which the frame number increments */
    int64 frame_num;                            /* number of frame just received */ 
    int64 old_frame_num = -1;                   /* number of previous frame received */  
    int64 old_frame_recv_time =0;               /* time previous frame was received 0.1 us */
    int64 old_frame_send_time =0;               /* time previous frame was sent 0.1 us */
    int ret;
    char port_str[128];

    struct param *params;
 
/* set the signal handler for SIGALRM */
    signal (SIGALRM, sig_alarm);
/* define signal handler for cntl_c */
    signal(SIGINT, cntlc_handler);
/* define signal handler for cntl_z */
    signal(SIGTSTP, cntlz_handler);

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

    /* these are initialised on CMD_ZEROSTATS */
    relative_sw.t1 = 0; 
    relative_sw.t2 = 0;
    ipg_sw.t1 = 0;
    ipg_sw.t2 = 0;

/* Set the default IP Ethernet */
/* IP protocol number ICMP=1 IGMP=2 TCP=6 UDP=17 */
    dest_ip_address[0]=0;
    dest_udp_port = 0x3799;           		/* The default RAW/UDP port number (14233 base10) */

/* get the input parameters */
    parse_command_line ( argc, argv);

    printf(" cpu_affinity_mask %lx stats cpu_affinity_mask %lx \n", cpu_affinity_mask, cpu_affinity_mask_stats);
/* set histogram parameters */
    hist[0].bin_width = bin_width;
    hist[0].low_lim = low_lim;

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

/* allocate memory for -G option - free first  */
    if(recv_time_start != NULL) {
        free(recv_time_start);
	recv_time_start = NULL;
    }
    recv_time_ptr = NULL;
    recv_time_end = NULL;
    if(info_data_len >0) {
        recv_time_start = (int64 *)malloc(info_data_len);
	if ( recv_time_start == NULL ) {
	    perror("Malloc for -G option failed");
	    exit(-1);
	}
	recv_time_end = recv_time_start + info_data_len/sizeof(int64) ;
	memset ( recv_time_start, 0, info_data_len );	/* initialise the memory to zeros */
	recv_time_ptr = recv_time_start;
    }

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

    if(!quiet) printf(" The UDP port is   %d 0x%x\n", dest_udp_port, dest_udp_port);

/* enable hardware timestamping */
	if(use_hwTstamps){
		enable_timestamp(soc);
	}
	
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

/* For the want IP address clear then load the hints */
    if( dest_ip_address[0] !=0){

        bzero(&hint_options, sizeof(struct addrinfo) );
	hint_options.ai_family = AF_INET;
#ifdef	IPV6
	if(use_IPv6 == 1) hint_options.ai_family = AF_INET6;
#endif
	hint_options.ai_socktype = SOCK_DGRAM;
	/* no hint flags as not a server */
	error = getaddrinfo(dest_ip_address, port_str, &hint_options, &result_want_ip);   
	if(error){
	    snprintf(error_msg, ERROR_MSG_SIZE,
		     "Error: Could not use address family for the wanted_IP %s", gai_strerror(error) );
	    perror(error_msg );
	    exit(EXIT_FAILURE);
	}
	/* get the length of the sock address struct */
	soc_want_address = result_want_ip->ai_addr;
    }

/* check which timer to set: timer_prog_lifetime to exit the program and print stats and hitograms */
   if(timer_prog_lifetime >0) timer_val = timer_prog_lifetime;
   /* only set&use the timer for stats if NOT using a thread to print stats */
   if((timer_interval >0) && (cpu_affinity_mask_stats <=0) ) timer_val = timer_interval;

/* check if need to set the timer to exit the program and print stats and histograms */
    if(timer_val >0){
	timer_value.it_interval.tv_sec = timer_val;              /* Value to reset the timer when the it_value time elapses:*/
	timer_value.it_interval.tv_usec = 0;                     /*  (in us) */
	timer_value.it_value.tv_sec = timer_val;                 /* Time to the next timer expiration: 1 seconds */
	timer_value.it_value.tv_usec = 0;                        /*  (in us) */
	/* set the interval timer to be decremented in real time */
	ret = setitimer( ITIMER_REAL, &timer_value, &timer_old_value );
	if(ret){
	    perror("set interval timer failed :" );
	    exit(-1);
	}
    }

   /* set attributes for the Stats Output thread */
   pthread_attr_init (&tattr);
   if( (ret = pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED)) ){
       printf("set Create Detatched failed: %d\n", ret );
   }
   if(cpu_affinity_mask_stats >0){
       /* clear all CPUs from the sets */
       CPU_ZERO(&cur_cpuset);
       CPU_ZERO(&new_cpuset);
       cur_mask =0;
       /* convert the new mask to a set */
       for(i=0; i<max_cpu; i++){
	  if((cpu_affinity_mask_stats & (1<<i))>0) CPU_SET(i, &new_cpuset);
       }

       if( (ret = pthread_attr_setaffinity_np(&tattr, sizeof(new_cpuset), &new_cpuset)) ){
	    perror("pthread_attr_setaffinity");
       }
       /* Check the actual affinity mask assigned to the thread */
       if(verbose){
	   pthread_attr_getaffinity_np(&tattr, sizeof(new_cpuset), &cur_cpuset);
	   /* convert to a mask */
	   for(i=0; i<max_cpu; i++){
	     cpu = CPU_ISSET(i,&cur_cpuset);
	     if(cpu !=0) cur_mask = cur_mask |(1<<i);
	   }
	   printf("Set affinity for stats thread: %08lx\n ",  cur_mask);
       }

       /* set attributes for recv thread to increase priority */
       //   pthread_attr_getschedparam (&tattr, &param);
       //   param.sched_priority+=recv_prio;
       //   pthread_attr_setschedparam (&tattr, &param);
       
       if( (stats_output_thread_ret = pthread_create( &stats_output_thread_id, &tattr, 
						      &stats_output_thread, 
						      (void *) &stats_output_thread_param)) ){
	 printf("Stats Output Thread creation failed: %d\n", stats_output_thread_ret );
       }
   /* we dont wait for the thread to complete hence no pthread_join( stats_output_thread_id, NULL); */
   }

/* clear the local stats */
    num_recv = 0;
    bytes_recv =0;

/* record initial interface & snmp info */
    net_snmp_Start(  &net_snmp_stats);
    nic_stats_Start( &nic_stats);
 
/* record initial CPU and interrupt info */
    CPUStat_Start(  &cpu_stats);

//RHJ expt
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

	int recvmsg_flags =0;		// 0 for now
	
/* set a time zero */
    gettimeofday(&start, NULL);
    StopWatch_Start(&ipg_sw);

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

		/* what do we have to do  - they are all case CMD_DATA: */
        params = (struct param *) udp_data_recv;
		frame_num = i8swap(params->frame_num);
		num_recv++;  /* the number of packets seen */
		bytes_recv = bytes_recv + (int64)error;
        if(verbose) {
			/* check if we want to print this */
			if((dest_ip_address[0] == 0) || 
				(sock_cmp_addr( soc_recv_address,  soc_want_address) == 0) ){
					printf("Packet length: %d bytes\n", error);
					printf(" From host: %s\n", sock_ntop(soc_recv_address ));

					for(j=0; j<64; j++){
						printf(" %x", udp_data_recv[j]);
					}
					printf(" \n");
					printf(" Frame num %" LONG_FORMAT "d old frame num %" LONG_FORMAT "d\n",  frame_num, old_frame_num);
				printf(" --------------\n");
			}
		}

		/* record the time between the frame arrivals */
		StopWatch_Stop(&ipg_sw);
		ipg_time = StopWatch_TimeDiff(&ipg_sw);
		relative_sw.t2 = ipg_sw.t2;
		StopWatch_Start(&ipg_sw);
		/* histogram with 0.11us  bins*/
		hist_time = (long) 10*(ipg_time);
		h_fill1( &hist[0], hist_time);

		/* hardware Time Stamp*/
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

		if(first ==1){
			/* set a time zero for average throughput */
			relative_sw.t1 = ipg_sw.t2;
			relative_last_sw.t1 = ipg_sw.t2;
			first =0;
		}
		else if(first ==2){
			/* set the time zero for throughput for the _last data */
			relative_last_sw.t1 = ipg_sw.t2;
			first =0;
		}
		
		/* check increment of frame number */
		inc = frame_num - old_frame_num;
		if(inc == 1){
		}

		if(inc > 1) {
			num_lost = num_lost +inc -1;
			hist_time = (long) (inc-1);
			h_fill1( &hist[1], hist_time);
			if((frame_num >= n_to_skip ) && (log_lost == 0)){
				/* increment the pointer for timing info for this frame - *2 as record 2 words per frame */
				recv_time_ptr = recv_time_ptr + (inc-1)*ITEMS_PER_G_RECV;
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
		
    }    /* end of loop receiving frames */

    close(soc);

    return(0);    
}

static void parse_command_line (int argc, char **argv)
/* --------------------------------------------------------------------- */
{
/* local variables */
    char c;
    int error;
    time_t date;
    char *date_str;
    int i;
    char cmd_text[128];
    char *str_loc;

    error =0;
    char *help ={
"Usage: udpmon_bw_mon -option<parameter> [...]\n\
options:\n\
	-6 = Use IPv6\n\
	-B = <bin width of remote histo in us>\n\
	-G = <number of packets on which to return information>\n\
	-H = Print histograms\n\
	-I = <interface name for NIC information e.g. enp131s0f1 [NULL]>\n\
	-L = <[number of packets to skip:]number of LOST packets on which to return information>\n\
	-M = <min (low limit) of remote histo in us>\n\
	-N = Use NIC hardware TimeStamps\n\
	-Q = <DSCP QoS bits set - in hex >\n\
	-S = <size of send and receive socket buffers in bytes>\n\
	-T = time in sec between printing statistic snapshots [0=never]\n\
	-V = print version number\n\
	-a = <cpu_mask set bitwise cpu_no 3 2 1 0 in hex> app-cpu [: statsoutput-cpu]\n\
	-d = <the IP name or IP address to print a.b.c.d>\n\
	-h = print this message\n\
	-q = quiet - only print results\n\
	-t = time in sec before program ends [0=never]\n\
	-u = <raw/udp port no - default 0x3799 ie 14233 decimal>\n\
	-v = turn on debug printout"};


#ifdef IPv6
    while ((c = getopt(argc, argv, "a:d:t:u:B:G:I:L:M:Q:S:T:hqv6DHNV")) != (char) EOF) {
#else
    while ((c = getopt(argc, argv, "a:d:t:u:B:G:I:L:M:Q:S:T:hqvDHNV")) != (char) EOF) {
#endif	
	switch(c) {

	    case 'a':
			if (optarg != NULL) {
				memset(cmd_text, 0, strlen(cmd_text));
				strcpy(cmd_text,  optarg);
				str_loc = strstr(cmd_text, ":");
				if (str_loc) {
					*str_loc=' ';
					sscanf(cmd_text, "%lx %lx", &cpu_affinity_mask, &cpu_affinity_mask_stats);
				}
				else {
					cpu_affinity_mask_stats=0;
					sscanf(cmd_text, "%lx", &cpu_affinity_mask);
				}
			}
		break;

	    case 'd':
		if (optarg != NULL) {
		    memset(dest_ip_address, 0, HOSTNAME_MAXLEN);
		    strncpy(dest_ip_address,  optarg, HOSTNAME_MAXLEN-1);
		} else {
		    error = 1;
		}
		break;

	    case 'h':
            fprintf (stdout, "%s \n", help);
	        exit(EXIT_SUCCESS);
		break;

	    case 'q':
	        quiet = 1;
		break;

	    case 't':
		if (optarg != NULL) {
		   timer_prog_lifetime = atoi(optarg);
		}
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

	    case 'B':
		if (optarg != NULL) {
		   bin_width = atoi(optarg);
		} else {
		    error = 1;
		}
		break;

	    case 'H':
	        get_hist = 1;
		break;

		case 'I':
			if (optarg != NULL) {
				interface_name = optarg;
			} else {
				error = 1;
			}
		break;

	    case 'G':
			if (optarg != NULL) {
				memset(cmd_text, 0, strlen(cmd_text));
				strcpy(cmd_text,  optarg);
				str_loc = strstr(cmd_text, ":");
				if (str_loc) {
					*str_loc=' ';
					sscanf(cmd_text, "%"LONG_FORMAT"d %d", &n_to_skip, &info_data_len);
				}
				else {
					n_to_skip =0;
					sscanf(cmd_text, "%d", &info_data_len);
				}
				info_data_len = info_data_len*sizeof(int64)*ITEMS_PER_G_RECV;  /* *8 for bytes *2 as 2 words recorded per frame */
			}
	        get_info = 1;
			log_lost =0;
		break;

	    case 'L':
			if (optarg != NULL) {
				memset(cmd_text, 0, strlen(cmd_text));
				strcpy(cmd_text,  optarg);
				str_loc = strstr(cmd_text, ":");
				if (str_loc) {
					*str_loc=' ';
					sscanf(cmd_text, "%"LONG_FORMAT"d %d", &n_to_skip, &info_data_len);
				}
				else {
					n_to_skip =0;
					sscanf(cmd_text, "%d", &info_data_len);
				}
				info_data_len = info_data_len*8*3;  /* *8 for bytes *3 as 3 words recorded per frame */
			}
			log_lost =1;
	        get_info = 0;
		break;

	    case 'M':
		if (optarg != NULL) {
		   low_lim =  atoi(optarg);
		} else {
		    error = 1;
		}
		break;

	    case 'N':
	        use_hwTstamps = 1;
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
		   timer_interval = atoi(optarg);
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

    date = time(NULL);
    date_str = ctime(&date);
    date_str[strlen(date_str)-1]=0;
    printf(" %s :", date_str );
    printf(" %s ", UDPMON_VERSION);
    printf(" Command line: ");
    for(i=0; i<argc; i++){
      printf(" %s", argv[i]);
    }
    printf(" \n");
}

static void print_stats_hist( int mode)
/* --------------------------------------------------------------------- */
{
/*
    mode 
	= MODE_TABLE    print the table
	= MODE_HEADER   print text header line and then line of results
	= MODE_LINE     just print line of results
	= MODE_DATA     print hist + info + lost

	* * * * 
	Note the order of testing mode and doing the printout is required to give the desired output
	* * * * 
*/

  int i=1;
  int num;
  int64 recv_time_i;
  int64 last_recv_time;
  int64 send_time_i;
  int64 last_send_time;
  int64 *recv_time_print_ptr;
  int64 frame_num;
  int64 last_frame_num;
  int64 num_lost_innet;
  int64 num_lost_innet_snap;
  int64 rx_out_of_buffer;
  int64 time_hwsystem;
  int64 last_time_hwsystem;
  int64 time_hwraw;
  int64 last_time_hwraw;

  double data_rate = 0.0;
  double data_rate_last = 0.0;
  double wire_rate = 0.0;
  double wire_rate_last = 0.0;
  double elapsed_time;
  double elapsed_time_last;
  int bytes_per_frame;
  int bytes_per_frame_last;
  int extended_output =1;
  double loss_pcent;
  double loss_pcent_last;
  double recv_time_packet;
  double recv_time_packet_last;
  double loss_pcent_innet;
  double loss_pcent_innet_last;
  int update_last_counters =0;

  int delta_sec;
  double delta_hr;
  double excel_time;

	loss_pcent = ((num_recv )>0)? 
	             (double)100.0*(double)(num_lost)/(double)(num_recv ) : 0;

	loss_pcent_last = ((num_recv - num_recv_last)>0)? 
	             (double)100.0*(double)(num_lost - num_lost_last)/(double)(num_recv - num_recv_last) : 0;
	bytes_per_frame = (num_recv >0)? bytes_recv/num_recv : 0;
	bytes_per_frame_last = ((num_recv - num_recv_last) >0)? (bytes_recv - bytes_recv_last)/(num_recv - num_recv_last) : 0;

	/* calc the data rate seen */
	elapsed_time = StopWatch_TimeDiff(&relative_sw);

	relative_last_sw.t2 = relative_sw.t2;
	elapsed_time_last = StopWatch_TimeDiff(&relative_last_sw);

	recv_time_packet = ((num_recv )>0)?
	                   elapsed_time/(double)(num_recv ) : 0;
	recv_time_packet_last = ((num_recv - num_recv_last )>0)?
	                        elapsed_time_last/(double)(num_recv-num_recv_last ) : 0;

	first = 2;  // next packet gives the start time for the _last data

	/* check we have received packets then elapsed_time >0.0 */
	if(elapsed_time > 0.0) {
	    data_rate =(double)8.0*(double)bytes_recv/elapsed_time;
	    /* IPG 12  Preamble+start 8  eth header 14 eth CRC 4  IP 20 UDP 8 = 66 */
	    wire_rate =(double)8.0*(double)(bytes_recv+ num_recv*(66))/elapsed_time;

	    data_rate_last =(double)8.0*(double)(bytes_recv - bytes_recv_last)/elapsed_time_last;
	    /* IPG 12  Preamble+start 8  eth header 14 eth CRC 4  IP 20 UDP 8 = 66 */
	    wire_rate_last =(double)8.0*(double)((bytes_recv - bytes_recv_last)+ (num_recv-num_recv_last)*66)/elapsed_time_last;
	} else {
	    elapsed_time = 0.0; 
	    elapsed_time_last = 0.0; 
	}

	/* avoid snapping unnecessarily */
	if((mode & (MODE_TABLE | MODE_LINE)) != 0){
	    /* get local CPU & interupt info */
	    CPUStat_Snap(  &cpu_stats, cpuinfo, inter_info);

	    /* get interface & snmp info */
	    net_snmp_Snap(  &net_snmp_stats, net_if_info, &snmp_info);

	    /* get NIC stats */
	    nic_stats_Snap( &nic_stats, &nic_info );
	}

	rx_out_of_buffer = nic_stats_getValue( &nic_info, "rx_out_of_buffer");
	/* check for counter not implemented */
	if( rx_out_of_buffer ==-1) rx_out_of_buffer=0;
	
	/* num_lost is that seen by the application; UDPInErrors includes those lost in the stack by ALL apps.
	   hence set to 0 if negative 
	   num_lost_innet       statistics fron the start
	   num_lost_innet_snap  statistics for last period (snap)
	*/
	UDPInErrors_total += snmp_info.UDPInErrors;
	rx_out_of_buffer_total += rx_out_of_buffer;
	num_lost_innet = num_lost - UDPInErrors_total - rx_out_of_buffer_total;
	if(num_lost_innet < 0) num_lost_innet =0;
 	num_lost_innet_snap = (num_lost - num_lost_last) - snmp_info.UDPInErrors - rx_out_of_buffer;
	if(num_lost_innet_snap < 0) num_lost_innet_snap =0;
	loss_pcent_innet = ((num_recv )>0)? 
	             (double)100.0*(double)(num_lost_innet)/(double)(num_recv ) : 0;
	loss_pcent_innet_last = ((num_recv - num_recv_last)>0)? 
	             (double)100.0*(double)(num_lost_innet_snap)/(double)(num_recv - num_recv_last) : 0;

 /* do printout as selected */
	if((mode & MODE_TABLE) == MODE_TABLE){
	    printf(" \n");
	    printf("Frames recv           : %"LONG_FORMAT"d \t delta: %"LONG_FORMAT"d\n", 
		   num_recv, (num_recv - num_recv_last) );
	    printf("Frames lost           : %"LONG_FORMAT"d \t delta: %"LONG_FORMAT"d\n", 
		   num_lost, (num_lost - num_lost_last) );
	    printf("Frames out of order   : %"LONG_FORMAT"d \t delta: %"LONG_FORMAT"d\n", 
		   num_badorder, (num_badorder - num_badorder_last) );
	    printf("%% lost                : %g \t delta: %g\n", loss_pcent, loss_pcent_last);
	    printf("Frames lost in net    : %"LONG_FORMAT"d \t delta: %"LONG_FORMAT"d\n", 
		   num_lost_innet, num_lost_innet_snap );
	    printf("%% lost in net         : %g \t delta: %g\n", loss_pcent_innet, loss_pcent_innet_last);
	    printf("Bytes received        : %"LONG_FORMAT"d \t delta: %"LONG_FORMAT"d\n", 
		   bytes_recv, (bytes_recv - bytes_recv_last) );
	    printf("Bytes/Frame           : %d \t delta: %d\n", bytes_per_frame, bytes_per_frame_last );
	    
	    printf("Elapsed_time us       : %g \t delta: %g\n", elapsed_time, elapsed_time_last);
	    printf("Recv time/pkt us      : %g \t delta: %g\n", recv_time_packet, recv_time_packet_last);
	    printf("User data rate Mbit/s : %g \t delta: %g\n", data_rate, data_rate_last);
	    printf("Wire rate Mbit/s      : %g \t delta: %g\n", wire_rate, wire_rate_last);

	    update_last_counters = 1;
	}  /* end of MODE_TABLE */

	if((mode & MODE_HEADER) == MODE_HEADER){
	    /* print titles  */
	    printf("num; linux time; excel time; hour;");
	    printf(" num_recv; num_lost; num_badorder; %%lost;");
	    printf(" num_lost_innet; %%lost_innet;");
	    printf(" Bytes Recv; Bytes/frame;");
	    printf(" elapsed time us; time/recv pkt;");
	    printf(" recv_user_data_rate Mbit; recv_wire_rate Mbit;");
	    
	    CPUStat_print_cpu_info( NULL, 1, 'L', extended_output);
	    //	    CPUStat_print_inter_info( inter_info, 1, 'L');
	    net_snmp_print_info( net_if_info, &snmp_info, 1, 'L');
	    nic_stats_print_info(  &nic_info, 1, 'L');
	    printf(" \n");
	  } /* end of MODE_HEADER */

	if((mode & MODE_LINE) == MODE_LINE){
	    delta_sec = (int) now.tv_sec - now_sec_first;
	    /* calc excel time 
	       Excel stores dates and times as a number representing the number of days since 1900-Jan-0, 
	       plus a fractional portion of a 24 hour day:   ddddd.tttttt
	       SO
	       excel_time = (linux_time now )/(num sec in day) + time(1 jan 1970)*/
	    delta_hr = (double)delta_sec / 3600.0; 
	    excel_time = (double)now.tv_sec/(double)86400.0 + (double)25569.0;
	    printf(" %d; %d; %f; %g;",num_output, (int) now.tv_sec, excel_time, delta_hr);
	    printf(" %"LONG_FORMAT"d; %"LONG_FORMAT"d; %"LONG_FORMAT"d;", 
		   (num_recv - num_recv_last), 
		   (num_lost - num_lost_last), 
		   (num_badorder - num_badorder_last));
	    printf(" %.2g;", loss_pcent_last);
	    printf(" %"LONG_FORMAT"d;", num_lost_innet_snap ); 
	    printf(" %.2g;", loss_pcent_innet_last);
	    
	    printf(" %"LONG_FORMAT"d;", (bytes_recv - bytes_recv_last) );
	    printf(" %d;", bytes_per_frame_last );
	    printf(" %g;", elapsed_time_last);
	    printf(" %g;", recv_time_packet_last);
	    printf(" %g; %g;", data_rate_last, wire_rate_last);
	    
  	    /* print total local CPU info */
	    CPUStat_print_cpu_info( cpuinfo, 2, 'L', extended_output);
	    
	    /* print total local interupt info */
	    //	    CPUStat_print_inter_info( inter_info, 2, 'L');
	    
	    /* print local interface & snmp info */
	    net_snmp_print_info( net_if_info, &snmp_info, 2, 'L');
            nic_stats_print_info(  &nic_info, 2, 'L');
	    printf(" \n");

	    num_output++;
	    update_last_counters =1;
	}  /* end of MODE_LINE */

	if((mode & MODE_DATA) == MODE_DATA){
	    /* Print the histogram data */
	  if(get_hist == 1){
	      h_output( &hist[0]);
	      h_output( &hist[1]);
	      h_output( &hist[2]);
		  if(use_hwTstamps){
			h_output( &hist[3]);
			h_output( &hist[4]);			  
		  }
	  } /* end of if(get_hist) */ 

		if(get_info ==1){
			i=1;
			last_recv_time = 0;
			last_send_time = 0;
			printf("recv_time ipg us; send_time ipg us; \n" );

			printf(" num packets skipped =; %"LONG_FORMAT"d\n", n_to_skip); 
			printf("packet num; recv_time 0.1us; send_time 0.1us; diff 0.1us; ;" );
			printf("delta recv_time us; delta send_time us; ;" );
			printf("HW system time ns; HW raw time ns; ;");
			printf("delta HW system time us; delta HW raw time us; ");
		
			printf(" \n");
			for(recv_time_print_ptr = recv_time_start; recv_time_print_ptr < recv_time_end; recv_time_print_ptr++){
				recv_time_i =*recv_time_print_ptr;
				recv_time_print_ptr++;
				send_time_i =*recv_time_print_ptr;
				recv_time_print_ptr++;

				printf(" %"LONG_FORMAT"d; %"LONG_FORMAT"d; %"LONG_FORMAT"d; %"LONG_FORMAT"d; ;", 
						i+n_to_skip, recv_time_i, send_time_i, (send_time_i - recv_time_i ));
				/* cannot have deltas on 1st packet */
				if(i ==1) {
					printf(" ; ; ;");
				}
				else {
					/* allow for a lost packet */
					if(recv_time_i ==0){
						printf("0; 0; ;");
					}
					else{
						printf("%"LONG_FORMAT"d; %"LONG_FORMAT"d; ;", 
								(recv_time_i - last_recv_time), (send_time_i - last_send_time) ); 
					}
				}
				last_recv_time = recv_time_i;
				last_send_time = send_time_i;

				/* HW TimeStamps */
				time_hwsystem = *recv_time_print_ptr;
				recv_time_print_ptr++;
				time_hwraw = *recv_time_print_ptr;

				printf(" %"LONG_FORMAT"d; %"LONG_FORMAT"d; ;", 
							time_hwsystem, time_hwraw );
				/* cannot have deltas on 1st packet */
				if(i ==1) {
					printf(" ; ; ;");
				}
				else {
					/* allow for a lost packet */
					if(recv_time_i ==0){
						printf("0; 0; ;");
					}
					else{
					printf(" %g; %g; ;", 
							(double)(time_hwsystem - last_time_hwsystem)/(double)1000.0, 
							(double)(time_hwraw - last_time_hwraw)/(double)1000.0 );  
					}
				}		 
				last_time_hwsystem = time_hwsystem; 
				last_time_hwraw = time_hwraw;

				printf("/n");				
				i++;
			}
		} /* end of if(get_info) */
		fflush(stdout);

	  /* Write the LOST packet data */
	  if(log_lost == 1){
	    num=1;
	    last_recv_time = 0;
	    last_send_time = 0;
 	    last_frame_num = -1;
	    printf(" num packets skipped =; %"LONG_FORMAT"d\n", n_to_skip);
	    printf("lost event; recv_time 0.1us; send_time 0.1us; lost packet num;" );
	    printf("delta recv_time us; delta send_time us; num packets between losses;\n" );
	    for(recv_time_print_ptr = recv_time_start; recv_time_print_ptr < recv_time_end; recv_time_print_ptr++){
	        recv_time_i =*recv_time_print_ptr;
		recv_time_print_ptr++;
		send_time_i =*recv_time_print_ptr;
		recv_time_print_ptr++;
		frame_num =*recv_time_print_ptr;

		printf(" %d; %"LONG_FORMAT"d; %"LONG_FORMAT"d; %"LONG_FORMAT"d;", 
		       num, recv_time_i, send_time_i, frame_num);
		if(num==1){
		    printf(" 0; 0; 0\n");
		}
		else{
		    if(i ==1) {
		        printf(" 0; 0; 0\n"); /* allow for more printout that packets */
		    }
		    else {
		        printf(" %g; %g; %"LONG_FORMAT"d\n", 
			       (double)(recv_time_i- last_recv_time)/(double)10.0,  
			       (double)(send_time_i- last_send_time)/(double)10.0,  
			       (frame_num- last_frame_num) );
		    }
		}
		last_recv_time = recv_time_i;
		last_send_time = send_time_i;
		last_frame_num = frame_num;
		num++;
	    }
	    fflush(stdout);

	  } /* end of if(log_lost) */
	} /* end of mode == MODE_DATA */

        first = 2;  // next packet gives the start time for the _last data

/* check if have to update the last counters */
	if(update_last_counters == 1){
	    /* update the _last counters */
	    num_recv_last = num_recv;
	    num_lost_last = num_lost;
	    num_badorder_last = num_badorder;
	    bytes_recv_last = bytes_recv;
	}
 
}

static void cntlc_handler( int signo)
/* --------------------------------------------------------------------- */
{
/* called on cntl_C */
    print_stats_hist(MODE_TABLE | MODE_HEADER | MODE_LINE);

    print_stats_hist(MODE_DATA);

    return;
}
 
static void cntlz_handler( int signo)
/* --------------------------------------------------------------------- */
{
/* called on cntl_Z */
	fflush(stdout);
	printf("\n");
        printf("Frames recv        : %"LONG_FORMAT"d \n", num_recv );
        printf("Frames lost        : %"LONG_FORMAT"d \n", num_lost );
        printf("Frames out of order: %"LONG_FORMAT"d \n", num_badorder );

	printf("cntl-Z received : Process ended\n");
	fflush(stdout);
        exit(0);

    return;
}

static void sig_alarm( int signo)
/* --------------------------------------------------------------------- */
{
  int delay_sec;

/* get the time now */
    gettimeofday(&now, NULL);
    delay_sec = (now.tv_sec - start.tv_sec);

/* check if we terminate the program or just print snapshot of statistics */
    if(delay_sec < timer_prog_lifetime || (timer_prog_lifetime ==0)){

/* just print snapshot of statistics, interface & snmp info */
	if(timer_first){
	    timer_first =0;
	    now_sec_first = (int)now.tv_sec;
	    print_stats_hist(MODE_HEADER | MODE_LINE);
	}
	else{
	    print_stats_hist(MODE_LINE);
	}
    }

    else{
/* record final interface & snmp info and exit */
    sleep(1);   // make sure the counters have been updated
	if(timer_first){
	    timer_first =0;
	    print_stats_hist(MODE_HEADER | MODE_LINE | MODE_DATA);
	}
	else{
	    print_stats_hist(MODE_LINE | MODE_DATA);
	}
	exit(0);
    }
  return;
}

void *stats_output_thread(void *param)
/* --------------------------------------------------------------------- */
{
  for(;;){
      sleep(timer_interval);

      /* get the time now */
      gettimeofday(&now, NULL);
      /* just print snapshot of statistics, interface & snmp info */
      if(timer_first){
          timer_first =0;
	  now_sec_first = (int)now.tv_sec;
	  print_stats_hist(MODE_HEADER | MODE_LINE);
      }
      else{
	  print_stats_hist(MODE_LINE);
      }
  } // end of for-ever loop

}
