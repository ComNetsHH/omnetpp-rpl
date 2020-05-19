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
    prefixLength(64),
//    preferredParent(nullptr),
//    backupParent(nullptr),
//    dodagId(nullptr),
//    objectiveFunctionType(nullptr),
//    objectiveFunctionPtr(nullptr),
    packetInProcessing(nullptr)
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
        trickleTimer = new TrickleTimer();
        isRoot = par("isRoot").boolValue();
        objectiveFunctionType = par("objectiveFunctionType").stdstringValue();
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

    objectiveFunctionPtr = new ObjectiveFunction(objectiveFunctionType);
    trickleTimer->setRoutingModulePtr( dynamic_cast<cSimpleModule *> (this) );

//     TODO: Use proper multicast group
//     interfaceEntryPtr->getProtocolData<Ipv6InterfaceData>()->joinMulticastGroup(LL_RPL_MULTICAST);
    if (isRoot) {
        trickleTimer->start();
        rank = 1;
    }
}

void Rpl::stop()
{
    //    TODO: Implement proper destructor, simple delete causes errors at finish()
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


void Rpl::processSelfMessage(cMessage *message)
{
    switch (message->getKind()) {
        case TRICKLE_TRIGGER_EVENT: {
            EV_DETAIL << "Processing self msg 'TRICKLE TRIGGER EVENT' " << endl;
            if (trickleTimer->checkRedundancyConst())
                sendRplPacket(createDio(), nullptr, Ipv6Address::ALL_ROUTERS_2, 0);
            trickleTimer->scheduleNext();
            break;
        }
        default: {
            throw cRuntimeError("Unknown kind of self message");
        }
    }
}

//
// Handling generic packets
//

void Rpl::processMessage(cMessage *message)
{
    if (Packet *fp = dynamic_cast<Packet *>(message)) {
        packetInProcessing = fp;
        processPacket(fp);
    }
    else
        throw cRuntimeError("Unknown message");
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

    EV_INFO << "Processing Rpl packet - ";
    auto rplPacketCopy = rplPacket->dupShared();
    switch (code) {
        case DIO: {
            EV_INFO << "DIO" << endl;
            trickleTimer->incrementDioReceivedCounter();
            processDio(packet, dynamicPtrCast<const Dio>(rplPacketCopy));
            break;
        }
        case DAO: {
            EV_INFO << "DAO" << endl;
            processDao(packet, dynamicPtrCast<const Dao>(rplPacketCopy));
            break;
        }
        case DIS: {
            EV_INFO << "DIS" << endl;
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

    // TODO: Fill DIO packet with relevant data
    dio->setInstanceId(0);
    dio->setChunkLength(b(192));
    dio->setMop(NON_STORING);
    dio->setRank(rank);
    dio->setDodagVersion(dodagVersion);
    dio->setDodagId(getSelfAddress());
    dio->setSrcAddress(getSelfAddress());

    return dio;
}


void Rpl::processDio(Packet *packet, const Ptr<const Dio>& dioPacket)
{
    // Join DODAG, if node's not a part of any DODAG yet
    if (dodagId == nullptr) {
        dodagId = new Ipv6Address(dioPacket->getDodagId());
    }
    // Take no action if DIO received from an unknown DODAG
    if (dioPacket->getDodagId() != *dodagId)
        return;
    if (dioPacket->getRank() <= rank) {
        // TODO: Check if DIO sender is not already present in neighbors' set
        neighborSet.push_back(dioPacket->dup());
    }
    // TODO: Handle various cases for parent selection and rank computation properly as in RFC6550
    if (objectiveFunctionPtr != nullptr) {
        auto preferredParentDio = objectiveFunctionPtr->getPreferredParent(neighborSet);
        preferredParent = new Ipv6Address(preferredParentDio->getSrcAddress());
        rank = objectiveFunctionPtr->computeRank(preferredParentDio->getRank());
        updateRoutingTable(true);

        EV_DETAIL << "Updated preferred parent to - " << preferredParent << endl;
        EV_DETAIL << "Updated rank = " << rank << endl;
    } else
        EV_WARN << "Objective function uninitialized, no route updates performed" << endl;

}


void Rpl::processDao(Packet *packet, const Ptr<const Dao>& daoPacket) {}
void Rpl::processDis(Packet *packet, const Ptr<const Dis>& disPacket) {}

//
// Utility methods
//

Ipv6Address Rpl::getSelfAddress()
{
    return interfaceEntryPtr->getNetworkAddress().toIpv6();
}

void Rpl::updateRoutingTable(bool updatePreferred) {
    EV_DETAIL << std::string("Updating routing table with ")
        + std::string(updatePreferred ? "preferred" : "backup") + std::string(" parent") << endl;
    Ipv6Route *route = new Ipv6Route(*dodagId, prefixLength, IRoute::MANET);
    route->setNextHop(updatePreferred ? *preferredParent : *backupParent);
    route->setDestination(*dodagId);
    route->setInterface(interfaceEntryPtr);
    addRouteIfNotPresent(route, routingTable);

//    routingTable->printRoutingTable();
}

void Rpl::addRouteIfNotPresent(IRoute *route, IRoutingTable *rt) {
    for (auto i = 0; i < rt->getNumRoutes(); i++) {
        if (equalRoutes(route, rt->getRoute(i))) {
            EV_DETAIL << "Route already present in routing table" << endl;
            return;
        }
    }
    rt->addRoute(route);
}

bool Rpl::equalRoutes(IRoute *r1, IRoute *r2) {
    return r1->getDestinationAsGeneric() == r2->getDestinationAsGeneric()
            && r1->getInterface() == r2->getInterface()
            && r1->getNextHopAsGeneric() == r2->getNextHopAsGeneric();
}

//void Rpl::printRoutes(IRoutingTable* rt) {
//    Ipv6Route *entry;
//    EV_DETAIL << "Routing table: " << endl;
//    for (int i = rt->getNumRoutes() - 1; i >= 0; i--)
//    {
//        entry = dynamic_cast<Ipv6Route *>(rt->getRoute(i));
//        EV_DETAIL << entry << endl;
////
////        const InterfaceEntry *ie = entry->getInterface();
////        if (strstr(ie->getInterfaceName(), "wlan") != nullptr)
////            rt->deleteRoute(entry);
//    }
//}

RplPacketCode Rpl::getIcmpv6CodeByClassname(const Ptr<RplPacket>& packet)
{
    std::string classname = std::string(packet->getClassName());
    EV_DETAIL << "Processing packet class name - " << classname << endl;

    if (classname.find(std::string("Dio")) != std::string::npos)
        return DIO;
    else if (classname.find(std::string("Dis")) != std::string::npos)
        return DIS;
    else if (classname.find(std::string("Dao")) != std::string::npos)
        return DAO;
    else
        throw cRuntimeError("Unknown code of RPL control message");
}


} // namespace inet




