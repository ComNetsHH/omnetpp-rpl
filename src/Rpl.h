#ifndef _RPL_H
#define _RPL_H

#include <map>
#include <vector>

#include "inet/networklayer/contract/IL3AddressType.h"
#include "inet/networklayer/contract/INetfilter.h"
#include "inet/networklayer/contract/IRoutingTable.h"
#include "inet/networklayer/contract/ipv6/Ipv6Address.h"
#include "inet/networklayer/ipv6/Ipv6InterfaceData.h"
#include "inet/routing/base/RoutingProtocolBase.h"
#include "inet/transportlayer/udp/UdpHeader_m.h"
#include "Rpl_m.h"
#include "RplDefs.h"


namespace inet {

class Rpl : public RoutingProtocolBase
{
  private:
    cModule *host;
    uint8_t dodagVersion;
    uint16_t rank;
    const char *interfaces;
    IInterfaceTable *interfaceTable;
    IRoutingTable *routingTable;
    InterfaceEntry *interfaceEntryPtr;
    INetfilter *networkProtocol;
    cPacket *packetInProcessing;
    cMessage *trickleTimerEvent;
    double trickleTimerDelay;
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

    // handling generic packets
    void sendPacket(cPacket *packet, double delay);
    void processPacket(Packet *packet);

    // handling RPL packets
    void processRplPacket(Packet *packet, const Ptr<const RplPacket>& rplPacket, RplPacketCode code);
    void processDio(Packet *packet, const Ptr<const Dio>& dioPacket);
    void processDao(Packet *packet, const Ptr<const Dao>& daoPacket);
    void processDis(Packet *packet, const Ptr<const Dis>& disPacket);
    void sendRplPacket(const Ptr<RplPacket>& packet, const InterfaceEntry *interfaceEntry,
                const L3Address& nextHop, double delay);
    const Ptr<Dio> createDio();

    // handling routing data
    void addParent(const Ipv6Address& srcAddr, bool preferred);

    // lifecycle
    virtual void handleStartOperation(LifecycleOperation *operation) override { start(); }
    virtual void handleStopOperation(LifecycleOperation *operation) override { stop(); }
    virtual void handleCrashOperation(LifecycleOperation *operation) override  { stop(); }
    void start();
    void stop();

    // configuration
    void configureInterfaces();

    // utility
    RplPacketCode getIcmpv6CodeByClassname(const Ptr<RplPacket>& packet);
    Ipv6Address getSelfAddress();

};

} // namespace inet

#endif

