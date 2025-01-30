#!/usr/bin/perl
# log system information
#input 
#   -o= prefix for output log files [sys_info]
#
#output
#
#     file logging all the system information

use strict;
use Getopt::Std;
#  ....................................................................
#

my $out;
my $cmd;
my $file_date_time;
my $log_file;
my $host_name;
my $loop_value;
my @dir_entries;

# kernel values from DTNpar

my $kernelValuesSource = "DTNpar" ;
my $rmem_default = 212992 ;
my $rmem_max = 2146435072 ;
my $wmem_default = 212992 ;
my $wmem_max = 2146435072 ;
my $netdev_max_backlog = 250000 ;
my $optmem_max = 20480 ;
my $tcp_rmem = "4096    87380   2146435072" ;
my $tcp_wmem = "4096    65536   2146435072" ;
my $tcp_mem = "6171744 8228992 12343488" ;
my $tcp_available_congestion_control = "cubic reno" ;
my $tcp_congestion_control = "cubic" ;
my $tcp_timestamps = 1 ;
my $tcp_sack = 1 ;
my $tcp_mtu_probing = 1 ;
my $tcp_tw_recycle = "No such file" ;
my $tcp_tw_reuse = 0 ;
my $tcp_no_metrics_save = 1 ;
my $tcp_window_scaling = 1 ;

# ....................................................................

#--- deal with command line options
# o = prefix for logfile output files [sys_info]

our ($opt_o);
getopt('o');
if(!defined($opt_o)) {$opt_o="sys_info";}

#get date-time
$file_date_time = date_time();

#get hostname
$cmd = "hostname";
$host_name =`$cmd 2>&1`;
chomp $host_name;

#--- open the log file
$log_file = $opt_o."_".$host_name."_".$file_date_time."_log.txt";
open(LOG, "> $log_file");

$cmd = "date";
print "cmd=".$cmd."\n";
$out =`$cmd 2>&1`;
print LOG $out."\n";

print LOG "host ".$host_name."\n";

# the system
$cmd = "uname -a";
print "cmd=".$cmd."\n";
$out =`$cmd 2>&1`;
print LOG $out."\n";

$cmd = "more /etc/redhat-release";
exec_log($cmd);

# number of CPU sockets (nodes)
$cmd = "ls /sys/devices/system/node/";
exec_log($cmd);

# number of CPU cores
$cmd = "cat /proc/cpuinfo |grep  processor";
exec_log($cmd);

# power and performance
$cmd = "cat /sys/devices/system/cpu/cpu*/cpufreq/cpuinfo_max_freq";
exec_log($cmd);

$cmd = "cat /proc/cpuinfo |grep  'cpu MHz'";
exec_log($cmd);

$cmd = "cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_available_governors";
exec_log($cmd);

$cmd = "cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor";
exec_log($cmd);


# network config 
$cmd = "more /proc/sys/net/core/rmem_default";
exec_log_print_value($cmd, $kernelValuesSource, $rmem_default);

$cmd = "more /proc/sys/net/core/rmem_max";
exec_log_print_value($cmd, $kernelValuesSource, $rmem_max);
#exec_log_print($cmd);

$cmd = "more /proc/sys/net/core/wmem_default";
exec_log_print_value($cmd, $kernelValuesSource, $wmem_default);

$cmd = "more /proc/sys/net/core/wmem_max";
exec_log_print_value($cmd, $kernelValuesSource, $wmem_max);

$cmd = "more /proc/sys/net/core/netdev_max_backlog";
exec_log_print_value($cmd, $kernelValuesSource, $netdev_max_backlog);

$cmd = "more /proc/sys/net/core/optmem_max";
exec_log_print_value($cmd, $kernelValuesSource, $optmem_max);

# tcp read buffer min / default / max
$cmd = "more /proc/sys/net/ipv4/tcp_rmem";
exec_log_print_value($cmd, $kernelValuesSource, $tcp_rmem);

# tcp write buffer min / default / max
$cmd = "more /proc/sys/net/ipv4/tcp_wmem";
exec_log_print_value($cmd, $kernelValuesSource, $tcp_wmem);

# tcp buffer space min / pressure / max
$cmd = "more /proc/sys/net/ipv4/tcp_mem";
exec_log_print_value($cmd, $kernelValuesSource, $tcp_mem);

$cmd = "more /proc/sys/net/ipv4/tcp_available_congestion_control";
exec_log_print_value($cmd, $kernelValuesSource, $tcp_available_congestion_control);

$cmd = "more /proc/sys/net/ipv4/tcp_congestion_control";
exec_log_print_value($cmd, $kernelValuesSource, $tcp_congestion_control);

$cmd = "more /proc/sys/net/ipv4/tcp_timestamps";
exec_log_print_value($cmd, $kernelValuesSource, $tcp_timestamps);

$cmd = "more /proc/sys/net/ipv4/tcp_sack";
exec_log_print_value($cmd, $kernelValuesSource, $tcp_sack);

$cmd = "more /proc/sys/net/ipv4/tcp_mtu_probing";
exec_log_print_value($cmd, $kernelValuesSource, $tcp_mtu_probing);

$cmd = "more /proc/sys/net/ipv4/tcp_tw_recycle";
exec_log_print_value($cmd, $kernelValuesSource, $tcp_tw_recycle);

$cmd = "more /proc/sys/net/ipv4/tcp_tw_reuse";
exec_log_print_value($cmd, $kernelValuesSource, $tcp_tw_reuse);

