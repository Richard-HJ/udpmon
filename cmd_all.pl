DEST=1.2.3.4
FILEPREFIX=hosta-b

./cmd_jitter.pl     -o ${FILEPREFIX} -d ${DEST} -l 10000
./cmd_1waydelay.pl -o ${FILEPREFIX} -d ${DEST} -l 10000
./cmd_throughput.pl -o ${FILEPREFIX} -d ${DEST} -l 100000
