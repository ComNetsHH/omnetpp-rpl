import inet.common.scenario.ScenarioManager;
import inet.networklayer.configurator.ipv6.Ipv6FlatNetworkConfigurator;
import inet.physicallayer.unitdisk.UnitDiskRadioMedium;
import inet.visualizer.contract.IIntegratedVisualizer;
import rpl.RplRouter;

network RplNetwork
{
    parameters:
        int numNodes;
        @display("bgb=450,650");
    submodules:
        visualizer: <default("IntegratedCanvasVisualizer")> like IIntegratedVisualizer if hasVisualizer() {
            @display("p=550,50");
        }
        radioMedium: UnitDiskRadioMedium {
            @display("p=550,250;is=s");
        }
        configurator: Ipv6FlatNetworkConfigurator {
            @display("p=550,150;is=s");
        }

        sink: RplRouter {
            @display("i=device/pocketpc_s;p=149.112,75.864");
        }
        host1: RplRouter {
            @display("i=device/pocketpc_s;p=83.712,179.196");
        }
        host2: RplRouter {
            @display("i=device/pocketpc_s;p=222.36,179.196");
        }
        host3: RplRouter {
            @display("i=device/pocketpc_s;p=83.712,306.072");
        }
        host4: RplRouter {
            @display("i=device/pocketpc_s;p=270.36,306.072");
        }
        host5: RplRouter {
            @display("i=device/pocketpc_s;p=222.36,430.332");
        }

    connections allowunconnected:

}
