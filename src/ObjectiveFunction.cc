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

#include "Rpl.h"

namespace inet {

ObjectiveFunction::ObjectiveFunction() :
    type(HOP_COUNT),
    minHopRankIncrease(0)
{}

ObjectiveFunction::ObjectiveFunction(std::string objFunctionType) {
    if (objFunctionType.compare(std::string("ETX")) == 0)
        type = ETX;
    else if (objFunctionType.compare(std::string("energy")) == 0)
        type = ENERGY;
    else
        type = HOP_COUNT;
    EV_DETAIL << "Objective function initialized with type - " << objFunctionType << endl;
}

ObjectiveFunction::~ObjectiveFunction() {

}

Dio* ObjectiveFunction::getPreferredParent(std::map<Ipv6Address, Dio *> candidateParents, Dio* currentPreferredParent)
{
    if (candidateParents.empty()) {
        EV_WARN << "Couldn't determine preferred parent, provided set is empty" << endl;
        return nullptr;
    }

    EV_DETAIL << "Address - Rank" << endl;
    for (auto cp : candidateParents)
        EV_DETAIL << cp.first << " - " << cp.second->getRank() << endl;

    // Select the node with lowest rank from the candidate parent list
    Dio *newPrefParent = candidateParents.begin()->second;
    uint16_t currentMinRank = newPrefParent->getRank();
    for (std::pair<Ipv6Address, Dio *> candidate : candidateParents) {
        uint16_t candidateParentRank = candidate.second->getRank();
        if (candidateParentRank < currentMinRank) {
            currentMinRank = candidateParentRank;
            newPrefParent = candidate.second;
        }
    }

    // If the node doesn't have a preferred parent yet, choose the one selected in previous step 
    if (!currentPreferredParent)
        return newPrefParent;

    // Otherwise check if the rank difference between suggested new parent and 
    // the current one is larger than the minHopRankIncrease threshold (to avoid oscillation)
    if (currentPreferredParent->getRank() - newPrefParent->getRank() >= minHopRankIncrease)
        return newPrefParent;
    else
        return currentPreferredParent;
}

uint16_t ObjectiveFunction::calcRank(Dio* preferredParent) {
    if (!preferredParent)
        throw cRuntimeError("Cannot calculate rank, preferredParent argument is null");

    uint16_t prefParentRank = preferredParent->getRank();
    /** Calculate node's rank based on the objective function policy */
    switch (type) {
        case HOP_COUNT:
            return prefParentRank + 1;
        default:
            return prefParentRank + DEFAULT_MIN_HOP_RANK_INCREASE;
    }
}

} // namespace inet




