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

#include "Rpl.h"

namespace inet {

ObjectiveFunction::ObjectiveFunction() :
    type(HOP_COUNT),
    minHopRankIncrease(DEFAULT_MIN_HOP_RANK_INCREASE)
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

Dio* ObjectiveFunction::getPreferredParent(std::map<Ipv6Address, Dio *> candidateParents) {
    // determine parent with lowest rank
    if (candidateParents.empty()) {
        EV_WARN << "Couldn't determine preferred parent, provided set is empty" << endl;
        return nullptr;
    }

    for (auto cp : candidateParents)
        EV_DETAIL << cp.first << " - " << cp.second << endl;

    Dio *preferredParent = candidateParents.begin()->second;
    uint16_t minRank = preferredParent->getRank();
    for (std::pair<Ipv6Address, Dio *> candidate : candidateParents) {
        uint16_t candidateParentRank = candidate.second->getRank();
        if (candidateParentRank < minRank) {
            minRank = candidateParentRank;
            preferredParent = candidate.second;
        }
    }

    return preferredParent;
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
            return prefParentRank + minHopRankIncrease;
    }
}

} // namespace inet




