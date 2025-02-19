# DTNLon-Par 10GE
#DEST=62.40.123.226 
# DTNLon-Par 100GE
#DEST=62.40.123.218
#FILEPREFIX=DTNlon-DTNpar_100G
# remus
#DEST=62.40.120.27
#FILEPREFIX=DTNLon-remus_100G
# dtn4 CT
#DEST=155.232.76.2    
#FILEPREFIX=DTNLon-dtn4CT
# dtn4 CT via GEANT Open & ZAOX-I
#DEST=155.232.212.98
#FILEPREFIX=DTNLon-dtn4CT_OEx
#ECMWF
#DEST=136.156.82.4
#FILEPREFIX=DTNLon-ECMWF_cpufreqON_go2
#CamDTN1
#DEST=192.84.5.1
#FILEPREFIX=DTNLon-CamDTN1

#CoralES
#DEST=161.111.167.189
#FILEPREFIX=DTNLon-CoralES

#CoralSWE
DEST=129.16.125.165
FILEPREFIX=DTNLon-CoralSWE_1500
   
./cmd_jitter.pl    -o ${FILEPREFIX} -d ${DEST} -a 0x40 -l 10000
./cmd_1waydelay.pl -o ${FILEPREFIX} -d ${DEST} -a 0x40 -l 10000
# for 10G
#./cmd_throughput_lite_x.pl -o ${FILEPREFIX} -d ${DEST} -a 0x40 -l 10000
# for 100G
./cmd_throughput_lite_x.pl -o ${FILEPREFIX} -d ${DEST} -a 0x40 -l 1000000
