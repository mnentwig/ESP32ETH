# simple ESP32 Ethernet (Olimex ESP32-ETH board) demo
Use 192.168.178.1 fixed IP on connecting ETH card.
Use Teraterm "new connection": Telnet, connect to 192.168.178.123 on port 79
Data gets echoed back (after pressing RETURN, unless Teraterm is set to send characters immediately)

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



