# M-Bus CD-Changer Controller

A controller board for standalone use of Alpine M-Bus CD-changers.

I got hold of an old car CD-changer that was originally built into a Mercedes-Benz car.
The car and the radio did not exist anymore, but since CD-changers are cool tech, I was wondering whether I could make it work again.
After some searching, I found that the actual manufacturer of the device is Alpine and that it is controlled by an M-Bus protocol.
The pinout of the connector apparently is proprietary, but it is not too difficult to identify the +12V / GND wires.
Just notice that there is a separate separate wire to the car ignition that also has to be connected to +12V to start it up.

The other wires are for M-Bus and the audio output, each with its own GND.

Fortunately, I found this website that did some reverse engineering of the M-Bus protocol: http://www.hohensohn.info/mbus/
Most of it matched my CD-changer, but several commands simply did not work, thus I could not implement typical CD-player functions like "pause".
I assume that this is a limitation of my specific device, but since I don't have the original radio controller, I cannot say for sure.

## Video

[![image](https://github.com/user-attachments/assets/5a3b88e0-bb79-4286-800f-44465af6df1f)](https://www.youtube.com/watch?v=HMd5gL0tslU)

[Demo video](https://www.youtube.com/watch?v=HMd5gL0tslU)

## Building it

For anyone interested in playing along, this repository contains the KiCad files for the PCB. The schematics are basically the voltage-level adapter as shown on http://www.hohensohn.info/mbus/
plus the Atmel microcontroller with a few switches, HD44780 compatible display, and power supply. I use an old 12V plug-in power supply. Note that the PCB has the footprint for a TO-92 7805 regulator for the 5V needed for the uC and display.
However, the TO-92 was a slightly too optimistic choice. When I tried, it immediately dissolved in a white cloud. Thus, a TO-220 should be fitted into it instead.
The LCD used is this one: [PDF datasheet](https://www.lcd-module.de/eng/pdf/doma/dips082e.pdf).
The uC-Board is a cheap [Arduino clone](https://www.amazon.com/Teyleten-Robot-Bootloadered-Development-Microcontroller/dp/B08THVMQ46/).

Because of the minimum ordering quantities, I have a couple of spare PCBs (3 available). Anyone interested can drop me a line and I'll send it to them for postage and packaging.

I removed the suspension mounting of the CD drive since it was worn out after so many years anyways, and I mounted the drive mechanics directly to the case.

![PXL_20250428_093103646](https://github.com/user-attachments/assets/62786af3-9d12-49df-9bc1-4eb0ce9c99dd)
