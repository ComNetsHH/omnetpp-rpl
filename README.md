# omnetpp-rpl

RPL (Routing Protocol for Low-Power and Lossy Networks) OMNeT++ simulation model

# Compatibility

This model is developed and tested with the following libraries versions:

- OMNeT++ 5.6.2
- INET 4.2.2

# Configuration

1. Set up new OMNeT++ project from existing files. 
2. Reference INET project in the Properties->Project References.
3. Disable xMIPv6 in INET project features
4. Set additional build options via Properties->OMNeT++->Makemake:
    - Select 'src' folder in the project view and click 'Options' under 'Makemake'. 
    - In the 'Compile' tab set the 'Add include paths exported from referenced projects' flag.
    - In the 'Custom' tab add the following snippet 'MSGC:=$(MSGC) --msg6' (to use newer message compiler) and click OK.

5. Replace INET src files with the modified versions to avoid hanging simulations and to make ICMPv6 unreachability detection less aggressive

# Known Issues
- Runtest batch script is not portable, needs adjustements to run test suite per machine/OS.


