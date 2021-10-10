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

#include "Ipv6NeighbourDiscoveryWireless.h"
#include "inet/networklayer/contract/IInterfaceTable.h"
#include "inet/networklayer/icmpv6/Icmpv6.h"
#include "inet/networklayer/ipv6/Ipv6Header.h"
#include "inet/networklayer/ipv6/Ipv6InterfaceData.h"
#include "inet/networklayer/ipv6/Ipv6RoutingTable.h"

Define_Module(Ipv6NeighbourDiscoveryWireless);

void Ipv6NeighbourDiscoveryWireless::initialize(int stage)
{
    Ipv6NeighbourDiscovery::initialize(stage);

    if (stage == INITSTAGE_LOCAL) {
        naSolicitedPacketSent = registerSignal("naSolicitedPacketSent");
        naUnsolicitedPacketSent = registerSignal("naUnsolicitedPacketSent");
        nsPacketSent = registerSignal("nsPacketSent");
        raPacketSent = registerSignal("raPacketSent");
        nsFwdDelay = par("nsForwardingDelay").doubleValue();
        pAddRandomDelays = par("addRandomDelays").boolValue();
        nudInitated = registerSignal("nudInitiated");
    }
}


void Ipv6NeighbourDiscoveryWireless::sendUnsolicitedNa(InterfaceEntry *ie)
{
    Ipv6NeighbourDiscovery::sendUnsolicitedNa(ie);
    emit(naUnsolicitedPacketSent, 1);
}

void Ipv6NeighbourDiscoveryWireless::createRaTimer(InterfaceEntry *ie)
{
    if (par("raEnabled").boolValue())
        Ipv6NeighbourDiscovery::createRaTimer(ie);
    else
        EV_DETAIL << "Skipping RA timer" << endl;
}

void Ipv6NeighbourDiscoveryWireless::sendSolicitedNa(Packet *packet,
            const Ipv6NeighbourSolicitation *ns, InterfaceEntry *ie)
{
    Ipv6NeighbourDiscovery::sendSolicitedNa(packet, ns, ie);
    emit(naSolicitedPacketSent, 1);
}

void Ipv6NeighbourDiscoveryWireless::createAndSendRaPacket(const Ipv6Address& destAddr, InterfaceEntry *ie) {
    Ipv6NeighbourDiscovery::createAndSendRaPacket(destAddr, ie);

    emit(raPacketSent, 1);
}

void Ipv6NeighbourDiscoveryWireless::handleMessage(cMessage *msg) {
    if (msg->isSelfMessage() && msg->getKind() == WIND_SEND_DELAYED) {
        auto controlInfo = check_and_cast<Ipv6NdPacketInfo*> (msg->getControlInfo());

        EV_DETAIL << "Sending NS delayed"
                << "dest: " << controlInfo->getDestAddr() << endl
                << "src: " << controlInfo->getSrcAddr() <<  endl
                << "ie id: " << controlInfo->getInterfaceId() << endl;

        Ipv6NeighbourDiscovery::sendPacketToIpv6Module(
            controlInfo->getMsgPtr(), controlInfo->getDestAddr(), controlInfo->getSrcAddr(), controlInfo->getInterfaceId());

        delete msg;
    } else
        Ipv6NeighbourDiscovery::handleMessage(msg);
}

