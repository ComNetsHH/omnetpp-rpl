/*
 * Simulation model for RPL (Routing Protocol for Low-Power and Lossy Networks)
 *
 * Copyright (C) 2021  Institute of Communication Networks (ComNets),
 *                     Hamburg University of Technology (TUHH)
 *           (C) 2021  Yevhenii Shudrenko
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

#include "TrickleTimer.h"
#include "RplRouteData.h"
#include "inet/applications/udpapp/UdpBasicApp.h"
#include "inet/applications/udpapp/UdpSink.h"
#include "inet/common/packet/dissector/PacketDissector.h"
#include "inet/common/packet/dissector/ProtocolDissector.h"
#include "inet/common/packet/dissector/ProtocolDissectorRegistry.h"
#include "inet/common/ModuleAccess.h"
#include "inet/mobility/static/StationaryMobility.h"
#include "inet/linklayer/common/InterfaceTag_m.h"
#include "inet/networklayer/common/L3AddressTag_m.h"
#include "inet/networklayer/common/L3Tools.h"

using namespace std;

namespace inet {

class Rpl : public RoutingProtocolBase, public cListener, public NetfilterBase::HookBase
{
public:

    class DaoTimeoutInfo : public cObject {
        public:
            cMessage *timeoutPtr;
            int numRetries;

            DaoTimeoutInfo() {
                this->timeoutPtr = nullptr;
                this->numRetries = 0;
            }

            DaoTimeoutInfo(cMessage *timeoutPtr) {
                this->timeoutPtr = timeoutPtr;
                this->numRetries = 0;
            }

            friend std::ostream& operator<<(std::ostream& os, const DaoTimeoutInfo& timeoutInfo)
            {
                os << timeoutInfo.numRetries << " retries";
                return os;
            }



    //            int getNumRetries() const {
    //                return numRetries;
    //            }
    //
    //            void setNumRetries(int numRetries) {
    //                this->numRetries = numRetries;
    //            }
    //
    //            const cMessage*& getTimeoutPtr() const {
    //                return timeoutPtr;
    //            }
    //
    //            void setTimeoutPtr(const cMessage *&timeoutPtr) {
    //                this->timeoutPtr = timeoutPtr;
    //            }
    };

    class DodagInfo : public cObject {

        public:
            Ipv6Address dagId;
            Ipv6Address prefParent;
            uint16_t prefParentRank;
            uint8_t rplInstanceId;

            DodagInfo() {
                this->dagId = Ipv6Address::UNSPECIFIED_ADDRESS;
                this->prefParent = Ipv6Address::UNSPECIFIED_ADDRESS;
                this->prefParentRank = INF_RANK;
                this->rplInstanceId = 0xFF; // TODO: check/define the default value
            }

            DodagInfo(Ipv6Address dagId, Ipv6Address prefParent, int prefParentRank, int rplInstanceId)
            {
                this->dagId = dagId;
                this->prefParent = prefParent;
                this->prefParentRank = prefParentRank;
                this->rplInstanceId = rplInstanceId;
            }

            const Ipv6Address& getDagId() const {
                return dagId;
            }
            void setDagId(const Ipv6Address &dagId) {
                this->dagId = dagId;
            }

            const Ipv6Address& getPrefParent() const {
                return prefParent;
            }
            void setPrefParent(const Ipv6Address &prefParent) {
                this->prefParent = prefParent;
            }

            uint16_t getPrefParentRank() const {
                return prefParentRank;
            }
            void setPrefParentRank(uint16_t prefParentRank) {
                this->prefParentRank = prefParentRank;
            }

            uint8_t getRplInstanceId() const {
                return rplInstanceId;
            }
            void setRplInstanceId(uint8_t rplInstanceId) {
                this->rplInstanceId = rplInstanceId;
            }

            void update(Dio *dio) {
                this->dagId = dio->getDodagId();
                this->prefParent = dio->getSrcAddress();
                this->prefParentRank = dio->getRank();
                this->rplInstanceId = dio->getInstanceId();
            }
    };



  private:

    /** Environment */
    IInterfaceTable *interfaceTable;
    Ipv6RoutingTable *routingTable;
    Ipv6NeighbourDiscovery *nd;
    InterfaceEntry *interfaceEntryPtr;
    INetfilter *networkProtocol;
    ObjectiveFunction *objectiveFunction;
    TrickleTimer *trickleTimer;
    cModule *host;
    cModule *udpApp;
    cModule *mac;
    vector<cModule*> apps;

    /** RPL configuration parameters and state management */
    DodagInfo dagInfo; // for display/tracking purposes only, since WATCH_PTR on preferredParent DIO crashes
    uint8_t dodagVersion;
    Ipv6Address dodagId;
    Ipv6Address selfAddr;
    Ipv6Address *lastTarget;
    Ipv6Address *lastTransit;
    uint8_t instanceId;
    double daoDelay;
    double daoAckTimeout;
    double clKickoffTimeout; // timeout for auto-triggering phase II of CL SF
    uint8_t daoRtxCtn;
    uint8_t daoRtxThresh;
    bool isRoot;
    bool daoEnabled;
    bool storing;
    bool pDaoAckEnabled;
    bool hasStarted;
    bool allowDodagSwitching;
    bool pUnreachabilityDetectionEnabled;
    bool pShowBackupParents;
    bool pAllowDaoForwarding;
    bool pJoinAtSinkAllowed;
    uint16_t rank;
    uint8_t dtsn;
    uint32_t branchChOffset;
    uint16_t branchSize;
    int daoSeqNum;
    Dio *preferredParent;
    std::string objectiveFunctionType;
    std::map<Ipv6Address, Dio *> backupParents;
    std::map<Ipv6Address, Dio *> candidateParents;
    std::map<Ipv6Address, Ipv6Address> sourceRoutingTable;
