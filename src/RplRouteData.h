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

#ifndef RPLROUTEDATA_H_
#define RPLROUTEDATA_H_

#include <omnetpp/cobject.h>
#include "inet/networklayer/ipv6/Ipv6.h"

namespace inet {

class RplRouteData: public omnetpp::cObject {

private:
    Ipv6Address dodagId;
    uint8_t instanceId;
    uint8_t dtsn;
    simtime_t expirationTime;

public:
    RplRouteData();
    virtual ~RplRouteData();

    Ipv6Address getDodagId() const { return dodagId; }
    void setDodagId(Ipv6Address dodagId) { this->dodagId = dodagId; }

    uint8_t getDtsn() const { return dtsn; }
    void setDtsn(uint8_t dtsn) { this->dtsn = dtsn; }

    simtime_t getExpirationTime() const { return expirationTime; }
    void setExpirationTime(simtime_t expirationTime) { this->expirationTime = expirationTime; }

    uint8_t getInstanceId() const { return instanceId; }
    void setInstanceId(uint8_t instanceId) { this->instanceId = instanceId; }

    virtual std::string str() const;
};

} // namespace inet

#endif /* RPLROUTEDATA_H_ */
