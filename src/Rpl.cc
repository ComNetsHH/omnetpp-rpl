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


// TODO: Store DODAG-related info in a separate class to
// to monitor it during the simulation

// TODO: Figure out 'false positives' triggering link-break signals
// on lost NeighborAdvertisement (NA) packets

// TODO: Replace plain pointers with shared pointers where possible for
// better garbage collection

Rpl::Rpl() :
    isRoot(false),
    hasStarted(false),
    daoDelay(DEFAULT_DAO_DELAY),
    branchSize(0),
    udpPacketsRecv(0),
    dodagId(Ipv6Address::UNSPECIFIED_ADDRESS),
//    daoAckTimeout(10),
    daoAckTimeout(10), // 5
    detachedTimeout(2), // manual suppressing previous DODAG info [RFC 6550, 8.2.2.1]
    daoRtxCtn(0),
    daoSeqNum(0),
    prefixLength(112),
    slotframeLength(0),
    clSlotframeChunk({0, 0}),
    prefParentConnector(nullptr),
    dodagColor(cFigure::BLACK),
//    prefixLength(128),
    objectiveFunctionType("hopCount"),
    terminusNode(Ipv6Address("fe80::8aa:ff:fe00:5")),
    preferredParent(nullptr),
    floating(false),
    crossLayerInfoFwdTimeout(5),
    channelOffsets({}),
    branchChOffset(UNDEFINED_CH_OFFSET)
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

        daoEnabled = par("daoEnabled").boolValue();
        host = getContainingNode(this);

        objectiveFunction = new ObjectiveFunction(par("objectiveFunctionType").stdstringValue());
        objectiveFunction->setMinHopRankIncrease(par("minHopRankIncrease").intValue());
        daoRtxThresh = par("numDaoRetransmitAttempts").intValue();
        allowDodagSwitching = par("allowDodagSwitching").boolValue();

        pDaoAckEnabled = par("daoAckEnabled").boolValue();
        pManualParentAssignment = par("assignParentManual").boolValue();
        pUseWarmup = par("useWarmup").boolValue();
        pJoinAtSinkAllowed = par("allowJoinAtSink").boolValue();
        clStartOnTimeout = par("clStartOnTimeout").boolValue();
        clKickoffTimeout = par("clPhase2Timeout").doubleValue();

        // cross-layer params
        pCrossLayerEnabled = par("crossLayerEnabled").boolValue();
        pTschNumChannels = pCrossLayerEnabled ? getNumTschChannels() : 0;
        EV_DETAIL << "Discovered " << pTschNumChannels << " TSCH channels" << endl;

        // statistic signals
        dioReceivedSignal = registerSignal("dioReceived");
        daoReceivedSignal = registerSignal("daoReceived");
        parentChangedSignal = registerSignal("parentChanged");
        parentUnreachableSignal = registerSignal("parentUnreachable");

        // cross-layer control & stats signals
        nodeIsSinkSignal = registerSignal("isSink");
        reschedule = registerSignal("reschedule");
        setChOffset = registerSignal("setChOffset");
        oneHopChildJoined = registerSignal("oneHopChildJoined");
        pickedRandomChOffsetSignal = registerSignal("pickedRandomChOffset");
        startDelay = par("startDelay").doubleValue();

        cModule* net = host->getParentModule();

        if (par("multiGwConfigurator").boolValue() && pCrossLayerEnabled)
            allocateSinkChOffsets(pTschNumChannels, net);

        if (par("layoutConfigurator").boolValue())
            generateLayout(net);

        WATCH(pTschChOffStart);
        WATCH(pTschChOffEnd);
        WATCH(branchChOffset);
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

    configureTopologyLayout(*anchor, padX, padY, gap, net, bLayout, numBranches, numHosts);
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

bool Rpl::isSinkNode(std::string nodeName)
{
    const std::regex base_regex("sink\\[([0-9]+)\\]");
    std::smatch base_match;

    if (std::regex_match(nodeName, base_match, base_regex)) {
        // The first sub_match is the whole string; the next
        // sub_match is the first parenthesized expression.
        if (base_match.size() == 2)
            return true;
    }
    return false;
}

cFigure::Color Rpl::pickRandomColor() {
    auto palette = selfId % 2 == 0 ? cFigure::GOOD_DARK_COLORS : cFigure::GOOD_LIGHT_COLORS;
    auto numColors = selfId % 2 == 0 ? cFigure::NUM_GOOD_DARK_COLORS : cFigure::NUM_GOOD_LIGHT_COLORS;
    return host->getParentModule()->par("numSinks").intValue() == 1 ?  cFigure::BLACK : palette[intrand(numColors, 0) - 1];
}

