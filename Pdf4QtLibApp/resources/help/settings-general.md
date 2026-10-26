---
title: General settings
category: settings
order: 12
summary: Language, color scheme, the history of recent files, the sidebar of documents, tool suggestions and the check for updates.
keywords: general, language, translation, locale, color scheme, theme, dark mode, light mode, recent files, history, sidebar, icon size, tool history, suggestions, then continue with, updates, check for updates
related: /settings/general, /help/settings, /help/recent-files, /help/whats-new-and-updates
---
[Settings › General](/settings/general) has the settings, which concern the whole application.

## Application

| Setting | Choices | Default |
| --- | --- | --- |
| [Language](/settings/general#language) | Automatic, English, Deutsch, 한국어, Español, Čeština, 简体中文, 繁體中文, Français, Türkçe, Русский | English |
| [Color scheme](/settings/general#color-scheme) | Automatic, Light, Dark | Automatic |

**Language** is the language of the user interface. A chosen language applies at once in all windows, without a restart; dates, times, file sizes and numbers follow it. **Automatic** uses the language of your system, when PDF4QT has its translation, and English otherwise. In the open list the arrow keys only move through the languages – press **Enter** or click a language to choose it, **Esc** keeps the current one.

> [!NOTE] Translations
> Texts, which are not translated yet, are shown in English. When the translation file of a language is missing or damaged, PDF4QT is shown in English and the row Language says so. The help topics are shown in your language, when they have a translation.

**Color scheme** chooses the light or the dark look of PDF4QT. **Automatic** follows the color scheme of your system and changes with it. The switch **Light / Dark / Auto** at the bottom of the sidebar of the Home tab is the same setting. The color scheme does not change the pages of documents – for darker pages use the [color modes](/help/color-modes).

## Documents

| Setting | What it does | Default |
| --- | --- | --- |
| [Recent files](/settings/general#recent-files) | How many files the history of [Recent](/help/recent-files) keeps (1–100) | 10 |
| [Show sidebar when opening documents](/settings/general#show-sidebar-when-opening-documents) | A newly opened document shows its sidebar | On |
| [Sidebar icon size](/settings/general#sidebar-icon-size) | The size of the buttons at the top of the sidebar of a document: Small, Medium, Large or Very large | Medium |

When you lower **Recent files**, the oldest files over the new limit leave the history at once; pinned files always stay. **Show sidebar when opening documents** concerns only documents opened later – **F4** shows or hides the sidebar of the current document at any time.

## Tools

PDF4QT counts which tools you run after each other, so that *Then continue with* can offer the tool you usually run next.

- [Suggest tools I often use next](/settings/general#suggest-next-tools) (on by default) turns this on and off. When it is off, only the default suggestions are shown and the tools you run one after another are not counted. Recent tools are still listed.
- **Tool history** shows how many runs are recorded. **Clear history…** asks first and then deletes the counts; your pinned tools, collections and the list of recently used tools stay.

Only tool identifiers and counts are kept on this computer, never file names or the content of documents.

## Updates

[Check for updates automatically](/settings/general#check-for-updates) (on by default) lets PDF4QT ask GitHub once a day, whether a newer version is available. The answer is shown on the page What's New; nothing is downloaded or installed. When it is off, PDF4QT asks only when you click **Check for updates** on What's New. See [What's New and updates](/help/whats-new-and-updates).
