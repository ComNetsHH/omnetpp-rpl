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

enum MOP {
	NON_STORING = 0;
	STORING = 1;
};	


//
// RPL packets
//

class RplPacket extends FieldsChunk {
    int hopLimit;
    
}

class DioMsg extends RplPacket {
    uint8_t instanceId;        	  	// RPL instance ID
    uint8_t dodagVersion;        	
	uint16_t rank;            	 	// Node's rank
    MOP mop;                  	  	// Mode of operation
    bool grounded;              		   
    uint8_t dtsn;                 	// Destination Advertisement Trigger Sequence Number       
   
	// Trickle timer parameters
    double minInterval;           
    int maxExponent;              
    int redunConst;                
    
    double lifetime;                  // Path lifetime
    double metric;
    Ipv6Address dodagId;              // IPv6 address set by DODAG root
    Ipv6Address destAddr;   	
    Ipv6Address srcAddr;
   	
}

class DaoMsg extends RplPacket {
    uint8_t instanceId;        	  	
    uint8_t dodagVersion;        	  			
    uint8_t daoSeqNum;				  // DAOSequence number

}

class DisMsg extends RplPacket {

}