void Ipv6NeighbourDiscoveryWireless::processNaForOtherNceStates(const Ipv6NeighbourAdvertisement *na, Neighbour *nce)
{
    bool naRouterFlag = na->getRouterFlag();
    bool naSolicitedFlag = na->getSolicitedFlag();
    bool naOverrideFlag = na->getOverrideFlag();
    MacAddress naMacAddr;
    if (auto tla = check_and_cast_nullable<const Ipv6NdTargetLinkLayerAddress*>(na->getOptions().findOption(IPv6ND_TARGET_LINK_LAYER_ADDR_OPTION)))
        naMacAddr = tla->getLinkLayerAddress();
    const Key *nceKey = nce->nceKey;
    InterfaceEntry *ie = ift->getInterfaceById(nceKey->interfaceID);

    /*draft-ietf-ipv6-2461bis-04
       Section 7.2.5: Receipt of Neighbour Advertisements
       If the target's Neighbor Cache entry is in any state other than INCOMPLETE
       when the advertisement is received, the following actions take place:*/

    if (naOverrideFlag == false && !(naMacAddr.equals(nce->macAddress))
        && !(naMacAddr.isUnspecified()))
    {
        EV_INFO << "NA override is FALSE and NA MAC addr is different.\n";

        //I. If the Override flag is clear and the supplied link-layer address
        //   differs from that in the cache, then one of two actions takes place:
        //(Note: An unspecified MAC should not be compared with the NCE's mac!)
        //a. If the state of the entry is REACHABLE,
        if (nce->reachabilityState == Ipv6NeighbourCache::REACHABLE) {
            EV_INFO << "NA mac is different. Change NCE state from REACHABLE to STALE\n";
            //set it to STALE, but do not update the entry in any other way.
            nce->reachabilityState = Ipv6NeighbourCache::STALE;
        }
        else
            //b. Otherwise, the received advertisement should be ignored and
            //MUST NOT update the cache.
            EV_INFO << "NCE is not in REACHABLE state. Ignore NA.\n";
    }
    else if (naOverrideFlag == true || naMacAddr.equals(nce->macAddress)
             || naMacAddr.isUnspecified())
    {
        EV_INFO << "NA override flag is TRUE. or Advertised MAC is same as NCE's. or"
                << " NA MAC is not specified.\n";
        /*II. If the Override flag is set, or the supplied link-layer address
           is the same as that in the cache, or no Target Link-layer address
           option was supplied, the received advertisement MUST update the
           Neighbor Cache entry as follows:*/

        /*- The link-layer address in the Target Link-Layer Address option
            MUST be inserted in the cache (if one is supplied and is
            Different than the already recorded address).*/
        if (!(naMacAddr.isUnspecified()) && !(naMacAddr.equals(nce->macAddress))) {
            EV_INFO << "Updating NCE's MAC addr with NA's.\n";
            nce->macAddress = naMacAddr;
        }

        //- If the Solicited flag is set,
        if (naSolicitedFlag == true) {
            EV_INFO << "Solicited Flag is TRUE. Set NCE state to REACHABLE.\n";
            //the state of the entry MUST be set to REACHABLE.
            nce->reachabilityState = Ipv6NeighbourCache::REACHABLE;
            //We have to cancel the NUD self timer message if there is one.

            cMessage *msg = nce->nudTimeoutEvent;
            if (msg != nullptr) {
                EV_INFO << "NUD in progress. Cancelling NUD Timer\n";
                bubble("Reachability Confirmed via NUD.");
                nce->reachabilityExpires = simTime() + ie->getProtocolData<Ipv6InterfaceData>()->_getReachableTime()
                        + par("nceExpiryOverride");
                EV_DETAIL << "Reachability expires at " << nce->reachabilityExpires << endl;
                cancelAndDelete(msg);
                nce->nudTimeoutEvent = nullptr;
            }
        }
        else {
            //If the Solicited flag is zero
            EV_INFO << "Solicited Flag is FALSE.\n";
            //and the link layer address was updated with a different address

            if (!(naMacAddr.equals(nce->macAddress))) {
                EV_INFO << "NA's MAC is different from NCE's.Set NCE state to STALE\n";
                //the state MUST be set to STALE.
                nce->reachabilityState = Ipv6NeighbourCache::STALE;
            }
            else
                //Otherwise, the entry's state remains unchanged.
                EV_INFO << "NA's MAC is the same as NCE's. State remains unchanged.\n";
        }
        //(Next paragraph with explanation is omitted.-WEI)

        /*- The IsRouter flag in the cache entry MUST be set based on the
           Router flag in the received advertisement.*/
        EV_INFO << "Updating NCE's router flag to " << naRouterFlag << endl;
        nce->isRouter = naRouterFlag;

        /*In those cases where the IsRouter flag changes from TRUE to FALSE as a
           result of this update, the node MUST remove that router from the Default
           Router List and update the Destination Cache entries for all destinations
           using that neighbor as a router as specified in Section 7.3.3. This is
           needed to detect when a node that is used as a router stops forwarding
           packets due to being configured as a host.*/
        if (nce->isDefaultRouter() && !nce->isRouter)
            neighbourCache.getDefaultRouterList().remove(*nce);

        //TODO: remove destination cache entries
    }
}


