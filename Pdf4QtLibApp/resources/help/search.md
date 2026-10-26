---
title: Search everything
category: getting-started
order: 30
summary: One search field for tools, commands, settings, documents and the user manual.
keywords: search, find, ctrl+k, ctrl+enter, ctrl+f, f3, command, results, quick search, search in document, find in document, regular expression, regex, wildcards, whole words, match case
related: /help/keyboard-shortcuts, /help/settings, /help/annotations
---
The pages of the Home tab have a search field *Search tools, documents, or help…* at the top right. One search looks through everything PDF4QT knows.

## What is searched

| Group | Results |
| --- | --- |
| Tools | The tools of the catalog by their names, descriptions and keywords |
| Commands | Commands of the application, for example *New tab* or *Close tab*, with their shortcuts |
| Settings | Single settings; a result opens its section and scrolls to the setting |
| Documents | Your recent and pinned documents |
| Help | Topics and sections of this manual and the video tutorials |

## Use the search

1. Press **Ctrl+K**, or click the search field.
2. Type a few words. The results appear while you type.
3. Choose a result with the mouse, or move to it with **Up** and **Down** and press **Enter**. **Esc** closes the list. **Ctrl+Enter** or the link *Show all … results* at the bottom of the list opens all results on their own page, where the chips show only one group and **Show all (n)** of a group lists all its results.

When nothing matches, the page of the results offers **Search the User Manual** and **Search everywhere**. The search finds a word also by its beginning and with one typo, and it ignores upper and lower case and accents.

> [!TIP] Settings by their names
> You do not need to know, in which section a setting is. Type its name, for example *color scheme*, and the result opens the right section of Settings.

## Search in a document

An open document has its own search field *Search in document…* at the right of its toolbar.

1. Press **Ctrl+F**, or click the search field.
2. Type a text and press **Enter**. The panel *Search results* opens in the sidebar with every occurrence, its page and the line around it; the occurrences are highlighted on the pages.
3. Press **F3** or **Enter** for the next result, **Shift+F3** for the previous one, or click a result in the list. The status bar shows *Result 3 of 12*.
4. Press **Esc** in the search field, or the close button of the panel, to end the search.

The options above the results change how the text is matched:

| Option | What it does |
| --- | --- |
| Match case | Upper and lower case letters are different |
| Whole words | The text is found only as a whole word, not inside of a longer word |
| Regex | The text is a regular expression, for example `20\d\d` finds the years 2000 to 2099 |
| Wildcards | `*` matches any characters of a word and `?` one character, for example `rev*` finds *revenue* and *review* |

**Regex** and **Wildcards** cannot be on together. A text, which is not a valid expression, is marked *The search text is not valid*.

The first search of a document reads the text of all its pages; a large document shows the progress of the indexing in the panel. The next searches are fast. When a search finds more than 100,000 occurrences, only the first 100,000 are shown – search for a longer text to find the others.

**Mark as annotations…** at the bottom of the results marks the found texts as highlights or other marks, see [Annotate a document](/help/annotations).

> [!TIP] Search again
> **F3** with a text in the search field and no results searches the text, so you can type a text and press F3 instead of Enter.
