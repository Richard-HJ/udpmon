/*
     udpmon_send.c     R. Hughes-Jones  The University of Manchester

     Aim is to send a stream of Ethernet packets without negociating with the remote end to allow link debugging
     Use UDP socket to:
	   send a series (-l) of n byte (-p) packets to remote node with a specified interpacket interval (-w)
     Print local stats

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
      Version 1.2.0                           
        rich  26 Jul 07 Initial version 

*/
#define INSTANTIATE true

#include "net_test.h"                            /* common inlcude file */
#include "version.h"                             /* common inlcude file */

#define UDP_DATA_MAX 128000
    unsigned char udp_data[UDP_DATA_MAX];        /* ethernet frame to send back */
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
    int bin_width;                              /* bin width of interframe time histo */
    int low_lim;                                /* low limit of interframe time histo */
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
    int extended_output =1;                     /* set to 1 for more printout (CPUStats */
    int n_to_skip=0;                            /* number of packets to skip before recording data for -G option */
    int log_lost=0;                             /* =1 to log LOST packets only -L option */
    long cpu_affinity_mask;                     /* cpu affinity mask set bitwise cpu_no 3 2 1 0 in hex */
    float run_rate =0.0;                        /* user data rate in Mbit/s */
    int rate_set = 0;                           /* flag =1 if a data rate is given - calc  wait_time_int */

/* control */
    int loop_max = 0;                           /* loop control - number of times to loop over the sending loop - allows run to time */

/* statistics */
    struct HIST hist[10];
    int num_sent=0;                             /* total no. of packets sent */
    int num_bursts = 0;                         /* number of packet bursts sent */

/* parameters for sig_*() */
    int loops_done=0;

#define LOCK_SLEEP_TIME    30

/* forward declarations */
static void parse_command_line (int argc, char **argv);
static void sig_alrm(int signo);
static void cntlc_handler(int signo);
static void cntlz_handler(int signo);


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
    SOC_INFO soc_info;
    unsigned int flags = 0;          	        /* flags for sendto() recvfrom() select() */
    int ipfamily_addr_len;                      /* length of soc_recv_address - depends on IP family IPv4, IPv6*/
    int source_ipfamily_addr_len;               /* length of soc_recv_address - depends on IP family IPv4, IPv6*/
    struct addrinfo hint_options, *result;      /* for getaddrinfo() */

/* statistics */
    CPUStat cpu_stats;
    CPUinfo cpuinfo[NET_SNMP_MAX_CPU+1];
    Interrupt_info  inter_info[NET_SNMP_MAX_IF];  

    NET_SNMPStat net_snmp_stats;
    NETIFinfo net_if_info[NET_SNMP_MAX_IF];
    SNMPinfo snmp_info;

/* timing */
    struct timeval before;           	        /* time before measurements */
    struct timeval after;            	        /* time after measurements */
    long delay;                      		/* time for one message loop */
    double time_per_frame;
    double wait_time=0;                         /* time to wait between packets */
    double gap_time=0;                          /* time to wait between sending bursts of packets */
    double relative_time;                       /* time between curent packet and start of loop - StopWatch us */

/* timers */
    StopWatch wait_sw;                          /* time to wait between sending packets */
    StopWatch gap_sw;                           /* time to wait between sending bursts */
    StopWatch relative_sw;                      /* used to time stamp each packet sent relative to start of loop */
    struct param *params;

/* local variables */
    int error;
    int soc;                         		/* handle for socket */
    int64 frame_num;                            /*  frame number */
    double data_rate;   
    int i;
    int ret;
    char port_str[128];
    char* remote_addr_text;


