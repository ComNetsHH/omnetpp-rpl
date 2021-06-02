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

#include <deque>
#include <numeric>
#include <string>
#include <random>
#include <algorithm>
#include <regex>
#include <math.h>

#include "Rpl.h"

namespace inet {

Define_Module(Rpl);

std::ostream& operator<<(std::ostream& os, std::vector<Ipv6Address> &list)
{
    for (auto const &addr: list) {
        os << addr << ", " << endl;
    }

    return os;
}

std::ostream& operator<<(std::ostream& os, std::map<Ipv6Address, std::pair<cMessage *, uint8_t>> acks)
{
    for (auto a : acks)
        os << a.first << " (" << a.second.second << " attempts), timeout event - " << a.second.first << endl;
    return os;
}

// TODO: Store DODAG-related info in a separate object
// TODO: Refactor utility functions out

Rpl::Rpl() :
    isRoot(false),
    dodagId(Ipv6Address::UNSPECIFIED_ADDRESS),
    daoDelay(DEFAULT_DAO_DELAY),
    hasStarted(false),
    daoAckTimeout(10),
    daoRtxCtn(0),
    detachedTimeout(2), // manually suppressing previous DODAG info [RFC 6550, 8.2.2.1]
    daoSeqNum(0),
    prefixLength(128),
    preferredParent(nullptr),
    objectiveFunctionType("hopCount"),
    dodagColor(cFigure::BLACK),
    pUnreachabilityDetectionEnabled(false),
    floating(false),
    prefParentConnector(nullptr),
    numDaoDropped(0),
    udpPacketsRecv(0),
    isLeaf(false),
    numParentUpdates(0),
    numDaoForwarded(0)
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

        EV_DETAIL << "got interface table - " << interfaceTable << endl;

        networkProtocol = getModuleFromPar<INetfilter>(par("networkProtocolModule"), this);
        nd = check_and_cast<Ipv6NeighbourDiscovery*>(getModuleByPath("^.ipv6.neighbourDiscovery"));

        routingTable = getModuleFromPar<Ipv6RoutingTable>(par("routingTableModule"), this);
        trickleTimer = check_and_cast<TrickleTimer*>(getModuleByPath("^.trickleTimer"));

        daoEnabled = par("daoEnabled").boolValue();
        host = getContainingNode(this);

        objectiveFunction = new ObjectiveFunction(par("objectiveFunctionType").stdstringValue());
        objectiveFunction->setMinHopRankIncrease(par("minHopRankIncrease").intValue());
        daoRtxThresh = par("numDaoRetransmitAttempts").intValue();
        allowDodagSwitching = par("allowDodagSwitching").boolValue();
        pDaoAckEnabled = par("daoAckEnabled").boolValue();
        pUseWarmup = par("useWarmup").boolValue();
        pUnreachabilityDetectionEnabled = par("unreachabilityDetectionEnabled").boolValue();
        pShowBackupParents = par("showBackupParents").boolValue();

        // statistic signals
        dioReceivedSignal = registerSignal("dioReceived");
        daoReceivedSignal = registerSignal("daoReceived");
        parentUnreachableSignal = registerSignal("parentUnreachable");
        parentChangedSignal = registerSignal("parentChanged");
        rankUpdatedSignal = registerSignal("rankUpdated");

        startDelay = par("startDelay").doubleValue();

        if (par("layoutConfigurator").boolValue())
            generateLayout(host->getParentModule()); // generate layout using the topmost simulation module

        WATCH(numParentUpdates);
        WATCH_MAP(candidateParents);
        WATCH_MAP(backupParents);
        WATCH(rank);
        WATCH(selfAddr);
        WATCH(selfId);
    }
    else if (stage == INITSTAGE_ROUTING_PROTOCOLS) {
        registerService(Protocol::manet, nullptr, gate("ipIn"));
        registerProtocol(Protocol::manet, gate("ipOut"), nullptr);
        host->subscribe(linkBrokenSignal, this);
        networkProtocol->registerHook(0, this);
    }
}

void Rpl::generateLayout(cModule *net) {
    auto sink = net->getSubmodule("sink", 0);
    auto numSinks = net->par("numSinks").intValue();
    if (!sink)
        throw cRuntimeError("Couldn't find sink[0] module during layout configuration");

    auto sinkPos = sink->getSubmodule("mobility");

    Coord *anchor = new Coord(sinkPos->par("initialX").doubleValue(), sinkPos->par("initialY").doubleValue());
    double padX = par("padX").doubleValue();
    double padY = par("padY").doubleValue();
    double gap = par("gridColumnsGapMultiplier").doubleValue();

    // optional
    bool bLayout = par("branchesLayout").boolValue();
    int numBranches = par("numBranches").intValue();
    int numHosts = net->par("numHosts").intValue();

    configureTopologyLayout(*anchor, padX, padY, gap, net, bLayout, numBranches, numHosts, numSinks);
}


