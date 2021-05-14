/**
 * Copyright (C) 2005 Andras Varga
 * Copyright (C) 2005 Wei Yang, Ng
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "inet/common/ModuleAccess.h"
#include "inet/common/ProtocolTag_m.h"
#include "inet/common/lifecycle/NodeStatus.h"
#include "inet/linklayer/common/InterfaceTag_m.h"
#include "inet/networklayer/common/HopLimitTag_m.h"
#include "inet/networklayer/common/L3AddressTag_m.h"
#include "inet/networklayer/contract/IInterfaceTable.h"
#include "inet/networklayer/icmpv6/Icmpv6.h"
#include "inet/networklayer/ipv6/Ipv6Header.h"
#include "inet/networklayer/ipv6/Ipv6InterfaceData.h"
#include "inet/networklayer/ipv6/Ipv6RoutingTable.h"
#include "RplNeighbourDiscovery.h"

namespace inet {

Define_Module(RplNeighbourDiscovery);

void RplNeighbourDiscovery::processNaForIncompleteNceState(const Ipv6NeighbourAdvertisement *na, Neighbour *nce)
{
    EV_DETAIL << "Yeah baby!" << endl;

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
            nce->reachabilityExpires = simTime() + ie->getProtocolData<Ipv6InterfaceData>()->_getReachableTime() + par("reachabilityOverride");
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

void RplNeighbourDiscovery::processNaForOtherNceStates(const Ipv6NeighbourAdvertisement *na, Neighbour *nce)
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
                nce->reachabilityExpires = simTime() + ie->getProtocolData<Ipv6InterfaceData>()->_getReachableTime() + par("reachabilityOverride");
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

} // namespace inet

