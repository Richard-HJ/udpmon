/* udpmon_bw_mon.c     R. Hughes-Jones  The University of Manchester

     Aim is to measure/monitor bandwidth and packet loss over a link
     Use UDP socket to:
           send a command to zero remote stats
	   wait for OK reponse - start send timer
	   send a series of n byte packets to remote node at specified interval
	   then send a command to request stats from remote node
	   wait for reponse - stop send timer
     Measure the time for n sends 
     Print local and remote stats

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

#include  <sys/utsname.h>                        /* for uname() get host name */
#include  <netdb.h>                              /* for struct hostent gethostbyname() */

#define UDP_DATA_MAX 128000
    unsigned char udp_data[UDP_DATA_MAX];        /* ethernet frame to send back */
    unsigned char udp_data_recv[UDP_DATA_MAX];   /* ethernet frames received */
    unsigned char udp_hist_recv[UDP_DATA_MAX];   /* ethernet frames received for remote histograms */
    unsigned char udp_snmp_recv[UDP_DATA_MAX];   /* ethernet frames received for snmp data */
    int *int_data_ptr;                           /* int pointer to data */
      
#define ERROR_MSG_SIZE 256
    char error_msg[ERROR_MSG_SIZE];                         /* buffer for error messages */

/* for command line options */
    extern char *optarg;

/* parameters */
    int source_udp_port = 0;
    int dest_udp_port;
    char dest_ip_address[HOSTNAME_MAXLEN];
    int pkt_len = 64;                		    /* length of request packet */
    int wait_time_max = 0;       	            /* max length of wait time used for looping over inter packet wait */
    int increment_len = 8;           	        /* size to increment response message */ 
    int get_hist =0;            	            /* set to 1 for histograms */
    int get_info =0;            	            /* set to 1 for information = relative packet arrival times */
    int bin_width =1;                           /* bin width of interframe time histo */
    int low_lim =0;                             /* low limit of interframe time histo */
    int wait_time_int=0;                        /* wait time between sending packets */
    int gap_time_int=0;                         /* time to wait time between sending busts of packets spaced by wait_time_int */
    int burst_mode = 0;                         /* =1 if send loop_count packets then wait gap_time_int 1/10 us */
    int soc_buf_size =65535;                    /* send & recv buffer size bytes */
    int precidence_bits=0;                      /* precidence bits for the TOS field of the IP header IPTOS_TOS_MASK = 0x1E */
    int tos_bits=0;                             /* tos bits for the TOS field of the IP header IPTOS_PREC_MASK = 0xE0 */
    int tos_set = 0;                            /* flag =1 if precidence or tos bits set */
    int dscp_bits=0;                            /* difserv code point bits for the TOS field of the IP header */
    int dscp_set = 0;                           /* flag =1 if dscp bits set - alternative to precidence or tos bits set */
    int quiet = 0;                              /* set =1 for just printout of results - monitor mode */
    int loop_count = 2;              	        /* no. times to loop over the message loop */
    int burst_count = 1;              	        /* no. bursts of packet to send in Burst mode */
    int response_len = 64;           	        /* length of response message */
    int verbose =0;                  		    /* set to 1 for printout (-v) */
    int use_IPv6 =0;                            /* set to 1 to use the IPv6 address family */
    int send_ack = 0;           	            /* set to n to tell remote end to send ACK aster n packets */
    int run_time_sec =0;                        /* no os sec to run test */
    int extended_output =0;                     /* set to 1 for more printout (CPUStats */
    int n_to_skip=0;                            /* number of packets to skip before recording data for -G option */
    int log_lost=0;                             /* =1 to log LOST packets only -L option */
    long cpu_affinity_mask;                     /* cpu affinity mask set bitwise cpu_no 3 2 1 0 in hex */
    float run_rate =0.0;                        /* user data rate in Mbit/s */
    int rate_set = 0;                           /* flag =1 if a data rate is given - calc  wait_time_int */
    int use_hwTstamps =0;                       /* set to 1 to use HW Time Stamp on NIC */

#define NUM_RECV_TIME  500
    int64 recv_time[NUM_RECV_TIME];             /* array for the recv times */

    int info_data_len = 0;                      /* length in bytes of info data to be returned by -G option */
    int loop_max = 0;                           /* loop control - number of times to loop over the sending loop - allows run to time */

/* statistics */
    struct HIST hist[10];
    int num_sent=0;                             /* total no. of packets sent */
    int num_timeout=0;                          /* total no. of recv timeouts */
    int num_ack=0;                              /* total no. of ACKs received */
    int num_bursts = 0;                         /* number of packet bursts sent */

/* parameters for sig_*() */
    int loops_done=0;

#define LOCK_SLEEP_TIME    30

/* forward declarations */
static void parse_command_line (int argc, char **argv);
static void sig_alrm(int signo);
static void cntlc_handler(int signo);
static void cntlz_handler(int signo);

int send_cmd(int soc, struct sockaddr *soc_address, socklen_t soc_address_len,  unsigned char *udp_data, int req_len, 
	     unsigned char *udp_data_recv, int recvbuf_size, int *num_timeout, char *name);
int time_sync(int soc, struct sockaddr *soc_address, socklen_t soc_address_len, int pkt_len, StopWatch *tsync_sw,
	      int *lsf_n, double *lsf_m, double *lsf_c );

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
    struct sockaddr *soc_address;               /* for remote host */
    struct sockaddr *source_soc_address;        /* for source (local) socket */
    int recvbuf_size = UDP_DATA_MAX;
    SOC_INFO soc_info;

/* for select on socket */
    fd_set file_set;                            /* list of file descriptors for select() */
    unsigned int flags = 0;          	        /* flags for sendto() recvfrom() select() */
    int ipfamily_addr_len;                      /* length of soc_recv_address - depends on IP family IPv4, IPv6*/
    int source_ipfamily_addr_len;               /* length of soc_recv_address - depends on IP family IPv4, IPv6*/
    struct addrinfo hint_options, *result;      /* for getaddrinfo() */

    struct param *params;

/* statistics */
    double mean, sigma, skew, kurtosis;         /* histogram moments */
    int32 median, iq25, iq75;                   /* median, 25% quartile 75% quartile */
    int64 num_in_hist;

    CPUStat cpu_stats;
    CPUinfo cpuinfo[NET_SNMP_MAX_CPU+1];
    CPUinfo *cpuload_ptr = NULL;
    Interrupt_info  inter_info[NET_SNMP_MAX_IF];  

    NET_SNMPStat net_snmp_stats;
    NETIFinfo net_if_info[NET_SNMP_MAX_IF];
    NETIFinfo net_if_info_recv[NET_SNMP_MAX_IF];
    SNMPinfo snmp_info;
    SNMPinfo snmp_info_recv;

    NICinfo nic_info;
    int64 rx_out_of_buffer;
    
/* timing */
    struct timeval before;           	        /* time before measurements */
    struct timeval after;            	        /* time after measurements */
    long delay;                      		    /* time for one message loop */
    double time_per_frame;
    double wait_time=0;                         /* time to wait between packets */
    double gap_time=0;                          /* time to wait between sending bursts of packets */
    double relative_time;                       /* time between curent packet and start of loop - StopWatch us */
    double first_last_time;

    int tsync_pkt_len =64;
    int tsync_n;
    double tsync_m, tsync_c;
    double one_way = 0;
    double last_one_way = 0;

