
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
#include "Rpl.h"
#include "Rpl_m.h"

namespace inet {

Define_Module(Rpl);

//
// construction
//

Rpl::Rpl() :
    interfaces(nullptr),
    interfaceTable(nullptr),
    routingTable(nullptr),
    networkProtocol(nullptr),
    isRoot(false)
{
}

Rpl::~Rpl()
{

}

//
// module interface
//

void Rpl::initialize(int stage)
{
    EV_DETAIL << "Initialization stage: " << stage << endl;

    if (stage == INITSTAGE_ROUTING_PROTOCOLS) {

    }

    RoutingProtocolBase::initialize(stage);

    if (stage == INITSTAGE_LOCAL) {
        interfaceTable = getModuleFromPar<IInterfaceTable>(par("interfaceTableModule"), this);
        routingTable = getModuleFromPar<IRoutingTable>(par("routingTableModule"), this);
        networkProtocol = getModuleFromPar<INetfilter>(par("networkProtocolModule"), this);
        isRoot = par("isRoot").boolValue();
    }
    else if (stage == INITSTAGE_ROUTING_PROTOCOLS) {
        registerService(Protocol::manet, nullptr, gate("ipIn"));
        registerProtocol(Protocol::manet, gate("ipOut"), nullptr);
    }
}

void Rpl::handleMessageWhenUp(cMessage *message)
{
    EV_DETAIL << "Handling message" << message << endl;
    if (message->isSelfMessage())
        processSelfMessage(message);
    else
        processMessage(message);
}

//
// handling messages
//

void Rpl::processSelfMessage(cMessage *message)
{
    EV_DETAIL << "Processing self message" << endl;
    if (isRoot)
        sendRplPacket(createDio(), nullptr, Ipv6Address::LL_MANET_ROUTERS, 0);
    scheduleAt(simTime() + 2, new cMessage("Another DIO multicast triggered"));
}

void Rpl::sendRplPacket(const Ptr<RplPacket>& packet, const InterfaceEntry *interfaceEntry,
        const L3Address& nextHop, double delay)
{
    auto className = packet->getClassName();
    EV_DETAIL << "RPL packet transmission started: " << endl << " packet classname - " << className << endl;
    Packet *udpPacket = new Packet(!strncmp("inet::", className, 6) ? className + 6 : className);
    auto udpHeader = makeShared<UdpHeader>();
    udpPacket->addTag<PacketProtocolTag>()->setProtocol(&Protocol::manet);
    udpHeader->setSourcePort(269);
    udpHeader->setDestinationPort(269);
    udpHeader->setCrcMode(CRC_DISABLED);
    udpPacket->addTag<DispatchProtocolReq>()->setProtocol(&Protocol::ipv6);
    if (interfaceEntry)
        udpPacket->addTag<InterfaceReq>()->setInterfaceId(interfaceEntry->getInterfaceId());
    auto addresses = udpPacket->addTag<L3AddressReq>();
    EV_DETAIL << "Self address: " << getSelfAddress();
    addresses->setSrcAddress(getSelfAddress());
    EV_DETAIL << "Next hop: " << nextHop;
    addresses->setDestAddress(nextHop);
//    udpPacket->addTag<HopLimitReq>()->setHopLimit(255);
    udpPacket->insertAtFront(udpHeader);
    udpPacket->insertAtBack(packet);
    sendUdpPacket(udpPacket, delay);
}


Ipv6Address Rpl::getSelfAddress()
{
    return routingTable->getRouterIdAsGeneric().toIpv6();
}


const Ptr<DioMsg> Rpl::createDio() {
    auto dioMsg = makeShared<DioMsg>();
    EV_DETAIL << "DIO packet created" << endl;

    // TODO: Fill DIO packet contents
    dioMsg->setInstanceId(0);
    dioMsg->setMop(NON_STORING);

    return dioMsg;
}

void Rpl::processMessage(cMessage *message)
{
    if (Packet *fp = dynamic_cast<Packet *>(message))
        processUdpPacket(fp);
    else
        throw cRuntimeError("Unknown message");
}


//
// handling Udp packets
//

void Rpl::sendUdpPacket(cPacket *packet, double delay)
{
    if (delay == 0) {
        send(packet, "ipOut");
    }
    else
        sendDelayed(packet, delay, "ipOut");
}

void Rpl::processUdpPacket(Packet *packet)
{

    packet->popAtFront<UdpHeader>();
//    processDymoPacket(packet, packet->peekDataAt<DymoPacket>(b(0), packet->getDataLength()));
    delete packet;
}

void Rpl::stop() {

}

void Rpl::start() {
    EV_DETAIL << "Start method invoked by " + std::string(isRoot ? "root" : "non-root") << endl;

    if (isRoot) {
        cMessage *startMsg = new cMessage("First DIO transmission triggered by root");
        scheduleAt(simTime() + 2, new cMessage("Another DIO broadcast triggered"));
    }

}


}