$cmd = "more /proc/sys/net/ipv4/tcp_no_metrics_save";
exec_log_print_value($cmd, $kernelValuesSource, $tcp_no_metrics_save);

$cmd = "more /proc/sys/net/ipv4/tcp_window_scaling";
exec_log_print_value($cmd, $kernelValuesSource, $tcp_window_scaling);

# The network interfaces on this system
$cmd = "/sbin/ifconfig";
exec_log($cmd);

# get a list of the network interfaces on this system
$cmd = "ls /sys/class/net";
$out =`$cmd 2>&1`;
@dir_entries =`$cmd 2>&1`;


foreach $loop_value (@dir_entries){
    chomp $loop_value;
    print LOG "\n\nNetwork interface ".$loop_value."\n";
    
    # which cores in use by this interface
    $cmd = "cat /sys/class/net/".$loop_value."/device/local_cpulist";
    exec_log_print($cmd);

    #IRQs
    my $irq;
    my @irq_list;
    # look for this interface in /interrupts, take the first token, remove the :
    $cmd = "cat /proc/interrupts |grep ".$loop_value." |awk '{print \$1}' | sed 's/://' ";
    $out =`$cmd 2>&1`;
    @irq_list =`$cmd 2>&1`;

    print LOG "IRQ: Affinity for $loop_value \n";
    foreach $irq (@irq_list){
        chomp $irq;
        $cmd = "cat /proc/irq/$irq/smp_affinity ";
        $out =`$cmd 2>&1`;
        chomp $out;
        print LOG $irq.": ".$out."\n";
    }
    print LOG "\n";
    
    #ethtool information
    $cmd = "ethtool ".$loop_value;
    $out =`$cmd 2>&1`;
    print  LOG $out."\n";
    # coalescence
    $cmd = "ethtool -c ".$loop_value;
    $out =`$cmd 2>&1`;
    print  LOG $out."\n";
    # offload features
    $cmd = "ethtool -k ".$loop_value;
    $out =`$cmd 2>&1`;
    print  LOG $out."\n";
    # ring buffer
    $cmd = "ethtool -g ".$loop_value;
    $out =`$cmd 2>&1`;
    print  LOG $out;

    # tc qdisc pacing
    $cmd = "tc qdisc show dev ".$loop_value;
    exec_log_print($cmd);

    print     "\n --------------------------------------------- \n";
    print LOG "\n --------------------------------------------- \n";    
}

# detailed info
$cmd = "cat /proc/cpuinfo";
exec_log($cmd);

$cmd = "cat /proc/interrupts";
exec_log($cmd);

$cmd = "cat /proc/meminfo";
exec_log($cmd);

$cmd = "df -T";
exec_log($cmd);

#the PCI bus
#/sbin/lspci -vvxs 02:01.0
$cmd = "/sbin/lspci -vt";
exec_log($cmd);

# all sysctl
$cmd = "/sbin/sysctl -a";
exec_log($cmd);

#$cmd = "dmesg";
#exec_log($cmd);

#--- close files
close LOG;

sub exec_log_print_value {
# -------------------------------------------------------------------------------
    my ($cmd) = @_[0];  # get the first parameter
	my ($source) = @_[1];
	my ($value) = @_[2];
    print "cmd=".$cmd."\n";
    print LOG "cmd=".$cmd."\n";
    $out =`$cmd 2>&1`;
	chomp $out;
    print $out."\n";
	print $source." : ".$value."\n\n";
    print LOG $out."\n";
	print LOG $source." : ".$value."\n\n";
}

sub exec_log_print {
# -------------------------------------------------------------------------------
    my ($cmd) = @_;  # get the first parameter
    print "cmd=".$cmd."\n";
    print LOG "cmd=".$cmd."\n";
    $out =`$cmd 2>&1`;
    print $out."\n";
    print LOG $out."\n";
}

sub exec_log {
    # -------------------------------------------------------------------------------
    my ($cmd) = @_;  # get the first parameter
    print "cmd=".$cmd."\n";
    print LOG "cmd=".$cmd."\n";
    $out =`$cmd 2>&1`;
    print LOG $out."\n";
}


sub date_time(){
# -------------------------------------------------------------------------------
# Get the all the values for current time
my @months = ('Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec');
my $Second;
my $Minute;
my $Hour;
my $Day;
my $Month;
my $Year;
my $WeekDay;
my $DayOfYear;
my $IsDST;
my $ans;

($Second, $Minute, $Hour, $Day, $Month, $Year, $WeekDay, $DayOfYear, $IsDST) = 
    localtime(time);
# Perl code will need to be adjusted for one-digit months
if($Day < 10) {
    $Day = "0".$Day; # add a leading zero to one-digit months              
}

$Month = $Month +1; # They count from 0
if($Month < 10) {
    $Month = "0".$Month; # add a leading zero to one-digit months
}
$Month =$months[$Month-1];
#$Year = $Year +1900;
$Year = $Year -100; # get as 05 etc.
if($Year < 10) {
    $Year = "0".$Year;
}
if($Hour < 10) {
    $Hour = "0".$Hour; # add a leading zero to one-digit numbers
}
if($Minute < 10) {
    $Minute = "0".$Minute; # add a leading zero to one-digit numbers 
}

#
$ans = $Day.$Month.$Year."_".$Hour.$Minute;

}

