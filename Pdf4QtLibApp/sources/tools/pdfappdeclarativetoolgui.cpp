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

#include "pdfappdeclarativetoolgui.h"

namespace pdf
{

PDFAppDeclarativeToolGUI::PDFAppDeclarativeToolGUI(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppDeclarativeToolGUI::~PDFAppDeclarativeToolGUI() = default;

QWidget* PDFAppDeclarativeToolGUI::createToolActions(QWidget* parent)
{
    QWidget* widget = new QWidget(parent);
    widget->setObjectName(QStringLiteral("toolActions"));
    PDFAppToolPanelBuilder builder(this, widget, Qt::Horizontal, PDFAppToolPanelBuilder::ACTION_GAP);
    describeToolbar(builder);
    if (builder.isEmpty())
    {
        delete widget;
        return nullptr;
    }
    return widget;
}

QWidget* PDFAppDeclarativeToolGUI::createRightPanel(QWidget* parent)
{
    QWidget* widget = new QWidget(parent);
    widget->setObjectName(QStringLiteral("toolRightContent"));
    PDFAppToolPanelBuilder builder(this, widget, Qt::Vertical, getRightPanelSpacing());
    describeRightPanel(builder);
    if (builder.isEmpty())
    {
        delete widget;
        return nullptr;
    }
    return widget;
}

QWidget* PDFAppDeclarativeToolGUI::createLeftPanel(QWidget* parent)
{
    QWidget* widget = new QWidget(parent);
    widget->setObjectName(QStringLiteral("toolLeftContent"));
    PDFAppToolPanelBuilder builder(this, widget, Qt::Vertical, 10);
    describeLeftPanel(builder);
    if (builder.isEmpty())
    {
        delete widget;
        return nullptr;
    }
    return widget;
}

void PDFAppDeclarativeToolGUI::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    Q_UNUSED(builder);
}

void PDFAppDeclarativeToolGUI::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    Q_UNUSED(builder);
}

void PDFAppDeclarativeToolGUI::describeLeftPanel(PDFAppToolPanelBuilder& builder)
{
    Q_UNUSED(builder);
}

}   // namespace pdf

#include "moc_pdfappdeclarativetoolgui.cpp"
