// 
//   Simulation model for RPL (Routing Protocol for Low-Power and Lossy Networks)
//  
//   Copyright (C) 2020  Institute of Communication Networks (ComNets),
//                       Hamburg University of Technology (TUHH)
//             (C) 2020  Yevhenii Shudrenko
//  
//   This program is free software: you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation, either version 3 of the License, or
//   (at your option) any later version.
//  
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//   GNU General Public License for more details.
//  
//   You should have received a copy of the GNU General Public License
//   along with this program.  If not, see <https://www.gnu.org/licenses/>.
//  

package rpl;

import inet.node.inet.AdhocHost;
import rpl.Rpl;
import rpl.TrickleTimer;

module RplRouter extends AdhocHost
{   
    submodules:
        rpl: Rpl {
            @display("p=825,226");
        }
        trickleTimer: TrickleTimer {
            @display("p=946.57495,225.22499");
        }

    connections:
        rpl.ipOut --> tn.in++;
        rpl.ipIn <-- tn.out++;
        rpl.ttModule <--> trickleTimer.rpModule;
}

