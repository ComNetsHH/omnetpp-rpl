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

#include "Rpl.h"
#include <limits>

namespace inet {

Define_Module(TrickleTimer);

TrickleTimer::TrickleTimer() :
    trickleTriggerEvent(nullptr),
    intervalTriggerEvent(nullptr),
    intervalUpdatesCtn(0),
    intervalExponent(2),
    numDoublings(DEFAULT_DIO_INTERVAL_DOUBLINGS),
    redundancyConst(DEFAULT_DIO_REDUNDANCY_CONST),
    started(false),
    ctrlMsgReceivedCtn(0)
{
}

TrickleTimer::~TrickleTimer() {
    stop();
}

void TrickleTimer::initialize(int stage) {
    if (stage == INITSTAGE_LOCAL) {
        intervalExponent = par("intervalExponent").intValue();
        pStartIntervalOverride = par("startIntervalOverride").intValue();
    }
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

void TrickleTimer::start(bool warmupDelay, int skipIntervalDoublings) {
    Enter_Method("TrickleTimer::start()");
    EV_INFO << "Trickle timer started" << endl;
    skipIntDoublings = skipIntervalDoublings;
    started = true;
    minInterval = DEFAULT_DIO_INTERVAL_MIN;
    currentInterval = warmupDelay ? minInterval * intervalExponent : minInterval;

    // FIXME: conflicts with warmupDelay to some extent
    if (pStartIntervalOverride > 0)
        currentInterval = pStartIntervalOverride;

    maxInterval = minInterval * (pow(intervalExponent, numDoublings));
    if (maxInterval < 0)
        maxInterval = std::numeric_limits<int>::max();

    ctrlMsgReceivedCtn = 0;

    intervalTriggerEvent = new cMessage("TT interval update", TRICKLE_INTERVAL_UPDATE_EVENT);
    trickleTriggerEvent = new cMessage("TT triggered", TRICKLE_TRIGGER_EVENT);

    scheduleAt(simTime() + currentInterval, intervalTriggerEvent);
    scheduleNext();

    WATCH(currentInterval);
    WATCH(skipIntDoublings);
    WATCH(maxInterval);
    WATCH_PTR(intervalTriggerEvent);
    WATCH_PTR(trickleTriggerEvent);
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
            if (skipIntDoublings)
                intervalUpdatesCtn++;

            if (currentInterval < maxInterval) {
                if (intervalUpdatesCtn >= skipIntDoublings) {
                    currentInterval *= intervalExponent;
                    EV_INFO << "Trickle interval doubled, current - " << currentInterval << endl;
                }
            }

            ctrlMsgReceivedCtn = 0;

            if (!intervalTriggerEvent)
                intervalTriggerEvent = new cMessage("TT interval update", TRICKLE_INTERVAL_UPDATE_EVENT);

            if (intervalTriggerEvent->isScheduled())
                cancelEvent(intervalTriggerEvent);

            scheduleAt(simTime() + currentInterval, intervalTriggerEvent);
            scheduleNext();

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
   auto delay = (double) currentInterval/2 + uniform(1, (double) currentInterval/2);
    // auto delay = (double) currentInterval/2; // avoid randomness for topology evaluation scenarios with 6TiSCH
    try {
        if (!trickleTriggerEvent)
            trickleTriggerEvent = new cMessage("TT triggered", TRICKLE_TRIGGER_EVENT);

        if (trickleTriggerEvent->isScheduled())
            cancelEvent(trickleTriggerEvent);

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
    currentInterval = pStartIntervalOverride > 0 ? pStartIntervalOverride : minInterval;
    intervalUpdatesCtn = 0;
    try {
        if (intervalTriggerEvent)
            cancelEvent(intervalTriggerEvent);
        else
            intervalTriggerEvent = new cMessage("TT interval update", TRICKLE_INTERVAL_UPDATE_EVENT);

        if (trickleTriggerEvent)
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
    if (intervalTriggerEvent)
        cancelEvent(intervalTriggerEvent);
    if (trickleTriggerEvent)
        cancelEvent(trickleTriggerEvent);
    EV_DETAIL << "Trickle timer suspended " << endl;
}

} // namespace inet




