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

/** RPL params [RFC6550, 17] */
#define INF_RANK 0xFFFF
#define ROOT_RANK 1
#define RPL_DEFAULT_INSTANCE 1
#define DEFAULT_INIT_DODAG_VERSION 0
#define DEFAULT_DAO_DELAY 1

/** Trickle timer params [RFC6550, 8.3.1] */
#define DEFAULT_DIO_INTERVAL_MIN 0x03
#define DEFAULT_DIO_REDUNDANCY_CONST 0x03
#define DEFAULT_DIO_INTERVAL_DOUBLINGS 0x14

/** Objective function parameters */
#define DEFAULT_MIN_HOP_RANK_INCREASE 0x100

/** Misc */
#define DEFAULT_PARENT_LIFETIME 5000
#define UNDEFINED_CH_OFFSET 127
#define SCHEDULE_PHASE_II_TIMEOUT 15
const Ipv6Address LL_RPL_MULTICAST("FF02:0:0:0:0:0:0:1A");

enum TRICKLE_EVENTS {
    TRICKLE_START,
    TRICKLE_INTERVAL_UPDATE_EVENT,
    TRICKLE_TRIGGER_EVENT,
};

enum RPL_SELF_MSG {
    DETACHED_TIMEOUT,
    DAO_ACK_TIMEOUT,
    START_CROSS_LAYER_PHASE_II,
    BRANCH_CH_ADV,
    RPL_START
};

struct SlotframeChunk
{
    uint16_t end;
    uint16_t start;
    friend std::ostream& operator << (std::ostream &os, const SlotframeChunk &s) {
        return os << "(" << s.start << ":" << s.end << ")";
    }

    std::string toString()
    {
        return "(" + std::to_string(start) + ":" + std::to_string(end) + ")";
    }


    std::string to_string(SlotframeChunk const& arg)
    {
        std::ostringstream ss;
        ss << arg;
        return std::move(ss).str();
    }
};
typedef std::list<SlotframeChunk> SlotframeChunkList;

inline std::ostream& operator<<(std::ostream& os, std::list<uint8_t> &list)
{
    for (auto const &el: list)
        os << el << ", ";
    os << ";";
    return os;
}

} // namespace inet

#endif

