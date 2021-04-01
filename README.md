# omnetpp-rpl

RPL (Routing Protocol for Low-Power and Lossy Networks) OMNeT++ simulation model

# Verified Compatibility
OMNeT++ 5.6.2, INET 4.3

# Installation Instructions:
1. Replace INET files with the ones included in this repo
2. In INET project settings -> OMNeT++ -> Project Features disable 'Visualization' and 'Mobile IPv6 Protocol (xMIPv6)'
3. In RPL project settings -> OMNeT++ -> Makemake -> src -> Build Makemake Options -> Custom add the following line:
`MSGC:=$(MSGC) --msg6`
