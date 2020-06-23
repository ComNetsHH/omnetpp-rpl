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
#include "inet/networklayer/ipv6/Ipv6Route.h"
#include "inet/routing/base/RoutingProtocolBase.h"
#include "inet/transportlayer/udp/UdpHeader_m.h"
#include "Rpl_m.h"
#include "RplDefs.h"
#include "TrickleTimer.h"
#include "ObjectiveFunction.h"


namespace inet {

class Rpl : public RoutingProtocolBase, public cListener
{
  private:
    IInterfaceTable *interfaceTable;
    IRoutingTable *routingTable;
    InterfaceEntry *interfaceEntryPtr;
    INetfilter *networkProtocol;
    ObjectiveFunction *objectiveFunction;
    TrickleTimer *trickleTimer;
    cModule *host;
    uint8_t dodagVersion;
    Ipv6Address dodagId;
    uint8_t instanceId;
    double pingDelay;
    double pingTimeoutDelay;
    double daoDelay;
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
    void processDio(Packet *packet, const Ptr<const Dio>& dio);
    void processDao(Packet *packet, const Ptr<const Dao>& dao);
    void processDaoAck(Packet *packet, const Ptr<const Dao>& daoAck);
    void processDis(Packet *packet, const Ptr<const Dis>& dis);
    void sendRplPacket(const Ptr<RplPacket>& packet, RplPacketCode code, const L3Address& nextHop, double delay);
    void sendRplPacket(const Ptr<RplPacket>& packet, RplPacketCode code, const L3Address& nextHop) {
        sendRplPacket(packet, code, nextHop, 0); }
    const Ptr<Dio> createDio();
    const Ptr<Dao> createDao(const Ipv6Address &reachableDest);

    // handling routing data
    void updateRoutingTable(const Ipv6Address &nextHop, const Ipv6Address &destination);
    void addNeighbour(const Ptr<const Dio>& dio);
    // TODO: Replace manual pinging with reachability check via NeighbourDiscovery module
    void pingPreferredParent(double delay, const Ipv6Address &parentAddr);
    void deletePrefParentRoute();
    void updatePreferredParent();

    // lifecycle
    virtual void handleStartOperation(LifecycleOperation *operation) override { start(); }
    virtual void handleStopOperation(LifecycleOperation *operation) override { stop(); }
    virtual void handleCrashOperation(LifecycleOperation *operation) override  { stop(); }
    void start();
    void stop();

    // utility
    bool checkUnknownDio(const Ptr<const Dio>& dio);
    Ipv6Address getSelfAddress();
    bool checkPrefParentChanged(Dio* updatedPrefParent);
    bool checkDestAlreadyKnown(const Ipv6Address &dest);
    void leaveDodag();

    // notification
    virtual void receiveSignal(cComponent *source, simsignal_t signalID, cObject *obj, cObject *details) override;

};

} // namespace inet

#endif