/* timers */
    StopWatch latency_sw;                       /* to measure total time to send data */
    StopWatch wait_sw;                          /* time to wait between sending packets */
    StopWatch gap_sw;                           /* time to wait between sending bursts */
    StopWatch relative_sw;                      /* used to time stamp each packet sent relative to start of loop */

/* local variables */
    int error;
    int soc;                         		    /* handle for socket */
    int64 frame_num;                            /*  frame number */
    int64 last_frame_num =0;                    /*  frame number */
    double data_rate;   
    int i,j;
    int ret;
    int bytes_to_read;
    int data_index;                             /* byte index to data read from the network */
	int hist_num=0;
    int num;
    int cmd;
    int resp_len;
    int num_recv;
    int num_lost;
    int num_badorder;
    int num_lost_innet;
    int protocol_version;
    double pcent_lost;
    double pcent_lost_innet;

    int64 recv_time_i;                          /* -G or -L option recv_time[i] - to allow byteswap */
    int64 recv_time_i1;                         /* -G or -L option recv_time[i+1] - to allow byteswap */
    int64 last_recv_time_i =0;
    int64 last_recv_time_i1 =0;
	int64 last_time_hwsystem = 0;               /* for NIC hardware Timestamp */
	int64 last_time_hwraw = 0;                  /* for NIC hardware Timestamp */

    char port_str[128];
    char* remote_hostname;

    struct hostent *host_ptr;                   /* pointer to gethostbyname() struct */
    struct utsname local_name;
    char str_buf[INET_ADDRSTRLEN];
    int print_headers = 1;                      /* flag to prevent repeated printing of headers */
	int last_hist_num;

/* Set the default IP Ethernet */
/* IP protocol number ICMP=1 IGMP=2 TCP=6 UDP=17 */
    dest_udp_port = 0x3799;           		    /* The default RAW/UDP port number (14233 base10) */
    soc =0;
    frame_num =0;

/* set the signal handler for SIGALRM */
    signal (SIGALRM, sig_alrm);
/* define signal handler for cntl_c */
    signal(SIGINT, cntlc_handler);
/* define signal handler for cntl_z */
    signal(SIGTSTP, cntlz_handler);

/* get the input parameters */
    parse_command_line ( argc, argv);

/* set the CPU affinity of this process*/
    set_cpu_affinity (cpu_affinity_mask, quiet);

/* initalise and calibrate the time measurement system */
    ret = RealTime_Initialise(quiet);
    if (ret) exit(EXIT_FAILURE);
    ret = StopWatch_Initialise(quiet);
    if (ret) exit(EXIT_FAILURE);

/* initalise CPUStats */
    CPUStat_Init();
	
/* initialise snmp stats */
//RHJ
	if(use_IPv6 == 1 ){
		net_snmp_Init( &net_snmp_stats, &snmp_info, SNMP_V6 );
	}
	else {
		net_snmp_Init( &net_snmp_stats, &snmp_info, SNMP_V4 );
	}

/* test system timer */	
    gettimeofday(&before, NULL);
    sleep(1);	
    gettimeofday(&after, NULL);
	
    delay = ((after.tv_sec - before.tv_sec) * 1000000) + (after.tv_usec - before.tv_usec);
    if(!quiet) printf("clock ticks for 1 sec = %ld us\n", delay);

/* create the socket address with the correct IP family for sending UDP packets */
    sprintf(port_str, "%d", dest_udp_port);
/* clear then load the hints */
    bzero(&hint_options, sizeof(struct addrinfo) );
    hint_options.ai_family = AF_INET;
#ifdef	IPV6
    if(use_IPv6 == 1) hint_options.ai_family = AF_INET6;
