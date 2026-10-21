---
title: Connect an AI assistant
category: ai-assistants
order: 10
summary: The built-in MCP server lets AI assistants work with your open documents.
keywords: ai, assistant, mcp, model context protocol, claude, codex, gemini, copilot, cursor, server, token
related: /ai, /help/ai-permissions
---
PDF4QT has a built-in server of the Model Context Protocol (MCP). AI assistants such as Claude, Codex, Gemini, GitHub Copilot or Cursor connect to it and can read, search and edit the documents you have open in PDF4QT, within the permissions you allow.

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
2. In **Connect an assistant**, choose your assistant. PDF4QT prepares the command or the configuration for it, with the address and the access token.
3. Copy the command and run it in a terminal (or put the configuration into the file of your client).
4. Open a document in PDF4QT and ask the assistant about it.

For Claude Code the command looks like this (the token is your own):

```
claude mcp add --transport http pdf4qt http://127.0.0.1:8765/mcp --header "Authorization: Bearer <token>"
```

Claude Desktop connects through the bridge `mcp-remote`, which needs Node.js; **Other client** shows the address, the header and a JSON configuration for any other MCP client.

## Connection settings

| Setting | Default | Meaning |
| --- | --- | --- |
| Port | 8765 | The port of the server (1024–65535) |
| Network access | This computer only | **Allow remote connections** lets other computers of your network connect |
| Require access token | On | Every request must carry the access token; with remote connections it is always on |
| Access token | – | A new token disconnects all assistants; they need the new command |

A change of the connection restarts a running server.

> [!IMPORTANT] More windows
> The server runs in the window of PDF4QT, which started it first. Assistants see only the documents of that window; the page AI of other windows tells you that another window uses the port.

## Who is connected

**Connected assistants** lists the assistants with their addresses and times. **Disconnect** ends the session of one assistant, **Disconnect All** of all of them. To block an assistant for good, create a new access token. **Recent activity** lists what the assistants did; **Clear** empties the list.
