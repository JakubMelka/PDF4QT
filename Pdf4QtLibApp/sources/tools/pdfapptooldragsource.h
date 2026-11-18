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

#ifndef PDFAPPTOOLDRAGSOURCE_H
#define PDFAPPTOOLDRAGSOURCE_H

#include "pdfappglobal.h"

#include <QWidget>

namespace pdf
{

/// A widget of a tool with a drag of the mouse, which Esc cancels (owner 2026-11-17, the first step of
/// Esc): the shortcut Esc of the shell takes the key before the widget, so the shell asks the widgets of
/// the tool first (PDFDocumentToolShell::onEscape), then the GUI (PDFDocumentToolGUI::handleEscape), and
/// only then it leaves the tool. The GUI thread only.
class PDF4QTLIBAPPSHARED_EXPORT IPDFAppToolDragSource
{
public:
    virtual ~IPDFAppToolDragSource() = default;

    /// A drag of the widget is in progress
    virtual bool isToolDragActive() const = 0;

    /// Cancels the drag: the state before it comes back, nothing is chosen, the release of the button
    /// does nothing. A signal is the last call (its slot may delete the widget).
    virtual void cancelToolDrag() = 0;

    /// The first visible widget of \p root and its descendants with a drag in progress, nullptr = none
    /// (the shell cancels it by Esc, the status text of the tool says so - one rule for both). A hidden
    /// widget (a page of the tool, which is not shown) has no drag the user could cancel (review
    /// RSRFCODE L6).
    static IPDFAppToolDragSource* findActive(QWidget* root)
    {
        if (!root || !root->isVisible())
        {
            return nullptr;
        }

        IPDFAppToolDragSource* rootSource = dynamic_cast<IPDFAppToolDragSource*>(root);
        if (rootSource && rootSource->isToolDragActive())
        {
            return rootSource;
        }
        for (QWidget* widget : root->findChildren<QWidget*>())
        {
            IPDFAppToolDragSource* source = widget->isVisible() ? dynamic_cast<IPDFAppToolDragSource*>(widget) : nullptr;
            if (source && source->isToolDragActive())
            {
                return source;
            }
        }
        return nullptr;
    }
};

}   // namespace pdf

#endif // PDFAPPTOOLDRAGSOURCE_H
