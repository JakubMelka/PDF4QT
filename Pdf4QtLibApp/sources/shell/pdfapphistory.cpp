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

#include "pdfapphistory.h"

namespace pdf
{

void PDFAppHistory::push(const PDFAppRoute& route)
{
    // The entries after the current one are removed (a new branch of the history)
    while (m_entries.size() > m_currentIndex + 1)
    {
        m_entries.removeLast();
    }

    PDFAppHistoryEntry entry;
    entry.route = route;
    m_entries.append(entry);

    while (m_entries.size() > MAXIMUM_ENTRIES)
    {
        m_entries.removeFirst();
    }
    m_currentIndex = int(m_entries.size()) - 1;
}

void PDFAppHistory::replace(const PDFAppRoute& route)
{
    if (m_currentIndex < 0 || m_currentIndex >= m_entries.size())
    {
        push(route);
        return;
    }

    PDFAppHistoryEntry& entry = m_entries[m_currentIndex];
    entry.route = route;
    entry.viewState.clear();
}

const PDFAppHistoryEntry* PDFAppHistory::getCurrentEntry() const
{
    return getEntry(m_currentIndex);
}

const PDFAppHistoryEntry* PDFAppHistory::getEntry(int index) const
{
    if (index < 0 || index >= m_entries.size())
    {
        return nullptr;
    }
    return &m_entries[index];
}

void PDFAppHistory::setCurrentViewState(const QVariantMap& state)
{
    if (m_currentIndex >= 0 && m_currentIndex < m_entries.size())
    {
        m_entries[m_currentIndex].viewState = state;
    }
}

bool PDFAppHistory::canGoBack() const
{
    return m_currentIndex > 0 && m_currentIndex < m_entries.size();
}

bool PDFAppHistory::canGoForward() const
{
    return m_currentIndex >= 0 && m_currentIndex + 1 < m_entries.size();
}

bool PDFAppHistory::goBack()
{
    if (!canGoBack())
    {
        return false;
    }
    --m_currentIndex;
    return true;
}

bool PDFAppHistory::goForward()
{
    if (!canGoForward())
    {
        return false;
    }
    ++m_currentIndex;
    return true;
}

void PDFAppHistory::clear()
{
    m_entries.clear();
    m_currentIndex = -1;
}

}   // namespace pdf
