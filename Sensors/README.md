# Not-So-Random

This is the code and electrical setup used for controlling the Not So Random sculpture work. This is the dual-MKR setup.

Last update:
Version 1.0
14 August 2025 / Joshua Prince


# 1.0 Background:


Not So Random is a commissioned sculpture selected by a student cohort for the E.A. Fernandez IDEA Factory at UMD, College Park, MD. Initally planned for a three year public exhibition starting 15 September 2025.

Artist:
Joshua Prince

Electrical + Software Design:
This control scheme uses 2x Arduino MKR boards, specifically the MKR 1010 WiFi, to control a wire feed mechanism. One MKR is designated the "Controller" and the other is designated the "Worker." The Controller is connected to the UMD IOT network (named Midori) and recieves data/commands from the Arduino cloud. This data is convereted into signal data and sent over an I2C bus to the Worker MKR. The Worker MKR uses these command signals to then drive a stepper motor, feeding wire in and out of the sculpture.

# 2.0 Setup

2.1 Hardware

2.1.1 Motor: 
1x StepperOnline PG17 NEMA 17 Bi-Polar Stepper Motor (0.4A per coil), 15:1 Gear Ratio, S/N: 

Wire Table:
```
BLACK  |  A+  |  1A  |	Green  / 3
GREEN  |  A-  |  1B	 |  Yellow / 4
RED    |  B+  |  2A  |  Brown  / 2
BLUE   |  B-  |  2B  |  White  / 1
```
2.1.2 Power:
```
120 VAC -->  	{{{ WAGO AC/DC Converter || --> 24 DCV
		24 DCV  ||5A Fuse|| -->	||1A CB|| --> ||TRACO DC/DC| --> 5.1 DCV --> |Controller|
				||	            ||1A CB|| --> ||TRACO DC/DC| --> 5.1 DCV --> |Worker|
				||5A Fuse|| --> ||3A CB|| --> ||Motor 1||
								||3A CB|| --> ||Reserved||
```
2.1.3 Microelectronics:

2.1.3.1 Data Flow Path:
```
{Arduino Cloud} --> |Controller| <-- ESLOV --> |ESLOV - I2C| <-- I2C --> |I2C - ESLOV| <-- ESLOV --> |Worker|
```
2.1.3.2 Wiring:

Controller
```
	5.1 V   --> | Vin    ESLOV   | --> To Worker
	GND     --> | GND            |
	            |________________|
```
Worker
```
	5.1 V   --> | Vin    ESLOV  | <-- From Controller 
	GND     --> | GND           | 
	VDD PIN <-- | Vcc      DIO 4| --> EN Pin 
	MS1 PIN <-- | DIO 2    DIO 5| --> (Reserved EN Pin for #2 Motor)
	MS2 PIN <-- | DIO 3    DIO 6| --> STEP Pin 
	            |          DIO 7| --> DIR Pin 
	            |          DIO 8| --> (Reserved STEP Pin for #2 Motor)
	            |          DIO 9| --> (Reserved DIR Pin for #2 Motor)
	            |_______________|
```
