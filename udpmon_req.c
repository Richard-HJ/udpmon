/*
     udpmon_req.c     R. Hughes-Jones  The University of Manchester

     Aim is to measure/monitor round trip latency over a link
     Ethernet request-respose response client: 
     Use UDP socket to:
           send fixed length message to server requesting n bytes
	   then wait for the response.
     Measure the time for n send and receives.

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

/*
     Modification: 
        rich  29 Jun 07 Used patch from Jack Eidsness from AboveNet Communications in McLean Virginia [jeidsness@above.net] :
				Add -h print help option
				Change sprintf() to snprintf() to avoid any potential buffer overflow security problems
				Assign EXIT_SUCCESS and EXIT_FAILURE to exit() calls

      Version 3.2-5      
        rich  30 Dec 03 Put byte-swap in cmd/resp
			Put "local" if there is no translaction for the local host name
      Version 3.2-3
         rich  12 Sep 03 use of arch.h to define IA32 IA64 int32 and int64

     Version 3.2-2
         rich  30 Jul 03 udp buffers 128k bytes long (was 16kbytes)
         rich  25 Jul 03 Make req_len min value of 64 bytes
                         Modifed struct param{} - for IA-64 arch. replace long by int; 
                         make frame_num 64 bit; re-order to allow 64 byte requests
                         
     Version 3.1D-3                           
         rich  11 Feb 03 Correct inital values for get_hist & get_info =0
                         Put the wait BETWEEN latency tests
     Version 3.1D-2                           
         rich  16 Jan 03 Use create_udp_socket() function with struct for the parameters to set
     Version 3.1D-1                          
         rich  26 Dec 02 Add -w wait between making req-resp measurements
                         Extend create_udp_socket() function to use struct for the parameters to set - put in lib
     Version 3.1D                           
         rich  22 Oct 02 Add diferve code point setting of tos field (-Q)
         rich  29 Aug 02 Add -V version number
     Version 3.0                           
         rich  14 Jul 02 Allow remote node to be given as IP name not just number
                         CPU and interrupt info for SMP- and uni- processor(s)
         rich  19 Jun 02 Add protocol version number
		         Fill data send with random values
                         Put struct params{} in net_test.h
                         Add create_socket() - function for open socket and set parameters
         rich  30 May 02 Remove IP fragmentation of responses eg hist data
	                      use cmd->data_offset & cmd->resp_len to do this
			      applies to GETHISTx [-H] GETINFO [-G]
			 Correct calc of data_rate (Thanks Hans)
         rich  14 Jan 02 Add CPU and interrupt info
         rich  26 Nov 01 Add destination IP address to error messages, use perror()
         rich  29 Oct 01 Printout Unix Time as sec since 1 Jan 1970
	                 All error messages have Error in them - easy for Perl to reject the line in a log.
         rich  14-16 Oct01 Timeout if remote host unavailable
	                 Separate out buffers for return stats / hist / info
	                 Print out stats for jitter histogram
         rich  Jul01     Allow setting of socket prarmeters - size / TOS ..
	                 Add -q quiet mode for DataGrid tests

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
    int *int_data_ptr;                           /* int pointer to data */
      
#define ERROR_MSG_SIZE 256
    char error_msg[ERROR_MSG_SIZE];                         /* buffer for error messages */

/* for command line options */
    extern char *optarg;

/* parameters */
    int dest_udp_port;
    char dest_ip_address[HOSTNAME_MAXLEN];
    int req_len = 64;                		/* length of request packet */
    int response_len = 64;           	        /* length of response message */
    int response_len_max = 0;       	        /* max length of response message used for looping over response len */
    int increment_len = 8;           	        /* size to increment response message */ 
    int get_hist =0;            	        /* set to 1 for histograms */
    int wait_time_int=0;                        /* wait time for input from user */
    int soc_buf_size =65535;                    /* send & recv buffer size bytes */
    int precidence_bits=0;                      /* precidence bits for the TOS field of the IP header IPTOS_TOS_MASK = 0x1E */
    int tos_bits=0;                             /* tos bits for the TOS field of the IP header IPTOS_PREC_MASK = 0xE0 */
    int tos_set = 0;                            /* flag =1 if precidence or tos bits set */
    int dscp_bits=0;                            /* difserv code point bits for the TOS field of the IP header */
    int dscp_set = 0;                           /* flag =1 if dscp bits set - alternative to precidence or tos bits set */
    int quiet = 0;                              /* set =1 for just printout of results - monitor mode */
    int loop_count = 1;              	        /* no. times to loop over the message loop */
    int verbose =0;                  		/* set to 1 for printout (-v) */
    int use_IPv6 =0;                            /* set to 1 to use the IPv6 address family */
    long cpu_affinity_mask;                     /* cpu affinity mask set bitwise cpu_no 3 2 1 0 in hex */

