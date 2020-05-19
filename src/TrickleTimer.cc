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
#include "RplDefs.h"

namespace inet {

TrickleTimer::TrickleTimer() :
    trickleTriggerEvent(nullptr),
    routingModulePtr(nullptr),
    dioIntervalMin(DEFAULT_DIO_INTERVAL_MIN),
    dioNumDoublings(DEFAULT_DIO_INTERVAL_DOUBLINGS),
    dioCurrentInterval(DEFAULT_DIO_INTERVAL_MIN),
    dioRedundancyConst(DEFAULT_DIO_REDUNDANCY_CONST),
    dioReceivedCounter(0)
{}

TrickleTimer::~TrickleTimer() {
    try {
        routingModulePtr->cancelAndDelete(trickleTriggerEvent);
    } catch (std::exception &e) {
        EV_WARN << "Exception occured while disposing of trickleTriggerEvent" << endl;
    }

}


void TrickleTimer::start() {
    EV_DETAIL << "Trickle timer started" << endl;
    trickleTriggerEvent = new cMessage("Trickle timer-triggered DIO broadcast");
    trickleTriggerEvent->setKind(TRICKLE_TRIGGER_EVENT);
    scheduleNext();
}

void TrickleTimer::scheduleNext() {
    // reset counter of DIOs heard during last interval
    dioReceivedCounter = 0;
    // TODO: Calculate delay properly
    unsigned long delay = intrand(new cMersenneTwister(), dioCurrentInterval/2);
    EV_DETAIL << "DIO broadcast scheduled with delay - " << delay << endl;
    routingModulePtr->scheduleAt(simTime() + delay, this->trickleTriggerEvent);
    dioCurrentInterval *= 2;
    EV_DETAIL << "Updated DIO broadcast interval - " << dioCurrentInterval << endl;
}

void TrickleTimer::reset() {
    EV_DETAIL << "Trickle timer reset" << endl;
    dioCurrentInterval = dioIntervalMin;
    routingModulePtr->scheduleAt(simTime() + 1, trickleTriggerEvent);
}


} // namespace inet