void Ipv6NeighbourDiscoveryWireless::assignLinkLocalAddress(cMessage *timerMsg) {
    //Node has booted up. Start assigning a link-local address for each
    //interface in this node.
    for (int i = 0; i < ift->getNumInterfaces(); i++) {
        InterfaceEntry *ie = ift->getInterface(i);

        //Skip the loopback interface.
        if (ie->isLoopback())
            continue;

        Ipv6Address linkLocalAddr = ie->getProtocolData<Ipv6InterfaceData>()->getLinkLocalAddress();
        if (linkLocalAddr.isUnspecified()) {
            //if no link local address exists for this interface, we assign one to it.
            EV_INFO << "No link local address exists. Forming one" << endl;
            linkLocalAddr = Ipv6Address().formLinkLocalAddress(ie->getInterfaceToken());
            EV_DETAIL << "Formed link local address (and assigned permanent) - " << linkLocalAddr << endl;
            ie->getProtocolData<Ipv6InterfaceData>()->assignAddress(linkLocalAddr, true, SIMTIME_ZERO, SIMTIME_ZERO);
            makeTentativeAddressPermanent(linkLocalAddr, ie);
        }
    }
    delete timerMsg;
}

void Ipv6NeighbourDiscoveryWireless::initiateAddressResolution(const Ipv6Address& dgSrcAddr, Neighbour *nce)
{
    const Key *nceKey = nce->nceKey;
    InterfaceEntry *ie = ift->getInterfaceById(nceKey->interfaceID);
    Ipv6Address neighbourAddr = nceKey->address;
    int ifID = nceKey->interfaceID;

    //RFC2461: Section 7.2.2
    //When a node has a unicast packet to send to a neighbor, but does not
    //know the neighbor's link-layer address, it performs address
    //resolution.  For multicast-capable interfaces this entails creating a
    //Neighbor Cache entry in the INCOMPLETE state(already created if not done yet)
    //WEI-If entry already exists, we still have to ensure that its state is INCOMPLETE.
    nce->reachabilityState = Ipv6NeighbourCache::INCOMPLETE;

    //and transmitting a Neighbor Solicitation message targeted at the
    //neighbor.  The solicitation is sent to the solicited-node multicast
    //address "corresponding to"(or "derived from") the target address.
    //(in this case, the target address is the address we are trying to resolve)
    EV_INFO << "Preparing to send NS to solicited-node multicast group\n";
    EV_INFO << "on the next hop interface\n";
    Ipv6Address nsDestAddr = neighbourAddr.formSolicitedNodeMulticastAddress();    //for NS datagram
    Ipv6Address nsTargetAddr = neighbourAddr;    //for the field within the NS
    Ipv6Address nsSrcAddr;

    /*If the source address of the packet prompting the solicitation is the
       same as one of the addresses assigned to the outgoing interface,*/
    if (ie->getProtocolData<Ipv6InterfaceData>()->hasAddress(dgSrcAddr))
        /*that address SHOULD be placed in the IP Source Address of the outgoing
           solicitation.*/
        nsSrcAddr = dgSrcAddr;
    else
        /*Otherwise, any one of the addresses assigned to the interface
           should be used.*/
        nsSrcAddr = ie->getProtocolData<Ipv6InterfaceData>()->getPreferredAddress();
    ASSERT(ifID != -1);
    //Sending NS on specified interface.
    createAndSendNsPacket(nsTargetAddr, nsDestAddr, nsSrcAddr, ie);
    nce->numOfARNSSent = 1;
    nce->nsSrcAddr = nsSrcAddr;

    /*While awaiting a response, the sender SHOULD retransmit Neighbor Solicitation
       messages approximately every RetransTimer milliseconds, even in the absence
       of additional traffic to the neighbor. Retransmissions MUST be rate-limited
       to at most one solicitation per neighbor every RetransTimer milliseconds.*/
    cMessage *msg = new cMessage("arTimeout", MK_AR_TIMEOUT);    //AR msg timer
    nce->arTimer = msg;
    msg->setContextPointer(nce);

    auto arTimeout = simTime() + ie->getProtocolData<Ipv6InterfaceData>()->_getRetransTimer() + par("nsForwardingDelay");
    EV_DETAIL << "AR timeout scheduled at " << arTimeout << "s" << endl;

    // Increase the timeout value if extra delays are used for NS packet forwarding!
    scheduleAt(arTimeout, msg);
}

