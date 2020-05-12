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
    Mop mop;                  	  	// Mode of operation
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
    Ipv6Address srcAddress;
   	
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


