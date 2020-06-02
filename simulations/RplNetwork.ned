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
            @display("i=device/pocketpc_s;p=207.972,71.939995");
        }
        host1: RplRouter {
            @display("i=device/pocketpc_s;p=109.871994,179.196");
        }
        host2: RplRouter {
            @display("i=device/pocketpc_s;p=287.75998,179.196");
        }
        host3: RplRouter {
            @display("i=device/pocketpc_s;p=196.2,329.616");
        }
        host4: RplRouter {
            @display("i=device/pocketpc_s;p=345.31198,290.376");
        }

    connections allowunconnected:

}