void Ipv6NeighbourDiscoveryWireless::createAndSendNsPacket(const Ipv6Address& nsTargetAddr, const Ipv6Address& dgDestAddr,
                const Ipv6Address& dgSrcAddr, InterfaceEntry *ie)
{
    MacAddress myMacAddr = ie->getMacAddress();

    //Construct a Neighbour Solicitation message
    auto ns = makeShared<Ipv6NeighbourSolicitation>();

    //Neighbour Solicitation Specific Information
    ns->setTargetAddress(nsTargetAddr);

    /*If the solicitation is being sent to a solicited-node multicast
       address, the sender MUST include its link-layer address (if it has
       one) as a Source Link-Layer Address option.*/
    if (dgDestAddr.matches(Ipv6Address("FF02::1:FF00:0"), 104) &&
            !dgSrcAddr.isUnspecified()) {
        auto sla = new Ipv6NdSourceLinkLayerAddress();
        sla->setLinkLayerAddress(myMacAddr);
        ns->getOptionsForUpdate().insertOption(sla);
        ns->addChunkLength(IPv6ND_LINK_LAYER_ADDRESS_OPTION_LENGTH);
    }
    auto packet = new Packet("NSpacket");
    Icmpv6::insertCrc(crcMode, ns, packet);
    packet->insertAtFront(ns);

    sendPacketToIpv6Module(packet, dgDestAddr, dgSrcAddr, ie->getInterfaceId(), nsFwdDelay ? uniform(0, nsFwdDelay) : 0);
    emit(nsPacketSent, 1);
}

void Ipv6NeighbourDiscoveryWireless::initiateNeighbourUnreachabilityDetection(Neighbour *nce) {
    emit(nudInitated, 1);
    Ipv6NeighbourDiscovery::initiateNeighbourUnreachabilityDetection(nce);
}

