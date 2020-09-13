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

#include <deque>
#include "inet/common/INETMath.h"
#include "inet/common/IProtocolRegistrationListener.h"
#include "inet/common/ModuleAccess.h"
#include "inet/common/ProtocolTag_m.h"
#include "inet/linklayer/common/InterfaceTag_m.h"
#include "inet/networklayer/common/HopLimitTag_m.h"
#include "inet/networklayer/common/IpProtocolId_m.h"
#include "inet/networklayer/common/L3AddressResolver.h"
#include "inet/networklayer/common/L3AddressTag_m.h"
#include "inet/networklayer/common/L3Tools.h"
#include "inet/networklayer/contract/IInterfaceTable.h"
#include "inet/transportlayer/contract/udp/UdpControlInfo.h"
#include "inet/networklayer/icmpv6/Icmpv6Header_m.h"
#include "Rpl.h"
#include "Rpl_m.h"

namespace inet {

Define_Module(Rpl);

// TODO: Store DODAG-related info in a separate class to
// to monitor it during the simulation

// TODO: Figure out 'false positives' root cause, triggering link-break signals
// on lost NeighborAdvertisement (NA) packets

// TODO: Replace plain pointers with shared pointers where possible for
// proper garbage collection

// TODO: Replace preferredParent with it's address (initialized to UNSPECIFIED_ADDRESS by default)
// to avoid nullptr dereferencing possibility in many places

// TODO: Finish source-routing headers

Rpl::Rpl() :
    isRoot(false),
    detachedTimeout(2), // manual suppressing previous DODAG info [RFC 6550, 8.2.2.1]
    prefixLength(112),
    objectiveFunctionType("hopCount"),
    daoDelay(DEFAULT_DAO_DELAY),
    preferredParent(nullptr),
    floating(false)
{}

Rpl::~Rpl()
{
    stop();
}


void Rpl::initialize(int stage)
{
    EV_DETAIL << "Initialization stage: " << stage << endl;

    RoutingProtocolBase::initialize(stage);

    if (stage == INITSTAGE_LOCAL) {
        interfaceTable = getModuleFromPar<IInterfaceTable>(par("interfaceTableModule"), this);
        networkProtocol = getModuleFromPar<INetfilter>(par("networkProtocolModule"), this);
        routingTable = getModuleFromPar<Ipv6RoutingTable>(par("routingTableModule"), this);
        trickleTimer = check_and_cast<TrickleTimer *>(getModuleByPath("^.trickleTimer"));
        isRoot = par("isRoot").boolValue();
        daoEnabled = par("daoEnabled").boolValue();
        host = getContainingNode(this);
        objectiveFunction = new ObjectiveFunction(par("objectiveFunctionType").stdstringValue());
        dioReceivedSignal = registerSignal("dioReceived");
        daoReceivedSignal = registerSignal("daoReceived");
        parentChangedSignal = registerSignal("parentChanged");
        parentUnreachableSignal = registerSignal("parentUnreachable");
    }
    else if (stage == INITSTAGE_ROUTING_PROTOCOLS) {
        registerService(Protocol::manet, nullptr, gate("ipIn"));
        registerProtocol(Protocol::manet, gate("ipOut"), nullptr);
        host->subscribe(linkBrokenSignal, this);
        networkProtocol->registerHook(0, this);
    }
}

//
// Lifecycle operations
//

void Rpl::start()
{
    InterfaceEntry* ie = nullptr;
    // set network interface entry pointer (TODO: Update for IEEE 802.15.4)
    for (int i = 0; i < interfaceTable->getNumInterfaces(); i++)
    {
        ie = interfaceTable->getInterface(i);
        if (strstr(ie->getInterfaceName(), "wlan") != nullptr)
        {
            interfaceEntryPtr = ie;
            break;
        }
    }

    dodagId = Ipv6Address::UNSPECIFIED_ADDRESS;
    rank = INF_RANK - 1;
    detachedTimeoutEvent = new cMessage("", DETACHED_TIMEOUT);

    if (isRoot && !par("disabled").boolValue()) {
        trickleTimer->start(true);
        rank = ROOT_RANK;
        dodagVersion = DEFAULT_INIT_DODAG_VERSION;
        instanceId = RPL_DEFAULT_INSTANCE;
        dtsn = 0;
        storing = par("storing").boolValue();
    }
}

void Rpl::stop()
{
    cancelAndDelete(detachedTimeoutEvent);
}

void Rpl::handleMessageWhenUp(cMessage *message)
{
    if (message->isSelfMessage())
        processSelfMessage(message);
    else
        processMessage(message);
}


void Rpl::processSelfMessage(cMessage *message)
{
    switch (message->getKind()) {
        case DETACHED_TIMEOUT: {
            floating = false;
            EV_DETAIL << "Detached state ended, processing new incoming RPL packets" << endl;
            break;
        }
    }
}

void Rpl::detachFromDodag() {
    /**
     * If node's parent set is emptied, node is no longer associated
     * with a DODAG and suppresses previous RPL state by
     * clearing dodagId, parent sets, setting it's rank to INFINITE_RANK [RFC6560, 8.2.2.1]
     */
    EV_DETAIL << "Candidate parent list empty, leaving DODAG" << endl;
    backupParents.erase(backupParents.begin(), backupParents.end());
    EV_DETAIL << "Backup parents list erased" << endl;
    /** Delete all routes associated with DAO destinations of the former DODAG */
    purgeDaoRoutes();
    rank = INF_RANK;
    trickleTimer->suspend();
    if (par("poisoning").boolValue())
        poisonSubDodag();
    dodagId = Ipv6Address::UNSPECIFIED_ADDRESS;
    floating = true;
    EV_DETAIL << "Detached state enabled, no RPL packets will be processed for "
            << (int)detachedTimeout << "s" << endl;
    cancelEvent(detachedTimeoutEvent);
    scheduleAt(simTime() + detachedTimeout, detachedTimeoutEvent);
}

void Rpl::purgeDaoRoutes() {
    std::list<Ipv6Route *> purgedRoutes;
    EV_DETAIL << "Purging DAO routes from the routing table: " << endl;
    auto numRoutes = routingTable->getNumRoutes();
    for (int i = 0; i < numRoutes; i++) {
        auto ri = routingTable->getRoute(i);
        auto routeData = dynamic_cast<RplRouteData *> (ri->getProtocolData());
        if (routeData && routeData->getDodagId() == dodagId && routeData->getInstanceId() == instanceId)
            purgedRoutes.push_front(ri);
    }
    if (!purgedRoutes.empty()) {
        for (auto route : purgedRoutes)
            EV_DETAIL << route->getDestPrefix() << " via " << route->getNextHop()
                    << " : " << boolStr(routingTable->deleteRoute(route), "Success", "Fail");
    } else
        EV_DETAIL << "No DAO-associated routes found" << endl;
}

void Rpl::poisonSubDodag() {
    ASSERT(rank == INF_RANK);
    EV_DETAIL << "Poisoning sub-dodag by advertising INF_RANK " << endl;
    sendRplPacket(createDio(), DIO, Ipv6Address::ALL_NODES_1, uniform(1, 2));
}

//
// Handling generic packets
//

void Rpl::processMessage(cMessage *message)
{
    std::string arrivalGateName = std::string(message->getArrivalGate()->getBaseName());
    if (arrivalGateName.compare("ttModule") == 0)
        processTrickleTimerMsg(message);
    else if (Packet *fp = dynamic_cast<Packet *>(message)) {
        try {
            processPacket(fp);
        }
        catch (std::exception &e) {
            EV_WARN << "Error occured during packet processing: " << e.what() << endl;
        }
    }
}

void Rpl::processTrickleTimerMsg(cMessage *message)
{
    /**
     * Process signal from trickle timer module,
     * indicating DIO broadcast event [RFC6560, 8.3]
     */
    EV_DETAIL << "Processing msg from trickle timer" << endl;
    switch (message->getKind()) {
        case TRICKLE_TRIGGER_EVENT: {
            /**
             * Broadcast DIO only if number of DIOs heard
             * from other nodes <= redundancyConstant (k) [RFC6206, 4.2]
             */
            if (trickleTimer->checkRedundancyConst()) {
                EV_DETAIL << "Redundancy OK, broadcasting DIO" << endl;
                sendRplPacket(createDio(), DIO, Ipv6Address::ALL_NODES_1);
            }
            break;
        }
        default: throw cRuntimeError("Unknown TrickleTimer message");
    }
    delete message;
}


void Rpl::processPacket(Packet *packet)
{
    if (floating) {
        EV_DETAIL << "Not processing packet " << packet
                << "\n due to floating (detached) state" << endl;
        delete packet;
        return;
    }
    auto rplHeader = packet->popAtFront<RplHeader>();
    auto rplBody = packet->peekData<RplPacket>();
    auto senderAddr = rplBody->getSrcAddress();
    switch (rplHeader->getIcmpv6Code()) {
        case DIO: {
            processDio(dynamicPtrCast<const Dio>(rplBody));
            break;
        }
        case DAO: {
            if (daoEnabled)
                processDao(dynamicPtrCast<const Dao>(rplBody));
            else
                EV_DETAIL << "DAO support not enabled, discarding packet" << endl;
            break;
        }
        case DAO_ACK: {
            processDaoAck(dynamicPtrCast<const Dao>(rplBody));
            break;
        }
        case DIS: {
            processDis(dynamicPtrCast<const Dis>(rplBody));
            break;
        }
        default: EV_WARN << "Unknown Rpl packet" << endl;
    }

    delete packet;
}

void Rpl::sendPacket(cPacket *packet, double delay)
{
    if (delay == 0)
        send(packet, "ipOut");
    else
        sendDelayed(packet, delay, "ipOut");
}

//
// Handling RPL packets
//

void Rpl::sendRplPacket(const Ptr<RplPacket>& body, RplPacketCode code, const L3Address& nextHop, double delay)
{
    Packet *pkt = new Packet(std::string("inet::RplPacket::" + rplIcmpCodeToStr(code)).c_str());
    auto header = makeShared<RplHeader>();
    header->setIcmpv6Code(code);
    pkt->addTag<PacketProtocolTag>()->setProtocol(&Protocol::manet);
    pkt->addTag<DispatchProtocolReq>()->setProtocol(&Protocol::ipv6);
    if (interfaceEntryPtr)
        pkt->addTag<InterfaceReq>()->setInterfaceId(interfaceEntryPtr->getInterfaceId());
    auto addresses = pkt->addTag<L3AddressReq>();
    addresses->setSrcAddress(getSelfAddress());
    addresses->setDestAddress(nextHop);
    pkt->insertAtFront(header);
    pkt->insertAtBack(body);
    sendPacket(pkt, delay);
}


const Ptr<Dio> Rpl::createDio()
{
    auto dio = makeShared<Dio>();
    dio->setInstanceId(instanceId);
    dio->setChunkLength(b(48)); // TODO: Set DIO size as constant or via function
    dio->setStoring(storing);
    dio->setRank(rank);
    dio->setDtsn(dtsn);
    dio->setDodagVersion(dodagVersion);
    dio->setDodagId(isRoot ? getSelfAddress() : dodagId);
    dio->setSrcAddress(getSelfAddress());
    EV_DETAIL << "DIO created advertising DODAG - " << dio->getDodagId()
            << " and rank " << dio->getRank() << endl;
    return dio;
}

const Ptr<Dao> Rpl::createDao(const Ipv6Address &reachableDest)
{
    auto dao = makeShared<Dao>();
    dao->setInstanceId(instanceId);
    dao->setChunkLength(b(64));
    dao->setSrcAddress(getSelfAddress());
    dao->setReachableDest(reachableDest);
    dao->setDaoAckRequired(false);
    EV_DETAIL << "DAO created advertising " << dao->getReachableDest() << " reachability" << endl;
    return dao;
}

void Rpl::processDio(const Ptr<const Dio>& dio)
{
    EV_DETAIL << "Processing DIO from " << dio->getSrcAddress() << endl;
    emit(dioReceivedSignal, dio->dup());
    // If node's not a part of any DODAG, join the one advertised
    if (!isRoot && dodagId == Ipv6Address::UNSPECIFIED_ADDRESS && dio->getRank() != INF_RANK) {
        dodagId = dio->getDodagId();
        dodagVersion = dio->getDodagVersion();
        instanceId = dio->getInstanceId();
        storing = dio->getStoring();
        dtsn = dio->getDtsn();
        EV_DETAIL << "Joined new DODAG with id - " << dodagId << endl;
        // Start broadcasting DIOs, diffusing DODAG control data
        if (trickleTimer->hasStarted())
            trickleTimer->reset();
        else
            trickleTimer->start();
    } else {
        /**
         * Immediately check if INFINITE_RANK is advertised, and the advertising
         * node is a preferred parent, in case 'poisoning' detected, delete
         * preferred parent and remove it from candidate neighbors
         */
        if (checkPoisonedParent(dio)) {
            EV_DETAIL << "Received poisoned DIO from preferred parent - "
                    << preferredParent->getSrcAddress() << endl;
            deletePrefParent(true);
            updatePreferredParent();
            return;
        }
    }
    trickleTimer->ctrlMsgReceived();
    // Do not process DIO from unknown DAG/RPL instance or if receiver is root
    if (checkUnknownDio(dio) || isRoot) {
        EV_DETAIL << "Unknown DODAG/InstanceId, or receiver is root - discarding DIO" << endl;
        return;
    }
    /**
     * If dodagVersion has been incremented by the root, join newer DODAG
     * by emptying neighbor sets (candidate and backup parents) and start
     * forwarding updated DODAG information via DIOs [RFC6560, 8.2.2.1].
     */
    if (dio->getDodagVersion() > dodagVersion) {
        EV_DETAIL << "Newer DODAG version discovered, joining and clearing former parent sets" << endl;
        backupParents.erase(backupParents.begin(), backupParents.end());
        candidateParents.erase(candidateParents.begin(), candidateParents.end());
        dodagVersion = dio->getDodagVersion();
    } else if (dio->getDodagVersion() < dodagVersion)
        EV_DETAIL << "Outdated DODAG version advertised, discarding DIO" << endl;
    /**
     * If Destination Advertisement Trigger Sequence Number has been incremented by parent
     * update node's own DTSN and unicast DAO to refresh downward routes.
     */
    if (dio->getDtsn() > dtsn) {
        EV_DETAIL << "DTSN incremented, unicast DAO scheduled" << endl;
        dtsn = dio->getDtsn();
        sendRplPacket(createDao(), DAO, preferredParent->getSrcAddress(), daoDelay);
    }
    if (dio->getRank() > rank) {
        EV_DETAIL << "Higher rank advertised, discarding DIO" << endl;
        return;
    }
    addNeighbour(dio);
    updatePreferredParent();
}

bool Rpl::checkPoisonedParent(const Ptr<const Dio>& dio) {
    return preferredParent && dio->getRank() == INF_RANK
            && matchesSuffix(preferredParent->getSrcAddress(), dio->getSrcAddress());
}

void Rpl::processDao(const Ptr<const Dao>& dao) {
    emit(daoReceivedSignal, dao->dup());
    auto daoSender = dao->getSrcAddress();
    auto advertisedDest = dao->getReachableDest();
    if (dao->getDaoAckRequired()) {
        sendRplPacket(createDao(Ipv6Address::UNSPECIFIED_ADDRESS), DAO_ACK, daoSender);
        EV_DETAIL << "DAO_ACK sent back to " << daoSender << endl;
    }

    /**
     * If a node is root or operates in storing mode
     * update routing table with destinations from DAO [RFC6560, 3.3].
     */
    if (storing || isRoot) {
        if (!checkDestKnown(daoSender, advertisedDest)) {
            updateRoutingTable(dao->dup());
            EV_DETAIL << "Destination learned from DAO - " << advertisedDest
                        << " reachable via " << daoSender << endl;
        }
    }
    /**
     * Forward DAO 'upwards' via preferred parent advertising destination to the root [RFC6560, 6.4]
     */
    if (!isRoot && preferredParent && !matchesSuffix(daoSender, preferredParent->getSrcAddress())) {
        sendRplPacket(createDao(advertisedDest), DAO,
                preferredParent->getSrcAddress(), daoDelay * uniform(1, 2));
        EV_DETAIL << "Forwarding DAO upwards to " << preferredParent->getSrcAddress()
                << " advertising " << advertisedDest << " reachability" << endl;
    }
}

// deprecated, to be replaced by default L3Address matches() method
bool Rpl::matchesSuffix(const Ipv6Address &addr1, const Ipv6Address &addr2, int prefixLength) {
    return addr1.getSuffix(prefixLength) == addr2.getSuffix(prefixLength);
}

void Rpl::processDaoAck(const Ptr<const Dao>& daoAck) {
    EV_INFO << "Received DAO_ACK from " << daoAck->getSrcAddress() << endl;
}

void Rpl::processDis(const Ptr<const Dis>& disPacket) {
    EV_INFO << "Processing DIS packet" << endl;
}

void Rpl::updatePreferredParent()
{
    Dio *newPrefParent;
    EV_DETAIL << "Choosing preferred parent from "
            << boolStr(candidateParents.empty() && par("useBackupAsPreferred").boolValue(),
                    "backup", "candidate") << " parent set" << endl;
    /**
     * Determine preferred parent from the candidate neighbor set or
     * leave DODAG if this set is empty
     */
    if (candidateParents.empty()) {
        if (par("useBackupAsPreferred").boolValue())
            newPrefParent = objectiveFunction->getPreferredParent(backupParents);
        else {
            detachFromDodag();
            return;
        }

    } else
        newPrefParent = objectiveFunction->getPreferredParent(candidateParents);

    auto newPrefParentAddr = newPrefParent->getSrcAddress();
    /**
     * If a better preferred parent has been discovered (based on objective function's metric),
     * update default route to DODAG sink with the new nextHop
     */
    if (checkPrefParentChanged(newPrefParentAddr)) {
        emit(parentChangedSignal, newPrefParent->dup());
        updateRoutingTable(newPrefParentAddr);
        EV_DETAIL << "Updated preferred parent to - " << newPrefParentAddr << endl;
        /**
         * Reset trickle timer due to inconsistency (preferred parent changed) detected, thus
         * maintaining higher topology reactivity and convergence rate [RFC6560, 8.3]
         */
        trickleTimer->reset();
        if (daoEnabled) {
            sendRplPacket(createDao(), DAO, newPrefParentAddr,
                    daoDelay * uniform(1, 2));
        }

    }
    preferredParent = newPrefParent->dup();
    /** Recalculate rank based on the objective function */
    rank = objectiveFunction->calcRank(preferredParent);
    EV_DETAIL << "Rank - " << rank << endl;
}

bool Rpl::checkPrefParentChanged(const Ipv6Address &newPrefParentAddr)
{
    if (!preferredParent) {
        EV_DETAIL << "Pref. parent is nullptr, HAS CHANGED " << endl;
        return true;
    }

    return !matchesSuffix(newPrefParentAddr, preferredParent->getSrcAddress());
}


//
// Handling routing data
//

void Rpl::updateRoutingTable(const Ipv6Address &nextHop, const Ipv6Address &dest, RplRouteData *routeData)
{
    auto route = routingTable->createRoute();
    route->setSourceType(IRoute::MANET);
    route->setPrefixLength(prefixLength);
    route->setInterface(interfaceEntryPtr);
    route->setDestination(dest);
    route->setNextHop(nextHop);
    /**
     * If a route through preferred parent is being added
     * (i.e. not downward route, learned from DAO), set it as default route
     */
    if (!routeData)
        routingTable->addDefaultRoute(nextHop, interfaceEntryPtr->getInterfaceId(), DEFAULT_PARENT_LIFETIME);
    else
        route->setProtocolData(routeData);
    routingTable->addRoute(route);
}

void Rpl::updateRoutingTable(Dao *dao) {
    auto routeData = new RplRouteData();
    routeData->setDodagId(dao->getDodagId());
    routeData->setInstanceId(dao->getInstanceId());
    routeData->setDtsn(dao->getDaoSeqNum());
    routeData->setExpirationTime(-1);
    updateRoutingTable(dao->getSrcAddress(), dao->getReachableDest(), routeData);
}

void Rpl::updateRoutingTable(const Ipv6Address &nextHop) {
    updateRoutingTable(nextHop, dodagId, nullptr);
}

void Rpl::appendRplPacketInfo(Packet *datagram) {
    auto rpi = makeShared<RplPacketInfo>();
    rpi->setChunkLength(B(4));
    rpi->setDown(isRoot || packetTravelsDown(datagram));
    rpi->setRankError(false);
    rpi->setFwdError(false);
    rpi->setInstanceId(instanceId);
    rpi->setSenderRank(rank);
    datagram->insertAtBack(rpi);
    EV_INFO << "Appended RPL Packet Information: \n" << printHeader(rpi.get())
            << "\n to UDP datagram: " << datagram << endl;
}

bool Rpl::packetTravelsDown(Packet *datagram) {
    auto networkProtocol = findNetworkProtocolHeader(datagram);
    auto dest = networkProtocol->getDestinationAddress().toIpv6();
    EV_DETAIL << "Determining packet forwarding direction:\n destination - " << dest << endl;
    auto ri = routingTable->doLongestPrefixMatch(dest);
    if (ri == nullptr) {
        auto errorMsg = std::string("Error while determining packet forwarding direction"
                + std::string(", couldn't find route to ") + dest.str());
        throw cRuntimeError(errorMsg.c_str());
    }

    EV_DETAIL << " next hop - " << ri->getNextHop() << endl;
    bool res = sourceRouted(datagram)
            || !(ri->getNextHop().matches(preferredParent->getSrcAddress(), prefixLength));
    EV_DETAIL << " Packet travels " << boolStr(res, "downwards", "upwards");
    return res;
}


bool Rpl::sourceRouted(Packet *pkt) {
    EV_DETAIL << "Checking if packet is source-routed" << endl;;
    try {
        auto srh = pkt->popAtBack<SourceRoutingHeader>(B(64));
        EV_DETAIL << "Retrieved source-routing header - " << srh << endl;
        return true;
    }
    catch (std::exception &e) { }

    return false;
}

B Rpl::getDaoLength() {
    return B(8);
}


void Rpl::appendDaoTransitOptions(Packet *pkt) {
    EV_DETAIL << "Appending target, transit options to DAO: " << pkt << endl;
    auto rplTarget = makeShared<RplTargetInfo>();
    auto rplTransit = makeShared<RplTransitInfo>();
    rplTarget->setChunkLength(getTransitOptionsLength());
    rplTransit->setChunkLength(getTransitOptionsLength());
    rplTarget->setTarget(getSelfAddress());
    rplTransit->setTransit(preferredParent->getSrcAddress());
    pkt->insertAtBack(rplTarget);
    pkt->insertAtBack(rplTransit);
    EV_DETAIL << "transit => target headers appended: "
            << rplTransit->getTransit() << " => " << rplTarget->getTarget() << endl;
}


bool Rpl::checkRplRouteInfo(Packet *datagram) {
    auto dest = findNetworkProtocolHeader(datagram)->getDestinationAddress().toIpv6();
    if (dest.matches(getSelfAddress(), prefixLength)) {
        EV_DETAIL << "Packet reached its destination, no RPI header checking needed" << endl;
        return true;
    }
    EV_DETAIL << "Checking RPI header for packet " << datagram << endl;
    RplPacketInfo *rpi;
    try {
        rpi = const_cast<RplPacketInfo *> (datagram->popAtBack<RplPacketInfo>(B(4)).get());
    }
    catch (std::exception &e) {
       EV_WARN << "No RPL Packet Information present in UDP datagram, appending" << endl;
       try {
           appendRplPacketInfo(datagram);
           return true;
       }
       catch (...) {
           return false;
       }
    }
    if (isRoot)
        return true;
    // check for rank inconsistencies
    EV_DETAIL << "Rank error before checking - " << rpi->getRankError() << endl;
    bool rankInconsistency = checkRankError(rpi);
    if (rpi->getRankError() && rankInconsistency) {
        EV_WARN << "Repeated rank error detected for packet "
                << datagram << "\n dropping..." << endl;
        return false;
    }
    if (rankInconsistency)
        rpi->setRankError(rankInconsistency);
    EV_DETAIL << "Rank error after check - " << rpi->getRankError() << endl;
    /**
     * If there's a forwarding error, packet should be returned to parent
     * with 'F' flag set to clear outdated DAO routes if DAO inconsistency detection
     * is enabled [RFC 6550, 11.2.2.3]
     */
    rpi->setFwdError(!isRoot && checkForwardingError(rpi, dest));
    if (rpi->getFwdError()) {
        EV_WARN << "Forwarding error detected for packet " << datagram
                << "\n destined to " << dest << ", dropping" << endl;
        return false;
    }
    // update packet forwarding direction if storing mode is in use
    // e.g. if unicast P2P packet reaches sub-dodag root with 'O' flag cleared,
    // and this root can route packet downwards to destination, 'O' flag has to be set.
    if (storing)
        rpi->setDown(isRoot || packetTravelsDown(datagram));
    // try make new shared ptr chunk RPI, to be refactored into reusing existing RPI object
    auto rpiCopy = makeShared<RplPacketInfo>();
    rpiCopy->setChunkLength(getRpiHeaderLength());
    rpiCopy->setDown(rpi->getDown());
    rpiCopy->setRankError(rpi->getRankError());
    rpiCopy->setFwdError(rpi->getFwdError());
    rpiCopy->setInstanceId(instanceId);
    rpiCopy->setSenderRank(rank);
    datagram->insertAtBack(rpiCopy);
    return true;
}

B Rpl::getTransitOptionsLength() {
    return B(16);
}

B Rpl::getRpiHeaderLength() {
    return B(4);
}


void Rpl::extractSourceRoutingData(Packet *dao) {
    try {
        auto transit = dao->popAtBack<RplTransitInfo>(getTransitOptionsLength()).get()->getTransit();
        auto target = dao->popAtBack<RplTargetInfo>(getTransitOptionsLength()).get()->getTarget();
        sourceRoutingTable.insert( std::pair<Ipv6Address, Ipv6Address>(target, transit) );

        EV_DETAIL << "Source routing table updated: " << printMap(sourceRoutingTable) << endl;
    }
    catch (std::exception &e) {
        EV_WARN << "Couldn't update source routing table: " << e.what() << endl;
    }
}


INetfilter::IHook::Result Rpl::checkRplHeaders(Packet *datagram) {
    // skip further checks if node doesn't belong to a DODAG
    if (!isRoot && (preferredParent == nullptr
            || dodagId.matches(Ipv6Address::UNSPECIFIED_ADDRESS, prefixLength)))
    {
        EV_DETAIL << "Node is detached from a DODAG, " <<
                " no forwarding/rank error checks will be performed" << endl;
        return ACCEPT;
    }
    auto datagramName = std::string(datagram->getFullName());
    EV_DETAIL << "packet fullname " << datagramName << endl;
    bool res = true;

    if (isDao(datagram) && !storing) {
        EV_DETAIL << "Checking source routing headers in DAO: " << datagram << endl;
        if (isRoot)
            extractSourceRoutingData(datagram);
        else if (selfGeneratedPkt(datagram))
            appendDaoTransitOptions(datagram);
    }
    if (isUdp(datagram)) {
        EV_DETAIL << "Udp datagram detected" << endl;
        if (isRoot && selfGeneratedPkt(datagram) && !storing) {
            appendSrcRoutingHeader(datagram);
        }
        else if (sourceRouted(datagram)) {
            forwardSourceRoutedPacket(datagram);
            return ACCEPT;
        }
        else
            res = checkRplRouteInfo(datagram);
    }

    return res ? ACCEPT : DROP;
}

void Rpl::appendSrcRoutingHeader(Packet *datagram) {
    Ipv6Address dest = findNetworkProtocolHeader(datagram)->getDestinationAddress().toIpv6();
    std::deque<Ipv6Address> srhAddresses;
    EV_DETAIL << "Appending routing header to datagram " << datagram << endl;
    Ipv6Address nextHop = sourceRoutingTable.find(dest)->second;
    srhAddresses.push_front(nextHop);
    while (!nextHop.matches(getSelfAddress(), prefixLength)) {
        nextHop = sourceRoutingTable.find(nextHop)->second;
        srhAddresses.push_front(nextHop);
    }
    EV_DETAIL << "Source routing header constructed : " << endl;
    for (auto i : srhAddresses) {
        EV_DETAIL << i << " => ";
    }
    EV_DETAIL << dest << endl;
    // TODO: appending Source Routing Header (SRH) directly to datagram
    // and manual forwarding on each hop by modifying IP destination field
}


void Rpl::forwardSourceRoutedPacket(Packet *datagram) {
    EV_DETAIL << "forwarding source-routed datagram - " << datagram << endl;
    auto srh = const_cast<SourceRoutingHeader *> (datagram->popAtBack<SourceRoutingHeader>(B(64)).get());
    auto srhAddresses = srh->getAddresses();
    auto nextHop = srhAddresses.front();
    srhAddresses.pop_front();
    (const_cast<NetworkHeaderBase *>(findNetworkProtocolHeader(datagram).get()))->setDestinationAddress(nextHop);

    auto updatedSrh =  makeShared<SourceRoutingHeader>();
    updatedSrh->setAddresses(srhAddresses);
    datagram->insertAtBack(updatedSrh);

    EV_DETAIL << "Set nextHop " << nextHop
            << "\n for source-routed packet - " << datagram
            << "\n sending via 'ipOut' gate" << endl;
}

bool Rpl::selfGeneratedPkt(Packet *pkt) {
    bool res;
    if (isRoot) {
        auto dest = findNetworkProtocolHeader(pkt)->getDestinationAddress().toIpv6();
        res = !(dest.matches(getSelfAddress(), prefixLength));
        EV_DETAIL << "Checking if packet is self generated by dest: " << dest << endl;
    }
    else {
        auto srcAddr = findNetworkProtocolHeader(pkt)->getSourceAddress().toIpv6();
        res = srcAddr.matches(getSelfAddress(), prefixLength);
        EV_DETAIL << "Checking if packet is self generated by source address: " << srcAddr << endl;
    }
    return res;
}

std::string Rpl::rplIcmpCodeToStr(RplPacketCode code) {
    switch (code) {
        case 0:
            return std::string("Dis");
        case 1:
            return std::string("Dio");
        case 2:
            return std::string("Dao");
        default:
            return std::string("Unknown");
    }
}

bool Rpl::isUdp(Packet *datagram) {
    return std::string(datagram->getFullName()).find("Udp") != std::string::npos;
}

bool Rpl::isDao(Packet *pkt) {
    bool res = std::string(pkt->getFullName()).find("Dao") != std::string::npos;
    EV_DETAIL << "Packet " << pkt << " " << boolStr(res, "is", "isn't") << " DAO packet " << endl;

    return res;
}

bool Rpl::checkRankError(RplPacketInfo *rpi) {
    auto senderRank = rpi->getSenderRank();
    EV_DETAIL << "Checking rank consistency: "
            << "\n direction - " << boolStr(rpi->getDown(), "down", "up")
            << "\n senderRank - " << senderRank << "; own rank - " << rank << endl;
    bool res = (!(rpi->getDown()) && (senderRank <= rank))
                    || (rpi->getDown() && (senderRank >= rank));
    EV_DETAIL << "Rank consistency check " << boolStr(res, "failed", "passed") << endl;
    return res;
}

bool Rpl::checkForwardingError(RplPacketInfo *rpi, Ipv6Address &dest) {
    EV_DETAIL << "Checking forwarding error: \n MOP - "
            << boolStr(storing, "storing", "non-storing")
            << "\n dest - " << dest
            << "\n direction - " << boolStr(rpi->getDown(), "down", "up") << endl;
    auto route = routingTable->doLongestPrefixMatch(dest);
    auto parentAddr = preferredParent != nullptr ? preferredParent->getSrcAddress() : Ipv6Address::UNSPECIFIED_ADDRESS;
    bool res = storing && rpi->getDown()
                    && (route == nullptr || route->getNextHop().matches(parentAddr, prefixLength));
    EV_DETAIL << "Forwarding " << boolStr(res, "error detected", "OK") << endl;
    return res;
}

void Rpl::printTags(Packet *packet) {
    EV_DETAIL << "Packet " << packet->str() << "\n Tags: " << endl;
    for (int i = 0; i < packet->getNumTags(); i++)
        EV_DETAIL << "classname: " << packet->getTag(i)->getClassName() << endl;
}

std::string Rpl::printHeader(RplPacketInfo *rpi) {
    std::ostringstream out;
    out << " direction: " << boolStr(rpi->getDown(), "down", "up")
        << "\n senderRank:  " << rpi->getSenderRank()
        << "\n rankError: " << boolStr(rpi->getRankError())
        << "\n forwardingError: " << boolStr(rpi->getFwdError());
    return out.str();
}


void Rpl::deletePrefParent(bool poisoned)
{
    Ipv6Route *routeToDelete;
    if (!preferredParent) {
        EV_WARN << "Cannot delete preferred parent, it's nullptr" << endl;
        return;
    }

    auto prefParentAddr = preferredParent->getSrcAddress();
    EV_DETAIL << "Preferred parent " << prefParentAddr
            << boolStr(poisoned, " detachment", " unreachability") << "detected" << endl;
    emit(parentUnreachableSignal, preferredParent);
    routingTable->deleteDefaultRoutes(interfaceEntryPtr->getInterfaceId());
    EV_DETAIL << "Deleted default route through unreachable preferred parent " << endl;
    auto totalRoutes = routingTable->getNumRoutes();
    for (int i = 0; i < totalRoutes; i++) {
        auto ri = routingTable->getRoute(i);
        if (matchesSuffix(ri->getNextHop(), prefParentAddr))
            routeToDelete = ri;
    }
    if (routeToDelete)
        routingTable->deleteRoute(routeToDelete);
    EV_DETAIL << "Deleted non-default route through unreachable preferred parent " << endl;
    candidateParents.erase(prefParentAddr.getSuffix(prefixLength));
    preferredParent = nullptr;
    EV_DETAIL << "Erased preferred parent from candidate parent set" << endl;
}

//
// Utility methods
//

Ipv6Address* Rpl::constructSrcRoutingHeader(const Ipv6Address &dest) {
    Ipv6Address *addresses;
    addresses[1] = (routingTable->doLongestPrefixMatch(dest)->getNextHop());


    return addresses;
}

std::string Rpl::boolStr(bool cond, std::string positive, std::string negative) {
    return cond ? positive : negative;
}

template<typename Map>
std::string Rpl::printMap(const Map& map) {
    std::ostringstream out;
    for (const auto& p : map)
        out <<p.first <<","<< p.second <<std::endl;
    return out.str();
}

Ipv6Address Rpl::getSelfAddress() {
    return interfaceEntryPtr->getNetworkAddress().toIpv6();
}

bool Rpl::checkDestKnown(const Ipv6Address &nextHop, const Ipv6Address &dest) {
    Ipv6Route *outdatedRoute = nullptr;
    for (int i = 0; i < routingTable->getNumRoutes(); i++) {
        auto ri = routingTable->getRoute(i);
        if (ri->getDestPrefix() == dest) {
            EV_DETAIL << "Destination " << ri->getDestPrefix() << " already known, ";
            if (ri->getNextHop() == nextHop) {
                EV_DETAIL << "reachable via " << ri->getNextHop() << endl;
                return true;
            }
            else {
                EV_DETAIL << "but next hop has changed to "
                        << nextHop << ", routing table to be updated" << endl;
                outdatedRoute = ri;
                break;
            }
        }
    }
    try {
        if (outdatedRoute)
            EV_DETAIL << "Deleting outdated route to dest " << dest
                    << " via " << outdatedRoute->getNextHop()
                    << " : \n "
                    << boolStr(routingTable->deleteRoute(outdatedRoute), "Success", "Fail");
    }
    catch (std::exception &e) {
        EV_WARN << "Exception while deleting outdated route " << e.what() << endl;
    }

    return false;
}

bool Rpl::checkUnknownDio(const Ptr<const Dio>& dio)
{
    return dio->getDodagId() != dodagId || dio->getInstanceId() != instanceId;
}

void Rpl::addNeighbour(const Ptr<const Dio>& dio)
{
    /**
     * Maintain following sets of link-local nodes:
     *  - backup parents
     *  - candidate parents
     * where preferred parent is chosen from the candidate parent set [RFC6560, 8.2.1]
     *
     * In current implementation, neighbor data is represented by most recent
     * DIO packet received from it.
     */
    auto dioCopy = dio->dup();
    auto dioSender = dio->getSrcAddress().getSuffix(prefixLength);
    /** If DIO sender has equal rank, consider this node as a backup parent */
    if (dio->getRank() == rank) {
        if (backupParents.find(dioSender) != backupParents.end())
            EV_DETAIL << "Backup parent entry updated, address suffix - " << dioSender << endl;
        else
            EV_DETAIL << "New backup parent added, address suffix - " << dioSender << endl;
        backupParents[dioSender] = dioCopy;
    }
    /** If DIO sender has lower rank, consider this node as a candidate parent */
    if (dio->getRank() < rank) {
        if (candidateParents.find(dioSender) != candidateParents.end())
            EV_DETAIL << "Candidate parent entry updated, address suffix - " << dioSender << endl;
        else
            EV_DETAIL << "New candidate parent added, address suffix - " << dioSender << endl;
        candidateParents[dioSender] = dioCopy;
    }
}

//
// Notification handling
//

void Rpl::receiveSignal(cComponent *source, simsignal_t signalID, cObject *obj, cObject *details)
{
    /**
     * Upon receiving broken link signal from MAC layer, check whether
     * preferred parent is unreachable
     */
    Enter_Method("receiveChangeNotification");
    EV_DETAIL << "Processing signal - " << signalID << endl;
    if (signalID == linkBrokenSignal) {
        EV_WARN << "Received link break" << endl;
        Packet *datagram = check_and_cast<Packet *>(obj);
        EV_DETAIL << "Packet " << datagram->str() << " lost?" << endl;
        const auto& networkHeader = findNetworkProtocolHeader(datagram);
        if (networkHeader != nullptr) {
            const Ipv6Address& destination = networkHeader->getDestinationAddress().toIpv6();
            EV_DETAIL << "Connection with destination " << destination << " broken?" << endl;
            // TODO: Handle destination unreachabilty detection
            if (!preferredParent) {
                EV_DETAIL << "Preferred parent not set, returning" << endl;
                return;
            }

            /**
             * If preferred parent unreachability detected, remove route with it as a
             * next hop from the routing table and select new preferred parent from the
             * candidate set.
             *
             * If candidate parent set is empty, leave current DODAG
             * (becoming either floating DODAG or poison child routes altogether)
             */
            if (destination == preferredParent->getSrcAddress()
                    && par("unreachabilityDetectionEnabled").boolValue())
            {
                deletePrefParent();
                updatePreferredParent();
            }
        }
    }
}

} // namespace inet




