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

#ifndef PDFAPPEDITCONTENTPREVIEW_H
#define PDFAPPEDITCONTENTPREVIEW_H

#include "pdfappglobal.h"
#include "pdfdeferreddeletion.h"
#include "pdfglobal.h"
#include "pdfpage.h"

#include <QColor>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QTransform>

#include <memory>

namespace pdf
{

class PDFAppEditContentContext;
class PDFAppEditContentPreviewJob;
class PDFAppEditContentPreviewLink;

// Edit Content, stage E1, package P11 (canvas). Plan: zadani_gui.md 4.15 (Preview), zadani.md
// KE.11, zadani_overeni.md 6.3. The entry point of the renderer and its thread rules:
// pe1_skeleton_notes.md section 8.1.

/// What one picture of the preview is rendered from
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentPreviewRequest
{
public:
    PDFInteger pageIndex = -1;          ///< The page
    QSize imageSize;                    ///< Pixels of the picture (the visible paper at the resolution of the screen)
    QRectF pageRectangle;               ///< The part of the default space of the page the picture covers
    bool isAnnotationsShown = true;     ///< The option PREVIEW_ANNOTATIONS
    bool isOverprintSimulated = true;   ///< The option PREVIEW_OVERPRINT
    QColor paperColor = Qt::white;      ///< The option PREVIEW_PAPER_COLOR

    bool operator==(const PDFAppEditContentPreviewRequest&) const = default;
};

/// The exact rendering of the edited page for the switch Preview: the snapshot of the session
/// is written into a document of one page (PDFContentDocumentWriter::writePagePreview) and
/// rendered by PDFTransparencyRenderer, both in a worker.
///
/// - render starts a job (PDFExecutionPolicy::startTask, one task) and cancels the job in
///   progress; the job gets the snapshot (shared, immutable), the document pointer, the color
///   management settings and the request - plain values and shared pointers of immutable data,
///   nothing of the session or of the GUI. Its result returns by a queued call
///   (PDFThreadAnnotations::TransportScope) with the number of its request; a result of an older
///   request, of another page or after retire is dropped.
/// - A change of the zoom renders again 150 ms after the last change (the caller: the view).
/// - The object renders only while PDFAppEditContentContext::isPreviewActive; switching the
///   preview off cancels the job and drops the picture.
///
/// Threads and ownership: a child of the context (it lives as long as the tool; the canvas
/// reads its picture and may be gone). GUI thread; the job runs in a worker and never touches
/// this object. The hook retire() cancels the job (its control is an atomic flag) and waits for
/// nothing; a late result finds the object retired or gone and is dropped. Signals are emitted
/// last.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentPreview : public QObject, public PDFRetirable
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// The delay of a new rendering after the last change of its input (ms)
    static constexpr int RENDER_DELAY = 150;

    explicit PDFAppEditContentPreview(PDFAppEditContentContext* context, QObject* parent);
    virtual ~PDFAppEditContentPreview() override;

    /// Renders the request (see the class); the same request as the one of the picture shown or
    /// of the job in progress starts nothing
    void render(const PDFAppEditContentPreviewRequest& request);

    /// Cancels the job in progress and drops the picture
    void clear();

    /// A job is in progress
    bool isRendering() const;

    /// The last finished picture and the request it was rendered for (a null picture = none)
    const QImage& getImage() const { return m_image; }
    const PDFAppEditContentPreviewRequest& getImageRequest() const { return m_imageRequest; }

    /// The translated error of the last job (the page could not be written or rendered); empty = none
    const QString& getErrorText() const { return m_errorText; }

    // ---- P11 (the canvas; no other package calls these) ----

    /// The matrix from the page space to the pixels of the picture of the request (the page shown
    /// with its /Rotate; the rectangle of the request fills the picture)
    static QTransform getPageToImageMatrix(const PDFAppEditContentPreviewRequest& request, PageRotation rotation);

    /// The most pixels of a picture (larger requests are scaled down by the caller)
    static constexpr qint64 MAXIMUM_PIXELS = 16 * 1024 * 1024;

Q_SIGNALS:
    /// A picture is ready, or the picture was dropped (getImage)
    void imageChanged();

    /// The last job failed (getErrorText)
    void failed();

protected:
    virtual void retire() override;

private:
    friend class PDFAppEditContentPreviewLink;

    void onRenderFinished(const std::shared_ptr<PDFAppEditContentPreviewJob>& job);
    void onPreviewActiveChanged(bool isActive);
    void cancelJob();

    QPointer<PDFAppEditContentContext> m_context;
    QImage m_image;
    PDFAppEditContentPreviewRequest m_imageRequest;
    QString m_errorText;
    std::shared_ptr<PDFAppEditContentPreviewJob> m_job;
    std::shared_ptr<PDFAppEditContentPreviewLink> m_link;
    PDFAppEditContentPreviewRequest m_jobRequest;
};

}   // namespace pdf

#endif // PDFAPPEDITCONTENTPREVIEW_H