void Ipv6NeighbourDiscoveryWireless::processNaForIncompleteNceState(const Ipv6NeighbourAdvertisement *na, Neighbour *nce)
{
    MacAddress naMacAddr;
    if (auto tla = check_and_cast_nullable<const Ipv6NdTargetLinkLayerAddress*>(na->getOptions().findOption(IPv6ND_TARGET_LINK_LAYER_ADDR_OPTION)))
        naMacAddr = tla->getLinkLayerAddress();
    bool naRouterFlag = na->getRouterFlag();
    bool naSolicitedFlag = na->getSolicitedFlag();
    const Key *nceKey = nce->nceKey;
    InterfaceEntry *ie = ift->getInterfaceById(nceKey->interfaceID);

    /*If the target's neighbour Cache entry is in the INCOMPLETE state when the
       advertisement is received, one of two things happens.*/
    if (naMacAddr.isUnspecified()) {
        /*If the link layer has addresses and no Target Link-Layer address option
           is included, the receiving node SHOULD silently discard the received
           advertisement.*/
        EV_INFO << "No MAC Address specified in NA. Ignoring NA\n";
        return;
    }
    else {
        //Otherwise, the receiving node performs the following steps:
        //- It records the link-layer address in the neighbour Cache entry.
        EV_INFO << "ND is updating Neighbour Cache Entry.\n";
        nce->macAddress = naMacAddr;

        //- If the advertisement's Solicited flag is set, the state of the
        //  entry is set to REACHABLE, otherwise it is set to STALE.
        if (naSolicitedFlag == true) {
            nce->reachabilityState = Ipv6NeighbourCache::REACHABLE;
            EV_INFO << "Reachability confirmed through successful Addr Resolution.\n";
            nce->reachabilityExpires = simTime() + ie->getProtocolData<Ipv6InterfaceData>()->_getReachableTime()
                    + par("nceExpiryOverride");
            EV_DETAIL << "Reachability expires at " << nce->reachabilityExpires << endl;
        }
        else
            nce->reachabilityState = Ipv6NeighbourCache::STALE;

        //- It sets the IsRouter flag in the cache entry based on the Router
        //  flag in the received advertisement.
        nce->isRouter = naRouterFlag;
        if (nce->isDefaultRouter() && !nce->isRouter)
            neighbourCache.getDefaultRouterList().remove(*nce);

        //- It sends any packets queued for the neighbour awaiting address
        //  resolution.
        sendQueuedPacketsToIpv6Module(nce);
        cancelAndDelete(nce->arTimer);
        nce->arTimer = nullptr;
    }
}

