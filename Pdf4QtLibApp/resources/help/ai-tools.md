---
title: What assistants can do
category: ai-assistants
order: 15
summary: The tools, documents and workflows the MCP server of PDF4QT gives to AI assistants.
keywords: ai, assistant, mcp, tools, read, search, render, annotate, highlight, form, outline, open, save, prompt, workflow, skill
related: /ai, /help/ai-assistant, /help/ai-permissions
---
An assistant connected to the MCP server of PDF4QT (see [Connect an AI assistant](/help/ai-assistant)) works with the documents you have open in PDF4QT – with the documents of the window, which runs the server. It sees nothing else of your computer: no other files, no list of your recent documents.

## The tools

| Tool | What the assistant can do | Permission |
| --- | --- | --- |
| PDF4QT application | See which documents are open, which one is active, and your permissions | – |
| Document information | Read the properties, pages, outline, annotations, form fields, attachments, signatures, layers, your text selection and your bookmarks | Read documents |
| Document text | Read the text of pages and search it | Read documents |
| Render pages | Look at pages as images – figures, tables, scans | Read documents |
| Show in PDF4QT | Switch tabs, go to a page, point at a place on a page, open the search, open a page of PDF4QT, close a tab | Read documents |
| Edit annotations | Add highlights, notes, text boxes, shapes, stamps and links; change, delete and reply to annotations | Edit documents |
| Fill forms | Fill and reset form fields | Edit documents |
| Edit the outline | Add, rename, move and delete outline items, or build a new outline | Edit documents |
| Open and save files | Open a PDF file whose path it knows, save a document, save a copy | Open and save files |

The permissions are explained in [Permissions of assistants](/help/ai-permissions). An action set to Deny is not offered to assistants at all.

## Changes you can undo

- Every request of an assistant is one step of **Undo** in PDF4QT, however many annotations or fields it changes.
- After a change, a bar over the page says what the assistant did, with **Show Changes** and **Undo**.
- The annotations of an assistant have the name of the assistant as their author, for example *Claude Code*.
- Nothing is saved to the file until you or the assistant save it; saving over the file is always confirmed by you.
- A locked document is never changed. To let an assistant work freely, give it a copy (Edit a Copy).

## What assistants cannot do

- Type passwords: when a document needs one, PDF4QT asks you for it.
- Run the scripts of a document or open its attachments – an assistant can only read the content of an attachment.
- Sign documents or change the settings of PDF4QT.
- Open files from network locations or list your folders.

## Built-in guidance for assistants

The server gives the assistants their own documentation, so you do not have to explain PDF4QT to them:

- documents about the tools, the coordinates of the pages, the permissions and the errors;
- skills for working with documents, reviewing, research with citations and filling forms;
- workflows, which many clients offer as prompts or commands: **Summarize a document**, **Review and comment**, **Find and highlight**, **Fill a form**, **Build an outline** and **Answer with citations**;
- this manual, so that an assistant can explain PDF4QT to you.

## Good to know

- Ask the assistant to work with "the open document" or name its title – it finds the document itself.
- Assistants cite pages by their physical number and by the label printed on the page.
- Large documents take longer: an assistant reads them in parts and searches first.
- The text of a document can contain instructions meant to mislead an assistant. Assistants are told to treat documents as data, and every change still needs your permission – but stay careful with documents from unknown sources, and check what an assistant proposes before you allow it.
