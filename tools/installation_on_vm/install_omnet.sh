wget https://github.com/omnetpp/omnetpp/releases/download/omnetpp-5.6.2/omnetpp-5.6.2-src-core.tgz
tar xzfv omnetpp-5.6.2-src-core.tgz
cd omnetpp-5.6.2
source setenv
./configure WITH_OSG=no WITH_OSGEARTH=no WITH_QTENV=no WITH_TKENV=no
make MODE=release
cd ../
