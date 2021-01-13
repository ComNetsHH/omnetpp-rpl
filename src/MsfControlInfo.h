/*
 * MsfControlInfo.h
 *
 *  Created on: Nov 24, 2020
 *      Author: cavalar
 */

#ifndef MSFCONTROLINFO_H_
#define MSFCONTROLINFO_H_

#include "RplDefs.h"

namespace inet {

class MsfControlInfo : public cObject {
    private:
        uint64_t nodeId;
        SlotframeChunkList slotframeChunks;
        SlotframeChunk slotRange;

    public:
        MsfControlInfo() {};
        MsfControlInfo(uint64_t nodeId) {
            this->nodeId = nodeId;
        }

        MsfControlInfo(SlotframeChunkList slotframeChunks) {
            this->slotframeChunks = slotframeChunks;
        }

        SlotframeChunkList getSlotframeChunks() { return this->slotframeChunks; }
        void setSlotframeChunks(SlotframeChunkList slotframeChunks) {
            this->slotframeChunks = slotframeChunks;
        }

        uint64_t getNodeId() { return this->nodeId; }
        void setNodeId(uint64_t nodeId) { this->nodeId = nodeId; }

        SlotframeChunk getSlotRange() const { return slotRange; }
        void setSlotRange(inet::SlotframeChunk slotRange) { this->slotRange = slotRange; }
};

}


#endif /* MSFCONTROLINFO_H_ */
