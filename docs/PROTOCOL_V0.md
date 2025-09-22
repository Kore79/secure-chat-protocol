# Protocol Specification - Version 0.1 

## Overiew
Version 0.1 is a plain text , line based protocol operating over TCP. It is designed to be human readable and simple, prioritizing ease of implementation over security and efficiency.

## Framing
- **Encoding:** UTF-8 text
- **Message Delimiter:** A single newline character('\n').
- **Maximum Message Length:** No inherent limit (relies on `BUFFER_SIZE` in server implementation).

## Session Flow
1. **Connection:** Client connects to server port.
2. **Greeting:** Server sends: `"Welcome! Please set your name with : NAME <yourname>\n"`
3. **Registration:** Client must send a `NAME` command before using `SAY`.
4. **Messaging:** Client can now participate in chat using `SAY`.
5. **Termination:**  Connection closes on client disconnect or server crash.

## Command Specification

## 'NAME' - Set Username
**Purpose:** Registers a client's chosen username with the server

**Format:**
NAME <username>\n

**Parameters:**
- '<username>': Any string of characters without a newline.

**Server Responses:**
- 'OK'\n - Success.
- 'ERROR: NAME requires a username\n' - Invalid syntax.
- (No response if command is ignored).

**Example:**
Client: NAME Bleson\n
Server: OK\n

### `SAY` - Send a Message to All
**Purpose:** Broadcasts a message to all connected clients.

**Format:**
SAY<message>\n

**Parameters:**
- '<message>': The text to be broadcast.

**Server Responses:**
- 'ERROR: SAY requires a message \n' - Invalid syntax
- 'ERROR: Set your name with NAME <username> first\n' - Client hasn't registered.
- `[<username>] <message>' - Message broadcast to all *other* clients.

**Example:**
Client: SAY Hello everyone!\n
Other Clients: [Bleson] Hello everyone!

## Security Considerations
This protocol version is intentionally insecure, Known vulnerabilities include
- No authentication (username spoofing is trivial)
- No encrypttion (all communication is plain text)
- Line-based framing is susceptible to injection attacks
- No message length limits enable buffer overflow attacks
