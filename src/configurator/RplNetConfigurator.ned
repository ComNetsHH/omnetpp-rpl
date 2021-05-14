
package rpl.configurator;

import inet.networklayer.configurator.ipv6.Ipv6FlatNetworkConfigurator;

@namespace(inet);

simple RplNetConfigurator extends Ipv6FlatNetworkConfigurator
{
    parameters:
        @class(RplNetConfigurator);
        @display("i=block/cogwheel");
        @labels(node);
}

