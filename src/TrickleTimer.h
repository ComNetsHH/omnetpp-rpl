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

#ifndef _TRICKLETIMER_H
#define _TRICKLETIMER_H

#include <map>
#include <vector>

#include "inet/networklayer/contract/IL3AddressType.h"
#include "inet/networklayer/contract/INetfilter.h"
#include "inet/networklayer/contract/IRoutingTable.h"
#include "inet/networklayer/contract/ipv6/Ipv6Address.h"
#include "inet/networklayer/ipv6/Ipv6InterfaceData.h"
#include "inet/networklayer/ipv6/Ipv6Route.h"
#include "inet/routing/base/RoutingProtocolBase.h"
#include "inet/transportlayer/udp/UdpHeader_m.h"
#include "inet/common/INETMath.h"
#include "inet/common/IProtocolRegistrationListener.h"
#include "inet/common/ModuleAccess.h"
#include "inet/common/ProtocolTag_m.h"
#include "Rpl_m.h"
#include "RplDefs.h"


namespace inet {

class TrickleTimer : public cSimpleModule
{
  private:
    uint8_t dioIntervalMin;
    uint8_t dioNumDoublings;
    int dioCurrentInterval;
    cMessage *trickleTriggerEvent;
    cMessage *intervalTriggerEvent;
    uint8_t dioRedundancyConst;
    uint8_t dioReceivedCounter;

  public:
    TrickleTimer();
    ~TrickleTimer();

    // lifecycle
    void initialize();
    virtual void start();
    virtual void reset();
    virtual void stop();

    void incrementDioReceivedCounter() { dioReceivedCounter++; }
    bool checkRedundancyConst() { return dioReceivedCounter < dioRedundancyConst; }
    void scheduleNext();
    void updateInterval();

    // Messsage processing
    void handleMessage(cMessage *message);
    void processSelfMessage(cMessage *message);
    void handleMessageWhenUp(cMessage *message);

    int getDioCurrentInterval() const { return dioCurrentInterval; }
    void setDioCurrentInterval(int dioCurrentInterval) { this->dioCurrentInterval = dioCurrentInterval; }

    uint8_t getDioIntervalMin() const { return dioIntervalMin; }
    void setDioIntervalMin(uint8_t dioIntervalMin) { this->dioIntervalMin = dioIntervalMin; }

    uint8_t getDioNumDoublings() const { return dioNumDoublings; }
    void setDioNumDoublings(uint8_t dioNumDoublings) { this->dioNumDoublings = dioNumDoublings; }

    uint8_t getDioRedundancyConst() const { return dioRedundancyConst; }
    void setDioRedundancyConst(uint8_t dioRedundancyConst) { this->dioRedundancyConst = dioRedundancyConst; }

};

} // namespace inet

#endif

