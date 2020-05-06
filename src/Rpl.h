#ifndef _RPL_H
#define _RPL_H

#include <map>
#include <vector>

#include "inet/networklayer/contract/IL3AddressType.h"
#include "inet/networklayer/contract/INetfilter.h"
#include "inet/networklayer/contract/IRoutingTable.h"
#include "inet/networklayer/contract/ipv6/Ipv6Address.h"
#include "inet/routing/base/RoutingProtocolBase.h"
#include "inet/transportlayer/udp/UdpHeader_m.h"
#include "Rpl_m.h"
#include "RplDefs.h"


namespace inet {

class Rpl : public RoutingProtocolBase
{
  private:
    cModule *host;
    const char *interfaces;
    IL3AddressType *addressType;
    IInterfaceTable *interfaceTable;
    IRoutingTable *routingTable;
    INetfilter *networkProtocol;
    bool isRoot;

  public:
    Rpl();
    ~Rpl();

  protected:
    // module interface
    virtual int numInitStages() const override { return NUM_INIT_STAGES; }
    void initialize(int stage) override;
    void handleMessageWhenUp(cMessage *message) override;

  private:
    // handling messages
    void processSelfMessage(cMessage *message);
    void processMessage(cMessage *message);

    // handling Udp packets
    void sendUdpPacket(cPacket *packet, double delay);
    void processUdpPacket(Packet *packet);

    // lifecycle
    virtual void handleStartOperation(LifecycleOperation *operation) override { start(); }
    virtual void handleStopOperation(LifecycleOperation *operation) override { stop(); }
    virtual void handleCrashOperation(LifecycleOperation *operation) override  { stop(); }
    void start();
    void stop();

    // configuration
    void configureInterfaces();

//     address
    Ipv6Address getSelfAddress();

    // RPL messages
    void sendRplPacket(const Ptr<RplPacket>& packet, const InterfaceEntry *interfaceEntry,
            const L3Address& nextHop, double delay);
    const Ptr<DioMsg> createDio();
//    bool isClientAddress(const L3Address& address);
};

} // namespace inet

#endif

