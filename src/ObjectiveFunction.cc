/*
 * Simulation model for RPL (Routing Protocol for Low-Power and Lossy Networks)
 *
 * Copyright (C) 2020  Institute of Communication Networks (ComNets),
 *                     Hamburg University of Technology (TUHH)
 *           (C) 2020  Yevhenii Shudrenko
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

#include "TrickleTimer.h"
#include "ObjectiveFunction.h"
#include "RplDefs.h"

namespace inet {

ObjectiveFunction::ObjectiveFunction() :
    type(HOP_COUNT),
    minHopRankIncrease(DEFAULT_MIN_HOP_RANK_INCREASE)
{}

ObjectiveFunction::ObjectiveFunction(std::string type) {
    if (type.compare(std::string("ETX")) == 0)
        type = ETX;
    else if (type.compare(std::string("energy")) == 0)
        type = ENERGY;
    else
        type = HOP_COUNT;

}

ObjectiveFunction::~ObjectiveFunction() {

}

Dio* ObjectiveFunction::getPreferredParent(std::vector<Dio *> parentSet) {
    // determine lowest rank parent
    uint16_t minRank = parentSet[0]->getRank();
    Dio *preferredParent = parentSet[0];
    for (int i = 1; i < parentSet.size(); i++) {
        uint16_t candidateParentRank = parentSet[i]->getRank();
        if (candidateParentRank < minRank) {
            minRank = candidateParentRank;
            preferredParent = parentSet[i];
        }
    }

    return preferredParent;
}

uint16_t ObjectiveFunction::computeRank(uint16_t preferredParentRank) {
    switch (type) {
        case HOP_COUNT:
            return preferredParentRank + 1;
        default:
            return preferredParentRank + minHopRankIncrease;
    }
}

} // namespace inet




