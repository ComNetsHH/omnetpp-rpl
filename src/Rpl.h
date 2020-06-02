/*
 * Simulation model for RPL (Routing Protocol for Low-Power and Lossy Networks)
 *
 * Copyright (C) 2020  Institute of Communication Networks (ComNets),
 *                     Hamburg University of Technology (TUHH)
 *           (C) 2020  Yevhenii Shudrenko
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef _RPL_H
#define _RPL_H

#include <map>
#include <vector>

#include "inet/networklayer/contract/IL3AddressType.h"
#include "inet/networklayer/contract/INetfilter.h"
#include "inet/networklayer/contract/IRoutingTable.h"
#include "inet/networklayer/contract/ipv6/Ipv6Address.h"
#include "inet/networklayer/ipv6/Ipv6InterfaceData.h"
#include "inet/networklayer/ipv6/Ipv6.h"
#include "inet/networklayer/icmpv6/Icmpv6.h"
#include "inet/networklayer/icmpv6/Ipv6NeighbourDiscovery.h"
#include "inet/networklayer/icmpv6/Ipv6NeighbourCache.h"
#include "inet/networklayer/ipv6/Ipv6Route.h"
#include "inet/routing/base/RoutingProtocolBase.h"
#include "inet/transportlayer/udp/UdpHeader_m.h"
#include "Rpl_m.h"
#include "RplDefs.h"
#include "TrickleTimer.h"
#include "ObjectiveFunction.h"


namespace inet {

class Rpl : public RoutingProtocolBase
{
  private:
    IInterfaceTable *interfaceTable;
    IRoutingTable *routingTable;
    InterfaceEntry *interfaceEntryPtr;
    INetfilter *networkProtocol;
    Ipv6NeighbourDiscovery *neighbourDiscovery;
    ObjectiveFunction *objectiveFunction;
    TrickleTimer *trickleTimer;
    uint8_t dodagVersion;
    Ipv6Address dodagId;
    uint8_t instanceId;
    int pingTimeoutDelay;
    Mop mop;
    bool isRoot;
    uint16_t rank;
    int prefixLength;
    Dio *preferredParent;
    Dio *backupParent;
    Packet *packetInProcessing;
    cMessage *pingTimeoutMsg;
    std::string objectiveFunctionType;
    std::map<Ipv6Address, Dio *> backupParents;
    std::map<Ipv6Address, Dio *> candidateParents;

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
    void processTrickleTimerMsg(cMessage *message);

    // handling RPL packets
    void processRplPacket(Packet *packet, const Ptr<const RplPacket>& rplPacket, RplPacketCode code);
    void processDio(Packet *packet, const Ptr<const Dio>& dioPacket);
    void processDao(Packet *packet, const Ptr<const Dao>& daoPacket);
    void processDis(Packet *packet, const Ptr<const Dis>& disPacket);
    void sendRplPacket(const Ptr<RplPacket>& packet, const L3Address& nextHop, double delay);
    const Ptr<Dio> createDio();

    // handling routing data
    void updateRoutingTable(Dio* newPrefParent);
    void addNodeAsNeighbour(const Ptr<const Dio>& dio);
    bool checkNodeReachable(Dio* node);
    void pingPreferredParent();
    Packet* createPingReq(const Ipv6Address& destAddress, RplPacketCode code);
    bool deletePrefParentRoute();
    void updatePreferredParent();

    // lifecycle
    virtual void handleStartOperation(LifecycleOperation *operation) override { start(); }
    virtual void handleStopOperation(LifecycleOperation *operation) override { stop(); }
    virtual void handleCrashOperation(LifecycleOperation *operation) override  { stop(); }
    void start();
    void stop();

    // utility
    RplPacketCode getIcmpv6CodeByClassname(const Ptr<RplPacket>& packet);
    bool checkUnknownDio(const Ptr<const Dio>& dio);
    Ipv6Address getSelfAddress();
    void tryGetInterfaceId(Packet *pkt);
    bool checkPrefParentChanged(Dio* updatedPrefParent);

};

} // namespace inet

#endif

