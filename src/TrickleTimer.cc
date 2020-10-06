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

Define_Module(TrickleTimer);

TrickleTimer::TrickleTimer() :
    trickleTriggerEvent(nullptr),
    intervalTriggerEvent(nullptr),
    numDoublings(DEFAULT_DIO_INTERVAL_DOUBLINGS),
    redundancyConst(DEFAULT_DIO_REDUNDANCY_CONST),
    started(false),
    ctrlMsgReceivedCtn(0)
{
}

TrickleTimer::~TrickleTimer() {
    stop();
}

void TrickleTimer::stop() {
    try {
        cancelAndDelete(trickleTriggerEvent);
        cancelAndDelete(intervalTriggerEvent);
    }
    catch (...) {
        EV_WARN << "Exception while deleting trickle timer's internal events" << endl;
    }

}

void TrickleTimer::start(bool warmupDelay) {
    Enter_Method("TrickleTimer::start()");
    EV_INFO << "Trickle timer started" << endl;
    started = true;
    minInterval = DEFAULT_DIO_INTERVAL_MIN;
    currentInterval = warmupDelay ? minInterval * 2 : minInterval;
    maxInterval = minInterval * (pow(2, numDoublings));
    ctrlMsgReceivedCtn = 0;

    intervalTriggerEvent = new cMessage("Trickle timer current interval ended",
                    TRICKLE_INTERVAL_UPDATE_EVENT);
    trickleTriggerEvent = new cMessage("Trickle timer trigger self-msg",
                TRICKLE_TRIGGER_EVENT);

    scheduleAt(simTime() + currentInterval, intervalTriggerEvent);
    scheduleNext();
}

void TrickleTimer::handleMessageWhenUp(cMessage *message)
{
    if (message->isSelfMessage())
        processSelfMessage(message);
    else
        handleMessage(message);
}

void TrickleTimer::handleMessage(cMessage *message)
{
    if (message->isSelfMessage())
        processSelfMessage(message);
}

void TrickleTimer::processSelfMessage(cMessage *message)
{
    switch (message->getKind()) {
        case TRICKLE_INTERVAL_UPDATE_EVENT: {
            if (currentInterval < maxInterval)
                currentInterval *= 2;
            ctrlMsgReceivedCtn = 0;
            scheduleAt(simTime() + currentInterval, intervalTriggerEvent);
            scheduleNext();
            EV_INFO << "Trickle interval doubled, current = " <<
                    currentInterval << endl;
            break;
        }
        case TRICKLE_TRIGGER_EVENT: {
            send(new cMessage("", TRICKLE_TRIGGER_EVENT), "rpModule$o");
            break;
        }
        default: {
            throw cRuntimeError("Unknown kind of trickle timer self message");
        }
    }
}


void TrickleTimer::scheduleNext() {
    // TODO: Make delay float to allow more diverse transmission intervals
    unsigned long delay = currentInterval/2 + intrand(currentInterval/2);
    try {
        scheduleAt(simTime() + delay, trickleTriggerEvent);
        EV_DETAIL << "DIO broadcast scheduled with delay - " << delay << endl;
    } catch (std::exception &e) {
        EV_WARN << "Exception while scheduling next DIO: " << e.what() << endl;
    }
}

bool TrickleTimer::hasStarted() {
    Enter_Method_Silent("TrickleTimer::hasStarted()");
    return started;
}

bool TrickleTimer::checkRedundancyConst() {
    Enter_Method_Silent("TrickleTimer::checkRedundancyConst()");
    return ctrlMsgReceivedCtn < redundancyConst;
}

void TrickleTimer::reset() {
    Enter_Method_Silent("TrickleTimer::reset()");
    ctrlMsgReceivedCtn = 0;
    currentInterval = minInterval;
    try {
        cancelEvent(intervalTriggerEvent);
        cancelEvent(trickleTriggerEvent);
        scheduleAt(simTime() + currentInterval, intervalTriggerEvent);
        EV_DETAIL << "Trickle timer reset" << endl;
    }
    catch (std::exception &e) {
        EV_WARN << "Exception: " << e.what() <<" while resetting trickle timer, currentInterval = "
            << currentInterval << "; \n next interval doubling would be scheduled at "
            << simTime() + currentInterval << endl;
    }
    scheduleNext();
}

void TrickleTimer::suspend() {
    Enter_Method_Silent("TrickleTimer::suspend()");
    cancelEvent(intervalTriggerEvent);
    cancelEvent(trickleTriggerEvent);
    EV_DETAIL << "Trickle timer suspended " << endl;
}

} // namespace inet




