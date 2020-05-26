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
	#include <stdint.h>
}}
	
namespace inet;


enum Mop {
    NON_STORING = 0;
    STORING = 1;
};

enum RplPacketCode {
    DIS = 0;
    DIO = 1;
    DAO = 2;
    DAO_ACK = 3;
};


//
// RPL packets
//

class RplHeader extends Icmpv6Header {
	RplPacketCode icmpv6Code;
	const uint8_t icmpv6Type = 155;
}

class RplPacket extends FieldsChunk {
    int hopLimit;
    
}

// DODAG Information Option
class Dio extends RplPacket {
    uint8_t instanceId;        	  	// RPL instance ID
    uint8_t dodagVersion;        	
	uint16_t rank;            	 	// Node's rank
    Mop mop;					    // Mode of operation               
    bool grounded;              		   
    uint8_t dtsn;                 	// Destination Advertisement Trigger Sequence Number       
   
	// Trickle timer parameters
    double minInterval;           
    int maxExponent;              
    int redunConst;                
    
    double lifetime;                  // Path lifetime
    double metric;
    Ipv6Address dodagId;              // IPv6 address set by DODAG root
    Ipv6Address destAddress;   		
    Ipv6Address srcAddress;			  // IPv6 sender address
   	
}


// Destination Advertisement Object
class Dao extends RplPacket {
    uint8_t instanceId;        	  	
    uint8_t dodagVersion;        	  			
    uint8_t daoSeqNum;				  // DAOSequence number

}


// DODAG Information Solicitation
class Dis extends RplPacket {

}


