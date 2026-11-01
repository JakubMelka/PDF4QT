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

#ifndef PDFAPPDECLARATIVETOOLGUI_H
#define PDFAPPDECLARATIVETOOLGUI_H

#include "pdfapptoolpanelbuilder.h"
#include "pdfdocumenttoolgui.h"

namespace pdf
{

/// Base of the thin tools (plan of the first batch, chapter 2.1): a tool describes its
/// actions and panels through the builder, each part is built only when it has an item.
/// The description may be called again (a change of the language): a pointer to a part
/// kept by the tool is valid until the next call of the same description.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDeclarativeToolGUI : public PDFDocumentToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFDocumentToolGUI;

public:
    explicit PDFAppDeclarativeToolGUI(QObject* parent = nullptr);
    virtual ~PDFAppDeclarativeToolGUI() override;

    virtual QWidget* createToolActions(QWidget* parent) override;
    virtual QWidget* createRightPanel(QWidget* parent) override;
    virtual QWidget* createLeftPanel(QWidget* parent) override;

protected:
    /// The actions of the toolbar (tool-actions 291:1135)
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder);

    /// The right panel (sidebar-right 291:1177)
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder);

    /// The left panel in the place of the sidebar; nothing = the sidebar stays
    virtual void describeLeftPanel(PDFAppToolPanelBuilder& builder);
};

}   // namespace pdf

#endif // PDFAPPDECLARATIVETOOLGUI_H
