#!/usr/bin/perl
# cmd_jitter_HWtstamp.pl open date-time file, 
#                     use udpmon_bw_mon to measure packet jitter as function of packet spacing & size
#input 
#   -a= CPU affinity
#   -o= prefix for output log files [terminal]
#   -d= remote host 
#   -l= no packets
#
#output
#    a set of .txt files
#
# you may wish to change the test paramerters given below between the .... lines
#

use strict;
use Getopt::Std;
use Sys::Hostname; my $host=hostname();
use Cwd;

#  ....................................................................
#
# 

my @pkt_wait         = (0,     5,     10,    15,    20,    20,    40,    40,    80,    80  );
my @file_postfix     = ("20",  "21",  "22",  "23",  "24",  "25",  "26",  "27",  "28",  "29");
my @file_postfix_100 = ("30",  "31",  "32",  "33",  "34",  "35",  "36",  "37",  "38",  "39");
# for P4 Lab
my @hist_min         = (0,     0,     0,     0,     0,     0,     0,     100,   200,   500 );
my @hist_bin         = (1,     1,     1,     2,     1,     2,     2,     4,     2,     4 );
# f0r 10G IP
#my @hist_min         = (0,     0,     0,     0,     0,     0,     100,   300,   260,   650 );
#my @hist_bin         = (1,     1,     1,     2,     1,     2,     1,     2,     1,     2 );
#my @hist_bin         = (2,     2,     2,     2,     2,     2,     2,     2,     2,     2 );

# for 100 GE tests
#my @pkt_wait         = (1,    2,    3,    5,    10,    20,    80);
#my @hist_bin         = (1,    1,    1,    1,     1,    1,     1,     2,     2,     2 );

my $npkt = 100000;

# commands will be of the form $PROG -d$DEST -p1472 -w20 -M0 -B2  -H -l100000  >${FILE}_20.txt
my $cmd_udpmon      =" -p1472 -H -x -N";
#my $cmd_udpmon_100  =" -p100  -H -x";
my $cmd_udpmon_100  =" -p8952  -H -x -N";
#
#  ....................................................................
# 

# locate udpmon_bw_mon. assumed to be in homedir/bin OR installed by the rpm in /usr/bin
my $home_dir = $ENV{HOME};
my $udpmon_prog = "udpmon_bw_mon -6 ";
my $udpmon_path;
my $rpm_install_path = "/usr/bin/".$udpmon_prog ;
if (-e $rpm_install_path) {$udpmon_path = $rpm_install_path}
else {$udpmon_path = $home_dir."/bin/".$udpmon_prog };

# for HW timestamp version
#$udpmon_path = $home_dir."/udpmon-1.5.1_expt/".$udpmon_prog ;

my $log_file;
my $out;
my $cmd;

my $pkt_index;
my $pkt_index_max = $#pkt_wait;
 
my $USAGE = "Usage:\t cmd_jitter.pl [opts]
        Opts:
            [-a CPU affinity]
            [-d remote host]
            [-l number of packets]
            [-o logfile_prefix]

Example ./cmd_jitter.pl -d 140.221.220.41 -o PC1-PC2 -l 1000
";

#die $USAGE if (!defined($ARGV[0]));

#--- deal with command line options
# a= CPU affinity
# d = remote host 
# l = no packets
# o = prefix for logfile output files
# 
getopt('adlo');
our ($opt_a, $opt_d, $opt_l, $opt_o );
if(!defined($opt_d)) {$opt_d="192.168.10.24";}
if(!defined($opt_l)) {$opt_l=$npkt;}
if(defined($opt_a)) {$cmd_udpmon = $cmd_udpmon." -a ".$opt_a;
		     $cmd_udpmon_100 = $cmd_udpmon_100." -a ".$opt_a;}

#get date
my $file_date = date_4file();

# find where to place the output files
my $log_file_dir = cwd();

#---choose packet size for 1472 bytes
for($pkt_index=0; $pkt_index<=$pkt_index_max; $pkt_index++){
    my $wait = $pkt_wait[$pkt_index];
    my $file_num = $file_postfix[$pkt_index];
    my $bin_min  = $hist_min[$pkt_index];
    my $bin      = $hist_bin[$pkt_index];

    #--- open the terminal OR the log file
    $log_file = $log_file_dir."/".$opt_o."_".$file_date."_".$file_num.".txt";
    if(!defined($opt_o)) {
	open( LOG,">-");
    }
    else {
	open (LOG,"> $log_file");
    }
    
    # make the udpmon command
    $cmd = $udpmon_path.$cmd_udpmon." -d ".$opt_d." -w ".$wait." -M ".$bin_min." -B ".$bin." -l ".$opt_l;   
#    print $cmd."\n";
    $out =`$cmd 2>&1`;
    chomp($out);  # remove terminating newline
    print LOG $out."\n";

    #--- close files
        close LOG;

} # end of packet size loop

#---choose packet size for 100 bytes
for($pkt_index=0; $pkt_index<=$pkt_index_max; $pkt_index++){
    my $wait = $pkt_wait[$pkt_index];
    my $file_num = $file_postfix_100[$pkt_index];
    my $bin_min  = $hist_min[$pkt_index];
    my $bin      = $hist_bin[$pkt_index];

    #--- open the terminal OR the log file
    $log_file = $log_file_dir."/".$opt_o."_".$file_date."_".$file_num.".txt";
    if(!defined($opt_o)) {
	open( LOG,">-");
    }
    else {
	open (LOG,"> $log_file");
    }
    
    # make the udpmon command
    $cmd = $udpmon_path.$cmd_udpmon_100." -d ".$opt_d." -w ".$wait." -M ".$bin_min." -B ".$bin." -l ".$opt_l;   
    $out =`$cmd 2>&1`;
    chomp($out);  # remove terminating newline
    print LOG $out."\n";

    #--- close files
        close LOG;

} # end of packet size loop


sub date_4file(){
# -------------------------------------------------------------------------------
# Get the all the values for current time
my @months = ('Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep',
	      'Oct', 'Nov', 'Dec');
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
    $Year = "0".$Year; # add a leading zero to one-digit numbers
}
if($Hour < 10) {
    $Hour = "0".$Hour; # add a leading zero to one-digit numbers
}
if($Minute < 10) {
    $Minute = "0".$Minute; # add a leading zero to one-digit numbers
}

#
$ans = $Day.$Month.$Year;

}

