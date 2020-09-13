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
#include "inet/common/packet/chunk/Chunk.h"
#include "inet/common/packet/dissector/PacketDissector.h"
#include "inet/common/packet/dissector/ProtocolDissector.h"
#include "inet/common/packet/dissector/ProtocolDissectorRegistry.h"
#include "Rpl_m.h"
#include "RplDefs.h"
#include "RplRouteData.h"
#include "TrickleTimer.h"
#include "ObjectiveFunction.h"

namespace inet {

class Rpl : public RoutingProtocolBase, public cListener, public NetfilterBase::HookBase
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

    /** RPL configuration parameters and state management */
    uint8_t dodagVersion;
    Ipv6Address dodagId;
    uint8_t instanceId;
    double daoDelay;
    bool isRoot;
    bool daoEnabled;
    bool storing;
    uint16_t rank;
    uint8_t dtsn;
    Dio *preferredParent;
    std::string objectiveFunctionType;
    std::map<Ipv6Address, Dio *> backupParents;
    std::map<Ipv6Address, Dio *> candidateParents;
    std::map<Ipv6Address, Ipv6Address> sourceRoutingTable;

    /** Statistics collection */
    simsignal_t dioReceivedSignal;
    simsignal_t daoReceivedSignal;
    simsignal_t parentChangedSignal;
    simsignal_t parentUnreachableSignal;

    /** Misc */
    bool floating;
    bool verbose;
    uint8_t detachedTimeout; // temporary variable to suppress msg processing after just leaving the DODAG
    cMessage *detachedTimeoutEvent; // temporary msg corresponding to triggering above functionality
    uint8_t prefixLength;


  public:
    Rpl();
    ~Rpl();

    static std::string boolStr(bool cond, std::string positive, std::string negative);
    static std::string boolStr(bool cond) { return boolStr(cond, "true", "false"); }

  protected:
    /** module interface */
    virtual int numInitStages() const override { return NUM_INIT_STAGES; }
    void initialize(int stage) override;
    void handleMessageWhenUp(cMessage *message) override;

  private:
    void processSelfMessage(cMessage *message);
    void processMessage(cMessage *message);

    /************ Handling generic packets *************/

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

    /************ Handling RPL packets *************/

    /**
     * Process DIO packet by inspecting it's source @see checkUnknownDio(),
     * joining a DODAG if not yet a part of one, (re)starting trickle timer,
     * and updating neighbor sets correspondingly @see updatePreferredParent(), addNeighbour()
     *
     * @param dio DIO packet object for processing
     */
    void processDio(const Ptr<const Dio>& dio);

    /**
     * Process DAO packet advertising node's reachability,
     * update routing table if destination advertised was unknown and
     * forward DAO further upwards until root is reached to enable P2P and P2MP communication
     *
     * @param dao DAO packet object for processing
     */
    void processDao(const Ptr<const Dao>& dao);

    /**
     * Process DAO_ACK packet if daoAckRequried flag is set
     *
     * @param daoAck decapsulated DAO_ACK packet for processing
     */
    void processDaoAck(const Ptr<const Dao>& daoAck);
    void processDis(const Ptr<const Dis>& dis);

    /**
     * Send RPL packet (@see createDao(), createDio(), createDis()) via 'ipOut'
     *
     * @param packet packet object encapsulating control and payload data
     * @param code icmpv6-based control code used for RPL packets, [RFC 6550, 6]
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
    const Ptr<Dao> createDao() {return createDao(getSelfAddress()); };

    /**
     * Update routing table with new route to destination reachable via next hop
     *
     * @param nextHop next hop address to reach the destination for findBestMatchingRoute()
     * @param dest discovered destination address being added to the routing table
     */
    void updateRoutingTable(const Ipv6Address &nextHop, const Ipv6Address &dest, RplRouteData *routeData);
    void updateRoutingTable(Dao *dao);
    void updateRoutingTable(const Ipv6Address &nextHop);
    void purgeDaoRoutes();

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
     * Update preferred parent based on the current best candidate
     * determined by the objective function from candidate neighbors
     * If change in preferred parent detected, restart Trickle timer [RFC 6550, 8.3]
     */
    void updatePreferredParent();

    /************ Lifecycle ****************/

    virtual void handleStartOperation(LifecycleOperation *operation) override { start(); }
    virtual void handleStopOperation(LifecycleOperation *operation) override { stop(); }
    virtual void handleCrashOperation(LifecycleOperation *operation) override  { stop(); }
    void start();
    void stop();

    /************ Utility *****************/

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
     * @return Ipv6 address set by autoconfigurator
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
    bool checkPrefParentChanged(const Ipv6Address &newPrefParentAddr);

    template<typename Map>
    std::string printMap(const Map& map);

    bool selfGeneratedPkt(Packet *pkt);

    /**
     * Check if destination advertised in DAO is already stored in
     * the routing table with the same nextHop address
     *
     * @param dest address of the advertised destination from DAO
     * @param nextHop address of the next hop to this destination
     * @return
     */
    bool checkDestKnown(const Ipv6Address &nextHop, const Ipv6Address &dest);

    /**
     * Detach from a DODAG in case preferred parent has been detected unreachable
     * and candidate parent set is empty. Clear RPL-related state including:
     *  - rank
     *  - dodagId
     *  - RPL instance
     *  - neighbor sets
     * [RFC 6550, 8.2.2.1]
     */
    void detachFromDodag();

