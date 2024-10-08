# ESP32-/Ethernet platform (Olimex ESP32-ETH board) e.g. for GPIO control
Generic remote-control application via simple ASCII protocol e.g. to operate GPIOs via the network.

Serves multiple TCP/IP ports.

## Protocol
- text-based only
- maximum input line length 255 characters
- response can be any length
- input may use terminating characters following Telnet convention \r\n or \r\0, Windows \r\n and UNIX \n (effectively, the first of \r\n\0 detects as end-of-line)
- leading whitespace and additional terminating characters from above sequences  are suppressed.
- output is terminated using \n
- a command may result in any number of reply lines, including zero The client would usually know from the context what reply to expect
- If the response length is not known (debug, "human-readable" reports, complex use cases), use "ECHO XYZ" as follow-up command with a unique, known token XYZ and read response until XYZ\n is received.
- "ECHO XYZ" may be used to wait for completion of previous, slow commands.
- TCP/IP is not used for framing. That is, sending commands character-by-character or a command sequence spanning tens of kB will give the same result (the latter being more efficient)

## connect via Teraterm
- change "Setup/Terminal/New Line/Receive" to AUTO
- File/New Connection, TCP/IP, "Other" (or "Telnet"). Set TCP/IP address and port e.g. 79 (see code)

## command set
### ERR? command
- Returns error count and the first error message, comma-separated.

- returns "0,NO_ERROR" in case of no error.

- clears the error status. E.g. use ERR? before a problematic command to see the actual error message, not an earlier one.

### ERRCLR
- clears the error status (same as ERR?) but does not return anything

### RESTART
Resets the processor, e.g. to apply new IP settings

### ETH:IP?
returns the static IP address stored in flash memory (may differ from actual interface setting which updates on REBOOT)

### ETH:GW?
returns the gateway IP address stored in flash memory (may differ from actual interface setting which updates on REBOOT)

### ETH:MASK?
returns the netmask stored in flash memory (may differ from actual interface setting which updates on REBOOT)

### ETH:IP xxx.xxx.xxx.xxx
changes the static IP address stored in flash memory (actual interface setting updates on REBOOT)

### ETH:GW xxx.xxx.xxx.xxx
changes the gateway IP address stored in flash memory (actual interface setting updates on REBOOT)

### ETH:MASK
changes the netmask stored in flash memory (actual interface setting updates on REBOOT)

## error messages
### SYNTAX_ERROR
Unrecognized command token
### ARG_COUNT
Unexpected number of command parameters
### ARG_PARSE_IP
Argument could not be parsed as IP4-address e.g. 192.168.1.100 (note: leading zeros e.g. 192.168.001.100 are not allowed)

# IDF version
use 5.2.1


# notes on low-latency (~1 ms roundtrip) Ethernet
- run Ethernet task at highest priority
- disable Nagle's algorithm
- 240 MHz CPU clock won't hurt
- FreeRTOS tick rate should not matter if ETH task prio is sufficient
- WIFI seems a bit slower but that may be my home network

# notes on memory, camera
The camera needs PSRAM enabled (menuconfig "components", typically 80 MHz). Select malloc with flag (plain memorymapped won't work with camera), but don't select full malloc or everything will slow down excessively.

At the moment, ADC and camera seem to clash in interrupt mapping (would moving camera or ADC init to core 1 help?)