//    std::map<Ipv6Address, std::pair<cMessage *, uint8_t>> pendingDaoAcks;

    std::map<Ipv6Address, DaoTimeoutInfo *> pendingDaoAcks;

    /** Statistics and control signals */
    simsignal_t dioReceivedSignal;
    simsignal_t daoReceivedSignal;
    simsignal_t parentChangedSignal;
    simsignal_t rankUpdatedSignal;
    simsignal_t rankUpdatedSignalSf;
    simsignal_t parentUnreachableSignal;
    simsignal_t childJoinedSignal;


    int numDaoDropped;

    /** Misc */
    bool floating;
    bool verbose;
    bool pUseWarmup;
    bool pLockParent;
    bool isLeaf;
    bool isMobile;
    uint8_t detachedTimeout; // temporary variable to suppress msg processing after just leaving the DODAG
    cMessage *detachedTimeoutEvent; // temporary msg corresponding to triggering above functionality
    cMessage *daoAckTimeoutEvent; // temporary msg corresponding to triggering above functionality
    uint8_t prefixLength;
    Coord position;
    uint64_t selfId;    // Primary IE MAC address in decimal
    std::vector<cFigure::Color> colorPalette;
    int udpPacketsRecv;
    cFigure::Color dodagColor;
    double currentFrequency; // stores current frequency reported by MAC

    // Low-latency params, only for use in 6TiSCH
    long uplinkSlotOffset; // smallest slot offset of the uplink cell
    simsignal_t uplinkSlotOffsetSignal; // emitted to notify SF about the slot offset learned from DIO

  public:
    Rpl();
    ~Rpl();

    friend std::ostream& operator<<(std::ostream& os, const std::map<Ipv6Address, Dio *> &parentSet)
    {
        os << "Address   Rank" << endl;
        for (auto const &entry : parentSet)
            os << entry.first << ": " << entry.second->getRank() << std::endl;
        return os;
    }

    /** Conveniently display boolean variable with custom true / false format */
    static std::string boolStr(bool cond, std::string positive, std::string negative);
    static std::string boolStr(bool cond) { return boolStr(cond, "true", "false"); }

    /** Search for a submodule of @param host by its name @param sname */
    cModule* findSubmodule(std::string sname, cModule *host);

    int numParentUpdates;
    int numDaoForwarded;

    virtual void finish() override;

  protected:
    /** module interface */
    void initialize(int stage) override;
    void handleMessageWhenUp(cMessage *message) override;
    virtual void refreshDisplay() const override;

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
    void processDio(const Ptr<const Dio>& dio, double rxPower);

    /**
     * Checks whether DIO is valid in terms of advertised rank
     *
     * @param dio pointer to the DIO object
     * @return true if the node should process the DIO, false otherwise
     */
    bool isInvalidDio(const Dio* dio);

    void processCrossLayerMsg(const Ptr<const Dio>& dio);

    /**
     * Process DAO packet advertising node's reachability,
     * update routing table if destination advertised was unknown and
     * forward DAO further upwards until root is reached to enable P2P and P2MP communication
     *
     * @param dao DAO packet object for processing
     */
    void processDao(const Ptr<const Dao>& dao);
