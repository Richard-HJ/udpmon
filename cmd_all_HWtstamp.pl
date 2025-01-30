#DTNLon-Par 10GE
#DEST=62.40.123.226 
#FILEPREFIX=DTNLon-Par_10G_HWtstamp
DEST=10.10.10.2
FILEPREFIX=DTNLon-Par_P4_Ams-Fra-81ms_HWtstamp_ICon    
#FILEPREFIX=DTNLon-Par_P4_Ams-Fra_HWtstamp_ICon
#DEST=fd00:bad:c0de:2::2 
#FILEPREFIX=DTNLon-Par_P4_L2_HWtstamp_IPv6
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

./cmd_jitter_HWtstamp.pl    -o ${FILEPREFIX} -d ${DEST} -a 0x40 -l 10000
./cmd_1waydelay_HWtstamp.pl -o ${FILEPREFIX} -d ${DEST} -a 0x40 -l 10000
# for 10G
./cmd_throughput_lite_x.pl -o ${FILEPREFIX} -d ${DEST} -a 0x40 -l 10000
# for 10G full scan
#./cmd_throughput.pl -o ${FILEPREFIX} -d ${DEST} -a 0x40 -l 10000
# for 100G
#./cmd_throughput_lite.pl -o ${FILEPREFIX} -d ${DEST} -a 0x40 -l 100000
