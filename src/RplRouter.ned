
package rpl;

import inet.node.inet.AdhocHost;
import rpl.Rpl; 

module RplRouter extends AdhocHost
{
    submodules:
        rpl: Rpl {
            @display("p=825,226");
        }
        
    connections:
        rpl.ipOut --> tn.in++;
        rpl.ipIn <-- tn.out++;
}