//    void retransmitDao(Dao *dao);
    void retransmitDao(Ipv6Address advDest);

    /**
     * Process DAO_ACK packet if daoAckRequried flag is set
     *
     * @param daoAck decapsulated DAO_ACK packet for processing
     */
    void processDaoAck(const Ptr<const Dao>& daoAck);
    void saveDaoTransitOptions(Packet *dao);

    /**
     * Send RPL packet (@see createDao(), createDio(), createDis()) via 'ipOut'
     *
     * @param packet packet object encapsulating control and payload data
     * @param code icmpv6-based control code used for RPL packets, [RFC 6550, 6]
     * @param nextHop next hop for the RPL packet to be sent out to (unicast DAO, broadcast DIO, DIS)
     * @param delay transmission delay before sending packet from outgoing gate
     */
    void sendRplPacket(const Ptr<RplPacket>& body, RplPacketCode code, const L3Address& nextHop, double delay, const Ipv6Address &target, const Ipv6Address &transit, std::string interfaceName);
    void sendRplPacket(const Ptr<RplPacket>& body, RplPacketCode code, const L3Address& nextHop, double delay, const Ipv6Address &target, const Ipv6Address &transit)
    {
        sendRplPacket(body, code, nextHop, delay, target, transit, "wlan");
    }

    void sendRplPacket(const Ptr<RplPacket>& body, RplPacketCode code, const L3Address& nextHop, double delay);
    void sendRplPacket(const Ptr<RplPacket>& body, RplPacketCode code, const L3Address& nextHop, double delay, std::string interfaceName);


    /**
     * Create DIO packet to broadcast DODAG info and configuration parameters
     *
     * @return initialized DIO packet object
     */
    const Ptr<Dio> createDio();
    B getDioSize() { return b(128); }

    /**
     * Create DAO packet advertising destination reachability
     *
     * @param reachableDest reachable destination, may be own address or forwarded from sub-dodag
     * @return initialized DAO packet object
     */
    const Ptr<Dao> createDao(const Ipv6Address &reachableDest);
    const Ptr<Dao> createDao(const Ipv6Address &reachableDest, bool ackRequired);
    const Ptr<Dao> createDao() {return createDao(getSelfAddress()); };

    /**
     * Update routing table with new route to destination reachable via next hop
     *
     * @param nextHop next hop address to reach the destination for findBestMatchingRoute()
     * @param dest discovered destination address being added to the routing table
     */
    void updateRoutingTable(const Ipv6Address &nextHop, const Ipv6Address &dest, RplRouteData *routeData, bool defaultRoute);
    void updateRoutingTable(const Ipv6Address &nextHop, const Ipv6Address &dest, RplRouteData *routeData) { updateRoutingTable(nextHop, dest, routeData, false); };
