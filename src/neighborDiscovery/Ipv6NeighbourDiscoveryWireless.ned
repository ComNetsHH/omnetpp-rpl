// 
//   Simulation model for RPL (Routing Protocol for Low-Power and Lossy Networks)
//  
//   Copyright (C) 2021  Institute of Communication Networks (ComNets),
//                       Hamburg University of Technology (TUHH)
//             (C) 2021  Yevhenii Shudrenko
//  
//   This program is free software: you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation, either version 3 of the License, or
//   (at your option) any later version.
//  
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//   GNU General Public License for more details.
//  
//   You should have received a copy of the GNU General Public License
//   along with this program.  If not, see <https://www.gnu.org/licenses/>.
//

package rpl.neighborDiscovery;

import inet.networklayer.icmpv6.Ipv6NeighbourDiscovery;
import inet.networklayer.contract.IIpv6NeighbourDiscovery;

//
// Extension of IPv6 Neighbour Discovery for wireless networks.
//
simple Ipv6NeighbourDiscoveryWireless extends Ipv6NeighbourDiscovery like IIpv6NeighbourDiscovery
{
    parameters:
        @class(Ipv6NeighbourDiscoveryWireless);
        
        @signal[naSolicitedPacketSent](type=int);
        @signal[naUnsolicitedPacketSent](type=int);
        @signal[nsPacketSent](type=int);
        @signal[raPacketSent](type=int);
        @signal[nudInitiated](type=int);
        
        @statistic[naSolicitedSent](title="Sent solicited NA packets"; source=naSolicitedPacketSent; record=count, vector; interpolationmode=none);
        @statistic[naUnsolicitedSent](title="Sent unsolicited NA packets"; source=naUnsolicitedPacketSent; record=count, vector; interpolationmode=none);
        @statistic[nsPacketsSent](title="Sent NS packets"; source=nsPacketSent; record=count, vector; interpolationmode=none);
        @statistic[raPacketsSent](title="Sent RA packets"; source=raPacketSent; record=count, vector; interpolationmode=none);
        @statistic[nudInitiated](title="Initiated Neighbor Unreachability Detections"; source=nudInitiated; record=count, vector; interpolationmode=none);
    
        bool raEnabled = default(false); // Router Advertisements partially overlap with DIOs and can generally be disabled
        // UPPER BOUND for the delay between NS packet generation and passing it to the IP layer,
        // note there's no expiry time for a NS packet, so this shouldn't cause any issues with validation,
        // but helps spread out in time NS packets from multiple neighbors
        double nsForwardingDelay @unit("s") = default(0);
        double nceExpiryOverride @unit("s") = default(90000s); // override default lifetime duration of Neighbor Cache Entry
}

