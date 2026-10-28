---
title: Connect an AI assistant
category: ai-assistants
order: 10
summary: The built-in MCP server lets AI assistants work with your open documents.
keywords: ai, assistant, mcp, model context protocol, claude, codex, gemini, copilot, cursor, server, token, setup
related: /ai, /help/ai-permissions, /help/ai-tools
---
PDF4QT has a built-in server of the Model Context Protocol (MCP). AI assistants such as Claude, Codex, Gemini, GitHub Copilot or Cursor connect to it and can read, search and edit the documents you have open in PDF4QT, within the permissions you allow. What they can do is described in [What assistants can do](/help/ai-tools).

## The server

The page [AI](/ai) shows the state of the server and its address:

```
http://127.0.0.1:8765/mcp
```

- **Start Server** and **Stop Server** start and stop it. The server runs only while PDF4QT is open.
- **Start with PDF4QT** starts it with the application (off by default).
- **Copy Address** copies the address, also when the server is stopped.

## Connect an assistant

1. Start the server on the page AI.
2. In **Connect an assistant**, choose your assistant. PDF4QT prepares the command or the configuration for it, with the address (and the access token, when **Require access token** is on).
3. Copy the command and run it in a terminal (or put the configuration into the file of your client).
4. Open a document in PDF4QT and ask the assistant about it.

For Claude Code the command looks like this:

```
claude mcp add --transport http pdf4qt http://127.0.0.1:8765/mcp
```

With **Require access token** on, it also carries your token:

```
claude mcp add --transport http pdf4qt http://127.0.0.1:8765/mcp --header "Authorization: Bearer <token>"
```

The page has a setup for each of these clients:

| Client | How it is set up |
| --- | --- |
| Claude Code | A command for the terminal; `claude mcp list` then shows the server |
| Claude Desktop | An entry of `claude_desktop_config.json` (Settings › Developer › Edit Config). It connects through the bridge `mcp-remote`, which needs Node.js; restart Claude Desktop afterwards |
| Codex | An entry of `~/.codex/config.toml`. It also raises the tool timeout of Codex to 180 seconds, so that you have time to answer the questions of PDF4QT |
| Gemini CLI | A command for the terminal; `gemini mcp list` then shows the server |
| GitHub Copilot | An entry of `mcp.json` of Visual Studio Code (MCP: Open User Configuration) |
| Cursor | An entry of `~/.cursor/mcp.json` |
| Other client | The address, the header with the token (when it is required), and a JSON configuration for any other MCP client |

> [!WARNING] The access token is a password
> With **Require access token** on, the commands and configurations contain your access token. A command you run stays in the shell history of your terminal, and a configuration file stays on the disk. Do not share them. When a token may have leaked, create a new one: the old one stops working at once.

## Other MCP clients

Any client, which supports MCP over Streamable HTTP, can connect. Choose **Other client**:

1. Add a server of type Streamable HTTP (called “http” in some clients) with the address of the server.
2. When **Require access token** is on, let the client send the header `Authorization: Bearer <token>` with every request.
3. Or add the JSON configuration of the page to the MCP configuration file of the client; the key names differ between clients.

PDF4QT understands the protocol versions 2026-07-28, 2025-11-25 and 2025-06-18, so also older clients work. Clients, which cannot ask you to choose a document, get a list of the open documents to choose from.

## Connection settings

| Setting | Default | Meaning |
| --- | --- | --- |
| Port | 8765 | The port of the server (1024–65535) |
| Network access | This computer only | **Allow remote connections** lets other computers of your network connect |
| Require access token | Off | Every request must carry the access token; with remote connections it is always on |
| Access token | – | A new token disconnects all assistants; they need the new command |

Without the token, only programs of this computer reach the server, and web pages in a browser are always refused. Turn the token on when other people use this computer with their own accounts – see [Permissions of assistants](/help/ai-permissions).

A change of the connection restarts a running server. When another program uses the port, the page AI says so; choose another port and set up your assistants again.

> [!WARNING] Remote connections
> With **Allow remote connections**, everyone on your network, who knows the address and the token, can work with your open documents. The connection is not encrypted – allow it only on a network you trust. Windows may ask whether PDF4QT may accept connections from the network.

> [!IMPORTANT] More windows
> The server runs in the window of PDF4QT, which started it first. Assistants see only the documents of that window; the page AI of other windows tells you that the server runs in another window, and **Show Other Window** brings it to the front.

## Who is connected

**Connected assistants** lists the assistants, which used the server in the last 30 minutes or keep a connection open to it, with their computers and the time they connected. An assistant, which has not used PDF4QT for 30 minutes, disappears from the list and comes back with its next request.

- **Disconnect** ends the session of one assistant: its running requests are canceled and its read approval is forgotten. The assistant can connect again with its next request.
- **Disconnect All** does it for all assistants.
- To block an assistant for good, turn on **Require access token** and create a new access token: every assistant without the new token is refused.

**Recent activity** lists the last requests of the assistants and how they ended; **Clear** empties the list. The meaning of the states is in [Permissions of assistants](/help/ai-permissions).
