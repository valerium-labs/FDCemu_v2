================================================================================================
Schematic file description

schematic0.pdf - complete version of FDCemu with ATMEGA644 MCU, EPM3032 CPLD, LCD&encoder controls;
FDCemu_CPLD.jpg - CPLD block diagram describing CPLD logic and some optional control signals;
FDCemu_discrete_ver_simplified.png - the equivalent diagram based on Pentagon's built-in FDC.

================================================================================================
ROM configuration

64K ROM contains four 16K-banks organized like this:

A15 A14
0   0  - not used
0   1  - TRDOS
1   0  - BASIC128
1   1  - BASIC48

================================================================================================
ROM bank control line - /DOSEN (uses A4 pin on ZXBUS in schematics)

/DOSEN=0:			Select TR-DOS ROM bank
/DOSEN=1:			Select Basic48/128 ROM bank


================================================================================================
Additional control lines at bottom side pads (you may manually connect them to ZXBUS):

ROM128=1 (or not connected):	FDC&TR-DOS enabled
ROM128=0:			Disable FDC&TR-DOS when Basic128 is on

SELRESET=1 (or not connected):	initial reset to Basic48
SELRESET=0: 			initial reset to TR-DOS

CPLDHV: "High voltage" pin to unlock EPM30xx CPLD when "JTAG disable" option is on.
(apply 10,5v to this pin (i.e. OE1) of "locked" CPLD when issuing ERASE of PROGRAM command)
!!! USE IT AT YOUR OWN RISK !!!