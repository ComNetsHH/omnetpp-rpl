import inet.common.scenario.ScenarioManager;
import inet.physicallayer.unitdisk.UnitDiskRadioMedium;
import inet.visualizer.contract.IIntegratedVisualizer;
import rpl.RplRouter;
import rpl.configurator.RplNetConfigurator;

network RplNetwork
{
    parameters:
        int numNodes;
        int numSinks = default(1);
        @display("bgb=450,650");
    submodules:
        visualizer: <default("IntegratedCanvasVisualizer")> like IIntegratedVisualizer if hasVisualizer() {
            @display("p=550,50");
            networkRouteVisualizer.displayRoutes = true;
        }
        radioMedium: UnitDiskRadioMedium {
            @display("p=550,250;is=s");
        }
        configurator: RplNetConfigurator {
            @display("p=550,150;is=s");
        }
        
        sink[numSinks]: RplRouter {
            @display("i=device/pocketpc_s;p=149.112,75.864");
        }
        host[numNodes]: RplRouter {
            @display("i=device/pocketpc_s;p=83.712,179.196");
        }

    connections allowunconnected:

}
