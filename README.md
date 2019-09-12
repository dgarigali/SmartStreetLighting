# Smart street lighting control network using Mbeds

This project was developed for the Networked Embedded Systems course. It consists on a network of nodes capable of detecting cars passing nearby (using ultrasonic sensors), lighting up the street only when necessary. When a sensor detects one car, the node should light up its own led and send a radio message (using Xbees) to the node the car is heading to, making it also light up its own led. The led is switched off after no cars are detected for a given time. Also, LDRs are used for periodically checking if there already is enough environment light, which will save some energy as the led action is not necessary. 

The system works in a dynamic and distributed manner as, in runtime, new nodes can be added or removed to the system (which means that each node only knows which are its adjacent nodes in runtime) and there is no central node (no single point of failure). 

## Hardware

The system was tested with three nodes. For the nodes on the edges, only one ultrasonic sensor is required as those nodes only have one adjacent node. On the other hand, all the nodes that are not on the edges must have two ultrasonic sensor. Each node is basically composed by:

**MB1200:** [ultrasonic sensor](https://www.maxbotix.com/Ultrasonic_Sensors/MB1200.htm) for detecting cars 

**Xbee 802.15.4:** [radio frequency module](https://www.digi.com/products/embedded-systems/rf-modules/2-4-ghz-modules/xbee-802-15-4) for communication between nodes

**LDR:** [light sensor](https://uk.rs-online.com/web/c/displays-optoelectronics/optocouplers-photodetectors-photointerrupters/ldr-light-dependent-resistors/) for checking if environment light is enough for the street visibility

**Led:** for artificially lighting up the street

**Mbed:** [microcontroller](https://os.mbed.com/platforms/mbed-LPC1768/) chosen for interfacing all the components above



