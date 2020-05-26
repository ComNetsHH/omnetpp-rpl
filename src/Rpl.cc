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
    interfaceEntryPtr(nullptr),
    rank(INF_RANK),
    dodagVersion(0),
    mop(NON_STORING),
    prefParentAddr(Ipv6Address::UNSPECIFIED_ADDRESS),
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
        isRoot = par("isRoot").boolValue();
        trickleTimer = check_and_cast<TrickleTimer *>(getModuleByPath("^.trickleTimer"));
        objectiveFunctionType = par("objectiveFunctionType").stdstringValue();
        objectiveFunction = new ObjectiveFunction(objectiveFunctionType);
    }
    else if (stage == INITSTAGE_ROUTING_PROTOCOLS) {
        registerService(Protocol::manet, nullptr, gate("ipIn"));
        registerProtocol(Protocol::manet, gate("ipOut"), nullptr);
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

//     TODO: Use proper multicast group
//     interfaceEntryPtr->getProtocolData<Ipv6InterfaceData>()->joinMulticastGroup(LL_RPL_MULTICAST);
    if (isRoot) {
        trickleTimer->start();
        rank = ROOT_RANK;
        dodagId = getSelfAddress();
    }
}

void Rpl::stop()
{
    delete packetInProcessing;
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


void Rpl::processSelfMessage(cMessage *message) {
    EV_DETAIL << "Processing self-msg" << message->getFullName() << endl;
}

//
// Handling generic packets
//

void Rpl::processMessage(cMessage *message)
{
    std::string arrivalGateName = std::string(message->getArrivalGate()->getBaseName());
    if (arrivalGateName.compare("ttModule") == 0) {
        processTrickleTimerMsg(message);
    }
    else if (Packet *fp = dynamic_cast<Packet *>(message)) {
        packetInProcessing = fp;
        processPacket(fp);
    }
    else
        throw cRuntimeError("Unknown message");
}

void Rpl::processTrickleTimerMsg(cMessage *message)
{
    EV_DETAIL << "Processing msg from trickle timer " << endl;
    switch (message->getKind()) {
        case TRICKLE_TRIGGER_EVENT: {
            EV_DETAIL << "Trickle trigger event" << endl;
            if (trickleTimer->checkRedundancyConst())
                EV_DETAIL << "Redundancy OK, preparing to broadcast DIO" << endl;
                sendRplPacket(createDio(), nullptr, Ipv6Address::ALL_ROUTERS_2, 0);
            break;
        }
        default: throw cRuntimeError("Unknown TrickleTimer message");
    }
}


void Rpl::processPacket(Packet *packet)
{
    auto rplHeader = packet->popAtFront<RplHeader>();
    processRplPacket(packet, packet->peekDataAt<RplPacket>(b(0), packet->getDataLength()),
            rplHeader->getIcmpv6Code());
}


void Rpl::sendPacket(cPacket *packet, double delay)
{
    if (delay == 0) {
        packetInProcessing = packet;
        send(packet, "ipOut");
        EV_DETAIL << packet << " sent via ipOut" << endl;
    }
    else
        sendDelayed(packet, delay, "ipOut");
}

//
// Handling RPL packets
//

void Rpl::sendRplPacket(const Ptr<RplPacket>& body, const InterfaceEntry *interfaceEntry,
        const L3Address& nextHop, double delay)
{
    Packet *pkt = new Packet("inet::RplPacket");
    auto header = makeShared<RplHeader>();
    header->setIcmpv6Code(getIcmpv6CodeByClassname(body));
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

void Rpl::processRplPacket(Packet *packet, const Ptr<const RplPacket>& rplPacket, RplPacketCode code)
{
    EV_DETAIL << "";
    auto rplPacketCopy = rplPacket->dupShared();
    switch (code) {
        case DIO: {
            processDio(packet, dynamicPtrCast<const Dio>(rplPacketCopy));
            break;
        }
        case DAO: {
            processDao(packet, dynamicPtrCast<const Dao>(rplPacketCopy));
            break;
        }
        case DIS: {
            processDis(packet, dynamicPtrCast<const Dis>(rplPacketCopy));
            break;
        }
        default: throw cRuntimeError("Unknown Rpl packet");
    }
}


const Ptr<Dio> Rpl::createDio()
{
    auto dio = makeShared<Dio>();
    EV_DETAIL << "DIO packet created" << endl;

    dio->setInstanceId(instanceId);
    dio->setChunkLength(b(192)); // TODO: Determine actual DIO packet size
    dio->setMop(mop);
    dio->setRank(rank);
    dio->setDodagVersion(dodagVersion);
    dio->setDodagId(dodagId);
    dio->setSrcAddress(getSelfAddress());

    return dio;
}

void Rpl::processDio(Packet *packet, const Ptr<const Dio>& dio)
{
    EV_INFO << "Processing RPL packet - DIO, from " << dio->getSrcAddress() << endl;
    trickleTimer->incrementDioReceivedCounter();
    // Join the DODAG advertised, if node's not a part of any DODAG yet
    if (dodagId == Ipv6Address::UNSPECIFIED_ADDRESS) {
        dodagId = dio->getDodagId();
        instanceId = dio->getInstanceId();
        mop = dio->getMop();
        // Start broadcasting DIOs
        trickleTimer->start();
    }
    // Do not process DIO from unknown DAG/RPL instance or if receiver is root
    if (checkUnknownDio(dio) || isRoot) {
        EV_DETAIL << "Received DIO with unknown DODAG/RPL instance or node is root" << endl;
        return;
    }
    addNodeAsNeighbour(dio);
    preferredParent = objectiveFunction->getPreferredParent(candidateParents);
    auto oldRank = rank;
    rank = objectiveFunction->calcRank(preferredParent);
    if (oldRank != rank)
        EV_DETAIL << "Updated rank = " << rank << endl;
    // If previously stored preferred parent's address has changed, update routing table
    // TODO: Check pref. parent reachability - if fails, use nodes from candidate/backup sets
    if (prefParentAddr != preferredParent->getSrcAddressForUpdate())
        updateRoutingTable();
}

void Rpl::processDao(Packet *packet, const Ptr<const Dao>& daoPacket) {
    EV_INFO << "Processing RPL packet - DAO" << endl;
}
void Rpl::processDis(Packet *packet, const Ptr<const Dis>& disPacket) {
    EV_INFO << "Processing RPL packet - DIS" << endl;
}

//
// Handling routing data
//

void Rpl::updateRoutingTable() {
    Ipv6Route *route = new Ipv6Route(dodagId, prefixLength, IRoute::MANET);
    route->setNextHop(preferredParent->getSrcAddress());
    route->setDestination(dodagId);
    route->setInterface(interfaceEntryPtr);
    routingTable->addRoute(route);
    prefParentAddr = preferredParent->getSrcAddressForUpdate();
    EV_DETAIL << "Updated preferred parent to - " << prefParentAddr << endl;
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

void Rpl::addNodeAsNeighbour(const Ptr<const Dio>& dio)
{
    // Check if DIO sender is already in a neighboring set (candidate or backup parent)
    auto dioCopy = dio->dup();
    auto dioSender = dioCopy->getSrcAddress();
    // TODO: Monitor backupParents and candidateParents map contents during simulation
    if (dio->getRank() == rank) {
        if (backupParents.count(dioSender) != 0)
            EV_DETAIL << "Backup parent entry updated, address - " << dioSender << endl;
        else
            EV_DETAIL << "New backup parent added, address - " << dioSender << endl;
        backupParents[dioSender] = dioCopy;
    }
    if (dio->getRank() < rank) {
        if (candidateParents.count(dioSender) != 0)
            EV_DETAIL << "Candidate parent entry updated, address - " << dioSender << endl;
        else
            EV_DETAIL << "New candidate parent added, address - " << dioSender << endl;
        candidateParents[dioSender] = dioCopy;
    }
}

RplPacketCode Rpl::getIcmpv6CodeByClassname(const Ptr<RplPacket>& packet)
{
    std::string classname = std::string(packet->getClassName());
    if (classname.find(std::string("Dio")) != std::string::npos)
        return DIO;
    else if (classname.find(std::string("Dis")) != std::string::npos)
        return DIS;
    else if (classname.find(std::string("Dao")) != std::string::npos)
        return DAO;
    else
        throw cRuntimeError("Unknown RPL control message");
}

} // namespace inet




