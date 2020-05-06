
package rpl;

import inet.routing.contract.IManetRouting;

simple Rpl like IManetRouting
{
    parameters:
        // properties
        @class("inet::Rpl");
        @display("i=block/routing");

        // context parameters
        string interfaceTableModule;   // The path to the InterfaceTable module
        string routingTableModule = default(absPath("^.ipv6.routingTable"));
        string networkProtocolModule = default(absPath("^.ipv6.ipv6"));
        
        bool isRoot = default(false);	 

    gates:
        input ipIn;
        output ipOut;
}

