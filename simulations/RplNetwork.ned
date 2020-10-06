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
            @display("i=device/pocketpc_s;p=213.204,133.416");
        }
        host1: RplRouter {
            @display("i=device/pocketpc_s;p=147.804,236.748");
        }
        host2: RplRouter {
            @display("i=device/pocketpc_s;p=283.836,236.748");
        }
        host3: RplRouter {
            @display("i=device/pocketpc_s;p=147.804,363.624");
        }
        host4: RplRouter {
            @display("i=device/pocketpc_s;p=283.836,363.624");
        }
        host5: RplRouter {
            @display("i=device/pocketpc_s;p=283.836,487.884");
        }
//        host6: RplRouter {
//            @display("i=device/pocketpc_s;p=147.804,487.884");
//        }

    connections allowunconnected:

}
