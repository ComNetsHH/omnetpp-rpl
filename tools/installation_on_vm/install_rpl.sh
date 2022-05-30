cd omnetpp-5.6.2/samples
git clone git@collaborating.tuhh.de:e-4/Research/omnetpp-rpl.git
cd omnetpp-rpl
source replace_inet_files.sh ../inet4
mv features.h ../inet4/src/inet/
cd ../inet4
make makefiles
make MODE=release
cd ../omnetpp-rpl/src
touch makefrag
echo 'MSGC:=$(MSGC) --msg6' > makefrag
opp_makemake -f --deep --make-so -DINET_IMPORT -I. -I../../inet4/src  -L../../inet4/src  -lINET$\(D\) --mode release
make MODE=release
cd ../../../..
