
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
    interfaces(nullptr),
    interfaceTable(nullptr),
    routingTable(nullptr),
    networkProtocol(nullptr),
    isRoot(false),
    interfaceEntryPtr(nullptr),
    rank(INF_RANK),
    dodagVersion(0),
    trickleTimerEvent(nullptr),
    trickleTimerDelay(2),
    packetInProcessing(nullptr)
{
}

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
        trickleTimerDelay = 2; // TODO: Implement Trickle timer
        trickleTimerEvent = new cMessage("DIO multicast triggered");
    }
    else if (stage == INITSTAGE_ROUTING_PROTOCOLS) {
        registerService(Protocol::manet, nullptr, gate("ipIn"));
        registerProtocol(Protocol::manet, gate("ipOut"), nullptr);
    }
}

//
// Lifecycle operations
//

void Rpl::start() {
    InterfaceEntry* ie = nullptr;

    // set network interface entry pointer (TODO: Update for 802.15.4 NIC)
    for (int i = 0; i < interfaceTable->getNumInterfaces(); i++)
    {
        ie = interfaceTable->getInterface(i);
        if (strstr(ie->getInterfaceName(), "wlan") != nullptr)
        {
            interfaceEntryPtr = ie;
            break;
        }
    }

//    interfaceEntryPtr->getProtocolData<Ipv6InterfaceData>()->joinMulticastGroup(LL_RPL_MULTICAST);
    if (isRoot) {
        rank = 1;
        scheduleAt(simTime() + trickleTimerDelay, trickleTimerEvent);
    } else {
        EV_DETAIL << "start() invoked by non-root" << endl;
    }
}

void Rpl::stop() {
    cancelAndDelete(trickleTimerEvent);
    //    TODO: Implement proper destructor, simple delete causes error during exit
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
    EV_DETAIL << "Processing self message" << endl;
    if (isRoot) {
        // TODO: Set link-local multicast address as in RFC6550
        sendRplPacket(createDio(), nullptr, Ipv6Address::ALL_ROUTERS_2, 0);
        scheduleAt(simTime() + trickleTimerDelay, trickleTimerEvent);
    }
}

//
// Handling generic packets
//

void Rpl::processMessage(cMessage *message)
{
    EV_DETAIL << "processMessage() invoked" << endl;
    if (Packet *fp = dynamic_cast<Packet *>(message)) {
        packetInProcessing = fp;
        processPacket(fp);
    }
    else
        throw cRuntimeError("Unknown message");
}


void Rpl::processPacket(Packet *packet)
{
    EV_DETAIL << "processPacket invoked" << endl;
    auto rplHeader = packet->popAtFront<RplHeader>();
    processRplPacket(packet, packet->peekDataAt<RplPacket>(b(0), packet->getDataLength()),
            rplHeader->getIcmpv6Code());
}


void Rpl::sendPacket(cPacket *packet, double delay)
{
    if (delay == 0) {
        packetInProcessing = packet;
        send(packet, "ipOut");
        EV_DETAIL << "packet " << packet << " sent via ipOut";
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

    EV_INFO << "Processing rpl packet - ";
    auto rplPacketCopy = rplPacket->dupShared();
    switch (code) {
        case DIO: {
            EV_INFO << "DIO" << endl;
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


const Ptr<Dio> Rpl::createDio() {
    auto dio = makeShared<Dio>();
    EV_DETAIL << "DIO packet created" << endl;

    // TODO: Fill DIO packet with relevant data
    dio->setInstanceId(0);
    dio->setChunkLength(b(192));
    dio->setMop(NON_STORING);
    dio->setRank(rank);
    dio->setDodagVersion(dodagVersion);


    return dio;
}


void Rpl::processDio(Packet *packet, const Ptr<const Dio>& dioPacket)
{
    auto advertisedRank = dioPacket->getRank();
    // if node is not a part of DODAG, compute own rank based on advertised
    if (rank == INF_RANK)
        rank = advertisedRank + 1;
    else if (advertisedRank < rank) {
        EV_DETAIL << "Updating preferred parent";
        addParent(dioPacket->getSrcAddress() , true);
    }

    else if (advertisedRank == rank) {
        EV_DETAIL << "Updating backup parent";
        addParent(dioPacket->getSrcAddress(), false);
    }
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

void Rpl::addParent(const Ipv6Address& srcAddr, bool preferred) {}

RplPacketCode Rpl::getIcmpv6CodeByClassname(const Ptr<RplPacket>& packet) {
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




