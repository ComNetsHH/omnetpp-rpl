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
    uint8_t dodagVersion;
    uint16_t rank;
    int prefixLength;
    IInterfaceTable *interfaceTable;
    IRoutingTable *routingTable;
    InterfaceEntry *interfaceEntryPtr;
    INetfilter *networkProtocol;
    cPacket *packetInProcessing;
    std::string objectiveFunctionType;
    ObjectiveFunction *objectiveFunctionPtr;
    TrickleTimer *trickleTimer;
    std::vector<Dio *> neighborSet;
    Ipv6Address *preferredParent;
    Ipv6Address *backupParent;
    Ipv6Address *dodagId;
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
    void updateRoutingTable(bool updatePreferred);

    // lifecycle
    virtual void handleStartOperation(LifecycleOperation *operation) override { start(); }
    virtual void handleStopOperation(LifecycleOperation *operation) override { stop(); }
    virtual void handleCrashOperation(LifecycleOperation *operation) override  { stop(); }
    void start();
    void stop();

    // utility
    RplPacketCode getIcmpv6CodeByClassname(const Ptr<RplPacket>& packet);
    bool equalRoutes(IRoute *r1, IRoute *r2);
    Ipv6Address getSelfAddress();
    void addRouteIfNotPresent(IRoute* route, IRoutingTable* rt);

};

} // namespace inet

#endif