/* statistics */
    struct HIST hist[10];
    int num_sent=0;                             /* total no. of packets sent */
    int num_timeout=0;                          /* total no. of recv timeouts */
    int loops_done=0;

#define LOCK_SLEEP_TIME    30

/* forward declarations */
static void parse_command_line (int argc, char **argv);
static void sig_alrm(int signo);
static void cntlc_handler(int signo);
static void cntlz_handler(int signo);

int send_cmd(int soc, struct sockaddr *soc_address, socklen_t soc_address_len,  unsigned char *udp_data, int req_len, 
	     unsigned char *udp_data_recv, int recvbuf_size, int *num_timeout, char *name);
int time_sync(int soc, struct sockaddr_in *soc_address, socklen_t soc_address_len, int pkt_len, StopWatch *tsync_sw,
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
    struct sockaddr *soc_address;
    struct sockaddr *soc_recv_address;
    unsigned int flags = 0;          	        /* flags for sendto() */
    int recvbuf_size = UDP_DATA_MAX;
    unsigned int recv_flags = 0;          	/* flags for recvfrom() */
    int soc_recv_len;
    SOC_INFO soc_info;

    char* remote_hostname;
    int ipfamily_addr_len;                      /* length of soc_recv_address - depends on IP family IPv4, IPv6*/
    struct addrinfo hint_options, *result;      /* for getaddrinfo() */

    struct param *params;

/* timing */
    struct timeval before;           	        /* time before measurements */
    struct timeval after;            	        /* time after measurements */
    long delay;                      		/* time for one message loop */
    double time_per_frame;
    double time;                                /* time for one message loop - StopWatch */
    long hist_time;                             /* integer value of time for hist input */
    double wait_time=0;                         /* time to wait between request-response */


/* timers */
    StopWatch latency_sw;                       /* to measure total time to send data */
    StopWatch wait_sw;                          /* time to wait between sending request-response */

/* for select on socket */
    fd_set file_set;                            /* list of file descriptors for select() */
    struct timeval time_out;                    /* used to set time out for select() recvfrom() */

/* statistics package */
    Statistics latency;

/* local variables */
    int error;
    int soc;                         		/* handle for socket */
    int64 frame_num;                        /*  frame number */
    int i;
    int j;
    int ret;
    int cmd;
    int protocol_version;

    char port_str[128];


/* Set the default IP Ethernet */
/* IP protocol number ICMP=1 IGMP=2 TCP=6 UDP=17 */
    dest_udp_port = 0x3799;           		/* The default RAW/UDP port number (14233 base10) */
    soc =0;
    frame_num =0;

/* set the signal handler for SIGALRM */
    signal (SIGALRM, sig_alrm);
/* define signal handler for cntl_c */
    signal(SIGINT, cntlc_handler);
/* define signal handler for cntl_z */
    signal(SIGTSTP, cntlz_handler);

/* book histograms */ 
/*      (struct HIST *hist, long id, long low_lim, long bin_width, long n_bins, title )  */ 
    h_book( &hist[0], 0, 0, 1, 150, "Time per req-resp us");

/* get the input parameters */
    parse_command_line ( argc, argv);

/* set the CPU affinity of this process*/
    set_cpu_affinity (cpu_affinity_mask, quiet);

/* initalise and calibrate the time measurement system */
    ret = RealTime_Initialise(quiet);
    if (ret) exit(EXIT_FAILURE);
    ret = StopWatch_Initialise(quiet);
    if (ret) exit(EXIT_FAILURE);

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

/* convert remote IP address to text */
        remote_hostname = sock_ntop (soc_address );

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

    if(!quiet){
        printf(" The destination IP name: %s IP address: %s\n", dest_ip_address, remote_hostname);
	printf(" The destination UDP port is   %d %x\n", dest_udp_port, dest_udp_port);
/* print titles  */
        printf("response len bytes; Req len;  loop_count;  num_bad; time/frame (tod); ");
	printf("ave time us; min time; max time; num timeouts; \n");
   }

/* set local variables */
        wait_time = (double) wait_time_int;
	//	h_clear( &hist[0] ); 

/* Write the request to the remote host to clear stats */
        params = (struct param *)&udp_data;
	params->cmd = i4swap(CMD_ZEROSTATS);
        params->protocol_version = 0; /* this should be PROTOCOL_VERSION - 
                                         but old Software just reflects the packet contents - so always true */
	params->frame_num = i8swap(frame_num);
	params->low_lim = 0;
	params->bin_width = i4swap(1);
	params->resp_len = 0;     /* length in bytes */
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
/* can we talk to this remote end ? */      
        if(protocol_version != PROTOCOL_VERSION){
            snprintf(error_msg, ERROR_MSG_SIZE,
		    "Error: wrong protocol version %d version %d required", 
		    protocol_version, PROTOCOL_VERSION );
	    perror(error_msg );
	    exit(EXIT_FAILURE);
	}
	if(params->cmd == CMD_INUSE) {
	    sleep(LOCK_SLEEP_TIME);
	    goto LOOP_START;
	}


/* loop over response message length to make a scan */
    do{

LOOP_START:
/* clear the stats */
	Statistics_Init(&latency, "Latency");
        frame_num = 0;
        num_timeout = 0;

/* Write the request to the remote host. */
        params = (struct param *)&udp_data;
	params->cmd = i4swap(CMD_REQ_RESP);
        params->protocol_version = i4swap(PROTOCOL_VERSION);
	params->frame_num = i8swap(frame_num);
	params->resp_len = i4swap(response_len);
   
/* loop over requests */
	gettimeofday(&before, NULL);

	for (loops_done = 0; loops_done < loop_count; loops_done++) {
START_REQUEST:
	    StopWatch_Start(&latency_sw);

/* send the request */
	    params = (struct param *)&udp_data;
	    params->frame_num = i8swap(frame_num);
	    /* note min length of 64 bytes for req_len */
	    error = sendto(soc, &udp_data, req_len , flags, soc_address, ipfamily_addr_len);
	    if(error != req_len) {
	        snprintf(error_msg, ERROR_MSG_SIZE,
                       "Error: on data send to %s: mock data frame sent %d bytes not %d ", 
                       dest_ip_address, error, req_len );
	        perror(error_msg );
	    }
	    num_sent++;
            frame_num++;

/* receive the response  */
	    /* set up params for select() to check if there is data to read from the socket */
	    FD_ZERO (&file_set);
	    FD_SET (soc, &file_set);
	    time_out.tv_sec = 1;                        /* set to 1 sec */
	    time_out.tv_usec = 0;
	    soc_recv_len = sizeof(soc_recv_address);
	    ret = select (soc+1, &file_set, NULL, NULL, &time_out); 
	    if(ret >0) {
	      /* we have data in the socket */
		alarm(0);    /* cancel */
		error = recvfrom(soc, &udp_data_recv, recvbuf_size, recv_flags, (struct sockaddr*)&soc_recv_address, (socklen_t*)&soc_recv_len );
		params = (struct param *)&udp_data_recv;
		if(verbose) {
                    cmd = i4swap(params->cmd);
		    frame_num = i8swap(params->frame_num);
		    printf("Packet: \n");
		    for(j=0; j<64; j++){
		      printf(" %x", udp_data_recv[j]);
		    }
		    printf("Command : %d Frame num %"LONG_FORMAT"d\n", cmd, frame_num);
		    printf(" \n");
		}
	
		if(error != response_len) {
	        snprintf(error_msg, ERROR_MSG_SIZE, 
                       "Error: full frame received from %s: with %d bytes not %d ", 
                       dest_ip_address, error, response_len );
	        perror(error_msg );
		}
	    } 
	    else if(ret == 0) {
	      /* timeout */
                   num_timeout++;
		   if(verbose) {
		       printf(" num_timeout %d\n", num_timeout);
		   }
                   goto START_REQUEST;
	    }
	    else {
	           perror("Error: from select() for socket read :" );
	    } /* end of select() test */

/* record time for this req-response */
	    StopWatch_Stop(&latency_sw);
	    time = StopWatch_TimeDiff(&latency_sw);
	    Statistics_Add(&latency, time);
/* histogram */
	    hist_time = (int64) (time);
	    h_fill1( &hist[0], hist_time);

/* set the stopwatch going for waiting between sending packets */
	    StopWatch_Start(&wait_sw);
/* wait the required time */
	    StopWatch_Delay(&wait_sw, wait_time);

	}    /* end of request loop */

/* calculate the time per packet for this loop in us */
	gettimeofday(&after, NULL);
	delay = ((after.tv_sec - before.tv_sec) * 1000000) + (after.tv_usec - before.tv_usec);
	time_per_frame =  (double)delay  / (double)loop_count;

        printf(" %d;  %d;  %d;   %d;  ", response_len, req_len, loop_count, num_timeout);
	printf("%g;  ", time_per_frame );
	printf("%7.3f;  %6.2f;  %7.3f; ",
		  Statistics_Mean(&latency),Statistics_Min(&latency),Statistics_Max(&latency) );
	printf("%d \n", num_timeout);
	fflush(stdout);
	if(get_hist){
	    h_output( &hist[0] ); 
	    h_prt_stats( &hist[0] ); 
	    fflush(stdout);
	    h_clear( &hist[0] ); 
	}

/* increment packet size */
	response_len = response_len + increment_len;

    } while(response_len <= response_len_max);


/* send packet to say test complete */
    params = (struct param *)&udp_data;
    params->cmd = i4swap(CMD_TESTEND);
    params->protocol_version = i4swap(PROTOCOL_VERSION);
    ret = send_cmd(soc, soc_address, ipfamily_addr_len, udp_data, sizeof(struct param), 
		   udp_data_recv, recvbuf_size, &num_timeout, "testend"); 
	/* check for no response / error */
    if(ret < 0) exit(EXIT_FAILURE);

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
    char *help ={
"Usage: udpmon_req -option<parameter> [...]\n\
options:\n\
         -6 = Use IPv6\n\
	 -H = Print histograms\n\
	 -P = <precidence bits set - in hex - will be shifted left by 9> \n\
	 -Q = <DSCP QoS bits set - in hex >\n\
	 -S = <size of send and receive socket buffers in bytes>\n\
	 -T = <tos bits set - in hex - will be shifted left by 1>\n\
         -V = print version number\n\
	 -a = <cpu_mask set bitwise cpu_no 3 2 1 0 in hex>\n\
	 -b = <bin width of histo in us>\n\
	 -d = <the destination IP name or IP address a.b.c.d>\n\
	 -e = <end value of message length in bytes>\n\
	 -i = <increment for message length in bytes>\n\
         -l = <no. of frames to send>\n\
	 -m = <min (low limit) of histo in us>\n\
	 -p = <length in bytes of mock data packet>\n\
	 -q = quiet - only print results\n\
         -r = <start size of response message in bytes>\n\
         -v = turn on debug printout\n\
	 -u = <raw/udp port no - default 0x3799 ie 14233 decimal>\n\
         -w = <wait time in us> "};

    error=0;

/* default values */
    response_len_max = 0;
    increment_len = 0; 
    loop_count = 10;
    req_len = 64;
    quiet = 0;
    response_len = 1472;
    verbose = 0;
    wait_time_int = 0;
    get_hist = 0;
    precidence_bits = 0;
    tos_set = 0;
    dscp_bits = 0;
    dscp_set = 0;
    tos_bits = 0;

#ifdef IPv6
    while ((c = getopt(argc, argv, "a:b:d:e:i:l:m:p:r:u:w:P:Q:S:T:qv6HV")) != (char) EOF) {
#else
    while ((c = getopt(argc, argv, "a:b:d:e:i:l:m:p:r:u:w:P:Q:S:T:qvHV")) != (char) EOF) {
# endif	
	switch(c) {

	    case 'a':
		if (optarg != NULL) {
		    sscanf(optarg, "%lx", &cpu_affinity_mask);
		} else {
		    error = 1;
		}
		break;

	    case 'b':
		if (optarg != NULL) {
		   hist[0].bin_width = (long)atoi(optarg);
		} else {
		    error = 1;
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

	    case 'e':
		if (optarg != NULL) {
		    response_len_max = atoi(optarg);
		} else {
		    error = 1;
		}
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

	    case 'm':
		if (optarg != NULL) {
		   hist[0].low_lim = (long)atoi(optarg);
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
		    req_len = atoi(optarg);
		    if(req_len < 64) req_len =64;
		} else {
		    error = 1;
		}
		break;

	    case 'q':
	        quiet = 1;
		break;

	    case 'r':
		if (optarg != NULL) {
		    response_len = atoi(optarg);
		} else {
		    error = 1;
		}
		break;

	    case 'v':
	        verbose = 1;
		break;

	    case 'w':
		if (optarg != NULL) {
		    sscanf(optarg, "%d", &wait_time_int);
		} else {
		    error = 1;
		}
		break;

	    case '6':
	        use_IPv6 = 1;
		break;

	    case 'H':
	        get_hist = 1;
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
    if ((req_len == 0) || (loop_count == 0)) {
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
        printf(" %s ", UDPMON_VERSION);
        printf(" Command line: ");
	for(i=0; i<argc; i++){
            printf(" %s", argv[i]);
	}
	printf(" \n");
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
/* Just interrupt the recvfrom() */

        printf("SIGALRM caught\n");

    return;
}