#endif
    hint_options.ai_socktype = SOCK_DGRAM;
    /* no flags as not a server */
    error = getaddrinfo(dest_ip_address, port_str, &hint_options, &result);   
    if(error){
        snprintf(error_msg, ERROR_MSG_SIZE,
		 "Error: Could not use address family %s", gai_strerror(error) );
	perror(error_msg );
        exit(EXIT_FAILURE);
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

/* assign a protocol address (UDP port) to the source (local) UDP socket */
    if(source_udp_port > 0){
        /* create the socket address with the correct IP family for listening to UDP packets */
        sprintf(port_str, "%d", source_udp_port);

        /* clear then load the hints */
        bzero(&hint_options, sizeof(struct addrinfo) );
	hint_options.ai_family = AF_INET;
#ifdef	IPV6
	if(use_IPv6 == 1) hint_options.ai_family = AF_INET6;
#endif
	hint_options.ai_socktype = SOCK_DGRAM;
	/* set flags as if a server to allow the source port to be defined*/
	hint_options.ai_flags = AI_PASSIVE;
	error = getaddrinfo(NULL, port_str, &hint_options, &result);   
	if(error){
	  snprintf(error_msg, ERROR_MSG_SIZE,
		 "Error: Could not use address family on local socket %s", gai_strerror(error) );
	  perror(error_msg );
	  exit(EXIT_FAILURE);
	}
	/* get the length of the sock address struct */
	source_ipfamily_addr_len = result->ai_addrlen;
	source_soc_address = result->ai_addr;

        error = bind(soc, source_soc_address, source_ipfamily_addr_len );
	if (error) {
	  perror("Bind of port to source (local) UDP IP socket failed :" );
	  exit(-1);
	}
    }

/* convert remote IP address to text */
    remote_hostname = sock_ntop (soc_address );
    if(!quiet){
        printf(" The destination IP name: %s IP address: %s\n", dest_ip_address, remote_hostname);
	printf(" The destination UDP port is   %d %x ", dest_udp_port, dest_udp_port);
	if(source_udp_port > 0){
	  printf(" The source UDP port is  %d %x", source_udp_port, source_udp_port);
	}
	printf(" \n");

/* print titles  */
        printf(" %d bytes\n ",pkt_len);
	printf(" pkt len; num_sent;");
	if(extended_output){
	    printf(" num_ACKs recv; num_timeout;");
	    printf(" Time/frame us;" );
	}
	printf(" inter-pkt_time us;" );
	if(extended_output) printf(" Send time;" );
	printf(" send_user_data_rate Mbit;" );

	printf(" num_recv; num_lost; num_badorder; %%lost;");
	printf(" num_lost_innet; %%lost_innet;");

	if(extended_output) printf(" Recv time; time/recv pkt;");	
	printf(" recv_user_data_rate Mbit; recv_wire_rate Mbit;");	
	/* The printout of the extended headings eg CPU loads can only be done when you know the number of remote CPUs 
	   - hence after the tests */
   }

/* Here we build the Ethernet packet(s), ready for sending.
   Fill with random values to prevent any compression on the network 
   The same frame is sent, we just make sure that the right size is reported
*/
    for(int_data_ptr= (int *)&udp_data; 
        int_data_ptr < (int *)&udp_data + UDP_DATA_MAX/4; int_data_ptr++){
        *int_data_ptr =rand();
    }
    for(i=0; i<20; i++){
        udp_data[i] = i;
    }

    gap_time = (double) gap_time_int / (double) 10.0;
    /* calc wait_time_int in 1/10 us from the given run_rate in Mbit/s */
    if(rate_set ==1){
        wait_time_int = (int)(10.0*(float)pkt_len*8.0/run_rate);
    }

/* set up params for select() to provide timeout and check if there is data to read from the socket */
    FD_ZERO (&file_set);
    FD_SET (soc, &file_set);

/* loop over performing the test */
    do{
LOOP_START:

        wait_time = (double) wait_time_int / (double) 10.0;

/* clear the local stats */
	num_ack = 0;
	num_timeout = 0;
	delay = 0;
	frame_num = 0;
	loops_done = 0;

/* Write the request to the remote host to ask to test and clear stats */
	params = (struct param *)&udp_data;
	params->cmd = i4swap(CMD_ZEROSTATS);
	params->protocol_version = 0; /* this should be PROTOCOL_VERSION - 
                                         but old Software just reflects the packet contents - so always true */
	params->frame_num = 0;
	params->low_lim   = i4swap(low_lim);
	params->bin_width = i4swap(bin_width);
	params->send_ack  = i4swap(send_ack);
	params->resp_len  = i4swap(info_data_len);     /* length in bytes corresponding to the number of packets to record */
	params->n_to_skip = i4swap(n_to_skip);
	params->log_lost  = i4swap(log_lost);
    params->use_hwTstamps = i4swap(use_hwTstamps);

	ret = send_cmd(soc, soc_address, ipfamily_addr_len, udp_data, sizeof(struct param), 
		       udp_data_recv, recvbuf_size, &num_timeout, "zerocmd"); 
	/* check for no response / error */
	if(ret < 0)  exit(EXIT_FAILURE);
    
	/* check the response from remote end */
	params = (struct param *)&udp_data_recv;
	cmd = i4swap(params->cmd);
	protocol_version = i4swap(params->protocol_version);
	if(verbose) printf(" zerostats reply: cmd [0x20 = CMD_OK] 0x%x protocol version %d\n",
                          cmd, protocol_version);
	if(verbose) {
	    printf("Packet: \n");
	    for(j=0; j<64; j++){
	      printf(" %x", udp_data_recv[j]);
	    }
	    printf(" \n");
	}

/* can we talk to this remote end ? */      
        if(protocol_version != PROTOCOL_VERSION){
            snprintf(error_msg, ERROR_MSG_SIZE,
		    "Error: wrong protocol version %d version %d required", 
		    protocol_version, PROTOCOL_VERSION );
	    perror(error_msg );
	    exit(EXIT_FAILURE);
	}
	if(cmd == CMD_INUSE) {
	    sleep(LOCK_SLEEP_TIME);
	    goto LOOP_START;
	}
/* get Time-zero for stamping the packets */
        StopWatch_Start(&latency_sw);
	relative_sw.t1 = latency_sw.t1;

/* send request-response to sync the CPU clocks ie calculate Tlocal = m*Tremote + c  */
	if((get_info == 1)||(log_lost == 1)){
	    time_sync( soc, soc_address, ipfamily_addr_len, tsync_pkt_len, &relative_sw,
		       &tsync_n, &tsync_m, &tsync_c);
/* Write the CMD_START request to the remote host just to clear stats */
	    params = (struct param *)&udp_data;
	    params->cmd = i4swap(CMD_START);
	    params->protocol_version = i4swap(PROTOCOL_VERSION);
	    params->frame_num = 0;
	    params->resp_len = i4swap(info_data_len);     /* length in bytes */
	    ret = send_cmd(soc, soc_address, ipfamily_addr_len, udp_data, sizeof(struct param), 
			   udp_data_recv, recvbuf_size, &num_timeout, "startcmd"); 
	    /* check for no response / error */
	    if(ret < 0)  exit(EXIT_FAILURE);
	    if(verbose) {
	        params = (struct param *)&udp_data_recv;
		cmd = i4swap(params->cmd);
		protocol_version = i4swap(params->protocol_version);
	        printf(" start reply: cmd [0x20 = CMD_OK] 0x%x protocol version %d\n",
		       cmd, protocol_version);
	    }
 	}

	loop_max = loop_count;
/* set the alarm to determine length of test - sig_alrm() handler sets loop_max to 0 to stop */
	if(run_time_sec >0) alarm(run_time_sec);

/* record initial interface & snmp info */
	net_snmp_Start(  &net_snmp_stats);

/* record initial CPU and interrupt info */
	CPUStat_Start(  &cpu_stats);

/* loop over sending mock data  */
DATA_LOOP_START:
	gettimeofday(&before, NULL);
	for (i = 0; i < loop_max; i++) {

/* set the stopwatch going for waiting between sending packets */
	    StopWatch_Start(&wait_sw);
	    /* allow tests for a given length of time */
	    if(run_time_sec >0) i=0;

	    params = (struct param *)&udp_data;
	    params->cmd = i4swap(CMD_DATA);
	    params->frame_num = i8swap(frame_num);
/* timestamp the packet to send relative to the time started to loop */
	    relative_sw.t2 = wait_sw.t1;
	    relative_time = StopWatch_TimeDiff(&relative_sw);
	    params->send_time = i4swap( (int)(relative_time*(double)10.0) );
/* send the mock data */
	    error = sendto(soc, &udp_data, pkt_len , flags, soc_address, ipfamily_addr_len);
	    if(error != pkt_len) {
	        snprintf(error_msg, ERROR_MSG_SIZE,
                       "Error: on data send to %s: mock data frame sent %d bytes not %d ", 
                       dest_ip_address, error, pkt_len );
	        perror(error_msg );
	    }
	    frame_num++;
	    loops_done++;
/* check for any ACKs received */
	    if(send_ack >0){
		ret = select (soc+1, &file_set, NULL, NULL, NULL);
		if(verbose) printf("select ret %d\n", ret);

		if(ret >0) {
/* we have data in the socket */
/* Read the incoming UDP packet from the remote host. */
		    error = recvfrom(soc, udp_data_recv, recvbuf_size, flags, NULL, NULL );
		    if(error <0 ) {
		        /* convert IP address to text */
		        remote_hostname = sock_ntop (soc_address);
			snprintf(error_msg, ERROR_MSG_SIZE,
				"Error: on receive from %s port 0x%x : received %d bytes not %d ", 
				remote_hostname, sock_get_port(soc_address), error, recvbuf_size);
			perror(error_msg );
		    }
		    params = (struct param *)&udp_data_recv;
		    cmd = i4swap(params->cmd);
		    if(verbose) {
		        printf("Packet: \n");
                        printf("Command : %d Frame num %" LONG_FORMAT "d\n", cmd, i8swap(params->frame_num) );
			printf(" \n");
		    }
		    if(cmd == CMD_ACK) {
		        num_ack++;
			if(verbose){
			    printf("ACK %d \n", num_ack);
			}
		    }
		    else{
		        printf("Unknown message received: %x\n", cmd);
		    }
		} 
		else if(ret != 0) {
	            perror("Error: from select() for socket read :" );
		    return (-1);
		} /* end of select() test */
	    }
/* wait the required time */
	    StopWatch_Delay(&wait_sw, wait_time);
	
	}    /* end of loop sending frames */

/* record the time */
	StopWatch_Stop(&latency_sw);
	gettimeofday(&after, NULL);

	if(burst_mode){
/* wait the required time */
	    StopWatch_Start(&gap_sw);
	    StopWatch_Delay(&gap_sw, gap_time);
	    num_bursts ++;
	    if(num_bursts < burst_count) goto DATA_LOOP_START;
	}

/* record final CPU and interrupt info */
	CPUStat_Stop( &cpu_stats);

/* record final interface & snmp info */
	sleep(1);   // make sure the counters have been updated
	net_snmp_Stop(  &net_snmp_stats);

/* calculate the send time per packet  and data rate */
	delay = ((after.tv_sec - before.tv_sec) * 1000000) + (after.tv_usec - before.tv_usec);
	time_per_frame =  (double)delay  / (double)loops_done;
	data_rate = ( (double)pkt_len * 8.0 * (double)loops_done ) / (double)delay;

/* Write the request to the remote host to return the  stats */
        params = (struct param *)&udp_data;
	params->cmd = i4swap(CMD_GETSTATS);
	params->protocol_version = i4swap(PROTOCOL_VERSION);
	ret = send_cmd(soc, soc_address, ipfamily_addr_len, udp_data, sizeof(struct param), 
		       udp_data_recv, recvbuf_size, &num_timeout, "getstats"); 
	/* check for no response / error */
	if(ret < 0) exit(EXIT_FAILURE);
	params = (struct param *)&udp_data_recv;
       	if(verbose) {
	    printf("From udp_data_recv :\n");
	    printf("num_recv: %d\n", i4swap(params->num_recv) );
	    printf("num_lost: %d\n", i4swap(params->num_lost) );
	    printf("num_badorder: %d\n", i4swap(params->num_badorder) );
	    printf("first_last_time 0.1us: %"LONG_FORMAT"d\n", i8swap(params->first_last_time) );
       	}
	/* point to statistics just returned */
	params = (struct param *)&udp_data_recv;
	num_recv = i4swap(params->num_recv);
	if(num_recv ==0) num_recv = 1;
	num_lost = i4swap(params->num_lost);
	num_badorder = i4swap(params->num_badorder);
	first_last_time = i8swap(params->first_last_time)/(double)10.0;
	pcent_lost= 100.0*(double)(loops_done - num_recv)/(double)loops_done ;

/* Write the request to the remote host to return the  hist0 
   to avoid IP fragmenting the data for me and HENCE getting blocked at firewalls
   loop to get the histogram data (~4500 bytes)
*/ 
	hist_num =0;
	params = (struct param *)&udp_data;
	bytes_to_read = sizeof(struct HIST);
	data_index =0;
	while (bytes_to_read >0){
	    params->cmd = i4swap(CMD_GETHIST0);
		params->low_lim = i4swap(hist_num);
	    resp_len = 1400;
	    if(bytes_to_read < 1400) resp_len = bytes_to_read;
	    params->resp_len = i4swap(resp_len);
	    params->data_index = i4swap(data_index);
	    ret = send_cmd(soc, soc_address, ipfamily_addr_len, udp_data, sizeof(struct param), 
			   (unsigned char*)&udp_hist_recv[data_index], resp_len , &num_timeout, "gethist0"); 
	    /* check for no response / error */
	    if(ret < 0) exit(EXIT_FAILURE);
	    /* adjust amount to read and the index to the data to store */
	    bytes_to_read -= resp_len;
	    data_index += resp_len;
	}

	/* copy histogram to allow for byte swapping */
	h_copy( (struct HIST *)udp_hist_recv, &hist[0]);
	/* extract the staticstics from the hiso */
	h_stats( &hist[0], &num_in_hist, &mean, &sigma, &skew, &kurtosis, &median, &iq25, &iq75);

 /* send command to get remote network & snmp stats */
	params = (struct param *)&udp_data;
	bytes_to_read = sizeof(NET_SNMPStat );
	data_index =0;
	while (bytes_to_read >0){
	    params->cmd = i4swap(CMD_GETNETSNMP);
	    resp_len = 1400;
	    if(bytes_to_read < 1400) resp_len = bytes_to_read;
	    params->resp_len = i4swap(resp_len);
	    params->data_index = i4swap(data_index);	    
	    ret = send_cmd(soc, soc_address, ipfamily_addr_len, udp_data, sizeof(struct param), 
			   (unsigned char*)&udp_snmp_recv[data_index], resp_len , &num_timeout, "getnetsnmp"); 
	    /* check for no response / error */
	    if(ret < 0) exit(EXIT_FAILURE);
	    /* adjust amount to read and the index to the data to store */
	    bytes_to_read -= resp_len;
	    data_index += resp_len;
	}
	/* extract remote interface & snmp info */
	net_snmp_Info(  ( NET_SNMPStat *)udp_snmp_recv, net_if_info_recv, &snmp_info_recv);

	/* send request for NIC stats */
	params = (struct param *)&udp_data;
	resp_len = sizeof( nic_info);
	data_index =0;
	params->cmd = i4swap(CMD_GETNICSTATS);
	params->resp_len = i4swap(resp_len);
	params->data_index = i4swap(data_index);
	ret = send_cmd(soc, soc_address, ipfamily_addr_len, udp_data, sizeof(struct param),
		       (unsigned char*)&nic_info, resp_len , &num_timeout, "getnicstats");
	/* check for no response / error */
	if(ret < 0) exit(EXIT_FAILURE);

	rx_out_of_buffer = nic_stats_getValue( &nic_info, "rx_out_of_buffer");
	/* check for counter not implemented */
	if( rx_out_of_buffer ==-1) rx_out_of_buffer=0;

	/* num_lost is that seen by the application; UDPInErrors includes those lost in the stack by ALL apps.
	   hence set to 0 if negative */
	num_lost_innet = num_lost - snmp_info_recv.UDPInErrors - rx_out_of_buffer;
	if(num_lost_innet < 0) num_lost_innet =0;
	pcent_lost_innet= 100.0*(double)(num_lost_innet)/(double)loops_done ;

	if(extended_output){
	    /* Write the request to the remote host to return the cpu load 
	       to avoid IP fragmenting the data for me and HENCE getting blocked at firewalls
	       loop to get the data if need be (~4500 bytes)
	    */ 
	    params = (struct param *)&udp_data;
	    bytes_to_read = sizeof( cpuinfo);
	    data_index =0;
	    while (bytes_to_read >0){
	      params->cmd = i4swap(CMD_GETCPULOAD);
	      resp_len = 1400;
	      if(bytes_to_read < 1400) resp_len = bytes_to_read;
	      params->resp_len = i4swap(resp_len);
	      params->data_index = i4swap(data_index);
	      ret = send_cmd(soc, soc_address, ipfamily_addr_len, udp_data, sizeof(struct param), 
			     (unsigned char*)&udp_data_recv[data_index], resp_len , &num_timeout, "getcpuload"); 
	      /* check for no response / error */
	      if(ret < 0) exit(EXIT_FAILURE);
	      /* adjust amount to read and the index to the data to store */
	      bytes_to_read -= resp_len;
	      data_index += resp_len;
	    }
	    cpuload_ptr = (struct _CPUinfo *)&udp_data_recv;
	}

/* Do Printout */
	if(!quiet){
	  if(print_headers){
	      print_headers = 0;
	      if(extended_output){
		  /* The printout of the extended headings eg CPU loads can only be done when you know the number of remote CPUs 
		     - hence after the tests */
		  printf(" num_in_hist; mean; sigma; skew; kurtosis; median; iq25; iq75;");
		  
		  CPUStat_print_cpu_info( NULL, 1, 'L', extended_output);
		  //**
		  //printf(" \n  ==== num_cpus remote %d \n", cpuload_ptr->num_cpus);
		  CPUStat_print_cpu_info( cpuload_ptr, 1, 'R', extended_output);
		  
		  net_snmp_print_info( net_if_info, &snmp_info, 1, 'L');
		  net_snmp_print_info( net_if_info, &snmp_info, 1, 'R');

		  nic_stats_print_info(&nic_info, 1, 'R');
	      }
	      printf("\n");
	  }
	}
	if(quiet) {
	    printf(" %d ;",(int) before.tv_sec);
	    /*  get the local IP address */
	    ret = uname(&local_name);
	    host_ptr = gethostbyname(local_name.nodename);
	    /* allow for unknown local host */
	    if(host_ptr == NULL){
	      strcpy(str_buf, "local");
	    }
	    else{
	      inet_ntop(AF_INET, host_ptr->h_addr_list[0], str_buf , sizeof(str_buf) );
	    }
	    printf(" %s ; %s ;", str_buf, local_name.nodename);

	    /* and the remote address; name */
	    printf(" %s ;", remote_hostname);
	    printf(" %s ;", dest_ip_address );  

	}

	printf(" %d; %d;", pkt_len, loops_done);
	if(extended_output){
	    printf(" %d; %d;", num_ack, num_timeout);
	    printf(" %g;", time_per_frame );
	}
	printf("  %g; ", wait_time);
	if(extended_output){
	    printf(" %ld;", delay);
	}
	printf(" %g;", data_rate);

	/* print data from remote node */
	printf(" %d; %d; %d;",  num_recv, num_lost, num_badorder );
	printf(" %.2g;", pcent_lost);
	printf(" %d; %.2g;", num_lost_innet, pcent_lost_innet);
	if(extended_output){
	    printf("  %g;", first_last_time );
	    printf("  %g;", (first_last_time/(double) num_recv) );
	}
	data_rate = (((double)pkt_len * (double)8 * (double)num_recv)) / first_last_time;
	printf("  %g;", data_rate);
	/* IPG 12  Preamble+start 8  eth header 14 eth CRC 4  IP 20 UDP 8 = 66 */
	data_rate = (((double) (pkt_len+66) * (double)8 * (double)num_recv))  / first_last_time;
	printf("  %g;", data_rate);

	if(extended_output){
	    printf("  %" LONG_FORMAT "d; %g; %g; %g; %g; %d; %d; %d;", num_in_hist, mean, sigma, skew, kurtosis, 
		   median, iq25, iq75);
	    /* print total local CPU info */
	    CPUStat_Info(  &cpu_stats, cpuinfo, inter_info);
	    CPUStat_print_cpu_info( cpuinfo, 2, 'L', extended_output);

	    /* print total remote CPU info */
	    CPUStat_print_cpu_info( cpuload_ptr, 2, 'R', extended_output);

	    /* print local interface & snmp info */
	    net_snmp_Info(  &net_snmp_stats, net_if_info, &snmp_info);
	    net_snmp_print_info( net_if_info, &snmp_info, 2, 'L');
	    /* print remote interface & snmp info */
	    net_snmp_print_info( net_if_info_recv, &snmp_info_recv, 2, 'R');
	    /* print remote NIC info */
	    nic_stats_print_info(&nic_info, 2, 'R');
	}
	printf("  \n" );
	fflush(stdout);

       	if(get_hist == 1){
/* Print the returned the histogram data */
	    h_output( &hist[0]);
	    fflush(stdout);

//RHJ expt new way to get hist - use index not CMD_GETHIST2
	    /* and get the number-lost and number-outoforder histograms */
		last_hist_num = 3;
		if(use_hwTstamps) last_hist_num = 5;
		for(hist_num=1; hist_num<last_hist_num; hist_num++) {
	    params = (struct param *)&udp_data;
		
	    bytes_to_read = sizeof(struct HIST);
	    data_index =0;
	    while (bytes_to_read >0){
		params->cmd = i4swap(CMD_GETHIST0);
		params->low_lim = i4swap(hist_num);

		resp_len = 1400;
		if(bytes_to_read < 1400) resp_len = bytes_to_read;
		params->resp_len = i4swap(resp_len);
		params->data_index = i4swap(data_index);
		ret = send_cmd(soc, soc_address, ipfamily_addr_len, udp_data, sizeof(struct param), 
			       (unsigned char*)&udp_hist_recv[data_index], resp_len , &num_timeout, "gethist1"); 
		/* check for no response / error */
		if(ret < 0) exit(EXIT_FAILURE);
		/* adjust amount to read and the index to the data to store */
		bytes_to_read -= resp_len;
		data_index += resp_len;
	    }
	    /* copy histogram to allow for byte swapping */
	    h_copy( (struct HIST *)udp_hist_recv, &hist[0]);
	    h_output( &hist[0]);
	    fflush(stdout);
		}   // end of loop over getting histos
		}


/* Write the requests to the remote host to return the info data */
	if(get_info == 1){
	    params = (struct param *)&udp_data;
	    bytes_to_read = info_data_len;
	    data_index =0;
	    num = 1;
	    printf(" num packets skipped =; %d\n", n_to_skip); 
	    printf("packet; recv_time 0.1us; send_time 0.1us; diff 0.1us; 1-way time us; ;" );
	    printf("delta recv_time us; delta send_time us; delta 1-way time us; ;" );
		printf("HW system time ns; HW raw time ns; ;");
		printf("delta HW system time us; delta HW raw time us; ");
		printf(" \n");
	    while (bytes_to_read >0){
	         params->cmd = i4swap(CMD_GETINFO1);
		 resp_len = 1408;
		 if(bytes_to_read < 1408) resp_len = bytes_to_read;
		 params->resp_len = i4swap(resp_len);
		 params->data_index = i4swap(data_index);
		 ret = send_cmd(soc, soc_address, ipfamily_addr_len, udp_data, sizeof(struct param), 
				(unsigned char*)&recv_time, resp_len , &num_timeout, "getinfo1"); 
		 /* check for no response / error */
		 if(ret < 0) exit(EXIT_FAILURE);
		 /* print out the section just received - NB 1-way times are in units of 0.1 us */
		 for(i=0; i< resp_len/8; i=i+4){
			/* byteswap */
			recv_time_i = i8swap(recv_time[i]);
			recv_time_i1 = i8swap(recv_time[i+1]);
			one_way = ((double)recv_time_i*tsync_m/(double)10.0 + tsync_c - 
				(double)recv_time_i1/(double)10.0 );
			/* check not a lost packet */
			if( (recv_time_i == 0) && (recv_time_i1 == 0) ) one_way=0;
			printf(" %d; %"LONG_FORMAT"d; %"LONG_FORMAT"d; %"LONG_FORMAT"d; %g; ;", 
					num+n_to_skip, recv_time_i, recv_time_i1, 
					(recv_time_i- recv_time_i1),  one_way);
			if(num==1){
				printf("\n");
				last_recv_time_i = recv_time_i;
				last_recv_time_i1 = recv_time_i1;
				last_one_way = one_way;
				last_time_hwsystem = i8swap(recv_time[i+2]); 
				last_time_hwraw = i8swap(recv_time[i+3]);
			}
			else{
		         /* allow for a lost packet */
				if( (recv_time_i == 0) || (last_recv_time_i == 0)){
		             printf(" 0; 0; 0; ;");
		             printf(" 0; 0; ;");
				}
				else{
		             printf(" %g; %g; %g; ;", 
							(double)(recv_time_i- last_recv_time_i)/(double)10.0,  
							(double)(recv_time_i1- last_recv_time_i1)/(double)10.0,  
							(one_way- last_one_way) );
					printf(" %"LONG_FORMAT"d; %"LONG_FORMAT"d; ;", 
							i8swap(recv_time[i+2]),
							i8swap(recv_time[i+3]) );
					printf(" %g; %g; ;", 
							(double)(i8swap(recv_time[i+2]) - last_time_hwsystem)/(double)1000.0, 
							(double)(i8swap(recv_time[i+3]) - last_time_hwraw)/(double)1000.0 );  
				}
			 
				last_recv_time_i = recv_time_i;
				last_recv_time_i1 = recv_time_i1;
				last_one_way = one_way;
				last_time_hwsystem = i8swap(recv_time[i+2]); 
				last_time_hwraw = i8swap(recv_time[i+3]);
			}
			 printf("\n");
		     num++;
		 }
                  /* adjust amount to read and the index to the data to store */
		 bytes_to_read -= resp_len;
		 data_index += resp_len/8;
 
	    }

	    fflush(stdout);
	}     /* end of check on getting info */

/* Write the requests to the remote host to return the LOST packet data */
	if(log_lost == 1){
	    params = (struct param *)&udp_data;
	    bytes_to_read = info_data_len;
	    data_index =0;
	    num = 1;

 	    printf(" num packets skipped =; %d\n", n_to_skip);
	    printf("lost event; recv_time 0.1us; send_time 0.1us; diff 0.1us; one_way time us; lost packet num; ;");
	    printf("delta recv_time us; delta send_time us; num packets between losses;\n" );
	    while (bytes_to_read >0){
	         params->cmd = i4swap(CMD_GETINFO1);
		 /* use 1416 as must be a multiple of 3*8 = 24 */
		 resp_len = 1416;
		 if(bytes_to_read < 1408) resp_len = bytes_to_read;
		 params->resp_len = i4swap(resp_len);
		 params->data_index = i4swap(data_index);
		 ret = send_cmd(soc, soc_address, ipfamily_addr_len, udp_data, sizeof(struct param), 
				(unsigned char*)&recv_time, resp_len , &num_timeout, "getinfo1"); 
		 /* check for no response / error */
		 if(ret < 0) exit(EXIT_FAILURE);
		 /* print out the section just received - NB 1-way times are in units of 0.1 us */
		 for(i=0; i< resp_len/8; i=i+3){
		     /* byteswap */
		     recv_time_i = i8swap(recv_time[i]);
		     recv_time_i1 = i8swap(recv_time[i+1]);
		     frame_num = i8swap(recv_time[i+2]);
                     one_way = ((double)recv_time_i*tsync_m/(double)10.0 + tsync_c -
                                (double)recv_time_i1/(double)10.0 );
                     /* check for no lost packets */
                     if( (recv_time_i == 0) && (recv_time_i1 == 0) ) one_way=0;
                      printf(" %d; %"LONG_FORMAT"d; %"LONG_FORMAT"d; %"LONG_FORMAT"d; %g; %"LONG_FORMAT"d; ;", 
			    num, recv_time_i, recv_time_i1,
                            (recv_time_i- recv_time_i1), one_way, frame_num);
		     if(num==1){
		         printf(" %g; %g; %"LONG_FORMAT"d\n", 
				(double)recv_time_i/(double)10.0,  
				(double)recv_time_i1/(double)10.0,  
				frame_num );
			 last_recv_time_i = recv_time_i;
			 last_recv_time_i1 = recv_time_i1;
			 last_frame_num = frame_num;
		     }
		     else{
		         printf(" %g; %g; %"LONG_FORMAT"d\n", 
				(double)(recv_time_i- last_recv_time_i)/(double)10.0,  
				(double)(recv_time_i1- last_recv_time_i1)/(double)10.0,  
				(frame_num- last_frame_num) );
			 last_recv_time_i = recv_time_i;
			 last_recv_time_i1 = recv_time_i1;
			 last_frame_num = frame_num;
		     }
		     num++;
		 }
                  /* adjust amount to read and the index to the data to store */
		 bytes_to_read -= resp_len;
		 data_index += resp_len/8;
 
	    }

	    fflush(stdout);
	}     /* end of check on getting info */


/* send packet to say test complete */
	params = (struct param *)&udp_data;
	params->cmd = i4swap(CMD_TESTEND);
	ret = send_cmd(soc, soc_address, ipfamily_addr_len, udp_data, sizeof(struct param), 
		   udp_data_recv, recvbuf_size, &num_timeout, "testend"); 
	/* check for no response / error */
	if(ret < 0) exit(EXIT_FAILURE);

	wait_time_int = wait_time_int + increment_len;

    } while(wait_time_int <= wait_time_max);

    close(soc);

    return(0);
}