    /**
     * Poison node's sub-dodag by advertising rank of INF_RANK in case
     * connection to the DODAG is lost. Child nodes upon hearing this message
     * remove poisoning node from their parent set [RFC 6550, 8.2.2.5]
     */
    void poisonSubDodag();

    /**
     * Check if INF_RANK is advertised in DIO and whether it comes from preferred parent
     */
    bool checkPoisonedParent(const Ptr<const Dio>& dio);

    /**
     * @deprecated
     * Check if IPv6 address suffixes match given the prefix length
     *
     * @param addr1 first address for comparison
     * @param addr2 second address for comparison
     * @param prefixLength IPv6 prefix length, representing network + subnet id
     * @return true on matching address' prefixes, false otherwise
     */
    bool matchesSuffix(const Ipv6Address &addr1, const Ipv6Address &addr2, int prefixLength);
    bool matchesSuffix(const Ipv6Address &addr1, const Ipv6Address &addr2) {
        return matchesSuffix(addr1, addr2, prefixLength);
    };

    /**
     * Print all packet tags' classnames
     *
     * @param packet packet to print tags from
     */
    void printTags(Packet *packet);

    /**
     * Print detailed info about RPL Packet Information header
     *
     * @param rpi RPL Packet Information object
     * @return string containing packet info
     */
    std::string printHeader(RplPacketInfo *rpi);

    /** Loop detection */

    /**
     * Check if a rank inconsistency happens during packet forwarding
     * for detailed procedure description [RFC 6550, 11.2.2.2]
     *
     * @param rpi RPL Route Infomration header to check for
     * @return true if ther's mismatched rank relationship, false otherwise
     */
    bool checkRankError(RplPacketInfo *rpi);

    /**
     * Check RPL Packet Information header to spot a forwarding error in storing mode
     * (packet expected to progress down, but no route found) [RFC 6550, 11.2.2.3]
     *
     * @param rpi RPL Route Infomration header to check for
     * @param dest destination address retrieved from packet IP header
     * @return true if forwarding error detected, false otherwise
     */
    bool checkForwardingError(RplPacketInfo *rpi, Ipv6Address &dest);

    /**
     * Get default length of Target/Transit option headers
     * @return predefined default byte value
     */
    B getTransitOptionsLength();

    /**
     * Get default length of Target/Transit option headers
     *
     * @return predefined default byte value
     */
    B getRpiHeaderLength();

    /**
     * Used by sink to collect Transit -> Target reachability information
     * for source-routing purposes [RFC6550, 9.7]
     *
     * @param dao
     */
    void extractSourceRoutingData(Packet *dao);

    /**
     * Determine packet forwarding direction - 'up' or 'down'
     *
     * @param datagram UDP packet to check for
     * @return true if packet travels down, false otherwise
     */
    bool packetTravelsDown(Packet *datagram);

    /**
     * Append RPL Packet Information header to outgoing packet,
     * captured by Netfilter hook
     *
     * @param datagram outgoing UDP packet
     */
    void appendRplPacketInfo(Packet *datagram);

    /**
     * Check RPL Packet Information header on rank consistency,
     * forwarding errors [RFC 6550, 11.2]
     *
     * @param datagram packet coming from upper or lower layers catched by Netfilter hook
     * @return INetfilter interface result specifying further actions with a packet
     */
    Result checkRplHeaders(Packet *datagram);

    /**
     * Append Target and Transit option headers representing
     * child->parent relationship, required for source-routing
     *
     * @param pkt DAO packet to insert option headers [RFC6550, 6.7.7-6.7.8]
     */
    void appendDaoTransitOptions(Packet *pkt);

    /**
     * Check RPL Packet Information header for loop detection purposes [RFC6550, 11.2]
     *
     * @param datagram application data to check RPL headers for
     * @return
     */
    bool checkRplRouteInfo(Packet *datagram);
    bool isDao(Packet *datagram);
    bool isUdp(Packet *datagram);

    /**
     * Check if packet has source-routing header (SRH) present
     * @param pkt packet to check for
     * @return true on SRH presence, false otherwise
     */
    bool sourceRouted(Packet *pkt);
    B getDaoFrontOffset();
    std::string rplIcmpCodeToStr(RplPacketCode code);

    /**
     * Manually forward application packet to next hop using the
     * info provided in SRH
     *
     * @param datagram
     */
    void forwardSourceRoutedPacket(Packet *datagram);

    /**
     * At sink append SRH for packets going downwards
     * @param datagram
     */
    void appendSrcRoutingHeader(Packet *datagram);

    /** Netfilter hooks */
    // catching incoming packet
    virtual Result datagramPreRoutingHook(Packet *datagram) override { Enter_Method("datagramPreRoutingHook"); return checkRplHeaders(datagram); }
    virtual Result datagramForwardHook(Packet *datagram) override { return ACCEPT; }
    virtual Result datagramPostRoutingHook(Packet *datagram) override { return ACCEPT; }
    virtual Result datagramLocalInHook(Packet *datagram) override { return ACCEPT; }
    // catching outgoing packet
    virtual Result datagramLocalOutHook(Packet *datagram) override { Enter_Method("datagramLocalOutHook"); return checkRplHeaders(datagram); }

    /** Source-routing methods */
    Ipv6Address* constructSrcRoutingHeader(const Ipv6Address& dest);
    B getDaoLength();

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

