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

#ifndef NETWORKLAYER_IPV6NEIGHBOURDISCOVERYWIRELESS_H
#define NETWORKLAYER_IPV6NEIGHBOURDISCOVERYWIRELESS_H

#include "inet/networklayer/icmpv6/Ipv6NeighbourDiscovery.h"

using namespace inet;

#define WIND_SEND_DELAYED              8 // self-msg kind, see Ipv6NeighbourDiscovery.h
#define MK_AR_TIMEOUT                  7 // self-msg kind from Ipv6NeighbourDiscovery.cc, cannot be included normally since it's in the source file

// Minor delays added for EACH packet forwarded to IP layer
#define WIND_MICRO_DELAY_MIN               0.3
#define WIND_MICRO_DELAY_MAX               0.5

class Ipv6NeighbourDiscoveryWireless : public Ipv6NeighbourDiscovery
{
protected:
    class Ipv6NdPacketInfo : public cObject {
        public:
            Ipv6NdPacketInfo() {}
            Ipv6NdPacketInfo(Packet *pkt, const Ipv6Address &destAddr, const Ipv6Address &srcAddr, int ie)
            {
                this->msgPtr = pkt;
                this->destAddr = destAddr;
                this->srcAddr = srcAddr;
                this->interfaceId = ie;
            }

            const Ipv6Address& getDestAddr() const {
                return destAddr;
            }

            void setDestAddr(const Ipv6Address &destAddr) {
                this->destAddr = destAddr;
            }

            int getInterfaceId() const {
                return interfaceId;
            }

            void setInterfaceId(int interfaceId) {
                this->interfaceId = interfaceId;
            }

            Packet*& getMsgPtr() {
                return msgPtr;
            }

            void setMsgPtr(Packet *&msgPtr) {
                this->msgPtr = msgPtr;
            }

            const Ipv6Address& getSrcAddr() const {
                return srcAddr;
            }

            void setSrcAddr(const Ipv6Address &srcAddr) {
                this->srcAddr = srcAddr;
            }

        private:
            Packet *msgPtr;
            Ipv6Address destAddr;
            Ipv6Address srcAddr;
            int interfaceId;
    };

    virtual void initialize(int stage) override;

    virtual void sendSolicitedNa(Packet *packet,
            const Ipv6NeighbourSolicitation *ns, InterfaceEntry *ie) override;

    virtual void sendUnsolicitedNa(InterfaceEntry *ie) override;
    virtual void createAndSendRaPacket(const Ipv6Address& destAddr, InterfaceEntry *ie) override;
    virtual void createAndSendNsPacket(const Ipv6Address& nsTargetAddr, const Ipv6Address& dgDestAddr,
                    const Ipv6Address& dgSrcAddr, InterfaceEntry *ie) override;

    virtual void createRaTimer(InterfaceEntry *ie) override;
    virtual void sendPacketToIpv6Module(Packet *msg, const Ipv6Address& destAddr,
            const Ipv6Address& srcAddr, int interfaceId, double delay);

    virtual void handleMessage(cMessage *msg) override;
    virtual void initiateNeighbourUnreachabilityDetection(Neighbour *nce) override;
    virtual void processNaForIncompleteNceState(const Ipv6NeighbourAdvertisement *na, Neighbour *nce) override;
    virtual void processIpv6Datagram(Packet *packet) override;
    virtual void processNaForOtherNceStates(const Ipv6NeighbourAdvertisement *na, Neighbour *nce) override;
    virtual void assignLinkLocalAddress(cMessage *timerMsg) override;
    virtual void initiateAddressResolution(const Ipv6Address& dgSrcAddr, Neighbour *nce) override;

    simsignal_t naSolicitedPacketSent;
    simsignal_t naUnsolicitedPacketSent;
    simsignal_t nsPacketSent;
    simsignal_t raPacketSent;
    simsignal_t nudInitated;

    double nsFwdDelay;
    bool pAddRandomDelays; // add a miniscule delay before sending ANY packet out, prevents the infinite loop bug
};

#endif    //NETWORKLAYER_IPV6NEIGHBOURDISCOVERYWIRELESS_H

