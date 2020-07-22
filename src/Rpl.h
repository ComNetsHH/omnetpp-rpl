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

#include "inet/common/INETDefs.h"
#include "inet/networklayer/contract/IL3AddressType.h"
#include "inet/networklayer/contract/INetfilter.h"
#include "inet/networklayer/contract/IRoutingTable.h"
#include "inet/networklayer/contract/ipv6/Ipv6Address.h"
#include "inet/networklayer/ipv6/Ipv6InterfaceData.h"
#include "inet/networklayer/ipv6/Ipv6.h"
#include "inet/networklayer/icmpv6/Icmpv6.h"
#include "inet/networklayer/ipv6/Ipv6Route.h"
#include "inet/networklayer/ipv6/Ipv6RoutingTable.h"
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

    /** Environment */
    IInterfaceTable *interfaceTable;
    Ipv6RoutingTable *routingTable;
    InterfaceEntry *interfaceEntryPtr;
    INetfilter *networkProtocol;
    ObjectiveFunction *objectiveFunction;
    TrickleTimer *trickleTimer;
    cModule *host;

    /** RPL configuration, some parameters are adjustable, see NED file */
    uint8_t dodagVersion;
    Ipv6Address dodagId;
    uint8_t instanceId;
    double daoDelay;
    double defaultPrefParentLifetime;
    bool isRoot;
    bool daoEnabled;
    bool storing;
    uint16_t rank;
    uint8_t dtsn;
    Dio *preferredParent;
    Dio *backupParent;
    std::string objectiveFunctionType;
    std::map<Ipv6Address, Dio *> backupParents;
    std::map<Ipv6Address, Dio *> candidateParents;

    /** Statistics collection */
    simsignal_t dioReceivedSignal;
    simsignal_t daoReceivedSignal;
    simsignal_t parentChangedSignal;
    simsignal_t parentUnreachableSignal;

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

    /* Handling generic packets */

    /**
     * Send packet via 'ipOut' gate with specified delay
     *
     * @param packet packet object to be sent out
     * @param delay transmission delay
     */
    void sendPacket(cPacket *packet, double delay);
    void processPacket(Packet *packet);

    /**
     * Process message from trickle timer to start DIO broadcast
     *
     * @param message notification message with kind assigned from enum
     */
    void processTrickleTimerMsg(cMessage *message);

    /* Handling RPL packets */

    /**
     * Process DIO packet by inspecting it's source @see checkUnknownDio(),
     * joining a DODAG if not yet a part of one, (re)starting trickle timer,
     * and updating neighbor sets correspondingly @see updatePreferredParent(), addNeighbour()
     *
     * @param packet encapsulated packet object with different protocol headers
     * @param dio decapsulated DIO packet for processing
     */
    void processDio(Packet *packet, const Ptr<const Dio>& dio);

    /**
     * Process DAO packet advertising node's reachability,
     * update routing table if destination advertised was unknown and
     * forward DAO further upwards until root is reached to enable P2P and P2MP communication
     *
     * @param packet encapsulated packet object with different protocol headers
     * @param dao decapsulated DAO packet for processing
     */
    void processDao(Packet *packet, const Ptr<const Dao>& dao);

    /**
     * Process DAO_ACK packet if daoAckRequried flag is set
     *
     * @param packet packet encapsulated packet object with different protocol headers
     * @param daoAck decapsulated DAO_ACK packet for processing
     */
    void processDaoAck(Packet *packet, const Ptr<const Dao>& daoAck);
    void processDis(Packet *packet, const Ptr<const Dis>& dis);

    /**
     * Send RPL packet (@see createDao(), createDio(), createDis()) via 'ipOut'
     *
     * @param packet packet object encapsulating control and payload data
     * @param code icmpv6-based control code used for RPL packets, see Section 6
     * @param nextHop next hop for the RPL packet to be sent out to (unicast DAO, broadcast DIO, DIS)
     * @param delay transmission delay before sending packet from outgoing gate
     */
    void sendRplPacket(const Ptr<RplPacket>& packet, RplPacketCode code, const L3Address& nextHop, double delay);
    void sendRplPacket(const Ptr<RplPacket>& packet, RplPacketCode code, const L3Address& nextHop) {
        sendRplPacket(packet, code, nextHop, 0); }

    /**
     * Create DIO packet to broadcast DODAG info and configuration parameters
     *
     * @return initialized DIO packet object
     */
    const Ptr<Dio> createDio();

    /**
     * Create DAO packet advertising destination reachability
     *
     * @param reachableDest reachable destination, may be own address or forwarded from sub-dodag
     * @return initialized DAO packet object
     */
    const Ptr<Dao> createDao(const Ipv6Address &reachableDest);

    // handling routing data and node's neighborhood sets
    void updateRoutingTable(const Ipv6Address &nextHop, const Ipv6Address &dest) { updateRoutingTable(nextHop, dest, false); }

    /**
     * Update routing table with new route to destination reachable via next hop
     *
     * @param nextHop next hop address to reach the destination for findBestMatchingRoute()
     * @param dest discovered destination address being added to the routing table
     * @param isDefaultRoute if next hop represents preferred parent, set this route as default
     */
    void updateRoutingTable(const Ipv6Address &nextHop, const Ipv6Address &dest, bool isDefaultRoute);

    /**
     * Add node (represented by most recent DIO packet) to one of the neighboring sets:
     *  - backup parents
     *  - candidate parents
     *
     * @param dio DIO packet received recently
     */
    void addNeighbour(const Ptr<const Dio>& dio);

    /**
     * Delete preferred parent and related info:
     *  - route with it as a next-hop from the routing table
     *  - erase corresponding entry from the candidate parent list
     */
    void deletePrefParent() { deletePrefParent(false); };
    void deletePrefParent(bool poisoned);

    /**
     * Update preferred parent on each DIO reception by invoking
     * objective function recalculation for candidate neighbors set.
     * If necessary (@see checkPrefParentChanged()), update routing table @see updateRoutingTable()
     * and reset trickle timer, see Section 8.3
     */
    void updatePreferredParent() { updatePreferredParent(false); };

    void updatePreferredParent(bool useBackups);

    /* Lifecycle */
    virtual void handleStartOperation(LifecycleOperation *operation) override { start(); }
    virtual void handleStopOperation(LifecycleOperation *operation) override { stop(); }
    virtual void handleCrashOperation(LifecycleOperation *operation) override  { stop(); }
    void start();
    void stop();

    /* Utility */
    /**
     * Check whether DIO comes from an uknown DODAG or has different
     * RPL instance id
     *
     * @param dio recently received DIO packet
     * @return true if DIO sender stems from an unknown DODAG
     */
    bool checkUnknownDio(const Ptr<const Dio>& dio);

    /**
     * Get address of the default network interface
     *
     * @return ipv6 address set by autoconfigurator
     */
    Ipv6Address getSelfAddress();

    /**
     * Check if node's preferred parent has changed after recalculating via
     * objective function to determine whether routing table updates are necessary
     * as well display corresponding log information
     *
     * @param newPrefParent DIO packet received from node chosen as preferred
     * parent by objective function
     * @return True if new preferred parent address differs from the one currently stored,
     * false otherwise
     */
    bool checkPrefParentChanged(Dio* newPrefParent);
    void checkDupEntry(std::map<Ipv6Address, Dio *> &parentSet, const Ipv6Address &key);

    /**
     * Check if destination advertised in DAO is already stored in the routing table
     * and shouldn't be forwarded upwards through the DODAG
     *
     * @param dest address of the advertised destination from DAO
     * @return
     */
    bool checkDestAlreadyKnown(const Ipv6Address &dest);

    /**
     * Detach from DODAG in case preferred parent has been detected unreachable
     * and candidate parent set is empty. Suppress (see Section 8.2.2.1) all former DODAG info:
     *  - rank
     *  - dodagId
     *  - backup parents
     *  - sub-dodag @see poisonSubDodag()
     */
    void detachFromDodag();

    /**
     * Poison node's sub-dodag by advertising rank of INF_RANK to enable them
     * joining a new DODAG (or updated DODAG version), see Section 8.2.2.5
     */
    void poisonSubDodag();
    /**
     * Check if INF_RANK is advertised in DIO and whether it comes from preferred parent
     */
    bool checkPoisonedParent(const Ptr<const Dio>& dio);

    /**
     * Handle signals by implementing @see cListener interface to
     * get notified when MAC layer reports link break
     *
     * @param source
     * @param signalID
     * @param obj
     * @param details
     */
    virtual void receiveSignal(cComponent *source, simsignal_t signalID, cObject *obj, cObject *details) override;

};

} // namespace inet

#endif

