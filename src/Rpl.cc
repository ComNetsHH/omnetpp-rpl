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
    interfaceTable(nullptr),
    routingTable(nullptr),
    networkProtocol(nullptr),
    isRoot(false),
    pingTimeoutDelay(DEFAULT_PING_TIMEOUT_DELAY),
    interfaceEntryPtr(nullptr),
    rank(INF_RANK),
    dodagVersion(0),
    mop(STORING),
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
        networkProtocol = getModuleFromPar<INetfilter>(par("networkProtocolModule"), this);
        trickleTimer = check_and_cast<TrickleTimer *>(getModuleByPath("^.trickleTimer"));
        isRoot = par("isRoot").boolValue();
        host = host = getContainingNode(this);
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

    if (isRoot) {
        trickleTimer->start();
        rank = ROOT_RANK;
        dodagId = getSelfAddress();
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
            // Delete route through unreachable preferred parent and remove
            // corresponding entry from candidate neighbour set
            deletePrefParentRoute();
            if (candidateParents.size() > 1)
                candidateParents.erase(preferredParent->getSrcAddress());
            // select next candidate parent as preferred
            updatePreferredParent();
            break;
        }
        case PING_TRIGGER: {
            pingPreferredParent();
            break;
        }
    }
    delete message;
}

// Temporary method to test connectivity to the preferred parent
void Rpl::pingPreferredParent()
{
    sendRplPacket(createDio(), PING, preferredParent->getSrcAddress());
    EV_DETAIL << "Ping request sent to preferred parent - " << preferredParent->getSrcAddress() << endl;
    auto pingTimeoutStamp = simTime() + uniform(1, 3) * pingTimeoutDelay;
    EV_DETAIL << "Ping timeout will occur at " << pingTimeoutStamp << " s" << endl;
    // TODO: Manage pingTimeoutMsg object lifecycle better
    pingTimeoutMsg = new cMessage("Preferred parent ping timeout", PING_TIMEOUT);
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
    EV_DETAIL << "Processing msg from trickle timer" << endl;
    switch (message->getKind()) {
        case TRICKLE_TRIGGER_EVENT: {
            if (trickleTimer->checkRedundancyConst()) {
                EV_DETAIL << "Redundancy OK, preparing to broadcast DIO" << endl;
                sendRplPacket(createDio(), DIO, Ipv6Address::ALL_ROUTERS_2);
            }
            break;
        }
        default: throw cRuntimeError("Unknown TrickleTimer message");
    }
    delete message;
}


void Rpl::processPacket(Packet *packet)
{
    EV_DETAIL << "Processing incoming packet " << packet << endl;
    auto rplHeader = packet->popAtFront<RplHeader>();
    EV_DETAIL << "Icmpv6 code - " << rplHeader->getIcmpv6Code() << endl;
    auto rplBody = packet->peekData<RplPacket>();
    auto senderAddr = rplBody->getSrcAddress();
    switch (rplHeader->getIcmpv6Code()) {
        case DIO: {
            processDio(packet, dynamicPtrCast<const Dio>(rplBody));
            break;
        }
        case DAO: {
            processDao(packet, dynamicPtrCast<const Dao>(rplBody));
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
            sendRplPacket(createDio(), PING_ACK, senderAddr, uniform(1, 3));
            EV_DETAIL << "Ping_ACK sent to " << senderAddr << endl;
            delete packet;
            break;
        }
        case PING_ACK: {
            EV_DETAIL << "Connectivity confirmed to pref. parent - " << senderAddr << endl;
            cancelEvent(pingTimeoutMsg);
            delete packet;
            break;
        }
        default: throw cRuntimeError("Unknown Rpl packet");
    }
}

void Rpl::sendPacket(cPacket *packet, double delay)
{
    if (delay == 0) {
        send(packet, "ipOut");
        EV_DETAIL << "Packet" << packet << " sent via ipOut" << endl;
    }
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
    dio->setChunkLength(b(192)); // TODO: Define DIO size as constant or via function
    dio->setMop(mop);
    dio->setRank(rank);
    dio->setDodagVersion(dodagVersion);
    dio->setDodagId(dodagId);
    dio->setSrcAddress(getSelfAddress());
    EV_DETAIL << "DIO packet created" << endl;
    return dio;
}

const Ptr<Dao> Rpl::createDao(const Ipv6Address &reachableDest)
{
    auto dao = makeShared<Dao>();
    // TODO[OPTIONAL]: expand with DAO optional fields as in RFC 6550
    dao->setInstanceId(instanceId);
    dao->setChunkLength(b(32)); // FIXME: replace with constant or function calculating DAO size
    dao->setSrcAddress(getSelfAddress());
    dao->setReachableDest(reachableDest);
    dao->setDaoAckRequired(false);
    EV_DETAIL << "DAO packet created, advertising reachable destination - "
            << reachableDest << endl;
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
    }
    trickleTimer->incrementCtrlMsgReceivedCounter();
    // Do not process DIO from unknown DAG/RPL instance or if receiver is root
    if (checkUnknownDio(dio) || isRoot) {
        EV_DETAIL << "Received DIO with unknown DODAG/RPL instance or node is root" << endl;
        return;
    }
    addNeighbour(dio);
    updatePreferredParent();
    delete packet;

}