/* Set the default IP Ethernet */
/* IP protocol number ICMP=1 IGMP=2 TCP=6 UDP=17 */
    dest_udp_port = 0x3799;           		/* The default UDP port number (14233 base10) */
    soc = 0;

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
    remote_addr_text = sock_ntop (soc_address );
    if(!quiet){
        printf(" The destination IP name: %s IP address: %s\n", dest_ip_address, remote_addr_text);
	printf(" The destination UDP port is  %d %x ", dest_udp_port, dest_udp_port);
	if(source_udp_port > 0){
	  printf(" The source UDP port is  %d %x", source_udp_port, source_udp_port);
	}
	printf(" \n");

/* print titles  */
        printf(" %d bytes\n ",pkt_len);
	printf(" pkt len; num_sent;");
	printf(" Time/frame us; wait_time; Send time; send_data_rate Mbit;" );
	CPUStat_print_cpu_info( NULL, 1, 'L', extended_output);
	//	CPUStat_print_inter_info( inter_info, 1, 'L');
	net_snmp_print_info( net_if_info, &snmp_info, 1, 'L');
	printf("\n");
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
   
   wait_time = (double) wait_time_int/ (double) 10.0;
   
/* clear the local stats */
   delay = 0;
   frame_num = 0;
   loops_done = 0;

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
/* get Time-zero for stamping the packets */
   StopWatch_Start(&relative_sw);

   for (i = 0; i < loop_max; i++) {

/* set the stopwatch going for waiting between sending packets */
       StopWatch_Start(&wait_sw);
       /* allow tests for a given length of time */
       if(run_time_sec >0) i=0;

       params = (struct param *)&udp_data;
       params->cmd = i4swap(CMD_DATA);
#ifdef VLBI
	params->frame_num = i8swap(i8SWAP(frame_num));
#else
	params->frame_num = i8swap(frame_num);
#endif
/* timestamp the packet to send relative to the time started to loop */
	    relative_sw.t2 = wait_sw.t1;
	    relative_time = StopWatch_TimeDiff(&relative_sw);
	    params->send_time = i8swap( (int)(relative_time*(double)10.0) );

/* send the mock data */
       if(verbose)printf(" pkt_len %d loops_done %d\n ", pkt_len, loops_done);
       error = sendto(soc, &udp_data, pkt_len , flags, soc_address, ipfamily_addr_len);
       if(error != pkt_len) {
	   snprintf(error_msg, ERROR_MSG_SIZE,
		    "Error: on data send to %s: mock data frame sent %d bytes not %d ", 
		    dest_ip_address, error, pkt_len );
	   perror(error_msg );
       }
       frame_num++;
       loops_done++;

/* wait the required time */
       StopWatch_Delay(&wait_sw, wait_time);
	
   }    /* end of loop sending frames */

/* record the time */
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
   sleep(3);   // make sure the counters have been updated
   net_snmp_Stop(  &net_snmp_stats);

/* calculate the time per packet  */
   delay = ((after.tv_sec - before.tv_sec) * 1000000) + (after.tv_usec - before.tv_usec);
   time_per_frame =  (double)delay  / (double)loops_done;
   data_rate = ( (double)pkt_len * 8.0 * (double)loops_done ) / (double)delay;
   

   printf(" %d; %d; ", pkt_len, loops_done);
   printf("  %g; ", time_per_frame );
   printf("  %g; ", wait_time);
   printf("  %ld; ", delay);
   printf("  %g; ", data_rate);

/* print total local CPU info */
   CPUStat_Info(  &cpu_stats, cpuinfo, inter_info);
   CPUStat_print_cpu_info( cpuinfo, 2, 'L', extended_output);

/* print local interface & snmp info */
   net_snmp_Info(  &net_snmp_stats, net_if_info, &snmp_info);
   net_snmp_print_info( net_if_info, &snmp_info, 2, 'L');
     
   printf("  \n" );
   fflush(stdout);
   
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
    char *str_loc;
    float value;

    char *help ={
"Usage: udpmon_bw_mon -option<parameter> [...]\n\
options:\n\
	-6 = Use IPv6\n\
	-P = <precidence bits set - in hex - will be shifted left by 9>\n\
	-Q = <DSCP QoS bits set - in hex >\n\
	-S = <size of send and receive socket buffers in bytes>\n\
	-T = <tos bits set - in hex - will be shifted left by 1>\n\
	-U = <source udp port no - default not bound>\n\
	-V = print version number\n\
	-a = <cpu_mask set bitwise cpu_no 3 2 1 0 in hex>\n\
	-d = <the destination IP name or IP address a.b.c.d>\n\
	-e = <end value of wait time in us>\n\
	-g = <gap time to wait between bursts in us>\n\
	-h = print this message\n\
	-i = <increment for wait time in us>\n\
	-l = <no. of frames to send>\n\
	-n = <no. of bursts to send in Burst Mode>\n\
	-p = <length in bytes of mock data packet>\n\
	-r = send data rate Mbit/s\n\
	-t = <no. of seconds to run the test - calculates no. of frames to send >\n\
	-q = quiet - only print results\n\
	-v = turn on debug printout\n\
	-u = <destination udp port no - default 0x3799 ie 14233 decimal>\n\
	-w = <wait time tt.t in us>\n\
	-x = print more info (CPUStats) "};

    low_lim = 0;
    bin_width = 1;
    run_time_sec = 0;
    error=0;
    
#ifdef IPv6
    while ((c = getopt(argc, argv, "a:d:e:g:i:l:n:p:r:t:u:w:P:Q:S:T:U:hqvx6V")) != (char) EOF) {
#else
      while ((c = getopt(argc, argv, "a:d:e:g:i:l:n:p:r:t:u:w:P:Q:S:T:U:hqvxV")) != (char) EOF) {
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
		    wait_time_max = atoi(optarg);
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
		    increment_len = atoi(optarg);
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

	    case '6':
	        use_IPv6 = 1;
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
    if (error ) {
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
        printf("Done  %d loops out of %d. \n", 
	       loops_done, loop_count );
	fflush(stdout);

    return;
}
 
static void cntlz_handler( int signo)
/* --------------------------------------------------------------------- */
{
/* called on cntl_Z */
	fflush(stdout);
        printf("Done  %d loops out of %d. \n", 
	       loops_done, loop_count );
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

