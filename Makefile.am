bin_PROGRAMS = udpmon_bw_mon udpmon_req udpmon_resp udpmon_send udpmon_recv 

ARCH=@ARCH@
HJ_LOCATION=@HJ_LOCATION@

udpmon_bw_mon_CPPFLAGS = -D$(ARCH) -Wall

udpmon_req_CPPFLAGS = -D$(ARCH) -Wall

udpmon_resp_CPPFLAGS = -D$(ARCH) -Wall

udpmon_send_CPPFLAGS = -D$(ARCH) -Wall

udpmon_recv_CPPFLAGS = -D$(ARCH) -Wall


INCLUDES = -I$(HJ_LOCATION)/include/libhj

udpmon_bw_mon_SOURCES = udpmon_bw_mon.c net_test.h

udpmon_req_SOURCES  = udpmon_req.c net_test.h

udpmon_resp_SOURCES = udpmon_resp.c net_test.h

udpmon_send_SOURCES  = udpmon_send.c net_test.h 

udpmon_recv_SOURCES  = udpmon_recv.c net_test.h 

EXTRA_DIST = udpmon.spec.in udpmon.spec bootstrap README get_sys_info.pl cmd_1waydelay.pl cmd_jitter.pl cmd_latency.pl cmd_throughput.pl cmd_all.pl cmd_throughput_lite.pl cmd_all_lite.pl 