void Ipv6NeighbourDiscoveryWireless::processIpv6Datagram(Packet *packet)
{
    const auto& msg = packet->peekAtFront<Ipv6Header>();
    EV_INFO << "WiND: Packet " << packet << " arrived from Ipv6 module.\n";

    Ipv6NdControlInfo *ctrl = check_and_cast<Ipv6NdControlInfo *>(packet->getControlInfo());
    int nextHopIfID = ctrl->getInterfaceId();
    Ipv6Address nextHopAddr = ctrl->getNextHop();
    //bool fromHL = ctrl->getFromHL();

    if (nextHopIfID == -1 || nextHopAddr.isUnspecified()) {
        EV_INFO << "Determining Next Hop" << endl;
        nextHopAddr = determineNextHop(msg->getDestAddress(), nextHopIfID);
        ctrl->setInterfaceId(nextHopIfID);
        ctrl->setNextHop(nextHopAddr);
    }

    if (nextHopIfID == -1) {
        //draft-ietf-ipv6-2461bis-04 has omitted on-link assumption.
        //draft-ietf-v6ops-onlinkassumption-03 explains why.
        delete packet->removeControlInfo();
        icmpv6->sendErrorMessage(packet, ICMPv6_DESTINATION_UNREACHABLE, NO_ROUTE_TO_DEST);
        return;
    }

    EV_INFO << "Next Hop Address is: " << nextHopAddr << " on interface: " << nextHopIfID << endl;

    //RFC2461: Section 5.2 Conceptual Sending Algorithm
    //Once the IP address of the next-hop node is known, the sender examines the
    //Neighbor Cache for link-layer information about that neighbor.
    Neighbour *nce = neighbourCache.lookup(nextHopAddr, nextHopIfID);

    if (nce == nullptr) {
        EV_INFO << "No Entry exists in the Neighbour Cache.\n";
        InterfaceEntry *ie = ift->getInterfaceById(nextHopIfID);
        if (ie->isPointToPoint()) {
            //the sender creates one, sets its state to STALE,
            EV_DETAIL << "Creating an STALE entry in the neighbour cache.\n";
            nce = neighbourCache.addNeighbour(nextHopAddr, nextHopIfID, MacAddress::UNSPECIFIED_ADDRESS);
        }
        else {
            //the sender creates one, sets its state to INCOMPLETE,
            EV_DETAIL << "Creating an INCOMPLETE entry in the neighbour cache.\n";
            nce = neighbourCache.addNeighbour(nextHopAddr, nextHopIfID);

            //initiates Address Resolution,
            EV_DETAIL << "Initiating Address Resolution for:" << nextHopAddr
                      << " on Interface:" << nextHopIfID << endl;
            initiateAddressResolution(msg->getSrcAddress(), nce);
        }
    }

    /*
     * A host is capable of sending packets to a destination in all states except INCOMPLETE
     * or when there is no corresponding NC entry. In INCOMPLETE state the data packets are
     * queued pending completion of address resolution.
     */
    switch (nce->reachabilityState) {
    case Ipv6NeighbourCache::INCOMPLETE:
        EV_INFO << "Reachability State is INCOMPLETE. Address Resolution already initiated.\n";
        EV_INFO << "Add packet to entry's queue until Address Resolution is complete.\n";
        bubble("Packet added to queue until Address Resolution is complete.");
        nce->pendingPackets.push_back(packet);
        pendingQueue.insert(packet);
        break;
    case Ipv6NeighbourCache::STALE:
        EV_INFO << "Reachability State is STALE.\n";
        send(packet, "ipv6Out");
        initiateNeighbourUnreachabilityDetection(nce);
        break;
    case Ipv6NeighbourCache::REACHABLE:
        EV_INFO << "Next hop is REACHABLE, sending packet to next-hop address.";
        if (pAddRandomDelays)
            sendDelayed(packet, uniform(WIND_MICRO_DELAY_MIN, WIND_MICRO_DELAY_MAX), "ipv6Out");
        else
            send(packet, "ipv6Out");
        break;
    case Ipv6NeighbourCache::DELAY:
        EV_INFO << "Next hop is in DELAY state, sending packet to next-hop address.";
        if (pAddRandomDelays)
            sendDelayed(packet, uniform(WIND_MICRO_DELAY_MIN, WIND_MICRO_DELAY_MAX), "ipv6Out");
        else
            send(packet, "ipv6Out");
        break;
    case Ipv6NeighbourCache::PROBE:
        EV_INFO << "Next hop is in PROBE state, sending packet to next-hop address.";
        if (pAddRandomDelays)
            sendDelayed(packet, uniform(WIND_MICRO_DELAY_MIN, WIND_MICRO_DELAY_MAX), "ipv6Out");
        else
            send(packet, "ipv6Out");
        break;
    default:
        throw cRuntimeError("Unknown Neighbour cache entry state.");
        break;
    }
}


void Ipv6NeighbourDiscoveryWireless::sendPacketToIpv6Module(Packet *msg, const Ipv6Address& destAddr, const Ipv6Address& srcAddr, int interfaceId, double delay)
{
    if (!delay)
        Ipv6NeighbourDiscovery::sendPacketToIpv6Module(msg, destAddr, srcAddr, interfaceId);
    else
    {
        EV_DETAIL << "Preparing WiND delayed\n"
                << "dest: " << destAddr << endl
                << "src: " << srcAddr << endl
                << "ie id:" << interfaceId << endl
                << "delay: " << delay << endl;

        auto selfmsg = new cMessage("WiND delayed packet");
        auto controlInfo = new Ipv6NdPacketInfo(msg, destAddr, srcAddr, interfaceId);
        selfmsg->setControlInfo(controlInfo);
        selfmsg->setKind(WIND_SEND_DELAYED);

        scheduleAt(simTime() + SimTime(delay, SIMTIME_S), selfmsg);
    }
}


