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

Rpl::Rpl() :
    isRoot(false),
    defaultPrefParentLifetime(10000),
    rank(INF_RANK),
    dodagId(Ipv6Address::UNSPECIFIED_ADDRESS),
    objectiveFunctionType("hopCount"),
    daoDelay(DEFAULT_DAO_DELAY),
    preferredParent(nullptr)
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
        routingTable = getModuleFromPar<Ipv6RoutingTable>(par("routingTableModule"), this);
        trickleTimer = check_and_cast<TrickleTimer *>(getModuleByPath("^.trickleTimer"));
        isRoot = par("isRoot").boolValue();
        daoEnabled = par("daoEnabled").boolValue();
        host = getContainingNode(this);
        dioReceivedSignal = registerSignal("dioReceived");
        daoReceivedSignal = registerSignal("daoReceived");
        parentChangedSignal = registerSignal("parentChanged");
        parentUnreachableSignal = registerSignal("parentUnreachable");
        objectiveFunction = new ObjectiveFunction(par("objectiveFunctionType").stdstringValue());
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

    if (isRoot && !par("disabled").boolValue()) {
        trickleTimer->start();
        rank = ROOT_RANK;
        dodagId = getSelfAddress();
        dodagVersion = DEFAULT_INIT_DODAG_VERSION;
        instanceId = DEFAULT_RPL_INSTANCE_ID;
        dtsn = 0;
        storing = par("storing").boolValue();
    }
}

void Rpl::stop()
{
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
}

void Rpl::detachFromDodag() {
    /**
     * If node's parent set turns empty, node is no longer associated
     * with the DODAG and suppresses former DODAG data by
     * clearing dodagId, instanceId, parent sets, setting it's rank to INF etc.
     * see RFC6560, 8.2.2.1.
     */
    EV_DETAIL << "Candidate parent list empty, leaving DODAG" << endl;
    backupParents.erase(backupParents.begin(), backupParents.end());
    EV_DETAIL << "Backup parents list erased" << endl;
    dodagId = Ipv6Address::UNSPECIFIED_ADDRESS;
    rank = INF_RANK;
    trickleTimer->stop();
    if (par("subDodagPoisoning").boolValue())
        poisonSubDodag();
}

