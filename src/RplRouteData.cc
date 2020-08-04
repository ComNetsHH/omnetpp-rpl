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

#include "RplRouteData.h"
#include "Rpl.h"

namespace inet {

RplRouteData::RplRouteData() {
    dtsn = 0;
    expirationTime = 0;
    dodagId = Ipv6Address::UNSPECIFIED_ADDRESS;
    instanceId = 0;
}

RplRouteData::~RplRouteData() {

}

std::string RplRouteData::str() const
{
    std::ostringstream out;
    out << "dodagId/RplInstance = " << getDodagId() << " - " << getInstanceId()
        << ", \n sequenceNumber = " << getDtsn()
        << ", \n expirationTime = " << getExpirationTime();
    return out.str();
};

}