void Rpl::allocateSinkChOffsets(int numTschChannels, cModule *network) {
    if (!network)
        throw cRuntimeError("No network module provided");

    auto numSinks = network->par("numSinks").intValue();

    if (numSinks <= 0)
      throw cRuntimeError("Invalid number of sinks");

    EV_DETAIL << "Allocating channel offsets to sinks" << endl;

    int j = 0;
    int chOffsetsPerSink = (int) ceil(numTschChannels / numSinks);

    for (cModule::SubmoduleIterator it(network); !it.end(); ++it) {
        cModule *subm = *it;
        std::string sname(subm->getFullName());
        auto subRpl = subm->getSubmodule("rpl");
        if (sname.find("sink") != std::string::npos) {
            subRpl->par("startChOffset") = (int) chOffsetsPerSink * j;
            j++;
            int lastChOf = chOffsetsPerSink * j - 1;
            subRpl->par("endChOffset") = lastChOf >= numTschChannels ? numTschChannels - 1 : lastChOf;
            EV_DETAIL << sname << ": " << chOffsetsPerSink * j << ".." << lastChOf << endl;
        }
    }
}

void Rpl::configureTopologyLayout(Coord anchorPoint, double padX, double padY, double columnGapMultiplier,
        cModule *network, bool branchLayout, int numBranches, int numHosts)
{
    EV_DETAIL << "Configuring topology for " << boolStr(branchLayout, "branch", "seatbelt") << " layout" << endl;

    int i = 1;
    int k = 1;
    int l = 0;
    int m = 0;
    int hopCtn = 0;
    int middleBranchId = (int) floor(numBranches / 2);
    int nodesPerBranch = branchLayout ? (int) ceil(numHosts / numBranches) : 0;

    double skewMod = numBranches % 2 == 0 ? 1.5 : 1.2;
    int numHops = par("numHops").intValue();

    double currentX = anchorPoint.x - (branchLayout ? (numBranches * skewMod) : columnGapMultiplier) * padX;
    double defaultY = branchLayout ? (anchorPoint.y + padY) : anchorPoint.y;
    double currentY = defaultY;

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
                double initialY = anchorPoint.y + pow(-1, k++) * padY * l * numHosts / 12;
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
                    currentY = anchorPoint.y + pow(-1, i) * padY * i++;
                    // 'jumping' over aircraft's central hallway
                } else if (nodeId % 3 == 0)
                    currentX += columnGapMultiplier * padX;
                else
                    currentX += padX;
            }
        }
    }
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
            break;
        }
    }

    selfId = interfaceTable->getInterface(1)->getMacAddress().getInt();
    auto mobility = check_and_cast<StationaryMobility*> (getParentModule()->getSubmodule("mobility"));
    position = mobility->getCurrentPosition();

    udpApp = findSubmodule("app", host);

//    if (!isRoot) {
//          // TODO: refactor this submodule search
//          udpApp = findSubmodule("app", host);
//          if (app)
//              app->subscribe("packetReceived", this);
//
//          udpApp = app ? check_and_cast<UdpBasicApp*> (app) : nullptr;
//    }

    rank = INF_RANK - 1;
    detachedTimeoutEvent = new cMessage("", DETACHED_TIMEOUT);

    if (isRoot && !par("disabled").boolValue()) {
        trickleTimer->start(pUseWarmup, par("numSkipTrickleIntervalUpdates").intValue());
        dodagColor = pickRandomColor();
        rank = ROOT_RANK;
        dodagVersion = DEFAULT_INIT_DODAG_VERSION;
        instanceId = RPL_DEFAULT_INSTANCE;
        dtsn = 0;
        storing = par("storing").boolValue();
        if (pCrossLayerEnabled) {
            // parameter initialization occurs here to allow multi-gateway configurator to
            // set everything properly
            pTschNumChannels = getNumTschChannels();
            if (!pTschNumChannels)
                throw cRuntimeError("Cross-layer framework enabled, but couldn't determine number of TSCH channels");

            pTschChOffStart = par("startChOffset").intValue();
            pTschChOffEnd = par("endChOffset").intValue();
            initChOffsetList(pTschChOffStart, pTschChOffEnd);
            if (clStartOnTimeout)
                scheduleAt(simTime() + clKickoffTimeout, new cMessage("", START_CROSS_LAYER_PHASE_II));
        }
        emit(nodeIsSinkSignal, true);
        udpApp->subscribe("packetReceived", this);
    }
}

void Rpl::refreshDisplay() const {
    if (isRoot) {
        host->getDisplayString().setTagArg("t", 0, std::string(" num rcvd: " + std::to_string(udpPacketsRecv)).c_str());
        host->getDisplayString().setTagArg("t", 1, "l"); // set display text position to 'left'
    }
}

