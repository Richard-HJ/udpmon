# DTNLon-Par 10GE
#DEST=62.40.123.226 
#FILEPREFIX=DTNLon-Par_10G_HWtstamp_go2
#DEST=10.10.10.2
#FILEPREFIX=DTNLon-Par_P4_L2_HWtstamp_ICoff    
#DEST=fd00:bad:c0de:2::2
DEST=fd00:bad:c0de:2:7efe:90ff:fe9e:89d0
FILEPREFIX=DTNLon-Par_P4_Ams-Fra-81ms_HWtstamp_IPv6
# DTNLon-Par 100GE
#DEST=62.40.123.218
#FILEPREFIX=DTNLon-Par_100G_HWtstamp_ICoff
# remus
#DEST=62.40.120.27
#FILEPREFIX=DTNLon-remus_100G
# dtn4 CT
#DEST=155.232.76.2    
#FILEPREFIX=DTNLon-dtn4CT
# dtn4 CT via GEANT Open & ZAOX-I
#DEST=155.232.212.98
#FILEPREFIX=DTNLon-dtn4CT_OEx

./cmd_jitter_HWtstamp_v6.pl    -o ${FILEPREFIX} -d ${DEST} -a 0x40 -l 10000
./cmd_1waydelay_HWtstamp_v6.pl -o ${FILEPREFIX} -d ${DEST} -a 0x40 -l 10000
# for 10G
./cmd_throughput_lite_x_v6.pl -o ${FILEPREFIX} -d ${DEST} -a 0x40 -l 10000
# for 100G
#./cmd_throughput_lite.pl -o ${FILEPREFIX} -d ${DEST} -a 0x40 -l 100000
