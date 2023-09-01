# Modified files summary
## Link layer
MacProtocolBase:
- Make InterfaceEntry public (TODO: list use-cases)

## Network layer
Ipv6: 
- added extra filter to allow forwarding of (UDP) application packets using link-local addresses 

Icmpv6:
- Skip Neighbor Unreachability Detection (NUD), which, with its default timings, doesn't make sense for LP-WANs
- Add optional randomized delays to Neighbor Solicitation (NS) forwarding to prevent weird simulation deadlocks
- Skip Duplicate Address Detection (DAD) by setting a freshly formed link-local address from tentative to permanent straight away
- Skip periodic Router Advertisements (RAs) as they are redundant next to DIOs

## Visualizer
NetworkCanvasVisualizer:
- Add dialog cloud visualizations for RPL control packets