static void parse_command_line (int argc, char **argv)
/* --------------------------------------------------------------------- */
{
/* local variables */
    char c;
    int error;
    int i;
    time_t date;
    char *date_str;
    char cmd_text[128];
    char *str_loc;
    float value;

    char *help ={
"Usage: udpmon_bw_mon -option<parameter> [...]\n\
options:\n\
	-6 = Use IPv6\n\
	-A = <number of packets to receive for remote end to send ACK>\n\
	-B = <bin width of remote histo in us>\n\
	-G = <[number of packets to skip:]number of packets on which to return information>\n\
	-H = get remote histograms\n\
	-L = <[number of packets to skip:]number of LOST packets on which to return information>\n\
	-M = <min (low limit) of remote histo in us>\n\
	-N = Use NIC hardware TimeStamps\n\
	-P = <precidence bits set - in hex - will be shifted left by 9>\n\
	-Q = <DSCP QoS bits set - in hex >\n\
	-S = <size of send and receive socket buffers in bytes>\n\
	-T = <tos bits set - in hex - will be shifted left by 1>\n\
	-U = <source udp port no - default not bound>\n\
	-V = print version number\n\
	-a = <cpu_mask set bitwise cpu_no 3 2 1 0 in hex>\n\
	-d = <the destination IP name or IP address a.b.c.d>\n\
	-e = <end value of wait time tt.t in us>\n\
	-g = <gap time to wait between bursts tt.t in us>\n\
	-h = print this message\n\
	-i = <increment for wait time tt.t in us>\n\
	-l = <no. of frames to send>\n\
	-n = <no. of bursts to send in Burst Mode>\n\
	-p = <length in bytes of mock data packet>\n\
	-r = send data rate Mbit/s\n\
	-t = <no. of seconds to run the test - calculates no. of frames to send >\n\
	-q = quiet - only print results\n\
	-v = turn on debug printout\n\
	-u = <raw/udp port no - default 0x3799 ie 14233 decimal>\n\
	-w = <wait time tt.t in us>\n\
	-x = print more info (CPUStats) "};

    error=0;

 #ifdef IPv6   
    while ((c = getopt(argc, argv, "a:d:e:g:i:l:n:p:r:t:u:w:A:B:G:L:M:P:Q:S:T:U:hqvx6HNV")) != (char) EOF) {
#else
    while ((c = getopt(argc, argv, "a:d:e:g:i:l:n:p:r:t:u:w:A:B:G:L:M:P:Q:S:T:U:hqvxHNV")) != (char) EOF) {
#endif	
	switch(c) {

	    case 'a':
		if (optarg != NULL) {
		    sscanf(optarg, "%lx", &cpu_affinity_mask);
		} else {
		    error = 1;
		}
		break;

	    case 'd':
		if (optarg != NULL) {
		    memset(dest_ip_address, 0, HOSTNAME_MAXLEN);
		    strncpy(dest_ip_address,  optarg, HOSTNAME_MAXLEN-1);
			/* see if it is IPv6 */
			str_loc = strstr(dest_ip_address, ":");
			if (str_loc) {
				use_IPv6 =1;
			}
		} else {
		    error = 1;
		}
		break;

	    case 'e':
		if (optarg != NULL) {
		    sscanf(optarg, "%f", &value);
		    wait_time_max = (int)(10.0*value);
		} else {
		    error = 1;
		}
		break;

	    case 'g':
		if (optarg != NULL) {
		    sscanf(optarg, "%f", &value);
		    gap_time_int = (int)(10.0*value);
		    burst_mode =1;
		} else {
		    error = 1;
		}
		break;

	    case 'h':
            fprintf (stdout, "%s \n", help);
	        exit(EXIT_SUCCESS);
		break;

	    case 'i':
		if (optarg != NULL) {
		    sscanf(optarg, "%f", &value);
		    increment_len = (int)(10.0*value);
 		} else {
		    error = 1;
		}
		break;

	    case 'l':
		if (optarg != NULL) {
		   loop_count = atoi(optarg);
		} else {
		    error = 1;
		}
		break;

	    case 'n':
		if (optarg != NULL) {
		   burst_count = atoi(optarg);
		} else {
		    error = 1;
		}
		break;

	    case 'u':
		if (optarg != NULL) {
		    dest_udp_port =  atoi(optarg); 
		} else {
		    error = 1;
		}
		break;

	    case 'p':
		if (optarg != NULL) {
		    pkt_len = atoi(optarg);
		} else {
		    error = 1;
		}
		break;

	    case 'q':
	        quiet = 1;
		break;

	    case 'r':
		if (optarg != NULL) {
		    sscanf(optarg, "%f", &run_rate);
		    rate_set =1;
		} else {
		    error = 1;
		}
		break;

	    case 't':
		if (optarg != NULL) {
		    run_time_sec = atoi(optarg);
		} else {
		    error = 1;
		}
		break;

	    case 'v':
	        verbose = 1;
		break;

	    case 'w':
		if (optarg != NULL) {
		    sscanf(optarg, "%f", &value);
		    wait_time_int = (int)(10.0*value);
		} else {
		    error = 1;
		}
		break;

		case 'x':
			extended_output = 1;
		break;

	    case '6':
	        use_IPv6 = 1;
		break;

	    case 'A':
		if (optarg != NULL) {
		    send_ack = atoi(optarg);
		} else {
		    error = 1;
		}
		break;

	    case 'B':
		if (optarg != NULL) {
		   bin_width = atoi(optarg);
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
                         sscanf(cmd_text, "%d %d", &n_to_skip, &info_data_len);
                    }
                    else {
                        n_to_skip =0;
                        sscanf(cmd_text, "%d", &info_data_len);
                    }
		   info_data_len = info_data_len*8*4;  /* *8 for bytes *2 as 4 words recorded per frame */
		}
	        get_info = 1;
		log_lost =0;
		break;

	    case 'H':
	        get_hist = 1;
		break;

	    case 'L':
		if (optarg != NULL) {
                    memset(cmd_text, 0, strlen(cmd_text));
                    strcpy(cmd_text,  optarg);
                    str_loc = strstr(cmd_text, ":");
                    if (str_loc) {
                        *str_loc=' ';
                         sscanf(cmd_text, "%d %d", &n_to_skip, &info_data_len);
                    }
                    else {
                        n_to_skip =0;
                        sscanf(cmd_text, "%d", &info_data_len);
                    }
		   info_data_len = info_data_len*8*3;  /* *8 for bytes *3 as 3 words recorded per frame */
		   log_lost =1;
		}
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

	    case 'U':
		if (optarg != NULL) {
		    source_udp_port =  atoi(optarg); 
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
    if ((pkt_len == 0) || (loop_count == 0)) {
	error = 1;
    }
    if (error || argc < 2) {
	fprintf (stderr, "%s \n", help);
	exit	(EXIT_FAILURE);
    }

    if(!quiet){
        date = time(NULL);
	date_str = ctime(&date);
        date_str[strlen(date_str)-1]=0;
        printf(" %s :", date_str );
        printf(" %s CPUs", UDPMON_VERSION);
        printf(" Command line: ");
	for(i=0; i<argc; i++){
            printf(" %s", argv[i]);
	}
	printf(" \n");
    }

    return;
}

static void cntlc_handler( int signo)
/* --------------------------------------------------------------------- */
{
/* called on cntl_C */
	fflush(stdout);
        printf("Done  %d loops out of %d.  response length %d total num frames sent %d num. timeouts   %d\n", 
	       loops_done, loop_count, response_len,  num_sent, num_timeout);
	fflush(stdout);

    return;
}
 
static void cntlz_handler( int signo)
/* --------------------------------------------------------------------- */
{
/* called on cntl_Z */
	fflush(stdout);
        printf("Done  %d loops out of %d.  response length %d num. timeouts   %d\n", 
	       loops_done, loop_count, response_len,  num_timeout);
	printf("cntl-Z received : Process ended\n");
	fflush(stdout);
        exit(EXIT_SUCCESS);

    return;
}

static void sig_alrm( int signo)
/* --------------------------------------------------------------------- */
{
  if(run_time_sec >0){
      /* timer determining the length of the test has expired */
      loop_max = 0;
  }
  else {
      /* Just interrupt the recvfrom() */

       printf("SIGALRM caught\n");
  }
  return;
}

int send_cmd(int soc, struct sockaddr *soc_address, socklen_t soc_address_len,  unsigned char *udp_data, int req_len, 
		 unsigned char *udp_data_recv, int recvbuf_size, int *num_timeout, char *name)
/* --------------------------------------------------------------------- */
{
/* for select on socket */
    fd_set file_set;                            /* list of file descriptors for select() */
    struct timeval time_out;                    /* used to set time out for select() recvfrom() */
    int ret;
    int error;
    unsigned int flags = 0;          	       /* flags for sendto() recvfrom() */
    int num_tries = 0;                          /* counter of how many attempts made to send the command */
    char* remote_hostname;

SEND_CMD:
/* send the request */
	error = sendto(soc, udp_data, req_len , flags, soc_address, soc_address_len);
	if(error != req_len) {
	    /* convert IP address to text */
	    remote_hostname = sock_ntop (soc_address);
	    snprintf(error_msg, ERROR_MSG_SIZE,
		    "Error: on send to %s port 0x%x : %s cmd frame sent %d bytes not %d ", 
		    remote_hostname, sock_get_port(soc_address), name, error, req_len );
	    perror(error_msg );
	}

/* receive the response  */
/* set up params for select() to provide timeout and check if there is data to read from the socket */
	FD_ZERO (&file_set);
	FD_SET (soc, &file_set);
	time_out.tv_sec = 3;                        /* set to 3s was 1 sec */
	time_out.tv_usec = 0;
	ret = select (soc+1, &file_set, NULL, NULL, &time_out);
	if(ret >0) {
	      /* we have data in the socket */
	    error = recvfrom(soc, udp_data_recv, recvbuf_size, flags, NULL, NULL );
	    if(error <0 ) {
	        /* convert IP address to text */
	        remote_hostname = sock_ntop (soc_address);
		snprintf(error_msg, ERROR_MSG_SIZE,
			"Error: on receive from %s port 0x%x : %s response frame received %d bytes not %d ", 
			remote_hostname, sock_get_port(soc_address), name, error, recvbuf_size);
		perror(error_msg );
	    }
	} 
	else if(ret == 0) {
	      /* timeout */
	    (*num_timeout)++;
	    num_tries++;
	    if(num_tries >=10){
	      /* convert IP address to text */
	      remote_hostname = sock_ntop (soc_address);
	      snprintf(error_msg, ERROR_MSG_SIZE,
                "Error: No response from remote host %s port 0x%x", 
		    remote_hostname, sock_get_port(soc_address) );
	      perror(error_msg );
	      return (-2);
	    }
	    if(verbose) {
	        printf(" num_timeout %s %d\n", name, *num_timeout);
	    }
	    goto SEND_CMD;
	}
	else {
	        perror("Error: from select() for socket read :" );
		return (-1);
        } /* end of select() test */

	return (0);
}

int time_sync(int soc, struct sockaddr *soc_address, socklen_t soc_address_len, int pkt_len,  StopWatch *tsync_sw,
	      int *lsf_n, double *lsf_m, double *lsf_c )
/* --------------------------------------------------------------------- */
{

/* routine to synchonise cpu cycle counters on 2 nodes */

/* for select on socket */
    fd_set file_set;                            /* list of file descriptors for select() */
    struct timeval time_out;                    /* used to set time out for select() recvfrom() */
    int i;
    int ret;
    int error;
    unsigned int flags = 0;          	        /* flags for sendto() recvfrom() */
    int num_tries = 0;                          /* counter of how many attempts made to send the command */
    int max_loop = 200;
    long long frame_num =0;                     /* frame number for TSYNC test */
    int recvbuf_size= 2000;

    struct param *params;

/* time */
    double rtt;
    double remote_time_estimate;                /* estimated time of remote clock */
    double remote_time;                         /* time of remote clock in packet */
    double time_diff;
    int64 recv_time;                            /* time in us that the returned frame was received */
    long test_sendto_time;

/* least squares fit */
    LsFit tsync_lsf;

    LsFit_Initialise(&tsync_lsf);

 /* loop sending time-stamped frames. Get back frame with remote time-stamp */
    for(i=0; i<max_loop; i++){

SEND_CMD:
        frame_num++;
/* send the CMD_TSYNC packet */
        params = (struct param *)&udp_data;
	params->cmd = i4swap(CMD_REQ_RESP);
	params->frame_num = frame_num;
	params->resp_len = pkt_len;
/* timestamp the packet to send */
	StopWatch_Stop(tsync_sw);
	params->send_time = StopWatch_TimeDiff(tsync_sw);

	error = sendto(soc, udp_data, pkt_len , flags, soc_address, soc_address_len);
	if(error != pkt_len) {
	    snprintf(error_msg, ERROR_MSG_SIZE,
		    "Error: on send to %s:  TSYNC frame sent %d bytes not %d ", 
		    sock_ntop(soc_address), error, pkt_len );
	    perror(error_msg );
	}
	/* * debug */
	StopWatch_Stop(tsync_sw);
	test_sendto_time = StopWatch_TimeDiff(tsync_sw);

/* receive the response  */
/* set up params for select() to provide timeout and check if there is data to read from the socket */
	FD_ZERO (&file_set);
	FD_SET (soc, &file_set);
	time_out.tv_sec = 1;                        /* set to 1 sec */
 	time_out.tv_usec = 0;
	ret = select (soc+1, &file_set, NULL, NULL, &time_out);
	if(ret >0) {
	      /* we have data in the socket - timestamp the packet received - relative to t0*/
	    StopWatch_Stop(tsync_sw);
	    alarm(0);    /* cancel */
	} 
	else if(ret == 0) {
	      /* timeout */
	    num_tries++;
	    if(num_tries >=10){
	      snprintf(error_msg, ERROR_MSG_SIZE, "Error: No response for TSYNC from remote host %s ", 
		       sock_ntop(soc_address) );
	      perror(error_msg );
	      return (-2);
	    }
	    if(verbose) {
	        printf("tsync: frame num %Ld  timeout  \n", frame_num);
	    }
	    goto SEND_CMD;
	}
	else {
	  /* check for interrupt of system service - just re-do */
	    if(ret == EINTR) goto SEND_CMD;
	    perror("Error: from TSYNC select() for socket read :" );
	    return (-1);
    } /* end of select() test */

/* read the response from the far end */
	error = recvfrom(soc, udp_data_recv, recvbuf_size, flags, NULL, NULL );
	if(error <0 ) {
	    snprintf(error_msg, ERROR_MSG_SIZE,
		    "Error: on receive from %s: TSYNC frame received %d bytes not %d ", 
		    sock_ntop(soc_address), error, recvbuf_size );
	    perror(error_msg );
	}
        params = (struct param *)&udp_data_recv;

/* check the frame received matches the sent frame - use frame number */
	if(params->frame_num == frame_num){
	    /* calculate the rtt in us */
	    recv_time = StopWatch_TimeDiff(tsync_sw);
	    /* calculate the rtt in us */
	    rtt = recv_time - params->send_time;
	    /* calc time at remote node */
	    remote_time_estimate = params->send_time + rtt/2;
	    time_diff =  remote_time_estimate -  params->resp_time;
	    remote_time = params->resp_time;

	    if(verbose) {
	        printf("frame %Ld rtt %g us send_time %"LONG_FORMAT"d resp_time %"LONG_FORMAT"d diff %g\n",  
		       frame_num, rtt, params->send_time, params->resp_time, time_diff);
                printf("frame %Ld send_time %"LONG_FORMAT"d sendto_time %ld diff %"LONG_FORMAT"d :: recv time %"LONG_FORMAT"d diff %"LONG_FORMAT"d\n",  
		       frame_num, params->send_time, test_sendto_time, (test_sendto_time-params->send_time),
		       recv_time, (recv_time - test_sendto_time) );
		usleep(1000); /* sleep for 1ms - needed to prevent interaction between printout and rtt measurements */
	    }

	    LsFit_Data(&tsync_lsf, remote_time, remote_time_estimate);
	}
   }  /* end of for() loop */

   LsFit_Fit(&tsync_lsf, lsf_n, lsf_m, lsf_c); 
   if(verbose) printf("n %d m %g c %g \n", *lsf_n, *lsf_m, *lsf_c);

   return (0);
}
