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

// TODO: Implement custom ping app?

Rpl::Rpl() :
    interfaceTable(nullptr),
    routingTable(nullptr),
    isRoot(false),
    pingTimeoutDelay(DEFAULT_PING_TIMEOUT_DELAY),
    pingDelay(1),
    interfaceEntryPtr(nullptr),
    rank(INF_RANK),
    dodagVersion(0),
    mop(NON_STORING),
    host(nullptr),
    dodagId(Ipv6Address::UNSPECIFIED_ADDRESS),
    prefixLength(DEFAULT_PREFIX_LENGTH),
    instanceId(DEFAULT_INSTANCE_ID),
    objectiveFunction(nullptr),
    objectiveFunctionType("hopCount"),
    packetInProcessing(nullptr),
    preferredParent(nullptr),
    trickleTimer(nullptr)
{}

Rpl::~Rpl()
{
    // TODO: Fix simulation crashing after modules called finish() function
    stop();
}


void Rpl::initialize(int stage)
{
    EV_DETAIL << "Initialization stage: " << stage << endl;

    RoutingProtocolBase::initialize(stage);

    if (stage == INITSTAGE_LOCAL) {
        interfaceTable = getModuleFromPar<IInterfaceTable>(par("interfaceTableModule"), this);
        routingTable = getModuleFromPar<IRoutingTable>(par("routingTableModule"), this);
        trickleTimer = check_and_cast<TrickleTimer *>(getModuleByPath("^.trickleTimer"));
        isRoot = par("isRoot").boolValue();
        daoEnabled = par("daoEnabled").boolValue();
        pingEnabled = par("pingEnabled").boolValue();
        host = getContainingNode(this);
        objectiveFunctionType = par("objectiveFunctionType").stdstringValue();
        objectiveFunction = new ObjectiveFunction(objectiveFunctionType);
    }
    else if (stage == INITSTAGE_ROUTING_PROTOCOLS) {
        registerService(Protocol::manet, nullptr, gate("ipIn"));
        registerProtocol(Protocol::manet, gate("ipOut"), nullptr);
        host->subscribe(linkBrokenSignal, this);
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

    pingTimeoutMsg = new cMessage("Preferred parent ping timeout", PING_TIMEOUT);

    if (isRoot && !par("disabled").boolValue()) {
        trickleTimer->start();
        rank = ROOT_RANK;
        dodagId = getSelfAddress();
        mop = getMopFromStr(par("mop").stringValue());
    }
}

void Rpl::stop()
{
    if (pingTimeoutMsg)
        cancelAndDelete(pingTimeoutMsg);
}

//
// Handling messages
//

void Rpl::handleMessageWhenUp(cMessage *message)
{
    if (message->isSelfMessage())
        processSelfMessage(message);
    else
        processMessage(message);
}


void Rpl::processSelfMessage(cMessage *message)
{
    EV_DETAIL << "Processing self-msg: " << message->getFullName() << endl;
    switch (message->getKind()) {
        case PING_TIMEOUT: {
            EV_DETAIL << "Deleting preferred parent route due to the ping timeout" << endl;
            /* Delete route through unreachable preferred parent and remove
             * corresponding entry from candidate neighbour set
             */
            ASSERT(preferredParent != nullptr);
//            candidateParents.erase(preferredParent->getSrcAddress());
            if (candidateParents.size() <= 1)
                /* TODO: poison sub-dodag when node is not a part
                 * of former DODAG, see Section 8.2.2.5
                 */
                leaveDodag();
//            else
//                updatePreferredParent();
            break;
        }
    }
}

void Rpl::leaveDodag() {
    /* If node's parent set turns empty, node is no longer associated
     * with the DODAG and suppresses former DODAG data it had, i.e.
     * clears dodagId, instanceId, rank, parent sets, etc. see Section 8.2.2.1.
     */
    EV_DETAIL << "Candidate parent list empty, leaving DODAG" << endl;
    candidateParents.erase(preferredParent->getSrcAddress());
    deletePrefParentRoute();
//    backupParents.erase(backupParents.begin(), backupParents.end());
    dodagId = Ipv6Address::UNSPECIFIED_ADDRESS;
    rank = INF_RANK;
    preferredParent = nullptr;
    trickleTimer->stop();
}

// Temporary method to test connectivity to the preferred parent
void Rpl::pingPreferredParent(double delay, const Ipv6Address &parentAddr)
{
    // cancel previously scheduled timeout in case preferred parent
    // has changed in the meantime
    if (pingTimeoutMsg)
        cancelEvent(pingTimeoutMsg);
    sendRplPacket(createDio(), PING, parentAddr, delay);
    EV_DETAIL << "Ping request sent to preferred parent - " << parentAddr << endl;
    auto pingTimeoutStamp = simTime() + uniform(3, 5) * pingTimeoutDelay;
    EV_DETAIL << "Ping timeout occurs at " << pingTimeoutStamp << " s" << endl;
    // TODO: Manage pingTimeoutMsg object lifecycle more efficiently
    scheduleAt(pingTimeoutStamp, pingTimeoutMsg);
}

//
// Handling generic packets
//

void Rpl::processMessage(cMessage *message)
{
    std::string arrivalGateName = std::string(message->getArrivalGate()->getBaseName());
    if (arrivalGateName.compare("ttModule") == 0)
        processTrickleTimerMsg(message);
    else if (Packet *fp = dynamic_cast<Packet *>(message))
        processPacket(fp);
    else
        throw cRuntimeError("Unknown message");
}

void Rpl::processTrickleTimerMsg(cMessage *message)
{
    /* Process signal from trickle timer module,
     * indicating DIO broadcast event, see Section 8.3
     */
    EV_DETAIL << "Processing msg from trickle timer" << endl;
    switch (message->getKind()) {
        case TRICKLE_TRIGGER_EVENT: {
            /* Broadcast DIO only if number of DIOs heard
             * from other nodes <= redundancyConstant (k), see RFC6206 Section 4.2 p.4
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
//    EV_DETAIL << "Received packet " << packet->str() << endl;
//    auto packetProtocolTag = packet->findTag<PacketProtocolTag>();
//    auto protocol = packetProtocolTag->getProtocol();
//    EV_DETAIL << "Protocol - " << protocol->str() << endl;

    auto rplHeader = packet->popAtFront<RplHeader>();
//    EV_DETAIL << "Processing incoming packet " << packet
//        << "Icmpv6 code - " << rplHeader->getIcmpv6Code() << endl;
    auto rplBody = packet->peekData<RplPacket>();
    auto senderAddr = rplBody->getSrcAddress();
    switch (rplHeader->getIcmpv6Code()) {
        case DIO: {
            processDio(packet, dynamicPtrCast<const Dio>(rplBody));
            break;
        }
        case DAO: {
            if (daoEnabled)
                processDao(packet, dynamicPtrCast<const Dao>(rplBody));
            else
                EV_DETAIL << "DAO support not enabled, discarding packet" << endl;
            break;
        }
        case DAO_ACK: {
            processDaoAck(packet, dynamicPtrCast<const Dao>(rplBody));
            break;
        }
        case DIS: {
            processDis(packet, dynamicPtrCast<const Dis>(rplBody));
            break;
        }
        case PING: {
            EV_DETAIL << "Received ping request from " << senderAddr << endl;
            if ((preferredParent || isRoot) && rank != INF_RANK) {
                sendRplPacket(createDio(), PING_ACK, senderAddr, pingDelay);
                EV_DETAIL << "Ping_ACK sent to " << senderAddr << endl;
            }
            delete packet;
            break;
        }
        case PING_ACK: {
            EV_DETAIL << "Connectivity confirmed to preferred parent - " << senderAddr << endl;
            cancelEvent(pingTimeoutMsg);
            delete packet;
            break;
        }
        default: throw cRuntimeError("Unknown Rpl packet");
    }
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
    Packet *pkt = new Packet("inet::RplPacket");
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
    dio->setChunkLength(b(32)); // TODO: Set DIO size as constant or via function
    dio->setMop(mop);
    dio->setRank(rank);
    dio->setDodagVersion(dodagVersion);
    dio->setDodagId(dodagId);
    dio->setSrcAddress(getSelfAddress());
    return dio;
}

const Ptr<Dao> Rpl::createDao(const Ipv6Address &reachableDest)
{
    auto dao = makeShared<Dao>();
    // TODO[OPTIONAL]: expand with DAO optional fields as specified in Section 6.4.3
    dao->setInstanceId(instanceId);
    dao->setChunkLength(b(64)); // FIXME: replace with constant or function calculating DAO size
    dao->setSrcAddress(getSelfAddress());
    dao->setReachableDest(reachableDest);
    dao->setDaoAckRequired(false);
    EV_DETAIL << "DAO created, advertising reachability of " << dao->getReachableDest() << endl;
//            << "to be sent to " << preferredParent->getSrcAddress() << endl;
    return dao;
}

void Rpl::processDio(Packet *packet, const Ptr<const Dio>& dio)
{
    EV_INFO << "Processing RPL packet - DIO, from " << dio->getSrcAddress() << endl;
    // Join the DODAG advertised, if node's not a part of any DODAG yet
    if (dodagId == Ipv6Address::UNSPECIFIED_ADDRESS) {
        dodagId = dio->getDodagId();
        instanceId = dio->getInstanceId();
        mop = dio->getMop();
        // Start broadcasting DIOs forwarding DODAG control data
        trickleTimer->start();
        EV_DETAIL << "Joined a DODAG with id - " << dodagId << endl;
    }
    trickleTimer->incrementCtrlMsgReceivedCounter();
    // Do not process DIO from unknown DAG/RPL instance or if receiver is root
    if (checkUnknownDio(dio) || isRoot) {
        EV_DETAIL << "Unknown DODAG/InstanceId, or receiver is root - discarding DIO" << endl;
        return;
    }
    // Check if dodagVersion has changed
    if (dio->getDodagVersion() > dodagVersion) {
        // new dodag version discovered, join it and erase parent sets
        EV_DETAIL << "New DODAG version discovered, clearing parent sets" << endl;
        backupParents.erase(backupParents.begin(), backupParents.end());
        candidateParents.erase(candidateParents.begin(), candidateParents.end());
        dodagVersion = dio->getDodagVersion();
    } else if (dio->getDodagVersion() < dodagVersion)
        EV_DETAIL << "Outdated DODAG version advertised, discarding DIO" << endl;
    addNeighbour(dio);
    updatePreferredParent();
    delete packet;
}

void Rpl::processDao(Packet *packet, const Ptr<const Dao>& dao) {
    EV_INFO << "Processing RPL packet - DAO, from " << dao->getSrcAddress() << "\n"
            << "destination advertised - " << dao->getReachableDest() << endl;
    if (dao->getDaoAckRequired())
        sendRplPacket(createDao(Ipv6Address::UNSPECIFIED_ADDRESS), DAO_ACK, dao->getSrcAddress());
    /* Update routing table with information about reachable destinations from DAO
     * if storing mode is enabled or receiver is root, see Section 3.3
     */
    if (mop == STORING || isRoot) {
        if (!checkDestAlreadyKnown(dao->getReachableDest())) {
            updateRoutingTable(dao->getSrcAddress(), dao->getReachableDest());
            EV_DETAIL << "New destination learned from DAO: " << dao->getReachableDest()
                        << " reachable via " << dao->getSrcAddress() << endl;
        } else
            EV_DETAIL << "Destination " << dao->getReachableDest()
                        << " already known, discarding DAO" << endl;
    } else {
        EV_DETAIL << "Non-storing mode, forwarding DAO upwards advertising "
                    << dao->getReachableDest() << " reachability" << endl;
    }
    /* Regardless of operational mode (storing, non-storing), forward DAO 'upwards'
     * through the preferred parent till root is reached, see Section 6.4
     */
    if (!isRoot)
        sendRplPacket(createDao(dao->getReachableDest()), DAO,
            preferredParent->getSrcAddress(), uniform(1, 2));
    delete packet;
}

void Rpl::processDaoAck(Packet *packet, const Ptr<const Dao>& daoAck) {
    EV_INFO << "Received DAO_ACK from " << daoAck->getSrcAddress() << endl;
    delete packet;
}

void Rpl::processDis(Packet *packet, const Ptr<const Dis>& disPacket) {
    EV_INFO << "Processing RPL packet - DIS" << endl;
}

void Rpl::updatePreferredParent()
{
    auto newPrefParent = objectiveFunction->getPreferredParent(candidateParents);
    // if preferred parent has changed, update routing table accordingly
    if (checkPrefParentChanged(newPrefParent)) {
        updateRoutingTable(newPrefParent->getSrcAddress(), dodagId);
        EV_DETAIL << "Updated preferred parent to - " << newPrefParent->getSrcAddress() << endl;
        /* Reset trickle timer if inconsistency (preferred parent changed) is detected, i.e.
         * maintain high topology convergence rate, see Section 8.3
         */
        trickleTimer->reset();
        // TODO: Determine default DAO delay
        if (daoEnabled)
            sendRplPacket(createDao(getSelfAddress()), DAO, newPrefParent->getSrcAddress(), uniform(1, 2));
    }
    if (pingEnabled)
        pingPreferredParent(pingDelay * uniform(1, 2), newPrefParent->getSrcAddress());
    preferredParent = newPrefParent->dup();
    // recalculate rank
    auto oldRank = rank;
    rank = objectiveFunction->calcRank(preferredParent);
    if (oldRank != rank)
        EV_DETAIL << "Updated rank = " << rank << endl;
}

bool Rpl::checkPrefParentChanged(Dio* newPrefParent)
{
    return preferredParent == nullptr
            || (newPrefParent->getSrcAddress() != preferredParent->getSrcAddress());
}


//
// Handling routing data
//

void Rpl::updateRoutingTable(const Ipv6Address &nextHop, const Ipv6Address &destination)
{
    Ipv6Route *route = new Ipv6Route(destination, prefixLength, IRoute::MANET);
    route->setNextHop(nextHop);
    route->setDestination(destination);
    route->setInterface(interfaceEntryPtr);
    routingTable->addRoute(route);
}

void Rpl::deletePrefParentRoute()
{
    auto totalRoutes = routingTable->getNumRoutes();
    auto prefParentAddr = preferredParent->getSrcAddressForUpdate();
    for (int i = 0; i < totalRoutes; i++) {
        auto ri = routingTable->getRoute(i);
        if (ri->getNextHopAsGeneric().toIpv6() == prefParentAddr) {
            routingTable->deleteRoute(ri);
            EV_DETAIL << "Deleted route through preferred parent - " << prefParentAddr << endl;
        }
    }
}

//
// Utility methods
//

Ipv6Address Rpl::getSelfAddress()
{
    return interfaceEntryPtr->getNetworkAddress().toIpv6();
}

bool Rpl::checkDestAlreadyKnown(const Ipv6Address &dest) {
    for (int i = 0; i < routingTable->getNumRoutes(); i++) {
        auto ri = routingTable->getRoute(i);
        if (ri->getDestinationAsGeneric().toIpv6() == dest)
            return true;
    }
    return false;
}

bool Rpl::checkUnknownDio(const Ptr<const Dio>& dio)
{
    return dio->getDodagId() != dodagId || dio->getInstanceId() != instanceId;
}

void Rpl::addNeighbour(const Ptr<const Dio>& dio)
{
    /* Maintain three sets of link-local nodes: backup parents, candidate parents
     * and preferred parent. For each DIO heard, push/update sender node as entry to
     * one of the sets, see Section 8.2.1
     */
    auto dioCopy = dio->dup();
    auto dioSender = dioCopy->getSrcAddressForUpdate();
    // If DIO received from node with equal rank, consider this node a backup parent
    if (dio->getRank() == rank) {
        if (backupParents.find(dioSender) != backupParents.end())
            EV_DETAIL << "Backup parent entry updated, address - " << dioSender << endl;
        else
            EV_DETAIL << "New backup parent added, address - " << dioSender << endl;
        backupParents[dioSender] = dioCopy;
    }
    // If DIO received from node with lower rank, consider this node a candidate parent
    if (dio->getRank() < rank) {
        if (candidateParents.find(dioSender) != candidateParents.end())
            EV_DETAIL << "Candidate parent entry updated, address - " << dioSender << endl;
        else
            EV_DETAIL << "New candidate parent added, address - " << dioSender << endl;
        candidateParents[dioSender] = dioCopy;
    }
}

Mop Rpl::getMopFromStr(std::string mopPar) {
    if (mopPar.compare(std::string("storing")) == 0)
        return STORING;
    else if (mopPar.compare(std::string("non_storing")) == 0)
        return NON_STORING;
    else
        throw cRuntimeError("Unknown mode of operation (mop) specified");
}

//
// notification handling
//

void Rpl::receiveSignal(cComponent *source, simsignal_t signalID, cObject *obj, cObject *details)
{
    /* Process NUD timeout and remove route through unreachable parent
     * from the routing table, Section 8.2.1 p.6
     */
    Enter_Method("receiveChangeNotification");
    EV_DETAIL << "Processing signal - " << signalID << endl;
    if (signalID == linkBrokenSignal) {
        EV_WARN << "Received link break" << endl;
        Packet *datagram = check_and_cast<Packet *>(obj);
        const auto& networkHeader = findNetworkProtocolHeader(datagram);
        if (networkHeader != nullptr) {
            const L3Address& destination = networkHeader->getDestinationAddress();
            EV_DETAIL << "Connection with destination " << destination << " broken?" << endl;
//            if (destination.getAddressType() == addressType) {
//                IRoute *route = routingTable->findBestMatchingRoute(destination);
//                if (route) {
//                    const L3Address& nextHop = route->getNextHopAsGeneric();
//                    sendRerrForBrokenLink(route->getInterface(), nextHop);
//                }
//            }
        }
    }
}

} // namespace inet