void Rpl::configureTopologyLayout(Coord anchorPoint, double padX, double padY, double columnGapMultiplier,
        cModule *network, bool branchLayout, int numBranches, int numHosts, int numSinks)
{
    EV_DETAIL << "Configuring topology for " << boolStr(branchLayout, "branch", "seatbelt") << " layout" << endl;

    int i = 1;
    int k = 1;
    int l = 0;
    int m = 0;
    int hopCtn = 0;
    int middleBranchId = (int) floor(numBranches / 2);
    int nodesPerBranch = branchLayout ? (int) ceil(numHosts / numBranches) : 0;

    double skew = numBranches % 2 == 0 ? 1.5 : 1.2;
    int numHops = par("numHops").intValue();

    double currentX = anchorPoint.x - (branchLayout ? (numBranches * skew) : columnGapMultiplier) * padX;
    double defaultY = branchLayout ? (anchorPoint.y + padY) : anchorPoint.y;
    double currentY = defaultY;

    // Shift starting location if we have an even number of sinks
    double bias = (!branchLayout && numSinks % 2 == 0) ? padY * numHosts / 16 : 0;

    for (cModule::SubmoduleIterator it(network); !it.end(); ++it) {
        cModule *subm = *it;
        std::string sname(subm->getFullName());
        auto nodeId = getNodeId(sname);
        auto subMobility = subm->getSubmodule("mobility");

        if (branchLayout) {
            if (nodeId <= 0)
                continue;

            subMobility->par("initialX") = currentX;
            subMobility->par("initialY") = currentY;

            if (currentY == defaultY) {
                // allow branch roots join the sink directly, since they are closest to it
                subm->getSubmodule("rpl")->par("allowJoinAtSink") = true;
                hopCtn = 1;
                if (m != (int) ceil(numBranches / 2)) {
                    auto drift = ((m < middleBranchId ? padX : -padX) * pow((m - middleBranchId + 0.0001) / 1.3, 2));
                    subMobility->par("initialX") = currentX + drift;
                } else
                    k = numBranches % 2;
                m++;
            } else {
                subm->getSubmodule("rpl")->par("allowJoinAtSink") = false;
                subm->getSubmodule("rpl")->par("assignParentManual") = true;
            }

            currentX += pow(-1, k++);
            // switching to next branch
            if (nodeId % nodesPerBranch == 0) {
                currentX += padX * columnGapMultiplier;
                EV_DETAIL << "Switching to next branch, updated currentX to " << currentX << endl;
                currentY = defaultY;
            } else
                currentY += padY;

            if (numHops > 0) {
                subm->par("numApps") = hopCtn == numHops ? 1 : 0;
                hopCtn++;
            }

        } else {
            if (sname.find("sink") != std::string::npos) {
                double initialY = anchorPoint.y + pow(-1, k++) * padY * l * numHosts / (numSinks * 2.5) - bias;
                if (l == 0 || k % 2 == 0)
                    l++;
                subMobility->par("initialY") = initialY;
                subMobility->par("initialX") = anchorPoint.x;
            } else {
                if (nodeId <= 0)
                    continue;

                subMobility->par("initialX") = currentX;
                subMobility->par("initialY") = currentY;
                // switching to next row
                if (nodeId % 6 == 0) {
                    currentX = anchorPoint.x - columnGapMultiplier * padX;
                    currentY = anchorPoint.y + pow(-1, i) * padY * i;
                    i++;
                    // 'jumping' over the aircraft central hallway
                } else if (nodeId % 3 == 0)
                    currentX += columnGapMultiplier * padX;
                else
                    currentX += padX;
            }
        }
    }
}


int Rpl::getNodeId(std::string nodeName) {
    const std::regex base_regex("host\\[([0-9]+)\\]");
    std::smatch base_match;

    if (std::regex_match(nodeName, base_match, base_regex)) {
        // The first sub_match is the whole string; the next
        // sub_match is the first parenthesized expression.
        if (base_match.size() == 2) {
            std::ssub_match base_sub_match = base_match[1];
            std::string base = base_sub_match.str();
            return std::stoi(base);
        }
    }
    return -1;
}

cFigure::Color Rpl::pickRandomColor() {
    auto palette = selfId % 2 == 0 ? cFigure::GOOD_DARK_COLORS : cFigure::GOOD_LIGHT_COLORS;
    auto numColors = selfId % 2 == 0 ? cFigure::NUM_GOOD_DARK_COLORS : cFigure::NUM_GOOD_LIGHT_COLORS;
    return host->getParentModule()->par("numSinks").intValue() == 1 ?  cFigure::BLACK : palette[intrand(numColors, 0) - 1];
}

cModule* Rpl::findSubmodule(std::string sname, cModule *host) {
    for (cModule::SubmoduleIterator it(host); !it.end(); ++it) {
        cModule *subm = *it;
        std::string s(subm->getFullName());

        if (s.find(sname) != std::string::npos)
            return *it;
    }
    return nullptr;
}

std::vector<int> Rpl::pickRandomly(int total, int numRequested) {
    // shuffle the array
    std::random_device rd;
    std::mt19937 e{rd()};

    std::vector<int> values(total);

    std::iota(values.begin(), values.end(), 1);

    std::shuffle(values.begin(), values.end(), e);

    // copy the picked ones
    std::vector<int> picked(numRequested);
//    for (auto i = 0; i < numRequested; i++)
//        picked.push_back(inputVec[i]);

    std::copy(values.begin(), values.begin() + numRequested, picked.begin());
    return picked;
}

//
// Lifecycle operations
//

void Rpl::start()
{
    InterfaceEntry* ie = nullptr;

    if (startDelay > 0) {
        scheduleAt(simTime() + startDelay, new cMessage("", RPL_START));
        return;
    }
    hasStarted = true;

    isRoot = par("isRoot").boolValue(); // Initialization of this parameter should be here to ensure
                                        // multi-gateway configurator will have time to assign 'root' roles
                                        // to randomly chosen nodes
    position = *(new Coord());

    // set network interface entry pointer (TODO: Update for IEEE 802.15.4)
    for (int i = 0; i < interfaceTable->getNumInterfaces(); i++)
    {
        ie = interfaceTable->getInterface(i);
        if (strstr(ie->getInterfaceName(), "wlan") != nullptr)
        {
            interfaceEntryPtr = ie;
            EV_DETAIL << "Interface #" << i << " set as IE pointer" << endl;
            break;
        }
    }

    selfId = interfaceTable->getInterface(1)->getMacAddress().getInt();
    auto mobility = check_and_cast<IMobility*> (getParentModule()->getSubmodule("mobility"));
    position = mobility->getCurrentPosition();

    udpApp = host->getSubmodule("app", 0); // TODO: handle more apps

    rank = INF_RANK - 1;
    detachedTimeoutEvent = new cMessage("", DETACHED_TIMEOUT);
    selfAddr = getSelfAddress();

    if (isRoot && !par("disabled").boolValue()) {
        trickleTimer->start(pUseWarmup, par("numSkipTrickleIntervalUpdates").intValue());
        dodagColor = pickRandomColor();
        rank = ROOT_RANK;
        dodagVersion = DEFAULT_INIT_DODAG_VERSION;
        instanceId = RPL_DEFAULT_INSTANCE;
        dtsn = 0;
        storing = par("storing").boolValue();
        if (udpApp)
            udpApp->subscribe("packetReceived", this);
    }
}

void Rpl::refreshDisplay() const {
    if (isRoot) {
        host->getDisplayString().setTagArg("t", 0, std::string(" num rcvd: " + std::to_string(udpPacketsRecv)).c_str());
        host->getDisplayString().setTagArg("t", 1, "l"); // set display text position to 'left'
    }
    // Draw dashed lines to visualize backup parents
//    if (pShowBackupParents)
//        for (auto bp : backupParents)
//            drawConnector(bp.second->getPosition(), bp.second->getColor(), bp.first);
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
        case RPL_START: {
            startDelay = 0;
            start();
            break;
        }
        case DAO_ACK_TIMEOUT: {
            Ipv6Address *advDest = (Ipv6Address*) message->getContextPointer();
            if (pendingDaoAcks.find(*advDest) == pendingDaoAcks.end())
                EV_WARN << "Received DAO_ACK timeout for deleted entry, ignoring" << endl;
            else
                retransmitDao(*advDest);
            break;
        }
        default: EV_WARN << "Unknown self-message received - " << message << endl;
    }
    delete message;
}

