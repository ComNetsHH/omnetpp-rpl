/*
 * Simulation model for RPL (Routing Protocol for Low-Power and Lossy Networks)
 *
 * Copyright (C) 2021  Institute of Communication Networks (ComNets),
 *                     Hamburg University of Technology (TUHH)
 *           (C) 2021  Yevhenii Shudrenko
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef TSCHSFCONTROLINFO_H_
#define TSCHSFCONTROLINFO_H_

#include "inet/common/INETDefs.h"
#include "RplDefs.h"

namespace inet {

class TschSfControlInfo : public cObject {
    private:
        uint64_t nodeId;
        SlotframeChunkList slotframeChunks;
        SlotframeChunk slotRange;
//        Ipv6Address dest;

    public:
        TschSfControlInfo() {};
        TschSfControlInfo(uint64_t nodeId) {
            this->nodeId = nodeId;
        }

        TschSfControlInfo(SlotframeChunkList slotframeChunks) {
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

//        void setDestAddress(Ipv6Address dest) { this->dest = dest; }
//        Ipv6Address getDestAddress() { return dest; }
};

}


#endif /* TSCHSFCONTROLINFO_H_ */
