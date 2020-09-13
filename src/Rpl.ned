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
        // statistics collection
        @signal[dioReceived](type=Dio);
        @signal[daoReceived](type=Dao);
        @signal[parentChanged](type=Dio);
        @signal[parentUnreachable](type=Dio);
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
    	
        bool isRoot = default(false);
        bool disabled = default(false);
        bool daoEnabled = default(true);
        bool storing = default(false);
        bool poisoning = default(true);
        bool useBackupAsPreferred = default(false);
        bool unreachabilityDetectionEnabled = default(false);
        string objectiveFunctionType = default("hopCount");	 // hopCount, ETX, energy, ...

    gates:
        input ipIn;
        output ipOut;
        inout ttModule; // trickle timer interface
}