void Rpl::clearDaoAckTimer(Ipv6Address daoDest) {
    if (pendingDaoAcks.count(daoDest))
        if (pendingDaoAcks[daoDest].first && pendingDaoAcks[daoDest].first->isSelfMessage())
            cancelEvent(pendingDaoAcks[daoDest].first);
    pendingDaoAcks.erase(daoDest);
}

void Rpl::retransmitDao(Ipv6Address advDest) {
    EV_DETAIL << "DAO_ACK for " << advDest << " timed out, attempting retransmit" << endl;

    if (!preferredParent) {
        EV_WARN << "Preferred parent not set, cannot retransmit DAO"
                << "erasing entry from pendingDaoAcks " << endl;
        clearDaoAckTimer(advDest);
        return;
    }

    auto rtxCtn = pendingDaoAcks[advDest].second++;
    if (rtxCtn > daoRtxThresh) {
        EV_DETAIL << "Retransmission threshold (" << std::to_string(daoRtxThresh)
            << ") exceeded, erasing corresponding entry from pending ACKs" << endl;
        clearDaoAckTimer(advDest);
        numDaoDropped++;
        return;
    }

    EV_DETAIL << "(" << std::to_string(rtxCtn) << " attempt)" << endl;

    sendRplPacket(createDao(advDest), DAO, preferredParent->getSrcAddress(), daoDelay);
}

void Rpl::detachFromDodag() {
    /**
     * If parent set of a node turns empty, it is no longer associated
     * with a DODAG and should suppress previous RPL state info by
     * clearing dodagId, neighbor sets and setting it's rank to INFINITE_RANK [RFC6560, 8.2.2.1]
     */

    // First clear any drawn connector lines
    cCanvas *canvas = getParentModule()->getParentModule()->getCanvas();
    for (auto entry : backupConnectors)
        canvas->removeFigure(entry.second);

    backupConnectors.erase(backupConnectors.begin(), backupConnectors.end());

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
    drawConnector(position, cFigure::BLACK);
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
    if (!hasStarted || par("disabled").boolValue())
        return;
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
                sendRplPacket(createDio(), DIO, Ipv6Address::ALL_NODES_1, uniform(0, 1));
            }
            break;
        }
        default: throw cRuntimeError("Unknown TrickleTimer message");
    }
    delete message;
}

bool Rpl::isRplPacket(Packet *packet) {
    auto fullname = std::string(packet->getFullName());
    return !(fullname.find("DIO") == std::string::npos && fullname.find("DAO") == std::string::npos);
}

void Rpl::processPacket(Packet *packet)
{
    if (floating || !isRplPacket(packet)) {
        EV_DETAIL << "Not processing packet " << packet
                << "\n due to floating (detached) state or because of unsupported packet type" << endl;
        delete packet;
        return;
    }

    RplHeader* rplHeader;
    EV_DETAIL << "Processing packet -" << packet << endl;

    try {
        rplHeader = const_cast<RplHeader*> ((packet->popAtFront<RplHeader>()).get());
    } catch (...) {
        EV_WARN << "Error trying to pop RPL header from " << packet << endl;
        return;
    }

    // in non-storing mode check for RPL Target, Transit Information options
    if (!storing && dodagId != Ipv6Address::UNSPECIFIED_ADDRESS)
        extractSourceRoutingData(packet);

    auto rplBody = packet->peekData<RplPacket>();
    switch (rplHeader->getIcmpv6Code()) {
        case DIO: {
            processDio(dynamicPtrCast<const Dio>(rplBody));
            break;
        }
        case DAO: {
            processDao(dynamicPtrCast<const Dao>(rplBody));
            break;
        }
        case DAO_ACK: {
            processDaoAck(dynamicPtrCast<const Dao>(rplBody));
            break;
        }
        default: EV_WARN << "Unknown Rpl packet" << endl;
    }

    delete packet;
}

