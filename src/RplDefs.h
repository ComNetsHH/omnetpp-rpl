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

#ifndef __INET_RPLDEFS_H
#define __INET_RPLDEFS_H

namespace inet {


#define INF_RANK 0xFFFF
// Trickle timer default parameters (RFC 6550)
#define DEFAULT_DIO_INTERVAL_MIN 0x03
#define DEFAULT_DIO_REDUNDANCY_CONST 0x01
#define DEFAULT_DIO_INTERVAL_DOUBLINGS 0x14

// Objective Function parameters
#define DEFAULT_MIN_HOP_RANK_INCREASE 0x100

static const Ipv6Address LL_RPL_MULTICAST("FF02::1A");

enum {
    TRICKLE_TRIGGER_EVENT,
    SELF_TRICKLE_EVENT
};

enum OF_TYPE {
    ETX,
    HOP_COUNT,
    ENERGY
};

//enum Mop {
//    NON_STORING,
//    STORING
//};
//
//enum RplPacketCode {
//    DIS = 0,
//    DIO = 1,
//    DAO = 2,
//    DAO_ACK = 3
//};

} // namespace inet

#endif

