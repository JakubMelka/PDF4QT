// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef PDFAPPHISTORY_H
#define PDFAPPHISTORY_H

#include "pdfappglobal.h"
#include "pdfapproute.h"

#include <QList>
#include <QVariantMap>

namespace pdf
{

/// Entry of the history of a tab: the route and the state of the view of its
/// page (scroll position, expanded sections), saved when the tab left it
class PDF4QTLIBAPPSHARED_EXPORT PDFAppHistoryEntry
{
public:
    PDFAppRoute route;
    QVariantMap viewState;
};

/// History of a tab (newapp_navigation.md 2): at most MAXIMUM_ENTRIES entries,
/// the oldest one is dropped. Push removes the entries after the current one
/// (as a web browser). Dialogs, menus and popovers are states of a page, they
/// are never in the history.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppHistory
{
public:
    static constexpr int MAXIMUM_ENTRIES = 50;

    PDFAppHistory() = default;

    /// Adds the route after the current entry (the forward entries are removed)
    void push(const PDFAppRoute& route);

    /// Replaces the route of the current entry (push, when the history is empty)
    void replace(const PDFAppRoute& route);

    bool isEmpty() const { return m_entries.isEmpty(); }
    int getCount() const { return int(m_entries.size()); }
    int getCurrentIndex() const { return m_currentIndex; }

    /// Current entry, nullptr for an empty history
    const PDFAppHistoryEntry* getCurrentEntry() const;

    /// Entry of the index, nullptr out of the range
    const PDFAppHistoryEntry* getEntry(int index) const;

    /// Stores the state of the view of the current entry
    void setCurrentViewState(const QVariantMap& state);

    bool canGoBack() const;
    bool canGoForward() const;

    /// Moves to the previous / next entry. \returns false, if there is none
    bool goBack();
    bool goForward();

    /// Removes all entries
    void clear();

private:
    QList<PDFAppHistoryEntry> m_entries;
    int m_currentIndex = -1;
};

}   // namespace pdf

#endif // PDFAPPHISTORY_H