void Rpl::initChOffsetList(int minChOffset, int maxChOffset) {
    for (int i = minChOffset; i < maxChOffset; i++)
        channelOffsets.push_back(i);
    EV_DETAIL << "Initialized channel offset list (to advertise to branches later on): ";
    for (auto chof : channelOffsets)
        EV_DETAIL << chof << ", ";
    EV_DETAIL << endl;
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
        case START_CROSS_LAYER_PHASE_II: {
            startSecondSchedulingPhase();
            break;
        }
        case BRANCH_CH_ADV: {
            auto branchRoot = (Ipv6Address*) message->getContextPointer();
            EV_DETAIL << "Preparing to send chOf advertisement to one of the branch roots - " << *branchRoot << endl;

            advertiseChOffset(*branchRoot, 0);
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

//void Rpl::retransmitDao(Dao *dao)

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
        return;
    }

    EV_DETAIL << "(" << std::to_string(rtxCtn) << " attempt)" << endl;

    sendRplPacket(createDao(advDest), DAO, preferredParent->getSrcAddress(), daoDelay);
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

bool Rpl::checkRplPacket(Packet *packet) {
    auto fullname = std::string(packet->getFullName());
    return !(fullname.find("DIO") == std::string::npos && fullname.find("DAO") == std::string::npos);
}

void Rpl::processPacket(Packet *packet)
{
    if (floating || !checkRplPacket(packet)) {
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
        EV_WARN << "impl. ser. error while trying to pop RPL header from " << packet << endl;
        return;
    }

    // in non-storing mode check for RPL Target, Transit Information options
    if (!storing && dodagId != Ipv6Address::UNSPECIFIED_ADDRESS)
        extractSourceRoutingData(packet);
    auto rplBody = packet->peekData<RplPacket>();
    auto senderAddr = rplBody->getSrcAddress();
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

uint16_t Rpl::calcBranchSize() {
    auto numRts = routingTable->getNumRoutes();
    uint16_t branchChildren = 0;
    for (int i = 0; i < numRts; i++) {
        auto ri = routingTable->getRoute(i);
        auto dest = ri->getDestinationAsGeneric().toIpv6();
        if (dest.isUnicast() && dest.isLinkLocal()) {
            branchChildren++;
//            EV_DETAIL << dest << " is unicast"
//                    << boolStr(dest.isGlobal(), "is", "is not") << " global\n"
//                    << boolStr(dest.isMulticast(), "is", "is not") << " multicast\n"
//                    << boolStr(dest.isLinkLocal(), "is", "is not") << " link-local\n"
//                    << boolStr(dest.isSolicitedNodeMulticastAddress(), "is", "is not") << " solicited...\n"
//                    << boolStr(dest.isSiteLocal(), "is", "is not") << " global\n" << " site-local" << endl;
        }
    }


    return branchChildren - 1;
}

uint16_t Rpl::calcBranchSize(Ipv6Address& rootChild) {
    auto numRts = routingTable->getNumRoutes();
    uint16_t branchChildren = 0;
    for (int i = 0; i < numRts; i++) {
        auto ri = routingTable->getRoute(i);
        if (ri->getNextHop() == rootChild)
            branchChildren++;
    }
    EV_DETAIL << "Found " << std::to_string(branchChildren)
        << " children on branch rooted at " << rootChild << endl;
    return numRts;
}

void Rpl::processCrossLayerMsg(const Ptr<const Dio>& ctrlDio) {
    if (!pCrossLayerEnabled || !preferredParent)
        return;

    if (ctrlDio->getSrcAddress() != preferredParent->getSrcAddress())
    {
        EV_DETAIL << "Not processing cross-layer DIOs coming not from our preferred parent" << endl;
        return;
    }

    if (preferredParent->getSrcAddress() != dodagId && ctrlDio->getAdvChOffset() == UNDEFINED_CH_OFFSET)
    {
        EV_DETAIL << "No channel offset advertised in DIO coming from "
                << "a non-root parent, aborting cross-layer scheduling" << endl;
        return;
    }

    EV_DETAIL << "Processing cross-layer control DIO from " << ctrlDio->getSrcAddress() << endl;

    if (preferredParent->getSrcAddress() == dodagId && branchChOffset == UNDEFINED_CH_OFFSET) {
        branchChOffset = intrand(pTschNumChannels);
        EV_WARN << "DIO is directly from the sink, but still no channel offset fixed for this branch "
                << "(may have missed DAO_ACK advertising it), choosing randomly - " << branchChOffset << endl;
    }

    slotframeLength = ctrlDio->getSlotframeLength();
    if (slotframeLength == 0) {
        EV_WARN << "Invalid slotframe length advertised" << endl;
        return;
    }

    if (branchChOffset == UNDEFINED_CH_OFFSET) {
//        if (ctrlDio->getAdvChOffset() == UNDEFINED_CH_OFFSET) {
//            advertisedChOffset = intrand(pTschNumChannels);
//            EV_WARN << "Undefined channel offset advertised, picking one randomly - "
//                    << std::to_string(advertisedChOffset) << endl;
//
//        } else {
//
//        }
        branchChOffset = ctrlDio->getAdvChOffset();
    }

    emit(setChOffset, (long) branchChOffset);

    if (branchSize == 0) {
        auto advBranchSize = ctrlDio->getBranchSize();
        if (advBranchSize == 0) {
            branchSize = calcBranchSize();
            if (!branchSize) {
                EV_WARN << "No children found on this branch? Aborting cross-layer scheduling" << endl;
                return;
            }
            EV_DETAIL << "Branch size calculated by enumerating unicast routes - " << branchSize << endl;
        }
        else
            branchSize = advBranchSize;
    }
    // If no cross-layer scheduling info fixed yet, try to extract it from DIO
    if (clSlotframeChunk.start == 0 && clSlotframeChunk.end == 0)  {
        auto advChunk = ctrlDio->getAdvSlotOffsets();
//        if ( !(advSlotOffsets.start == 0 && advSlotOffsets.end == 0) ) {
        if (isValidSlotframeChunk(advChunk.start, advChunk.end, slotframeLength)) {
            clSlotframeChunk.start = advChunk.start;
            clSlotframeChunk.end = advChunk.end;
            EV_DETAIL << "Extracted advertised slotframe chunk: " << clSlotframeChunk << endl;
        }
        else {
            EV_DETAIL << "Branch root calculating slotframe chunk" << endl;
            clSlotframeChunk.end = slotframeLength;
            clSlotframeChunk.start = (uint16_t) (slotframeLength - getNearestChildren().size() * slotframeLength / branchSize);
            EV_DETAIL << "Calculated slot offset range - " << clSlotframeChunk << endl;
        }
        inet::TschSfControlInfo *mci = new inet::TschSfControlInfo();
        mci->setSlotRange(clSlotframeChunk);
        emit(reschedule, 0, (cObject *) mci);
    }

    // forward ctrl dio immediately and reset trickle timer
    sendRplPacket(createDio(), DIO, Ipv6Address::ALL_NODES_1, uniform(1, 2));
    trickleTimer->reset();
}


std::string Rpl::printSlotframeChunks(std::list<inet::SlotframeChunk> &list) {
    std::string os = "{ ";
    for (auto &chunk : list)
        os += chunk.toString() + ", ";
    os += "}";
    return os;
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
    if (code == CROSS_LAYER_CTRL)
        EV_DETAIL << "Preparing to broadcast cross-layer control DIO " << endl;

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
        auto timeout = simTime() + SimTime(daoAckTimeout, SIMTIME_S) * uniform(1, 3); // + delay?

        EV_DETAIL << "Scheduling DAO_ACK timeout at " << timeout << " for advertised dest "
                << advertisedDest << endl;

//        // check if there's already an entry for this packet (more than one retransmission attempt)
//        if (pendingDaoAcks[advertisedDest].first != nullptr) {
//            EV_DETAIL << "Found already existing entry in pending DAO acks map for adv dest -  "
//                    << advertisedDest << endl;
//            scheduleAt(daoTimeoutTimestamp, pendingDaoAcks[advertisedDest].first);
//            EV_DETAIL << "Another DAO timeout scheduled for " << daoTimeoutTimestamp << endl;
//            return;
//        }
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

SlotframeChunk Rpl::calcAdvSlotframeChunk() {
    if (clSlotframeChunk.start == 0 && clSlotframeChunk.end == 0)
        return {0, 0};

    if (branchSize == 0) {
        EV_WARN << "slofOffsets set to " << clSlotframeChunk
                << "but branch size is undefined, cannot calculate"
                << " slot offsets to advertise" << endl;
        return {0, 0};
    }

    if (clSlotframeChunk.start - slotframeLength * getNearestChildren().size() / branchSize > slotframeLength || clSlotframeChunk.start - 1 < 0)
        return { clSlotframeChunk.end, 5 };

    return {
        (uint16_t) (clSlotframeChunk.start - 1),
        (uint16_t) (clSlotframeChunk.start - slotframeLength * getNearestChildren().size() / branchSize)
    };
}

const Ptr<Dio> Rpl::createDio()
{
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

    if (pCrossLayerEnabled) {
        dio->setAdvChOffset(branchChOffset);
        dio->setAdvSlotOffsets(calcAdvSlotframeChunk());
        dio->setSlotframeLength(slotframeLength);
        dio->setBranchSize(branchSize);

        EV_DETAIL << "Cross-layer info included:\n" << "chOffset - "
                << branchChOffset << ", advSlotframeChunk - " << dio->getAdvSlotOffsets()
                << "\nslotframe length - " << slotframeLength << ", branch size - " << branchSize << endl;
    }

    return dio;
}

// @p channelOffset - cross-layer specific parameter, useless in default RPL
const Ptr<Dao> Rpl::createDao(const Ipv6Address &reachableDest, uint8_t channelOffset)
{
    auto dao = makeShared<Dao>();
    dao->setInstanceId(instanceId);
    dao->setChunkLength(b(64));
    dao->setChOffset(channelOffset);
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
    dao->setChOffset(UNDEFINED_CH_OFFSET);
    dao->setSrcAddress(getSelfAddress());
    dao->setReachableDest(reachableDest);
    dao->setSeqNum(daoSeqNum++);
    dao->setNodeId(selfId);
    dao->setDaoAckRequired(ackRequired);
    EV_DETAIL << "Created DAO with seqNum = " << std::to_string(dao->getSeqNum()) << " advertising " << reachableDest << endl;
    return dao;
}

bool Rpl::matchesRequiredParent(const Ipv6Address &addr) {
    // Because decimal repr. of node's own IPv6 address is wrong somehow, we take only
    // last 2 HEX digits and calculate if the advertised address matches the required
    auto mySuffix = getSelfAddress().str().substr(getSelfAddress().str().length() - 2);
    auto advSuffix = addr.str().substr(addr.str().length() - 2);

    if (mySuffix.find(":") != std::string::npos)
        mySuffix = mySuffix.back();
    if (advSuffix.find(":") != std::string::npos)
        advSuffix = advSuffix.back();

    bool doesMatch = (std::stoi(mySuffix, nullptr, 16) - 1) == std::stoi(advSuffix, nullptr, 16);

    return doesMatch || pJoinAtSinkAllowed || !pManualParentAssignment;
}

void Rpl::processDio(const Ptr<const Dio>& dio)
{
    if (isRoot)
        return;
    EV_DETAIL << "Processing DIO from " << dio->getSrcAddress() << endl;
    emit(dioReceivedSignal, dio->dup());

    // If node's not a part of any DODAG, join the one advertised
    if (dodagId == Ipv6Address::UNSPECIFIED_ADDRESS && dio->getRank() != INF_RANK)
    {
        // Artificial restriction to limit the number of nodes joining directly at the sink
        if (dio->getSrcAddress() == dio->getDodagId() && !pJoinAtSinkAllowed)
            return;
        // Allows creating deep branched tree
        if (!matchesRequiredParent(dio->getSrcAddress()))
            return;

        dodagId = dio->getDodagId();
        dodagVersion = dio->getDodagVersion();
        instanceId = dio->getInstanceId();
        storing = dio->getStoring();
        dtsn = dio->getDtsn();
        lastTarget = new Ipv6Address(getSelfAddress());
        selfAddr = getSelfAddress();
        dodagColor = dio->getColor();
        EV_DETAIL << "Joined DODAG with id - " << dodagId << endl;
        // Start broadcasting DIOs, diffusing DODAG control data, TODO: refactor TT lifecycle
        if (trickleTimer->hasStarted())
            trickleTimer->reset();
        else
            trickleTimer->start(false, par("numSkipTrickleIntervalUpdates").intValue());
        if (udpApp)
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

    // TODO: Distinguish CL packets more reliably
    if ( (branchChOffset == UNDEFINED_CH_OFFSET && dio->getAdvChOffset() != UNDEFINED_CH_OFFSET)
                    || (slotframeLength == 0 && dio->getSlotframeLength() != 0) )
    {
        processCrossLayerMsg(dio);
    }
//    // Do not process DIO from unknown DAG/RPL instance or if receiver is root
//    if (checkUnknownDio(dio) || isRoot) {
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

bool Rpl::checkPoisonedParent(const Ptr<const Dio>& dio) {
    return preferredParent && dio->getRank() == INF_RANK
            && matchesSuffix(preferredParent->getSrcAddress(), dio->getSrcAddress());
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
     */
    if (storing || isRoot) {
        if (!checkDestKnown(daoSender, advertisedDest)) {
            updateRoutingTable(daoSender, advertisedDest, prepRouteData(dao.get()));

            // Notify CLSF about new one-hop neighbor to schedule dedicated TX to it
            if (daoSender == advertisedDest && pCrossLayerEnabled && isRoot) {
                emit(oneHopChildJoined, (long) dao->getNodeId());
                // Check if DAO received is from a 1-hop neighbor
                // to allocate unique channel offset per branch
                auto msg = new cMessage("", BRANCH_CH_ADV);
                msg->setContextPointer(new Ipv6Address(advertisedDest));
                scheduleAt(simTime() + uniform(40, 50), msg);
            }

            EV_DETAIL << "Destination learned from DAO - " << advertisedDest
                    << " reachable via " << daoSender << endl;
        }
        else
            return;
    }
    /**
     * Forward DAO 'upwards' via preferred parent advertising destination to the root [RFC6560, 6.4]
     */
    if (!isRoot && preferredParent && !matchesSuffix(daoSender, preferredParent->getSrcAddress())) {
        if (!storing)
            sendRplPacket(createDao(advertisedDest), DAO,
                preferredParent->getSrcAddress(), daoDelay * uniform(1, 2), *lastTarget, *lastTransit);
        else
            sendRplPacket(createDao(advertisedDest), DAO,
                preferredParent->getSrcAddress(), daoDelay * uniform(1, 2));

        EV_DETAIL << "Forwarding DAO to " << preferredParent->getSrcAddress()
                << " advertising " << advertisedDest << " reachability" << endl;
    }


    if (isRoot && pCrossLayerEnabled) {
        // Kick-off phase II of the cross-layer scheduling using
        // heuristics to estimate which DAO is the last one
        if (advertisedDest == terminusNode && !clStartOnTimeout)
            scheduleAt(simTime() + SCHEDULE_PHASE_II_TIMEOUT, new cMessage("", START_CROSS_LAYER_PHASE_II));
    }

}

void Rpl::advertiseChOffset(Ipv6Address child, double delay) {
    if (channelOffsets.empty()) {
        EV_DETAIL << "Refreshing empty channel offset list" << endl;
        initChOffsetList(pTschChOffStart, pTschChOffEnd);
    }
    auto ctrlDao = createDao();
//
//    EV_DETAIL << "Channel offsets available: ";
//    for (auto chof : channelOffsets) {
//        EV_DETAIL << std::to_string(chof) << ", ";
//    }

    auto advChOffset = channelOffsets.back();
    EV_DETAIL << "Advertising chOffset - " << std::to_string(advChOffset)
        << " to branch rooted at " << child << " after " << delay << " s delay" << endl;
    channelOffsets.pop_back();
    ctrlDao->setChOffset((uint8_t) advChOffset);
    sendRplPacket(ctrlDao, DAO_ACK, child, delay);
}

void Rpl::startSecondSchedulingPhase() {
    EV_DETAIL << "Starting CLSF phase 2" << endl;

//    auto children = getNearestChildren();
//    EV_DETAIL << "Found one-hop neighbors: " << children << endl;
//    for (auto ch : children)
//        calcBranchSize(ch);

    slotframeLength = getTschSlotframeLength();
    if (!slotframeLength) {
        EV_WARN << "Cannot proceed with cross-layer scheduling, TSCH slotframe length undefined" << endl;
        return;
    }

    EV_DETAIL << "Slotframe length set to " << slotframeLength << endl;
    auto ctrlDio = createDio();

//
//    auto ctrlMsg = createDio();
//    uint16_t slotframeLen = getSlotframeLength();
//    ctrlMsg->setSlotframeChunks(allocateSlotframeChunks(getNodesPerHopDistance(), getSlotframeLength()));
    sendRplPacket(ctrlDio, DIO, Ipv6Address::ALL_NODES_1, uniform(1, 2));
    trickleTimer->reset();
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

// TODO: Handle case with multiple wireless interfaces
int Rpl::getTschSlotframeLength() {
//    return getModuleByPath("^.wlan[0].mac.sixtischInterface.sf")->par("slotframeLen").intValue();
    auto tschSchedule = getModuleByPath("^.wlan[0].mac.schedule");
    if (tschSchedule)
        return tschSchedule->par("macSlotframeSize").intValue();
    return 0;
}

int Rpl::getNumTschChannels() {
    auto sixpInt = getModuleByPath("^.wlan[0].mac.sixtischInterface");
    if (sixpInt)
        return sixpInt->par("nbRadioChannels").intValue();
    return 0;
}

SlotframeChunkList Rpl::allocateSlotframeChunks(std::vector<uint16_t> nodesPerLevel, uint16_t slotframeLen) {
    uint16_t weight = static_cast<uint16_t>(slotframeLen / std::accumulate(nodesPerLevel.begin(), nodesPerLevel.end(), 0));
    uint16_t currentPos = 0;
    SlotframeChunkList slotframeChunks;
    EV_DETAIL << "Weight per level - " << weight << endl;
//
    for (auto& numNodes : nodesPerLevel) {
        uint16_t start = currentPos + 1;
        uint16_t end = currentPos + numNodes * weight;
        slotframeChunks.push_front({ static_cast<uint16_t> (start) , static_cast<uint16_t>(end >= 100 ? 99 : end)});
        currentPos += numNodes * weight;
    }
//
    EV_DETAIL << "allocated slotframe chunks list - " << printSlotframeChunks(slotframeChunks) << endl;

    return slotframeChunks;
}


// deprecated, to be replaced by default L3Address matches() method
bool Rpl::matchesSuffix(const Ipv6Address &addr1, const Ipv6Address &addr2, int prefixLength) {
    return addr1.getSuffix(prefixLength) == addr2.getSuffix(prefixLength);
}

void Rpl::processDaoAck(const Ptr<const Dao>& daoAck) {
    auto advDest = daoAck->getReachableDest();

    EV_INFO << "Received DAO_ACK from " << daoAck->getSrcAddress()
            << " for advertised dest - "  << advDest << endl;

    if (pCrossLayerEnabled) {
        if (branchChOffset == UNDEFINED_CH_OFFSET && daoAck->getChOffset() != UNDEFINED_CH_OFFSET)
        {
            branchChOffset = daoAck->getChOffset();
            EV_DETAIL << "Channel offset extracted from DAO_ACK (fixed for the branch) - "
                    << std::to_string(branchChOffset) << endl;;
            emit(setChOffset, (long) branchChOffset);
            return;
        }
    }
    if (pendingDaoAcks.empty()) {
        EV_DETAIL << "No DAO_ACKs were expected!" << endl;
        return;
    }

    clearDaoAckTimer(advDest);

//    EV_DETAIL << "pending dao acks: " << endl;
//    for (auto e : pendingDaoAcks)
//        EV_DETAIL << e.first << " rtxs: " << std::to_string(e.second.second)
//            << ", msg ptr - " << e.second.first << endl;
//
//    EV_DETAIL << "advertised destination - " << advDest <<
//            " pendingDaoAcks[advDest] - " << pendingDaoAcks[advDest].first << endl;
//
//    if (pendingDaoAcks.count(advDest) && pendingDaoAcks[advDest].first == nullptr) {
//        EV_WARN << "No control message pointer attached to pending DAO ack for "
//                << advDest << ", removing entry" << endl;
//        pendingDaoAcks.erase(advDest);
//        return;
//    }
//
//    auto msgToCancel = pendingDaoAcks[advDest].first;
//    EV_DETAIL << "msgToCancel scheduled (before cancellation)? - "
//            << boolStr(pendingDaoAcks[advDest].first->isScheduled()) << endl;
//
//    if (msgToCancel)
//        cancelEvent(msgToCancel);
//    EV_DETAIL << "msgToCancel scheduled (after cancellation)? - "
//                << boolStr(pendingDaoAcks[advDest].first->isScheduled()) << endl;
//    pendingDaoAcks.erase(advDest);
    EV_DETAIL << " (Should've) Cancelled timeout event and erased entry in the pendingDaoAcks map" << endl;

    EV_DETAIL << "remaining dao acks: " << endl;
    for (auto e : pendingDaoAcks)
        EV_DETAIL << e.first << " rtxs: " << std::to_string(e.second.second)
            << ", msg ptr - " << e.second.first << endl;

}

void Rpl::drawConnector(Coord target, cFigure::Color col) {
    // (0, 0) corresponds to default Coord constructor, meaning no target position was provided
    if (!target.x && !target.y || !par("drawConnectors").boolValue())
        return;

    cCanvas *canvas = getParentModule()->getParentModule()->getCanvas();
    if (prefParentConnector) {
        prefParentConnector->setLineColor(col);
        prefParentConnector->setEnd(cFigure::Point(target.x, target.y));
        return;
    }

    prefParentConnector = new cLineFigure("preferredParentConnector");
    prefParentConnector->setStart(cFigure::Point(position.x, position.y));
    prefParentConnector->setEnd(cFigure::Point(target.x, target.y));
    prefParentConnector->setLineWidth(2);
    prefParentConnector->setLineColor(col);
    prefParentConnector->setLineOpacity(0.6);
    prefParentConnector->setEndArrowhead(cFigure::ARROW_BARBED);
    prefParentConnector->setVisible(true);
    canvas->addFigure(prefParentConnector);
}

void Rpl::updatePreferredParent()
{
    Dio *newPrefParent;
    EV_DETAIL << "Choosing preferred parent from "
            << boolStr(candidateParents.empty() && par("useBackupAsPreferred").boolValue(),
                    "backup", "candidate") << " parent set" << endl;
    /**
     * Choose parent from candidate neighbor set or leave DODAG if it's empty
     */
    if (candidateParents.empty())
        if (par("useBackupAsPreferred").boolValue())
            newPrefParent = objectiveFunction->getPreferredParent(backupParents, preferredParent);
        else {
            detachFromDodag();
            return;
        }
    else
        newPrefParent = objectiveFunction->getPreferredParent(candidateParents, preferredParent);

    auto newPrefParentAddr = newPrefParent->getSrcAddress();
    /**
     * If a better preferred parent has been discovered (based on objective function's metric),
     * update default route to DODAG sink with the new nextHop
     */
    if (checkPrefParentChanged(newPrefParentAddr)) {
        // Hacky ways to achieve required topology / stability
        if (newPrefParentAddr == dodagId && !pJoinAtSinkAllowed) // -----//-------
            return;
        if (preferredParent && par("disableParentSwitching").boolValue()) // preventing
            return;
        if (!matchesRequiredParent(newPrefParentAddr))
            return;

        auto newPrefParentDodagId = newPrefParent->getDodagId();
        /** Silently join new DODAG and update UdpApp dest address, TODO: Check with RFC 6550 */
        dodagId = newPrefParentDodagId;
        if (udpApp)
            udpApp->par("destAddresses") = newPrefParentDodagId.str();

        emit(parentChangedSignal, (long) newPrefParent->getNodeId());
        clearParentRoutes();
        daoSeqNum = 0;
        drawConnector(newPrefParent->getPosition(), newPrefParent->getColor());

        updateRoutingTable(newPrefParentAddr, dodagId, nullptr, true);
        lastTransit = new Ipv6Address(newPrefParentAddr);
        EV_DETAIL << "Updated preferred parent to - " << newPrefParentAddr << endl;
        /**
         * Reset trickle timer due to inconsistency (preferred parent changed) detected, thus
         * maintaining higher topology reactivity and convergence rate [RFC 6550, 8.3]
         */
        trickleTimer->reset();
        if (daoEnabled) {
            if (storing)
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
    rank = objectiveFunction->calcRank(preferredParent);
    EV_DETAIL << "Rank - " << rank << endl;
}

bool Rpl::checkPrefParentChanged(const Ipv6Address &newPrefParentAddr)
{
    if (!preferredParent) {
        EV_DETAIL << "Pref. parent not set, HAS CHANGED " << endl;
        return true;
    }

    return !matchesSuffix(newPrefParentAddr, preferredParent->getSrcAddress());
}


//
// Handling routing data
//

//void Rpl::updateRoutingTable(const Dao *dao) {
//    auto routeData = new RplRouteData();
//    routeData->setDodagId(dao->getDodagId());
//    routeData->setInstanceId(dao->getInstanceId());
//    routeData->setDtsn(dao->getSeqNum());
//    routeData->setExpirationTime(-1);
//    updateRoutingTable(dao->getSrcAddress(), dao->getReachableDest(), routeData);
//}

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

void Rpl::appendSrcRoutingHeader(Packet *datagram) {
    Ipv6Address dest = findNetworkProtocolHeader(datagram)->getDestinationAddress().toIpv6();
    std::deque<Ipv6Address> srhAddresses;
    EV_DETAIL << "Appending routing header to datagram " << datagram << endl;
    if ( sourceRoutingTable.find(dest) == sourceRoutingTable.end() ) {
        EV_WARN << "Required destination " << dest << " not yet present in source-routing table: \n"
                << printMap(sourceRoutingTable) << endl;
        return;
    }

    Ipv6Address nextHop = sourceRoutingTable[dest];
    srhAddresses.push_front(dest);
    srhAddresses.push_front(nextHop);
    EV_DETAIL << "constructing routing header for dest - " << dest
            << "\n next hop: " << nextHop << endl;
    EV_DETAIL << "Current source-routing table: \n" << printMap(sourceRoutingTable) << endl;

    while ( sourceRoutingTable.find(nextHop) != sourceRoutingTable.end() )
    {
        nextHop = sourceRoutingTable[nextHop];
        srhAddresses.push_front(nextHop);
    }
    // pop unnecessary link-local next hop, which is already known from the routing table
    srhAddresses.pop_front();

    EV_DETAIL << "Source routing header constructed : " << endl;
    for (auto i : srhAddresses) {
        EV_DETAIL << i << " => ";
    }

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


    if (srhAddresses.back().matches(getSelfAddress(), prefixLength)) {
        EV_DETAIL << "Source-routed destination "
                << getSelfAddress() << " reached" << endl;
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
    // TODO: Figure out why dequeue remains unchanged at each next hop,
    // regardless of explicitly removing element from the back
    srhAddresses.pop_front();
//
//    // re-insert updated routing header
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
            << boolStr(poisoned, " detachment", " unreachability") << "detected" << endl;
    emit(parentUnreachableSignal, preferredParent);
    clearParentRoutes();
    candidateParents.erase(prefParentAddr.getSuffix(prefixLength));
    preferredParent = nullptr;
    EV_DETAIL << "Erased preferred parent from candidate parent set" << endl;
}

void Rpl::clearParentRoutes() {
    if (!preferredParent) {
        EV_WARN << "Pref. parent not set, cannot delete associate routes from routing table " << endl;
        return;
    }

    Ipv6Route *routeToDelete;
    routingTable->deleteDefaultRoutes(interfaceEntryPtr->getInterfaceId());
    EV_DETAIL << "Deleted default route through preferred parent " << endl;
    auto totalRoutes = routingTable->getNumRoutes();
    for (int i = 0; i < totalRoutes; i++) {
        auto ri = routingTable->getRoute(i);
        if (matchesSuffix(ri->getNextHop(), preferredParent->getSrcAddress()))
            routeToDelete = ri;
    }
    if (routeToDelete)
        routingTable->deleteRoute(routeToDelete);
    EV_DETAIL << "Deleted non-default route through preferred parent " << endl;
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
            EV_DETAIL << "Connection with destination " << destination << " broken?" << endl;

            /**
             * If preferred parent unreachability detected, remove route with it as a
             * next hop from the routing table and select new preferred parent from the
             * candidate set.
             *
             * If candidate parent set is empty, leave current DODAG
             * (becoming either floating DODAG or poison child routes altogether)
             */
            if (preferredParent && destination == preferredParent->getSrcAddress()
                    && par("unreachabilityDetectionEnabled").boolValue())
            {
                deletePrefParent();
                updatePreferredParent();
            }
        }
    }
}

} // namespace inet