//    void updateRoutingTable(const Dao *dao);
    RplRouteData* prepRouteData(const Dao *dao);

    /** Delete default routes for given interface
     *
     * @param interfaceID id of the interface to delete default route for
     *
     * NOTE: This method is taken directly from inet::Ipv6RoutingTable due to
     * the fact it's only available with xMIPv6 enabled
     */
    void deleteDefaultRoutes(int interfaceID);

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
    void clearParentRoutes();

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

    void generateLayout(cModule *net);
    void configureTopologyLayout(Coord anchorPoint, double padX, double padY, double columnGapMultiplier,
            cModule *network, bool branchLayout, int numBranches, int numHosts, int numSinks);

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
    bool isUdpSink(cModule* app);
    void purgeRoutingTable();

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
    B getDaoLength();

    // TODO: replace by dynamic calculation based on the number of addresses in source routing header
    // + additional field specifying length of this header to allow proper decapsulation
    B getSrhSize() { return B(64); }

    bool isDao(Packet *pkt) { return std::string(pkt->getFullName()).find("Dao") != std::string::npos; }
    bool isUdp(Packet *datagram) { return std::string(datagram->getFullName()).find("Udp") != std::string::npos; }

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
    bool isDownlinkPacket(Packet *datagram);

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
    void appendDaoTransitOptions(Packet *pkt, const Ipv6Address &target, const Ipv6Address &transit);
    void appendDaoTransitOptions(Packet *pkt);

    /**
     * Check RPL Packet Information header for loop detection purposes [RFC6550, 11.2]
     *
     * @param datagram application data to check RPL headers for
     * @return
     */
    bool checkRplRouteInfo(Packet *datagram);

    bool checkDuplicateRoute(Ipv6Route *route);

    /**
     * Check if packet has source-routing header (SRH) present
     * @param pkt packet to check for
     * @return true on SRH presence, false otherwise
     */
    bool isSourceRouted(Packet *pkt);
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
    void constructSrcRoutingHeader(std::deque<Ipv6Address> &addressList, Ipv6Address dest);
    bool destIsRoot(Packet *datagram);

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
    virtual void receiveSignal(cComponent *src, simsignal_t id, long value, cObject *details) override;
    virtual void receiveSignal(cComponent *source, simsignal_t signalID, double value, cObject *details) override;


    std::vector<uint16_t> getNodesPerHopDistance();
    bool isRplPacket(Packet *packet);
    void clearDaoAckTimer(Ipv6Address daoDest);
    void clearAllDaoAckTimers();

    std::vector<Ipv6Address> getNearestChildren();
    int getNumDownlinks();

    /** Misc */
    void drawConnector(Coord target, cFigure::Color col, Ipv6Address backupParent) const;
    void drawConnector(Coord target, cFigure::Color col) const
    {
        drawConnector(target, col, Ipv6Address::UNSPECIFIED_ADDRESS);
    }
    void drawConnector(Ipv6Address neighborAddr, Coord pos, cFigure::Color col);

    void setLineProperties(cLineFigure *connector, Coord target, cFigure::Color col, bool dashed) const;
    void setLineProperties(cLineFigure *connector, Coord target, cFigure::Color col) const
    {
        setLineProperties(connector, target, col, false);
    }

    static int getNodeId(std::string nodeName);

    mutable cLineFigure *prefParentConnector;

    // map of pointers to the dashed connector line between a node and its backup parents
    mutable map<Ipv6Address, cLineFigure*> backupConnectors;

    virtual void eraseBackupParentList(map <Ipv6Address, Dio*> &backupParents);
    virtual void clearObsoleteBackupParents(map <Ipv6Address, Dio*> &backupParents);

    double startDelay;

    string hostName; // name of the host module, e.g. "host" or "sink" etc.

    IMobility *parentMobilityMod; // preferred parent mobility module
    IMobility *mobility;

    // look for mobility module of the parent, if it's mobile, to dynamically update the connection arrow
    void setParentMobility(Dio* prefParent);

    /** Pick random color for parent-child connector drawing (if node's sink) */
    cFigure::Color pickRandomColor();

    /** Clears from routing table weird destination prefixes, which disrupt next hop selection */
    virtual void deleteManualRoutes();
};

} // namespace inet

#endif

