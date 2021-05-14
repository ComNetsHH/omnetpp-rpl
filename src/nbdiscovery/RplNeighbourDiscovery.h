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

#ifndef __RPLNEIGHBOURDISCOVERY_H
#define __RPLNEIGHBOURDISCOVERY_H

#include <map>
#include <set>
#include <vector>

#include "inet/common/lifecycle/LifecycleUnsupported.h"
#include "inet/common/packet/Packet.h"
#include "inet/networklayer/contract/ipv6/Ipv6Address.h"
#include "inet/networklayer/icmpv6/Ipv6NdMessage_m.h"
#include "inet/networklayer/icmpv6/Ipv6NeighbourDiscovery.h"
#include "inet/networklayer/icmpv6/Ipv6NeighbourCache.h"
#include "inet/transportlayer/common/CrcMode_m.h"

namespace inet {

/**
 * Extends RFC 2461 Neighbor Discovery for Ipv6.
 */
class RplNeighbourDiscovery: public Ipv6NeighbourDiscovery
{
  protected:
    virtual void processNaForIncompleteNceState(const Ipv6NeighbourAdvertisement *na, Ipv6NeighbourCache::Neighbour *nce) override;
    virtual void processNaForOtherNceStates(const Ipv6NeighbourAdvertisement *na, Ipv6NeighbourCache::Neighbour *nce) override;
};

} // namespace inet

#endif    //__RPLNEIGHBOURDISCOVERY_H_H

