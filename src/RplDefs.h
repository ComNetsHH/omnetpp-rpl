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
#define ROOT_RANK 1
#define DEFAULT_PREFIX_LENGTH 64
#define DEFAULT_INSTANCE_ID 1
// temporary experimental delays to alleviate 'Received message from upper layers while transmitting' errors
#define DEFAULT_PING_REPEAT_DELAY 3 // 3 ms - base experimental delay before sending
                                    // new ping after former timeout (additionally multiplied with random modifier)
#define DEFAULT_PING_TIMEOUT_DELAY 2 // 2 s - empirical value based on time
                                     // needed to successfully deliver a packet in simulation

// Trickle timer defaults (RFC 6550)
#define DEFAULT_DIO_INTERVAL_MIN 0x03
#define DEFAULT_DIO_REDUNDANCY_CONST 0x03
#define DEFAULT_DIO_INTERVAL_DOUBLINGS 0x14

// Objective Function parameters
#define DEFAULT_MIN_HOP_RANK_INCREASE 0x100

static Ipv6Address LL_RPL_MULTICAST("FF02::1A");

// Trickle timer and internal event types
enum {
    TRICKLE_START,
    TRICKLE_INTERVAL_UPDATE_EVENT,
    TRICKLE_TRIGGER_EVENT,
    PING_TRIGGER,   // Temporary functionality for manual
    PING_TIMEOUT    // pinging of the preferred parent
};

} // namespace inet

#endif

