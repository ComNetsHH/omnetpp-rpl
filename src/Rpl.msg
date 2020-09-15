//
//  Simulation model for RPL (Routing Protocol for Low-Power and Lossy Networks)
// 
//  Copyright (C) 2020  Institute of Communication Networks (ComNets),
//                      Hamburg University of Technology (TUHH)
//            (C) 2020  Yevhenii Shudrenko
// 
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
// 
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
// 
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <https://www.gnu.org/licenses/>.
//

import inet.common.INETDefs;
import inet.common.packet.chunk.Chunk;
import inet.networklayer.common.L3Address;
import inet.networklayer.contract.ipv6.Ipv6Address;
import inet.networklayer.icmpv6.Icmpv6Header;

cplusplus {{
	#include "RplDefs.h"
	#include <deque>
	#include <stdint.h>
}}
	
namespace inet;


enum RplPacketCode {
    DIS = 0;
    DIO = 1;
    DAO = 2;
    DAO_ACK = 3;
    // for manual testing purposes, to be removed
    PING = 4;
    PING_ACK = 5;
};

// Objective Code Point - defines objective function of RPL instance
enum Ocp {
    ETX = 0;
    HOP_COUNT = 1;
    ENERGY = 2;
};


//
// RPL packets
//

class RplHeader extends Icmpv6Header {
	RplPacketCode icmpv6Code;
	const uint8_t icmpv6Type = 155;
}

class RplPacket extends FieldsChunk {
    Ipv6Address destAddress;   		
    Ipv6Address srcAddress;
    uint8_t instanceId;        	  	// RPL instance ID
    Ipv6Address dodagId;            // IPv6 address of the DODAG root
}

// DODAG Information Option [RFC 6550, 6.3]
class Dio extends RplPacket {
    // DODAG version number, may be incremented by the root to trigger topology refresh 
    uint8_t dodagVersion;       	
	uint16_t rank;            	 	// Node's rank within the DODAG 
    bool storing;					// Mode of operation: storing / non-storing                
    bool grounded;              	// DODAG grounded flag, indicates whether sink is connected to backbone	   
    
    // Destination Advertisement Trigger Sequence Number, 
    // may be incremented to trigger downward routes update
    uint8_t dtsn;
    uint8_t dioId;  // temporary field for debugging purposes   
   
	// Trickle timer parameters (DAG Configuration Option)
    int minInterval;           
    int dioRedundancyConst;              
    int dioNumDoublings;						
    Ocp ocp;                
}


// Destination Advertisement Object [RFC 6550, 6.4] 
class Dao extends RplPacket {
    uint8_t daoSeqNum;			// ID for each unique DAO sent by a node
    bool daoAckRequired;		// indicates whether DAO-ACK is expected by the sender 
    Ipv6Address reachableDest;	// advertised reachable destination		

}

// DODAG Information Solicitation
class Dis extends RplPacket {

}

// RPL Information Packet header [RFC 6550 11.2]
class RplPacketInfo extends RplPacket {
    bool down;
    bool rankError;
    bool fwdError;
    uint16_t senderRank;
}


class RplTargetInfo extends RplPacket {
    Ipv6Address target;
    // tbd fields according to 
}

class RplTransitInfo extends RplPacket {
    Ipv6Address transit;
    // tbd fields
}



class SourceRoutingHeader extends FieldsChunk {	
}

cplusplus (SourceRoutingHeader) {{
    std::deque<Ipv6Address> addresses;
    
    std::deque<Ipv6Address> getAddresses() { return this->addresses; }
    void setAddresses(std::deque<Ipv6Address> hopList) { handleChange(); this->addresses = hopList; }	
}}



