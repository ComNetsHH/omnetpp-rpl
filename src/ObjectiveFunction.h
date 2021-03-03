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

#ifndef _OBJECTIVEFUNCTION_H
#define _OBJECTIVEFUNCTION_H

#include <map>
#include <vector>

#include "inet/common/INETDefs.h"
#include "Rpl_m.h"
#include "RplDefs.h"

namespace inet {

class ObjectiveFunction : public cObject
{
  private:
    Ocp type; /** Objective Function (OF) type as defined in RFC 6551. */
    int minHopRankIncrease; /** base step of rank increment [RFC 6550, 6.7.6] */

  public:
    ObjectiveFunction();
    ObjectiveFunction(std::string type);
    virtual ~ObjectiveFunction();

    /**
     * Determine node's preferred parent from the candidate neighbor set using
     * relevant metric (defined by OF type).
     *
     * @param candidateParents map of node's neighborhood in form of latest DIO packets
     * from each neighbor
     * @return best parent candidate based on the type of objective function in use
     */
    virtual Dio* getPreferredParent(std::map<Ipv6Address, Dio *> candidateParents, Dio* currentPreferredParent);
    /**
     * Calculate node's rank based on the chosen preferred parent [RFC 6550, 3.5].
     *
     * @param preferredParent node's preferred parent properties (rank, address, ...)
     * represented by last DIO received from it
     * @return updated rank based on the minHopRankIncrease and OF
     */
    virtual uint16_t calcRank(Dio* preferredParent);

    void setMinHopRankIncrease(int incr) { minHopRankIncrease = incr; }

};

} // namespace inet

#endif

