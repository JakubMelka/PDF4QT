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

#ifndef PDFAPPRESTOREDFILEBAR_H
#define PDFAPPRESTOREDFILEBAR_H

#include "pdfappglobal.h"
#include "pdfdocument.h"

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>

namespace pdf
{

class PDFAppDocument;
class PDFAppDocumentTab;
class PDFAppDocumentView;

/// The info bar of a document restored from a damaged file (PDFAppDocument::getRestoration, owner
/// 2026-11-05; no frame of the design: the tone Warning of the bar of 1028:24277). It follows the
/// document: shown when the document is ready, gone when the file is written whole by this tab
/// (Save As, the Save of a document without a signature) or a reload reads a valid file. A bar
/// closed by the user stays closed for these bytes of the file; other damaged bytes (a reload by
/// the automatic refresh) show it again. A part of the file handler of the tab, the view outlives it.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppRestoredFileBar : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAppRestoredFileBar(PDFAppDocumentTab* tab, QObject* parent);
    virtual ~PDFAppRestoredFileBar() override;

    static QString getBarId() { return QStringLiteral("file-restored"); }

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateBar();
    void onDocumentChanged(const PDFModifiedDocument& modified);
    void onInfoBarActionTriggered(const QString& infoBarId, const QString& actionId);

    QPointer<PDFAppDocument> m_document;
    QPointer<PDFAppDocumentView> m_view;
    bool m_isClosed = false;        ///< The user has closed the bar
    QByteArray m_closedData;        ///< The bytes of the file, whose bar the user has closed (shared with the document)
};

}   // namespace pdf

#endif // PDFAPPRESTOREDFILEBAR_H
