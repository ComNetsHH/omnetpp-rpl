// 
//   Simulation model for RPL (Routing Protocol for Low-Power and Lossy Networks)
//  
//   Copyright (C) 2020  Institute of Communication Networks (ComNets),
//                       Hamburg University of Technology (TUHH)
//             (C) 2020  Yevhenii Shudrenko
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

package rpl;

import inet.routing.contract.IManetRouting;

module Rpl like IManetRouting
{
    parameters:
        // Control signals
        @signal[parentUnreachable](type=long);
        
        // Statistics collections
        @signal[dioReceived](type=inet::Dio);
        @signal[daoReceived](type=inet::Dao);
     	@signal[isSink](type=bool);
     	@statistic[isSink](title="Node is a sink"; source="isSink"; record=count; interplationmode=none);
        @statistic[dioReceived](title = "DIO packets received"; source="dioReceived"; record=count; interpolationmode=none);  
        @statistic[daoReceived](title = "DAO packets received"; source="daoReceived"; record=count; interpolationmode=none);
        @statistic[parentChanged](title = "Preferred parent has changed"; source="parentChanged"; record=count; interpolationmode=none);
        @statistic[parentUnreachable](title = "Preferred parent unreachability detected"; source="parentUnreachable"; record=count; interpolationmode=none);
        
        // properties
        @class("inet::Rpl");
        @display("i=block/routing");

        // context parameters
        string interfaceTableModule;   // The path to the InterfaceTable module
        string routingTableModule = default(absPath("^.ipv6.routingTable"));
        string networkProtocolModule = default(absPath("^.ipv6.ipv6"));
    	
    	// General parameters
        bool isRoot = default(false);
        bool disabled = default(false);
        bool daoEnabled = default(true);
        bool daoAckEnabled = default(true);
        int numDaoRetransmitAttempts = default(3);
        bool storing = default(true);
        bool poisoning = default(false);
        bool useBackupAsPreferred = default(false);
        bool unreachabilityDetectionEnabled = default(false);
        
        // How much better (lower) rank should be advertised for node to consider the sender as a parent
        int minHopRankIncrease = default(1); 
        double startDelay = default(0);
        
        // TODO: replace by enum
        string objectiveFunctionType = default("hopCount");	 // hopCount, ETX, energy, ...
        
        // Utility params (mostly required for specific simulation scenarios)
        bool assignParentManual = default(false);
        bool useWarmup = default(true);
        // allows node to switch to another DODAG if better rank is advertised
        bool allowDodagSwitching = default(false); 
        int numSkipTrickleIntervalUpdates = default(0);
		int connectorColorId = default(0); // index of the connector line color from the color palette vector
		bool drawConnectors = default(true);

    gates:
        input ipIn;
        output ipOut;
        inout ttModule; // trickle timer interface
}

