# omnetpp-rpl

RPL (Routing Protocol for Low-Power and Lossy Networks) OMNeT++ simulation model

# Compatibility

This model is developed and tested with the following libraries versions:

- OMNeT++ 5.6.1
- OMNeT++ 6.0-Pre8
- INET 4.2

# Configuration

1. Set up new OMNeT++ project from existing files. 
2. Reference INET project in the Properties->Project References.
3. Set additional build options via Properties->OMNeT++->Makemake:
    - Select 'src' folder in the project view and click 'Options' under 'Makemake'. 
    - In the 'Compile' tab set the 'Add include paths exported from referenced projects' flag.
    - In the 'Custom' tab add the following snippet 'MSGC:=$(MSGC) --msg6' (to use newer message compiler) and click OK.
 






