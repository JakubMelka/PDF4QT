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

#ifndef PDFFULLSCREENWIDGET_H
#define PDFFULLSCREENWIDGET_H

#include "pdfviewerglobal.h"
#include "pdfdocument.h"
#include "pdfsignaturehandler.h"
#include "pdfrenderer.h"

#include <QDialog>
#include <vector>

class QVBoxLayout;

namespace pdf
{
class PDFWidget;
class PDFCMSManager;
}

namespace pdfviewer
{

class PDF4QTLIBGUILIBSHARED_EXPORT PDFFullscreenWidget : public QDialog
{
    Q_OBJECT

public:
    explicit PDFFullscreenWidget(const pdf::PDFCMSManager* cmsManager, pdf::RendererEngine engine, QWidget* parent = nullptr);

    pdf::PDFWidget* getPdfWidget() const { return m_pdfWidget; }
    void setDocument(const pdf::PDFModifiedDocument& document, std::vector<pdf::PDFSignatureVerificationResult> signatureVerificationResult);

signals:
    void exitRequested();

protected:
    virtual void closeEvent(QCloseEvent* event) override;

private:
    QVBoxLayout* m_layout;
    pdf::PDFWidget* m_pdfWidget;
};

}   // namespace pdfviewer

#endif // PDFFULLSCREENWIDGET_H
