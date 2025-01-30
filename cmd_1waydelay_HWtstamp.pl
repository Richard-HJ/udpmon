#!/usr/bin/perl
# cmd_1waydelay_HWtstamp.pl open date-time file, 
#                     use udpmon_bw_mon to measure 1-way delay as function of packet spacing
#input 
#   -a= CPU affinity
#   -d= remote host 
#   -l= no packets
#   -o= prefix for output log files [terminal]
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

# for 1Gigabit tests
my @pkt_wait      = (0,     5,     10,    15,    20,    40,    80,    100,   200 );
my @file_postfix  = ("51",  "52",  "53",  "54",  "55",  "56",  "57",  "58",  "59");
my $npkt = 10000;
my $opt_p = 1472;

# for 10Gigabit
#my @pkt_wait     = (15.4,  15.5,  15.6,  15.7,  15.8,  16.0,  16.1,  16.2,  16.3 );
#my @file_postfix  = ("51",  "52",  "53",  "54",  "55",  "56",  "57",  "58",  "59");
#my $npkt = 100000;
#my $opt_p = 8192;
#
#  ....................................................................
# 

# locate udpmon_bw_mon. assumed to be in homedir/bin OR installed by the rpm in /usr/bin
my $home_dir = $ENV{HOME};
my $udpmon_prog = "udpmon_bw_mon";
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

# will be of the form $PROG -x  -d$DEST  -p1400 -l${NPKT}  >${FILE}_10.txt
my $cmd_udpmon =" -x ";

my $USAGE = "Usage:\t cmd_1waydelay.pl [opts]
        Opts:
            [-a CPU affinity]
            [-d remote host]
            [-l number of packets]
            [-o logfile_prefix]

Example ./cmd_1waydelay.pl -d 140.221.220.41 -o PC1-PC2 -l 1000
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
if(defined($opt_a)) {$cmd_udpmon = $cmd_udpmon." -a ".$opt_a;}

#get date
my $file_date = date_4file();

# find where to place the output files
my $log_file_dir = cwd();

#---choose packet size
for($pkt_index=0; $pkt_index<=$pkt_index_max; $pkt_index++){
    my $wait = $pkt_wait[$pkt_index];
    my $file_num = $file_postfix[$pkt_index];

    #--- open the terminal OR the log file
    $log_file = $log_file_dir."/".$opt_o."_".$file_date."_".$file_num.".txt";
    if(!defined($opt_o)) {
	open( LOG,">-");
    }
    else {
	open (LOG,"> $log_file");
    }
    
    # make the udpmon command
    $cmd = $udpmon_path.$cmd_udpmon." -d ".$opt_d." -p ".$opt_p." -w ".$wait." -l ".$opt_l." -G ".$opt_l;   
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

