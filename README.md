# omnetpp-rpl

RPL (Routing Protocol for Low-Power and Lossy Networks) OMNeT++ simulation model

# Verified Compatibility
OMNeT++ [5.6.X](https://omnetpp.org/download/old), INET [4.2.X](https://github.com/inet-framework/inet/releases/download/v4.2.5/inet-4.2.5-src.tgz).

# Installation Instructions:
1. Replace INET src files with the ones included in this repo, **following the folder structure**.
2. In INET project Properties -> OMNeT++ -> Project Features disable "Mobile IPv6 Protocol (xMIPv6)".
3. In RPL project Properties -> OMNeT++ -> Makemake -> src -> Options -> Custom add the following line:  
`MSGC:=$(MSGC) --msg6`
4. Add INET to RPL project references, by navigating RPL project Properties -> Project References.
4. Compile and test included sample scenarios in the .ini file.
5. To use RPL in other projects, compile it as a shared library: project Properties -> OMNeT++ -> Makemake -> src -> Options -> Target. And also select "Export this shared/static library for other projects".
