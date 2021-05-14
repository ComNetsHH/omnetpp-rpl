

import inet.networklayer.icmpv6.Ipv6NeighbourDiscovery;

package rpl.nbdiscovery;

@namespace(inet);

simple RplNeighbourDiscovery extends Ipv6NeighbourDiscovery
{
    parameters:
        @class(RplNeighbourDiscovery);
        double reachabilityOverride @unit(s) = default(10000s);
}

