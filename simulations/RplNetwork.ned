import inet.common.scenario.ScenarioManager;
import inet.networklayer.configurator.ipv6.Ipv6FlatNetworkConfigurator;
//import inet.physicallayer.ieee80211.packetlevel.Ieee80211ScalarRadioMedium;
import inet.physicallayer.contract.packetlevel.IRadioMedium;
import inet.visualizer.contract.IIntegratedVisualizer;
import rpl.RplRouter;

network RplNetwork
{
    parameters:
        int numNodes;
        @display("bgb=650,650");
    submodules:
        visualizer: <default("IntegratedCanvasVisualizer")> like IIntegratedVisualizer if hasVisualizer() {
            @display("p=550,50");
        }
        radioMedium: <default("UnitDiskRadioMedium")> like IRadioMedium {
            @display("p=550,250;is=s");
        }
        configurator: Ipv6FlatNetworkConfigurator {
            @display("p=550,150;is=s");
        }
        //        host[numNodes]: RplRouter {
        //            @display("i=device/pocketpc_s;p=218.43599,81.096");
        //        }
        sink: RplRouter {
            @display("i=device/pocketpc_s;p=94.175995,149.112");
        }
        host1: RplRouter {
            @display("i=device/pocketpc_s;p=207.972,90.252");
        }
        host2: RplRouter {
            @display("i=device/pocketpc_s;p=319.152,149.112");
        }

    connections allowunconnected:
        
}