int Rpl::getNumDownlinks() {
    EV_DETAIL << "Calculating number of downlinks" << endl;
    auto numRts = routingTable->getNumRoutes();
    int downlinkRoutes = 0;
    for (int i = 0; i < numRts; i++) {
        auto ri = routingTable->getRoute(i);
        auto dest = ri->getDestinationAsGeneric().toIpv6();

        if (dest.isUnicast() && ri->getPrefixLength() == prefixLength)
            downlinkRoutes++;
    }


    return downlinkRoutes - 1; // minus uplink route through the preferred parent
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

void Rpl::sendRplPacket(const Ptr<RplPacket>& body, RplPacketCode code,
        const L3Address& nextHop, double delay)
{
    sendRplPacket(body, code, nextHop, delay, Ipv6Address::UNSPECIFIED_ADDRESS, Ipv6Address::UNSPECIFIED_ADDRESS);
}

void Rpl::sendRplPacket(const Ptr<RplPacket> &body, RplPacketCode code,
        const L3Address& nextHop, double delay, const Ipv6Address &target, const Ipv6Address &transit)
{
    Packet *pkt = new Packet(std::string("inet::RplPacket::" + rplIcmpCodeToStr(code)).c_str());
    auto header = makeShared<RplHeader>();
    header->setIcmpv6Code(code);
    pkt->addTag<PacketProtocolTag>()->setProtocol(&Protocol::manet);
    pkt->addTag<DispatchProtocolReq>()->setProtocol(&Protocol::ipv6);
//    if (interfaceEntryPtr)
//        pkt->addTag<InterfaceReq>()->setInterfaceId(interfaceEntryPtr->getInterfaceId());
    auto addresses = pkt->addTag<L3AddressReq>();
    addresses->setSrcAddress(getSelfAddress());
    addresses->setDestAddress(nextHop);
    pkt->insertAtFront(header);
    pkt->insertAtBack(body);
    // append RPL Target + Transit option headers if corresponding addresses were provided (non-storing mode)
    if (target != Ipv6Address::UNSPECIFIED_ADDRESS && transit != Ipv6Address::UNSPECIFIED_ADDRESS)
        appendDaoTransitOptions(pkt, target, transit);

    if (code == DAO && ((dynamicPtrCast<Dao>) (body))->getDaoAckRequired()) {
        auto outgoingDao = (dynamicPtrCast<Dao>) (body);
        auto advertisedDest = outgoingDao->getReachableDest();
        auto timeout = simTime() + SimTime(daoAckTimeout, SIMTIME_S) * uniform(1, 3); // TODO: Magic numbers

        EV_DETAIL << "Scheduling DAO_ACK timeout at " << timeout << " for advertised dest "
                << advertisedDest << endl;

        cMessage *daoTimeoutMsg = new cMessage("", DAO_ACK_TIMEOUT);
        daoTimeoutMsg->setContextPointer(new Ipv6Address(advertisedDest));
        EV_DETAIL << "Created DAO_ACK timeout msg with context pointer - "
                << *((Ipv6Address *) daoTimeoutMsg->getContextPointer()) << endl;

        auto daoAckEntry = pendingDaoAcks.find(advertisedDest);
        if (daoAckEntry != pendingDaoAcks.end()) {
            pendingDaoAcks[advertisedDest].first = daoTimeoutMsg;
            EV_DETAIL << "Found existing entry in DAO_ACKs map for dest - " << advertisedDest
                    << " updating timeout event ptr" << endl;
        }
        else
            pendingDaoAcks[advertisedDest] = {daoTimeoutMsg, 0};

        EV_DETAIL << "Pending DAO_ACKs:" << endl;
        for (auto e : pendingDaoAcks)
            EV_DETAIL << e.first << " (" << std::to_string(e.second.second) << " attempts), timeout msg ptr - " << e.second.first << endl;
        scheduleAt(timeout, daoTimeoutMsg);
    }
    sendPacket(pkt, delay);

}

const Ptr<Dio> Rpl::createDio()
{
    auto ourMacAddr = interfaceTable->getInterface(1)->getMacAddress();
    selfId = interfaceTable->getInterface(1)->getMacAddress().getInt();
    EV_DETAIL << "Our MAC address - " << ourMacAddr << ", toInt() = " << selfId << endl;
    auto dio = makeShared<Dio>();
    dio->setInstanceId(instanceId);
    dio->setChunkLength(getDioSize());
    dio->setStoring(storing);
    dio->setRank(rank);
    dio->setDtsn(dtsn);
    dio->setNodeId(selfId);
    dio->setDodagVersion(dodagVersion);
    dio->setDodagId(isRoot ? getSelfAddress() : dodagId);
    dio->setSrcAddress(getSelfAddress());
    dio->setPosition(position);
    if (isRoot)
        dio->setColor(dodagColor);
    else
        dio->setColor(preferredParent ? preferredParent->getColor() : cFigure::GREY);

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
    dao->setSeqNum(daoSeqNum++);
    dao->setNodeId(selfId);
    dao->setDaoAckRequired(pDaoAckEnabled);
    EV_DETAIL << "Created DAO with seqNum = " << std::to_string(dao->getSeqNum()) << " advertising " << reachableDest << endl;
    return dao;
}

const Ptr<Dao> Rpl::createDao(const Ipv6Address &reachableDest, bool ackRequired)
{
    auto dao = makeShared<Dao>();
    dao->setInstanceId(instanceId);
    dao->setChunkLength(b(64));
    dao->setSrcAddress(getSelfAddress());
    dao->setReachableDest(reachableDest);
    dao->setSeqNum(daoSeqNum++);
    dao->setNodeId(selfId);
    dao->setDaoAckRequired(ackRequired);
    EV_DETAIL << "Created DAO with seqNum = " << std::to_string(dao->getSeqNum()) << " advertising " << reachableDest << endl;
    return dao;
}

bool Rpl::isUdpSink() {
    if (udpApp)
        try {
            auto udpSink = check_and_cast<UdpSink*> (udpApp);
            return true;
        }
        catch (...) {}

    return false;
}

void Rpl::processDio(const Ptr<const Dio>& dio)
{
    if (isRoot)
        return;
    EV_DETAIL << "Processing DIO from " << dio->getSrcAddress()
            << ", advertised rank - " << dio->getRank() << endl;
    emit(dioReceivedSignal, dio->dup());

    // If node's not a part of any DODAG, join the first one advertised
    if (dodagId == Ipv6Address::UNSPECIFIED_ADDRESS && dio->getRank() != INF_RANK)
    {
        dodagId = dio->getDodagId();
        dodagVersion = dio->getDodagVersion();
        instanceId = dio->getInstanceId();
        storing = dio->getStoring();
        dtsn = dio->getDtsn();
        lastTarget = new Ipv6Address(getSelfAddress());
//        selfAddr = getSelfAddress();
        dodagColor = dio->getColor();
        EV_DETAIL << "Joined DODAG with id - " << dodagId << endl;
//        purgeRoutingTable();
        // Start broadcasting DIOs, diffusing DODAG control data, TODO: refactor TT lifecycle
        if (trickleTimer->hasStarted())
            trickleTimer->reset();
        else
            trickleTimer->start(false, par("numSkipTrickleIntervalUpdates").intValue());
        if (udpApp && !isUdpSink() && udpApp->par("destAddresses").str().empty())
            udpApp->par("destAddresses") = dio->getDodagId().str();
    }
    else {
        if (!allowDodagSwitching && dio->getDodagId() != dodagId) {
            EV_DETAIL << "Node already joined a DODAG, skipping DIO advertising other ones" << endl;
            return;
        }
        /**
         * If INFINITE_RANK is advertised from a preferred parent (route poisoning),
         * delete preferred parent and remove it from candidate neighbors
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

//    // Do not process DIO from unknown DAG/RPL instance, TODO: check with RFC
//    if (checkUnknownDio(dio)) {
//        EV_DETAIL << "Unknown DODAG/InstanceId, or receiver is root - discarding DIO" << endl;
//        return;
//    }
    if (dio->getRank() > rank) {
        EV_DETAIL << "Higher rank advertised, discarding DIO" << endl;
        return;
    }

    addNeighbour(dio);
    updatePreferredParent();
}

void Rpl::purgeRoutingTable() {
    auto numRoutes = routingTable->getNumRoutes();
    for (auto i = 0; i < numRoutes; i++)
        routingTable->deleteRoute(routingTable->getRoute(i));
}

bool Rpl::checkPoisonedParent(const Ptr<const Dio>& dio) {
    return preferredParent && dio->getRank() == INF_RANK && preferredParent->getSrcAddress() == dio->getSrcAddress();
}

void Rpl::processDao(const Ptr<const Dao>& dao) {
    if (!daoEnabled) {
        EV_WARN << "DAO support not enabled, discarding packet" << endl;
        return;
    }

    emit(daoReceivedSignal, dao->dup());
    auto daoSender = dao->getSrcAddress();
    auto advertisedDest = dao->getReachableDest();
    EV_DETAIL << "Processing DAO with seq num " << std::to_string(dao->getSeqNum()) << " from "
            << daoSender << " advertising " << advertisedDest << endl;

    if (dao->getDaoAckRequired()) {
        sendRplPacket(createDao(advertisedDest), DAO_ACK, daoSender, uniform(1, 3));
        EV_DETAIL << "DAO_ACK sent to " << daoSender
                << " acknowledging advertised dest - " << advertisedDest << endl;
    }

    /**
     * If a node is root or operates in storing mode
     * update routing table with destinations from DAO [RFC6560, 3.3].
     * TODO: Implement DAO aggregation!
     */
    if (storing || isRoot) {
        if (!checkDestKnown(daoSender, advertisedDest)) {
            updateRoutingTable(daoSender, advertisedDest, prepRouteData(dao.get()));

            EV_DETAIL << "Destination learned from DAO - " << advertisedDest
                    << " reachable via " << daoSender << endl;
        }
        else
            return;
    }
    /**
     * Forward DAO 'upwards' via preferred parent advertising destination to the root [RFC6560, 6.4]
     */
    if (!isRoot && preferredParent) {
        if (!storing)
            sendRplPacket(createDao(advertisedDest), DAO,
                preferredParent->getSrcAddress(), daoDelay * uniform(1, 2), *lastTarget, *lastTransit);
        else
            sendRplPacket(createDao(advertisedDest), DAO,
                preferredParent->getSrcAddress(), daoDelay * uniform(1, 2));

        numDaoForwarded++;
        EV_DETAIL << "Forwarding DAO to " << preferredParent->getSrcAddress()
                << " advertising " << advertisedDest << " reachability" << endl;
    }
}

std::vector<Ipv6Address> Rpl::getNearestChildren() {
    auto prefParentAddr = preferredParent ? preferredParent->getSrcAddress() : Ipv6Address::UNSPECIFIED_ADDRESS;
    std::vector<Ipv6Address> neighbrs = {};
    for (auto i = 0; i < routingTable->getNumRoutes(); i++) {
        auto rt = routingTable->getRoute(i);
        auto dest = rt->getDestPrefix();
        auto nextHop = rt->getNextHop();

        if (dest == nextHop && dest != prefParentAddr)
            neighbrs.push_back(nextHop);
    }

    EV_DETAIL << "Found 1-hop neighbors (" << neighbrs.size() << ") - " << neighbrs << endl;
    return neighbrs;
}

void Rpl::processDaoAck(const Ptr<const Dao>& daoAck) {
    auto advDest = daoAck->getReachableDest();

    EV_INFO << "Received DAO_ACK from " << daoAck->getSrcAddress()
            << " for advertised dest - "  << advDest << endl;

    if (pendingDaoAcks.empty()) {
        EV_DETAIL << "No DAO_ACKs were expected!" << endl;
        return;
    }

    clearDaoAckTimer(advDest);

    EV_DETAIL << "Cancelled timeout event and erased entry in the pendingDaoAcks, remaining: " << endl;

    for (auto e : pendingDaoAcks)
        EV_DETAIL << e.first << " rtxs: " << std::to_string(e.second.second)
            << ", msg ptr - " << e.second.first << endl;

}


void Rpl::setLineProperties(cLineFigure *connector, Coord target, cFigure::Color col, bool dashed) const
{
    EV_DETAIL << "Setting line propeties, X = "  << target.x << ", Y = " << target.y << endl;
    EV_DETAIL << "our coords X = "  << position.x << ", Y = " << position.y << endl;
    connector->setStart(cFigure::Point(position.x, position.y));
    connector->setEnd(cFigure::Point(target.x, target.y));
    connector->setLineWidth(2);
    connector->setLineColor(col);
    connector->setLineOpacity(dashed ? 0.5 : 1);
    if (dashed)
        connector->setLineStyle(cFigure::LineStyle::LINE_DASHED);
    connector->setEndArrowhead(cFigure::ARROW_BARBED);
    connector->setVisible(true);
}


void Rpl::drawConnector(Coord target, cFigure::Color col, Ipv6Address backupParent) const {
    // (0, 0) corresponds to default Coord constructor, meaning no target position was provided
    if ((!target.x && !target.y) || !par("drawConnectors").boolValue())
        return;

    cCanvas *canvas = getParentModule()->getParentModule()->getCanvas();

    // If a valid pointer to backup parent address is provided
    // And there's no line drawn to it, create a new one
    if (backupParent != Ipv6Address::UNSPECIFIED_ADDRESS) {
        if (backupConnectors.find(backupParent) == backupConnectors.end()) {
            backupConnectors[backupParent] = new cLineFigure("backupParentConnector");
            setLineProperties(backupConnectors[backupParent], target, col, true);
            canvas->addFigure(backupConnectors[backupParent]);
        }
        return;
    }


    // If an arrow to preferred parent is already present, we only need to update its properties
    if (prefParentConnector) {
        prefParentConnector->setLineColor(col);
        prefParentConnector->setEnd(cFigure::Point(target.x, target.y));
        return;
    }

    // Otherwise create a new line and add it to canvas
    prefParentConnector = new cLineFigure("preferredParentConnector");
    setLineProperties(prefParentConnector, target, col);
    canvas->addFigure(prefParentConnector);
}

void Rpl::updatePreferredParent()
{
    Dio *newPrefParent;
    EV_DETAIL << "Choosing preferred parent from "
            << boolStr(candidateParents.empty() && par("useBackupAsPreferred").boolValue(),
                    "backup", "candidate") << " parent set:" << endl;
    /**
     * Choose parent from candidate neighbour set. If it's empty, leave the DODAG.
     */
    if (candidateParents.empty())
    {
        EV_DETAIL << "Candidate parent list empty" << endl;
        if (par("useBackupAsPreferred").boolValue()) {
            EV_DETAIL << "Selecting preferred parent from backup parents:" << endl;
            newPrefParent = objectiveFunction->getPreferredParent(backupParents, preferredParent);
        }
        else {
            EV_DETAIL << "Leaving DODAG" << endl;
            detachFromDodag();
            return;
        }
    }
    else
        newPrefParent = objectiveFunction->getPreferredParent(candidateParents, preferredParent);

    auto newPrefParentAddr = newPrefParent->getSrcAddress();
    /**
     * If a better preferred parent is discovered (based on the objective function metric),
     * update default route to DODAG sink with the new nextHop
     */
    if (checkPrefParentChanged(newPrefParentAddr)) {

        auto newPrefParentDodagId = newPrefParent->getDodagId();

        /** Silently join new DODAG and update dest address for application, TODO: Check with RFC */
        dodagId = newPrefParentDodagId;
        if (udpApp && !isUdpSink() && udpApp->par("destAddresses").str().empty())
            udpApp->par("destAddresses") = newPrefParentDodagId.str();

        /** Notify 6TiSCH Scheduling Function about parent change */
        auto rplCtrlInfo = new RplGenericControlInfo(newPrefParent->getNodeId());
        emit(parentChangedSignal, 0, (cObject*) rplCtrlInfo);

        daoSeqNum = 0;
        clearParentRoutes();
        drawConnector(newPrefParent->getPosition(), newPrefParent->getColor());
        updateRoutingTable(newPrefParentAddr, dodagId, nullptr, true);

        // required for proper nextHop address resolution
        if (newPrefParentAddr != dodagId)
            updateRoutingTable(newPrefParentAddr, newPrefParentAddr, nullptr, false);

        lastTransit = new Ipv6Address(newPrefParentAddr);
        EV_DETAIL << "Updated preferred parent to - " << newPrefParentAddr << endl;
        numParentUpdates++;
        /**
         * Reset trickle timer due to inconsistency (preferred parent changed) detected, thus
         * maintaining higher topology reactivity and convergence rate [RFC 6550, 8.3]
         */
        trickleTimer->reset();
        if (daoEnabled) {
            if (storing)
                // TODO: magic numbers
                sendRplPacket(createDao(), DAO, newPrefParentAddr, daoDelay * uniform(1, 7));
            else
                sendRplPacket(createDao(), DAO, newPrefParentAddr, daoDelay * uniform(1, 7),
                            getSelfAddress(), newPrefParentAddr);

            EV_DETAIL << "Sending DAO to new pref. parent - " << newPrefParentAddr
                    << " advertising " << getSelfAddress() << " reachability" << endl;
        }
    }
    preferredParent = newPrefParent->dup();

    /** Recalculate rank based on the objective function */
    auto newRank = objectiveFunction->calcRank(preferredParent);
    if (newRank != rank) {
        rank = newRank;
        EV_DETAIL << "Updated rank - " << rank << endl;

        // TODO: refactor into separate function
        cCanvas *canvas = getParentModule()->getParentModule()->getCanvas();

        vector<Ipv6Address> parentsToDelete;

        for (auto bp : backupParents) {
            if (bp.second->getRank() > rank) {
                auto bkConnector = backupConnectors.find(bp.second->getSrcAddress());

                if (bkConnector != backupConnectors.end()) {
                    canvas->removeFigure((*bkConnector).second);
                    backupConnectors.erase(bkConnector);
                }
                parentsToDelete.push_back(bp.second->getSrcAddress());
            }
        }

        for (auto addr: parentsToDelete)
            backupParents.erase(backupParents.find(addr));

        emit(rankUpdatedSignal, (long) rank);
    }
}

bool Rpl::checkPrefParentChanged(const Ipv6Address &newPrefParentAddr)
{
    return !preferredParent || preferredParent->getSrcAddress() != newPrefParentAddr;
}

//
// Handling routing data
//

RplRouteData* Rpl::prepRouteData(const Dao *dao) {
    auto routeData = new RplRouteData();
    routeData->setDodagId(dao->getDodagId());
    routeData->setInstanceId(dao->getInstanceId());
    routeData->setDtsn(dao->getSeqNum());
    routeData->setExpirationTime(-1);
    return routeData;
}

void Rpl::updateRoutingTable(const Ipv6Address &nextHop, const Ipv6Address &dest, RplRouteData *routeData, bool defaultRoute)
{
    auto route = routingTable->createRoute();
    route->setSourceType(IRoute::MANET);
    route->setPrefixLength(isRoot ? 128 : prefixLength);
    route->setInterface(interfaceEntryPtr);
    route->setDestination(dest);
    route->setNextHop(nextHop);
    /**
     * If a route through preferred parent is being added
     * (i.e. not downward route, learned from DAO), set it as default route
     */
    if (defaultRoute) {
        routingTable->addDefaultRoute(nextHop, interfaceEntryPtr->getInterfaceId(), DEFAULT_PARENT_LIFETIME);
        EV_DETAIL << "Adding default route via " << nextHop << endl;
    }
    if (routeData)
        route->setProtocolData(routeData);

    if (!checkDuplicateRoute((Ipv6Route*) route))
        routingTable->addRoute(route);
}

bool Rpl::checkDuplicateRoute(Ipv6Route *route) {
    for (auto i = 0; i < routingTable->getNumRoutes(); i++) {
        auto rt = routingTable->getRoute(i);
        auto rtdest = rt->getDestinationAsGeneric().toIpv6();
        auto dest = route->getDestinationAsGeneric().toIpv6();
        if (dest == rtdest) {
            if (rt->getNextHop() != route->getNextHop()) {
                rt->setNextHop(route->getNextHop());
                EV_DETAIL << "Duplicate route, updated next hop to " << rt->getNextHop() << " for dest " << dest << endl;
                if (route->getProtocolData())
                    rt->setProtocolData(route->getProtocolData());
            }
            return true;
        }
    }
    return false;
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
    auto networkProtocolHeader = findNetworkProtocolHeader(datagram);
    auto dest = networkProtocolHeader->getDestinationAddress().toIpv6();
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
    appendDaoTransitOptions(pkt, getSelfAddress(), preferredParent->getSrcAddress());
}

void Rpl::appendDaoTransitOptions(Packet *pkt, const Ipv6Address &target, const Ipv6Address &transit) {
    EV_DETAIL << "Appending target, transit options to DAO: " << pkt << endl;
    auto rplTarget = makeShared<RplTargetInfo>();
    auto rplTransit = makeShared<RplTransitInfo>();
    rplTarget->setChunkLength(getTransitOptionsLength());
    rplTransit->setChunkLength(getTransitOptionsLength());
    rplTarget->setTarget(target);
    rplTransit->setTransit(transit);
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
    EV_DETAIL << "Checking RPI header for packet " << datagram
            << " \n coming from "
            << findNetworkProtocolHeader(datagram)->getSourceAddress().toIpv6()
            << endl;
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


void Rpl::extractSourceRoutingData(Packet *pkt) {
    try {
        lastTransit = new Ipv6Address(pkt->popAtBack<RplTransitInfo>(getTransitOptionsLength()).get()->getTransit());
        lastTarget = new Ipv6Address(pkt->popAtBack<RplTargetInfo>(getTransitOptionsLength()).get()->getTarget());
        if (!isRoot)
            return;

        sourceRoutingTable.insert( std::pair<Ipv6Address, Ipv6Address>(*lastTarget, *lastTransit) );
        EV_DETAIL << "Source routing table updated with new:\n"
                << "target: " << lastTarget << "\n transit: " << lastTransit << "\n"
                << printMap(sourceRoutingTable) << endl;
    }
    catch (std::exception &e) {
        EV_WARN << "Couldn't pop RPL Target, Transit Information options from packet: "
                << pkt << endl;
    }
}

INetfilter::IHook::Result Rpl::checkRplHeaders(Packet *datagram) {
    // skip further checks if node doesn't belong to a DODAG
    auto datagramName = std::string(datagram->getFullName());
    EV_INFO << "packet fullname " << datagramName << endl;
    if (!isRoot && (preferredParent == nullptr || dodagId == Ipv6Address::UNSPECIFIED_ADDRESS))
    {
        EV_DETAIL << "Node is detached from a DODAG, " <<
                " no forwarding/rank error checks will be performed" << endl;
        return ACCEPT;
    }

    if (isUdp(datagram)) {
        // in non-storing MOP source routing header is needed for downwards traffic
        if (!storing) {
            // generate one if the sender is root
            if (isRoot) {
                if (selfGeneratedPkt(datagram)) {
                    appendSrcRoutingHeader(datagram);
                    return ACCEPT;
                }
                else
                    EV_DETAIL << "P2P source-routing not yet supported" << endl;
            }
            // or forward packet further using the routing header
            else {
                if (!destIsRoot(datagram))
                    forwardSourceRoutedPacket(datagram);
                return ACCEPT;
            }
        }

        // check for loops
//        return checkRplRouteInfo(datagram) ? ACCEPT : DROP;
    }

    return ACCEPT;
}


bool Rpl::destIsRoot(Packet *datagram) {
    return findNetworkProtocolHeader(datagram).get()->getDestinationAddress().toIpv6().matches(dodagId, prefixLength);
}

void Rpl::saveDaoTransitOptions(Packet *dao) {
    try {
        lastTransit = new Ipv6Address(dao->popAtBack<RplTransitInfo>(getTransitOptionsLength()).get()->getTransit());
        lastTarget = new Ipv6Address(dao->popAtBack<RplTargetInfo>(getTransitOptionsLength()).get()->getTarget());
        EV_DETAIL << "Updated lastTransit => lastTarget to: " << *lastTransit << " => " << *lastTarget << endl;
    }
    catch (std::exception &e) {
        EV_DETAIL << "No Target, Transit headers found on packet:\n " << *dao << endl;
        return;
    }
}

void Rpl::constructSrcRoutingHeader(std::deque<Ipv6Address> &addressList, Ipv6Address dest)
{
    Ipv6Address nextHop = sourceRoutingTable[dest];
    addressList.push_front(dest);
    addressList.push_front(nextHop);
    EV_DETAIL << "Constructing routing header for dest - " << dest
            << "\n next hop: " << nextHop << endl;

    // Create sequence of 'next hop' addresses recursively using the source routing table learned from DAOs
    while (sourceRoutingTable.find(nextHop) != sourceRoutingTable.end())
    {
        nextHop = sourceRoutingTable[nextHop];
        addressList.push_front(nextHop);
    }
    // pop redundant link-local next hop, which is already known from the routing table
    addressList.pop_front();
}


void Rpl::appendSrcRoutingHeader(Packet *datagram) {
    Ipv6Address dest = findNetworkProtocolHeader(datagram)->getDestinationAddress().toIpv6();
    std::deque<Ipv6Address> srhAddresses;
    EV_DETAIL << "Appending routing header to datagram " << datagram << endl;
    if ( sourceRoutingTable.find(dest) == sourceRoutingTable.end() ) {
        EV_WARN << "Required destination " << dest << " not yet present in source-routing table: \n"
                << printMap(sourceRoutingTable) << endl;
        return;
    }

    constructSrcRoutingHeader(srhAddresses, dest);

    EV_DETAIL << "Source routing header constructed : " << srhAddresses << endl;

    auto srh = makeShared<SourceRoutingHeader>();
    srh->setAddresses(srhAddresses);
    srh->setChunkLength(getSrhSize());
    datagram->insertAtBack(srh);
}

void Rpl::forwardSourceRoutedPacket(Packet *datagram) {
    EV_DETAIL << "processing source-routed datagram - " << datagram
            << "\n with routing header: " << endl;
    auto srh = const_cast<SourceRoutingHeader *> (datagram->popAtBack<SourceRoutingHeader>(getSrhSize()).get());
    auto srhAddresses = srh->getAddresses();

    if (srhAddresses.back() == getSelfAddress()) {
        EV_DETAIL << "Source-routed destination reached" << endl;
        return;
    }

    for (auto addr : srhAddresses)
        EV_DETAIL << addr << " => ";

    Ipv6Address nextHop;
    for (auto it = srhAddresses.cbegin(); it != srhAddresses.cend(); it++) {
        if (it->matches(getSelfAddress(), prefixLength)) {
            nextHop = *(++it);
            break;
        }
    }
    EV_DETAIL << "\n Forwarding source-routed datagram to " << nextHop << endl;

    updateRoutingTable(nextHop, nextHop, nullptr, false);

    srhAddresses.pop_front();
//
    // re-insert updated routing header
    auto updatedSrh =  makeShared<SourceRoutingHeader>();
    updatedSrh->setChunkLength(getSrhSize());
    updatedSrh->setAddresses(srhAddresses);
    datagram->insertAtBack(updatedSrh);
    (const_cast<NetworkHeaderBase *>(findNetworkProtocolHeader(datagram).get()))->setDestinationAddress(nextHop);
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
        EV_DETAIL << "Checking if packet is self generated by source address: "
                << srcAddr << "; is self-generated: " << boolStr(res) << endl;
    }
    return res;
}

std::string Rpl::rplIcmpCodeToStr(RplPacketCode code) {
    switch (code) {
        case 0:
            return std::string("DIS");
        case 1:
            return std::string("DIO");
        case 2:
            return std::string("DAO");
        case 3:
            return std::string("DAO_ACK");
        default:
            return std::string("Unknown");
    }
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
    if (!preferredParent) {
        EV_WARN << "Cannot delete preferred parent, it's nullptr" << endl;
        return;
    }

    auto prefParentAddr = preferredParent->getSrcAddress();
    EV_DETAIL << "Preferred parent " << prefParentAddr
            << boolStr(poisoned, " detachment from DODAG", " unreachability") << " detected" << endl;
    emit(parentUnreachableSignal, preferredParent);
    clearParentRoutes();
    candidateParents.erase(prefParentAddr);
    preferredParent = nullptr;
    EV_DETAIL << "Erased preferred parent from candidate parent set" << endl;
}

void Rpl::clearParentRoutes() {
    if (!preferredParent) {
        EV_WARN << "Pref. parent not set, cannot delete associated routes from routing table " << endl;
        return;
    }

    // Delete routes with prefix length 0 (default routes)
    deleteDefaultRoutes(interfaceEntryPtr->getInterfaceId());

    std::vector<Ipv6Route*> routesToDelete;
    auto totalRoutes = routingTable->getNumRoutes();

    // Collect any remaining routes with preferred parent as the next hop
    for (int i = 0; i < totalRoutes; i++) {
        auto ri = routingTable->getRoute(i);
        if (ri->getNextHop() == preferredParent->getSrcAddress())
            routesToDelete.push_back(ri);
    }

    // And delete them too
    for (auto rt : routesToDelete)
        routingTable->deleteRoute(rt);

    EV_DETAIL << "Deleted " << routesToDelete.size() << " routes through preferred parent " << endl;
    routingTable->purgeDestCache();
}

void Rpl::deleteDefaultRoutes(int interfaceID) {
    if (interfaceID < 0) {
        EV_ERROR << "Invalid interface id provided" << endl;
        return;
    }

    EV_INFO << "/// Removing default routes for interface=" << interfaceID << endl;

    auto numRts = routingTable->getNumRoutes();
    std::vector<Ipv6Route*> rtsForDeletion;

    for (auto i = 0; i < numRts; i++) {
        auto rt = routingTable->getRoute(i);
        // default routes have prefix length 0
        if (rt->getInterface()
                && rt->getInterface()->getInterfaceId() == interfaceID
                && rt->getPrefixLength() == 0)
        {
            rtsForDeletion.push_back(rt);
        }
    }

    for (auto rt: rtsForDeletion)
        routingTable->deleteRoute(rt);
}

//
// Utility methods
//

std::string Rpl::boolStr(bool cond, std::string positive, std::string negative) {
    return cond ? positive : negative;
}

template<typename Map>
std::string Rpl::printMap(const Map& map) {
    std::ostringstream out;
    for (const auto& p : map)
        out << p.first << " => " << p.second << endl;
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
    auto dioSender = dio->getSrcAddress();
    /** If DIO sender has equal rank, consider this node as a backup parent */
    if (dio->getRank() == rank) {
        if (backupParents.find(dioSender) != backupParents.end())
            EV_DETAIL << "Backup parent entry updated - " << dioSender;
        else
            EV_DETAIL << "New backup parent added - " << dioSender;
        backupParents[dioSender] = dioCopy;
    }
    /** If DIO sender has lower rank, consider this node as a candidate parent */
    if (dio->getRank() < rank) {
        if (candidateParents.find(dioSender) != candidateParents.end())
            EV_DETAIL << "Candidate parent entry updated - " << dioSender;
        else
            EV_DETAIL << "New candidate parent added - " << dioSender;
        candidateParents[dioSender] = dioCopy;
    }
    EV_DETAIL << " (rank " << dio->getRank() << ")" << endl;

    // Draw dashed connector line to candidate or backup parents
    cCanvas *canvas = getParentModule()->getParentModule()->getCanvas();
    EV_DETAIL << "Canvas - " << canvas << endl;
    if (backupConnectors.find(dioSender) == backupConnectors.end()) {
        EV_DETAIL << "No connector yet found for this neighbor" << endl;
        auto connector = new cLineFigure("backupParentConnector");
        setLineProperties(connector, dio->getPosition(), dio->getColor(), true);
        canvas->addFigure(connector);
        EV_DETAIL << "Connector added - " << connector << endl;
        backupConnectors[dioSender] = connector;
    }
}

//
// Notification handling
//

void Rpl::receiveSignal(cComponent *source, simsignal_t signalID, cObject *obj, cObject *details)
{

//    Enter_Method("receiveChangeNotification");
//
    Enter_Method_Silent();

    EV_DETAIL << "Processing signal - " << signalID << endl;
    if (signalID == packetReceivedSignal)
        udpPacketsRecv++;

    /**
     * Upon receiving broken link signal from MAC layer, check whether
     * preferred parent is unreachable
     */
    if (signalID == linkBrokenSignal) {
        EV_WARN << "Received link break" << endl;
        Packet *datagram = check_and_cast<Packet *>(obj);
        EV_DETAIL << "Packet " << datagram->str() << " lost?" << endl;
        const auto& networkHeader = findNetworkProtocolHeader(datagram);
        if (networkHeader != nullptr) {
            const Ipv6Address& destination = networkHeader->getDestinationAddress().toIpv6();
            auto nextHop = routingTable->getNextHopForDestination(destination);
            EV_DETAIL << "Connection with destination " << destination << " reachable via "
                    << nextHop << " broken?" << endl;
            /**
             * If preferred parent unreachability detected, remove route with it as a
             * next hop from the routing table and select new preferred parent from the
             * candidate set.
             *
             * If candidate parent set is empty, leave current DODAG
             * (becoming either floating DODAG or poison child routes altogether)
             */
            if (preferredParent && nextHop == preferredParent->getSrcAddress()
                    && pUnreachabilityDetectionEnabled)
            {
                deletePrefParent();
                // TODO: Redirect all packets already enqueued for previous pref. parent to the new one
                updatePreferredParent();
                pUnreachabilityDetectionEnabled = false; // FIXME: remove this after calibrating lossy scenario
            }
        }
    }
}

} // namespace inet