void Rpl::processDao(Packet *packet, const Ptr<const Dao>& dao) {
    EV_INFO << "Processing RPL packet - DAO, from " << dao->getSrcAddress() << "\n"
            << "destination advertised - " << dao->getReachableDest() << endl;
    // store routing data contained in DAO
    if (mop == STORING || isRoot) {
        updateRoutingTable(dao->getSrcAddress(), dao->getReachableDest());
        EV_DETAIL << "New destination learned from DAO: " << dao->getReachableDest()
                << " reachable via " << dao->getSrcAddress() << endl;
    }

    EV_DETAIL << "Forwarding DAO to pref. parent, advertising "
            << dao->getReachableDest() << " reachability" << endl;
    if (dao->getDaoAckRequired())
        sendRplPacket(createDao(Ipv6Address::UNSPECIFIED_ADDRESS), DAO_ACK, dao->getSrcAddress());
    // TODO: Schedule DAO with proper delay
    if (preferredParent)
        sendRplPacket(createDao(dao->getReachableDest()), DAO,
                preferredParent->getSrcAddress(), uniform(2, 4));
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
    // if preferred parent has changed, update routing table
    if (checkPrefParentChanged(newPrefParent)) {
        updateRoutingTable(newPrefParent->getSrcAddress(), dodagId);
        EV_DETAIL << "Updated preferred parent to - " << newPrefParent->getSrcAddress() << endl;
        // TODO: Schedule DAO with proper delay
        sendRplPacket(createDao(getSelfAddress()), DAO, newPrefParent->getSrcAddress(), uniform(3, 5));
    }
    preferredParent = newPrefParent->dup();
    // schedule ping request to verify preferred parent connectivity
    scheduleAt(simTime() + pingDelay + dblrand() * pingDelay, new cMessage("", PING_TRIGGER));
    // recalculate rank
    auto oldRank = rank;
    rank = objectiveFunction->calcRank(preferredParent);
    if (oldRank != rank)
        EV_DETAIL << "Updated rank = " << rank << endl;
}

bool Rpl::checkPrefParentChanged(Dio* newPrefParent)
{
    return preferredParent == nullptr
            || newPrefParent->getSrcAddress() != preferredParent->getSrcAddress();
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

bool Rpl::deletePrefParentRoute()
{
    auto totalRoutes = routingTable->getNumRoutes();
    auto prefParentAddr = preferredParent->getSrcAddressForUpdate();
    for (int i = 0; i < totalRoutes; i++) {
        auto ri = routingTable->getRoute(i);
        if (ri->getNextHopAsGeneric().toIpv6() == prefParentAddr) {
            routingTable->deleteRoute(ri);
            EV_DETAIL << "Deleted route through preferred parent - " << prefParentAddr << endl;
            return true;
        }
    }
    EV_DETAIL << "No route through pref. parent " << prefParentAddr << " found" << endl;
    return false;
}

//
// Utility methods
//

Ipv6Address Rpl::getSelfAddress()
{
    return interfaceEntryPtr->getNetworkAddress().toIpv6();
}

bool Rpl::checkUnknownDio(const Ptr<const Dio>& dio)
{
//    EV_DETAIL << "Checking if DIO is from unknown DODAG/RPL instance" << endl;
//    EV_DETAIL << "DodagId = " << dodagId << "; advertisedDodagId = " << dio->getDodagId() << endl;
//    EV_DETAIL << "InstId = " << int(instanceId) << "; advertisedInstId = " << int(dio->getInstanceId()) << endl;
    return dio->getDodagId() != dodagId || dio->getInstanceId() != instanceId;
}

void Rpl::addNeighbour(const Ptr<const Dio>& dio)
{
    // Check if DIO sender is already in a neighboring set (candidate or backup parent)
    auto dioCopy = dio->dup();
    auto dioSender = dioCopy->getSrcAddressForUpdate();
    // TODO: Monitor backupParents and candidateParents map contents during simulation
    if (dio->getRank() == rank) {
        if (backupParents.find(dioSender) != backupParents.end())
            EV_DETAIL << "Backup parent entry updated, address - " << dioSender << endl;
        else
            EV_DETAIL << "New backup parent added, address - " << dioSender << endl;
        backupParents[dioSender] = dioCopy;
    }
    if (dio->getRank() < rank) {
        if (candidateParents.find(dioSender) != backupParents.end())
            EV_DETAIL << "Candidate parent entry updated, address - " << dioSender << endl;
        else
            EV_DETAIL << "New candidate parent added, address - " << dioSender << endl;
        candidateParents[dioSender] = dioCopy;
    }
}

//
// notification handling
//

void Rpl::receiveSignal(cComponent *source, simsignal_t signalID, cObject *obj, cObject *details)
{
    Enter_Method("receiveChangeNotification");
    EV_DETAIL << "Processing signal - " << signalID << endl;
    if (signalID == linkBrokenSignal) {
        EV_WARN << "Received link break" << endl;
        Packet *datagram = check_and_cast<Packet *>(obj);
        const auto& networkHeader = findNetworkProtocolHeader(datagram);
        if (networkHeader != nullptr) {
            const L3Address& destination = networkHeader->getDestinationAddress();
            EV_DETAIL << "Connection with destination " << destination << " broken?" << endl;
            // TODO: Implement node unreachability handling
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




