---
title: Window and tabs
category: getting-started
order: 20
summary: Tabs in the title bar, New tab, closing and reordering tabs and more windows.
keywords: tab, window, title bar, new tab, close, reorder, instance, second window, maximize
related: /help/keyboard-shortcuts, /help/open-documents
---
The window of PDF4QT has no title bar of the system. Its top bar holds the tabs, the button of a new tab and the buttons of the window, as in a web browser.

## Tabs

- **Home** is the first tab. It has the sidebar with the pages of the application and it cannot be closed.
- Every document has its own tab with the name of the file. A lock shows a document, which is locked against changes, a dot shows unsaved changes, and a document protected by a password has the icon of a password.
- The button **+** (or **Ctrl+T**) opens a tab **New tab** with the page Open. When you open a file in it, it becomes the tab of the document.
- **Ctrl+W** or the button **×** of the tab closes it. **Ctrl+Tab** and **Ctrl+Shift+Tab** switch to the next and to the previous tab.
- **Help** (or **F1**) opens this user manual in a tab of its own. A window has at most one Help tab: the next topic opens in the same tab, and **Back** and **Forward** move between the topics you have read there. Links from the manual to other pages open them in the Home tab.
- Drag a tab to change the order of the tabs.

> [!NOTE] Links from New tab
> A tab New tab shows only the page Open. Any other link in it (for example *View all*) switches to the Home tab and opens the page there.

## The buttons of the window

On Windows and Linux the buttons **Minimize**, **Maximize** and **Close** are at the right end of the top bar; on macOS the buttons of the system are at the left. Double-click the free area of the top bar to maximize the window or to restore it, and drag it to move the window. On Windows, hold the pointer over **Maximize** to see the snap layouts of the system.

## More windows

Each window of PDF4QT is a separate instance of the application:

- Starting PDF4QT without files (from its icon or from the start menu) opens a new window.
- Opening files from the file manager or from the command line passes them to the window, which is already running; they open there as new tabs and the window comes to the front.
- The option `--new-window` of the command line starts a separate window, which does not receive the files of other starts.

All windows share the same settings and history, see [Where PDF4QT keeps your data](/help/data-storage).