void Rpl::poisonSubDodag() {
    ASSERT(rank == INF_RANK);
    EV_DETAIL << "Poisoning sub-dodag by advertising INF_RANK";
    sendRplPacket(createDio(), DIO, Ipv6Address::ALL_NODES_1);
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
    /**
     * Process signal from trickle timer module,
     * indicating DIO broadcast event, see RFC6560, 8.3
     */
    EV_DETAIL << "Processing msg from trickle timer" << endl;
    switch (message->getKind()) {
        case TRICKLE_TRIGGER_EVENT: {
            /**
             * Broadcast DIO only if number of DIOs heard
             * from other nodes <= redundancyConstant (k), see RFC6206, 4.2
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
//    auto packetProtocolTag = packet->findTag<PacketProtocolTag>();
//    auto protocol = packetProtocolTag->getProtocol();
//    EV_DETAIL << "Received packet protocol - " << protocol->str() << endl;
    try {
        auto rplHeader = packet->popAtFront<RplHeader>();
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
                else {
                    EV_DETAIL << "DAO support not enabled, discarding packet" << endl;
                    delete packet;
                }
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
//            default: throw cRuntimeError("Unknown Rpl packet");
            default: EV_WARN << "Unknown Rpl packet" << endl;
        }
    }
    catch (...) {
        EV_WARN << "Implicit chunk serialization?" << endl;
        delete packet;
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
    dio->setChunkLength(b(48)); // TODO: Set DIO size as constant or via function
    dio->setStoring(storing);
    dio->setRank(rank);
    dio->setDtsn(dtsn);
    dio->setDodagVersion(dodagVersion);
    dio->setDodagId(dodagId);
    dio->setSrcAddress(getSelfAddress());
    return dio;
}

const Ptr<Dao> Rpl::createDao(const Ipv6Address &reachableDest)
{
    auto dao = makeShared<Dao>();
    // TODO: Implement RPL Target, Transit Information objects as described in RFC6550, 6.4.3
    dao->setInstanceId(instanceId);
    dao->setChunkLength(b(64)); // FIXME: replace with constant or function calculating DAO size
    dao->setSrcAddress(getSelfAddress());
    dao->setReachableDest(reachableDest);
    dao->setDaoAckRequired(false);
    EV_DETAIL << "DAO created advertising " << dao->getReachableDest() << " reachability" << endl;
    return dao;
}

void Rpl::processDio(Packet *packet, const Ptr<const Dio>& dio)
{
//        emit(dioReceivedSignal, dio->dup());
        EV_INFO << "Processing DIO from " << dio->getSrcAddress() << endl;

        // Join the DODAG advertised, if node's not a part of any DODAG yet
        if (dodagId == Ipv6Address::UNSPECIFIED_ADDRESS) {
            dodagId = dio->getDodagId();
            dodagVersion = dio->getDodagVersion();
            instanceId = dio->getInstanceId();
            storing = dio->getStoring();
            dtsn = dio->getDtsn();
            // Start broadcasting DIOs forwarding DODAG control data
            trickleTimer->start();
            EV_DETAIL << "Joined new DODAG with id - " << dodagId << endl;
        } else {
            /**
             * Immediately check if INF_RANK is advertised, and the advertising node is a preferred parent,
             * in case 'poisoning' detected, delete preferred parent and remove it from candidate neighbors
             */
            if (checkPoisonedParent(dio)) {
                deletePrefParent(true);
                // Temporary functionality, selecting a backup parent as new preferred,
                // in case candidate set is empty, to be revised.
                updatePreferredParent(candidateParents.empty() ? true : false);
                delete packet;
                return;
            }
        }
        trickleTimer->incrementCtrlMsgReceivedCounter();
        // Do not process DIO from unknown DAG/RPL instance or if receiver is root
        if (checkUnknownDio(dio) || isRoot) {
            EV_DETAIL << "Unknown DODAG/InstanceId, or receiver is root - discarding DIO" << endl;
            delete packet;
            return;
        }
        /**
         * If dodagVersion has been incremented by the root, join newer DODAG
         * by emptying neighbor sets (candidate and backup parents) and start
         * forwarding updated DODAG information via DIOs, see RFC6560, 8.2.2.1.
         */
        if (dio->getDodagVersion() > dodagVersion) {
            EV_DETAIL << "New DODAG version discovered, joining and clearing former parent sets" << endl;
            backupParents.erase(backupParents.begin(), backupParents.end());
            candidateParents.erase(candidateParents.begin(), candidateParents.end());
            dodagVersion = dio->getDodagVersion();
        } else if (dio->getDodagVersion() < dodagVersion)
            EV_DETAIL << "Outdated DODAG version advertised, discarding DIO" << endl;
        /**
         * If Destination Advertisement Trigger Sequence Number has been incremented by parent
         * update node's own DTSN and unicast DAO to refresh downward routes
         */
        if (dio->getDtsn() > dtsn) {
            EV_DETAIL << "DTSN incremented, unicast DAO scheduled" << endl;
            dtsn = dio->getDtsn();
            sendRplPacket(createDao(getSelfAddress()), DAO, preferredParent->getSrcAddress(), daoDelay);
        }
        addNeighbour(dio);
        updatePreferredParent();
        delete packet;
}

bool Rpl::checkPoisonedParent(const Ptr<const Dio>& dio) {
//    EV_DETAIL << "Checking if poisoned DIO is received: " << endl;
//    EV_DETAIL << "Advertised rank = " << dio->getRank() << endl;
//    EV_DETAIL << "Sender addr - " << dio->getSrcAddress()
//            << "; preferred parent - " << preferredParent->getSrcAddress() << endl;
    return preferredParent && dio->getRank() == INF_RANK
            && preferredParent->getSrcAddress() == dio->getSrcAddress();
}

void Rpl::processDao(Packet *packet, const Ptr<const Dao>& dao) {
    emit(daoReceivedSignal, dao->dup());
    if (dao->getDaoAckRequired()) {
        sendRplPacket(createDao(Ipv6Address::UNSPECIFIED_ADDRESS), DAO_ACK, dao->getSrcAddress());
        EV_DETAIL << "DAO_ACK sent back to " << dao->getSrcAddress() << endl;
    }

    /**
     * If node is root or operates in storing mode
     * update routing table with destinations from DAO
     * see RFC6560, 3.3.
     */
    if (storing || isRoot) {
        if (!checkDestAlreadyKnown(dao->getReachableDest())) {
            updateRoutingTable(dao->getSrcAddress(), dao->getReachableDest());
            EV_DETAIL << "Destination learned from DAO - " << dao->getReachableDest()
                        << " reachable via " << dao->getSrcAddress() << endl;
        }
    }
    /**
     * Forward DAO 'upwards' via preferred parent to advertise destination to the root,
     * see RFC6560, 6.4.
     */
    if (!isRoot && preferredParent->getSrcAddress() != dao->getSrcAddress()) {
        sendRplPacket(createDao(dao->getReachableDest()), DAO,
                preferredParent->getSrcAddress(), daoDelay * (1 + uniform(0.1, 1)));
        EV_DETAIL << "Forwarding DAO upwards advertising "
                    << dao->getReachableDest() << " reachability" << endl;
    }
    delete packet;
}

void Rpl::processDaoAck(Packet *packet, const Ptr<const Dao>& daoAck) {
    EV_INFO << "Received DAO_ACK from " << daoAck->getSrcAddress() << endl;
    delete packet;
}

void Rpl::processDis(Packet *packet, const Ptr<const Dis>& disPacket) {
    EV_INFO << "Processing DIS packet" << endl;
}

void Rpl::updatePreferredParent(bool useBackups)
{
    auto newPrefParent = objectiveFunction->getPreferredParent(useBackups ? backupParents : candidateParents);
    /**
     * If a better preferred parent is advertised (based on the objective function),
     * update default route to DODAG sink with new nextHop
     */
    if (checkPrefParentChanged(newPrefParent)) {
        emit(parentChangedSignal, newPrefParent->dup());
        updateRoutingTable(newPrefParent->getSrcAddress(), dodagId, true);
        EV_DETAIL << "Updated preferred parent to - " << newPrefParent->getSrcAddress() << endl;
        /**
         * Reset trickle timer due to inconsistency (preferred parent changed) detected, thus
         * maintaining higher topology reactivity and convergence rate, see RFC6560, 8.3.
         */
        trickleTimer->reset();
        if (daoEnabled)
            sendRplPacket(createDao(getSelfAddress()), DAO, newPrefParent->getSrcAddress(),
                    daoDelay * (1 + uniform(0.1, 1)));
    }
    preferredParent = newPrefParent->dup();

    /** Recalculate rank using the objective function */
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

void Rpl::updateRoutingTable(const Ipv6Address &nextHop, const Ipv6Address &dest, bool isDefaultRoute)
{
    /** If route through preferred parent is being added, set it as default route */
    if (isDefaultRoute)
        routingTable->addDefaultRoute(nextHop, interfaceEntryPtr->getInterfaceId(), 500);
    else {
        auto route = routingTable->createRoute();
        route->setSourceType(IRoute::MANET);
        route->setPrefixLength(112);
        route->setInterface(interfaceEntryPtr);
        route->setDestination(dest);
        route->setNextHop(nextHop);
        routingTable->addRoute(route);
    }
}


void Rpl::deletePrefParent(bool poisoned)
{
    auto totalRoutes = routingTable->getNumRoutes();
    auto prefParentAddr = preferredParent->getSrcAddress();
    EV_DETAIL << "Preferred parent " << prefParentAddr << " unreachability detected"
            << (poisoned ? std::string(" through poisoning") : std::string(" ")) << endl;
    emit(parentUnreachableSignal, preferredParent);
    for (int i = 0; i < totalRoutes; i++) {
        auto ri = routingTable->getRoute(i);
        if (ri->getNextHopAsGeneric().toIpv6().matches(prefParentAddr, 112)) {
            routingTable->deleteRoute(ri);
            EV_DETAIL << "Deleted route through unreachable preferred parent " << endl;
        }
    }
    candidateParents.erase(prefParentAddr);
//    checkDupEntry(candidateParents, preferredParent->getSrcAddress());

    // temporary part to detect duplicate parent entries in candidate parent set
    Ipv6Address dupPrefParent;
    EV_DETAIL << "Checking duplicate entry for " << prefParentAddr << endl;
    for (auto parent : candidateParents) {
        EV_DETAIL << parent.first << " ? " << prefParentAddr << endl;
        if (parent.first.getSuffix(112).matches(prefParentAddr.getSuffix(112), 0)) {
            dupPrefParent = parent.first;
        }
    }
    candidateParents.erase(dupPrefParent);
    preferredParent = nullptr;
    EV_DETAIL << "Erased preferred parent from candidate parents list" << endl;
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
        if (ri->getDestinationAsGeneric().toIpv6() == dest) {
            EV_DETAIL << "Destination " << ri->getDestinationAsGeneric().toIpv6().str()
                    << " already known" << endl;
            return true;
        }
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
     * where preferred parent is chosen from the candidate parent set, see RFC6560, 8.2.1.
     *
     * In current implementation, neighbor data is represented by most recent
     * DIO packet received from it.
     */
    auto dioCopy = dio->dup();
    auto dioSender = dioCopy->getSrcAddressForUpdate();
    /** If DIO sender has equal rank, consider this node as a backup parent */
    if (dio->getRank() == rank) {
        if (backupParents.find(dioSender) != backupParents.end())
            EV_DETAIL << "Backup parent entry updated, address - " << dioSender << endl;
        else
            EV_DETAIL << "New backup parent added, address - " << dioSender << endl;
        backupParents[dioSender] = dioCopy;
    }
    /** If DIO sender has lower rank, consider this node as a candidate parent */
    if (dio->getRank() < rank) {
        if (candidateParents.find(dioSender) != candidateParents.end())
            EV_DETAIL << "Candidate parent entry updated, address - " << dioSender << endl;
        else
            EV_DETAIL << "New candidate parent added, address - " << dioSender << endl;
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
        const auto& networkHeader = findNetworkProtocolHeader(datagram);
        if (networkHeader != nullptr) {
            const L3Address& destination = networkHeader->getDestinationAddress();
            EV_DETAIL << "Connection with destination " << destination << " broken?" << endl;
            /**
             * If preferred parent unreachability detected, remove route with it as a
             * next hop from the routing table and select new preferred parent from the
             * candidate set.
             *
             * If candidate parent set is empty, leave current DODAG
             * (becoming either floating DODAG or poison child routes)
             */
            if (!preferredParent) {
                EV_DETAIL << "Preferred parent not defined, returning" << endl;
                return;
            }
            EV_DETAIL << "Check if preferred parent - " << preferredParent->getSrcAddress()
                    << " unreachable " << endl;
            if (destination.matches(preferredParent->getSrcAddress(), 112)) {
                deletePrefParent();
                if (!candidateParents.empty())
                    updatePreferredParent();
                else
                    detachFromDodag();
            }
        }
    }
}

} // namespace inet




