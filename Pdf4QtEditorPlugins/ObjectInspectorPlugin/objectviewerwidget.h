// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#ifndef OBJECTVIEWERWIDGET_H
#define OBJECTVIEWERWIDGET_H

#include "pdfdocument.h"
#include "pdfcms.h"

#include <QWidget>

namespace Ui
{
class ObjectViewerWidget;
}

namespace pdfplugin
{

class ObjectViewerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ObjectViewerWidget(QWidget* parent);
    explicit ObjectViewerWidget(bool isPinned, QWidget* parent);
    virtual ~ObjectViewerWidget() override;

    ObjectViewerWidget* clone(bool isPinned, QWidget* parent);

    void setPinned(bool isPinned);
    void setData(pdf::PDFObjectReference currentReference, pdf::PDFObject currentObject, bool isRootObject);

    const pdf::PDFDocument* getDocument() const;
    void setDocument(const pdf::PDFDocument* document);

    const pdf::PDFCMS* getCms() const;
    void setCms(const pdf::PDFCMS* cms);

    QString getTitleText() const;

signals:
    void pinRequest();
    void unpinRequest();

private:
    void updateUi();
    void updatePinnedUi();

    Ui::ObjectViewerWidget* ui;
    const pdf::PDFCMS* m_cms;
    const pdf::PDFDocument* m_document;
    bool m_isPinned;

    pdf::PDFObjectReference m_currentReference;
    pdf::PDFObject m_currentObject;
    bool m_isRootObject;
    QByteArray m_printableCharacters;
};

}   // pdfplugin

#endif // OBJECTVIEWERWIDGET_H
