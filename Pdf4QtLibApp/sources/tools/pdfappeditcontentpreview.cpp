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

#include "pdfappeditcontentpreview.h"

#include "pdfappeditcontentcontext.h"
#include "pdfappeditcontentitems.h"
#include "pdfapptoolhost.h"
#include "pdfcatalog.h"
#include "pdfcms.h"
#include "pdfcolorspaces.h"
#include "pdfconstants.h"
#include "pdfcontentdocumentwriter.h"
#include "pdfdocumenttoolgui.h"
#include "pdfdocumenttoolrunner.h"
#include "pdfexception.h"
#include "pdfexecutionpolicy.h"
#include "pdffont.h"
#include "pdfoperationcontrol.h"
#include "pdfoptionalcontent.h"
#include "pdfoutputpreviewanalyzer.h"
#include "pdfrenderer.h"
#include "pdfthreadannotations.h"
#include "pdftransparencyrenderer.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QThreadPool>
#include <QtGlobal>

#include <atomic>

namespace pdf
{

// Stage E1, package P11: Preview (zadani_gui.md 4.15, KE.11; the entry point and the thread rules of
// pe1_skeleton_notes.md 8.1). One job renders one picture: the page of the snapshot is written into a document of the
// preview (PDFContentDocumentWriter::writePagePreview), then PDFTransparencyRenderer draws it - both in a worker, which
// holds the document, the snapshot and the settings (plain values); the picture comes back by a queued call.

/// The way of a finished picture back to the preview: cut by its destructor and its retire
class PDFAppEditContentPreviewLink
{
public:
    explicit PDFAppEditContentPreviewLink(PDFAppEditContentPreview* preview) :
        m_preview(preview)
    {

    }

    void detach()
    {
        QMutexLocker locker(&m_mutex);
        m_preview = nullptr;
    }

    void finished(const std::shared_ptr<PDFAppEditContentPreviewJob>& job);

private:
    QMutex m_mutex;
    PDFAppEditContentPreview* m_preview;
};

/// One rendering of the preview: plain values written before the start and read after the end (the hand-offs are
/// annotated), except of the cancellation
class PDFAppEditContentPreviewJob : public PDFOperationControl
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppEditContentPreview)

public:
    std::shared_ptr<PDFAppEditContentPreviewLink> link;
    PDFDocumentPointer document;                ///< The document of the session (held)
    PDFContentEditSnapshotPointer snapshot;     ///< The work of the session (immutable)
    std::optional<PDFCMSSettings> cmsSettings;  ///< The color management of the application; none = the defaults of the document
    PDFAppEditContentPreviewRequest request;
    QImage image;                               ///< The result
    QString errorText;                          ///< The error

    virtual bool isOperationCanceled() const override
    {
        return m_isCanceled.load(std::memory_order_relaxed);
    }

    void cancel()
    {
        m_isCanceled.store(true, std::memory_order_relaxed);
    }

    /// Runs in a worker (the sequence of pe1_skeleton_notes.md 8.1)
    void run()
    {
        PDFContentWriteResult writeResult;
        const PDFDocumentPointer previewDocument = PDFContentDocumentWriter::writePagePreview(document.data(), *snapshot, request.pageIndex, this, &writeResult);
        if (isOperationCanceled())
        {
            return;
        }
        if (!previewDocument)
        {
            errorText = tr("The page cannot be written for the preview.");
            return;
        }

        const PDFDocument* pageDocument = previewDocument.data();
        const PDFCatalog* catalog = pageDocument->getCatalog();
        const PDFPage* page = request.pageIndex >= 0 && request.pageIndex < PDFInteger(catalog->getPageCount()) ? catalog->getPage(request.pageIndex) : nullptr;
        if (!page || request.imageSize.isEmpty())
        {
            errorText = tr("The page cannot be rendered.");
            return;
        }

        // The objects of the renderer are created and destroyed in this thread (QObjects of no other thread)
        PDFOptionalContentActivity activity(pageDocument, OCUsage::View, nullptr);
        PDFCMSManager cmsManager(nullptr);
        cmsManager.setDocument(pageDocument);
        cmsManager.setSettings(cmsSettings ? cmsSettings->withoutProofing() : cmsManager.getDefaultSettings().withoutProofing());

        const PDFModifiedDocument modifiedDocument(const_cast<PDFDocument*>(pageDocument), &activity);
        PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
        fontCache.setDocument(modifiedDocument);
        PDFFontCacheShrinkGuard shrinkGuard(&fontCache, this);
        PDFICCProfileCache iccProfileCache;
        iccProfileCache.setDocument(modifiedDocument);
        PDFInkMapper inkMapper(&cmsManager, pageDocument);

        // Transparency and overprint (KE.11); the overprint simulation is switched off by the option of the card
        PDFTransparencyRendererSettings settings;
        settings.flags.setFlag(PDFTransparencyRendererSettings::DisableOverprintSimulation, !request.isOverprintSimulated);
        const PDFCMSPointer cms = cmsManager.getCurrentCMS();
        const QTransform pageToImage = PDFAppEditContentPreview::getPageToImageMatrix(request, page->getPageRotation());

        try
        {
            PDFTransparencyRenderer renderer(page, pageDocument, &fontCache, &iccProfileCache, cms.data(), &activity, &inkMapper, settings, pageToImage);
            renderer.setOperationControl(this);
            renderer.beginPaint(request.imageSize);
            renderer.processContents();
            if (request.isAnnotationsShown && !isOperationCanceled())
            {
                renderer.processPrintedAnnotations();
            }
            renderer.endPaint();
            if (isOperationCanceled())
            {
                return;
            }

            const QColor paper = request.paperColor.isValid() ? request.paperColor : QColor(Qt::white);
            image = renderer.toImage(false, true, PDFRGB{ PDFColorComponent(paper.redF()), PDFColorComponent(paper.greenF()), PDFColorComponent(paper.blueF()) });
        }
        catch (const PDFException& exception)
        {
            errorText = exception.getMessage();
        }
        catch (const PDFRendererException& exception)
        {
            errorText = exception.getError().message;
        }
    }

private:
    std::atomic_bool m_isCanceled = false;
};

void PDFAppEditContentPreviewLink::finished(const std::shared_ptr<PDFAppEditContentPreviewJob>& job)
{
    QMutexLocker locker(&m_mutex);
    if (m_preview)
    {
        PDFThreadAnnotations::happensBefore(job.get());
        PDFThreadAnnotations::TransportScope transportScope;
        QMetaObject::invokeMethod(m_preview, &PDFAppEditContentPreview::onRenderFinished, Qt::QueuedConnection, job);
    }
}

/// The task of the pool: it holds the job, never the preview
class PDFAppEditContentPreviewTask
{
public:
    explicit PDFAppEditContentPreviewTask(std::shared_ptr<PDFAppEditContentPreviewJob> job) :
        m_job(std::move(job))
    {

    }

    void operator()()
    {
        m_job->run();
        const std::shared_ptr<PDFAppEditContentPreviewLink> link = m_job->link;
        link->finished(m_job);
    }

private:
    std::shared_ptr<PDFAppEditContentPreviewJob> m_job;
};

// ---------------------------------------------------------------- PDFAppEditContentPreview

PDFAppEditContentPreview::PDFAppEditContentPreview(PDFAppEditContentContext* context, QObject* parent) :
    BaseClass(parent),
    PDFRetirable(this),
    m_context(context),
    m_link(std::make_shared<PDFAppEditContentPreviewLink>(this))
{
    if (context)
    {
        PDFDeferredDeletion::connect(context, &PDFAppEditContentContext::previewActiveChanged, this, &PDFAppEditContentPreview::onPreviewActiveChanged);
    }
}

PDFAppEditContentPreview::~PDFAppEditContentPreview()
{
    // A late picture finds nobody; the job ends by itself (it holds its data)
    m_link->detach();
    cancelJob();
}

QTransform PDFAppEditContentPreview::getPageToImageMatrix(const PDFAppEditContentPreviewRequest& request, PageRotation rotation)
{
    // The rectangle of the request (the page space) fills the picture, turned by /Rotate (pe1_skeleton_notes.md 8.1 (3))
    if (request.pageRectangle.isEmpty() || request.imageSize.isEmpty())
    {
        return QTransform();
    }
    const QRectF rotated = PDFPage::getRotatedBox(request.pageRectangle, rotation);
    return PDFRenderer::createMediaBoxToDevicePointMatrix(rotated, QRectF(QPointF(0.0, 0.0), QSizeF(request.imageSize)), rotation);
}

void PDFAppEditContentPreview::render(const PDFAppEditContentPreviewRequest& request)
{
    PDFAppEditContentContext* context = m_context.data();
    if (!context || PDFDeferredDeletion::isRetired(this) || !context->isPreviewActive())
    {
        return;
    }

    // The same picture or the same job in progress: nothing
    if ((!m_image.isNull() && request == m_imageRequest) || (m_job && request == m_jobRequest))
    {
        return;
    }

    const qint64 pixels = qint64(request.imageSize.width()) * qint64(request.imageSize.height());
    if (request.pageIndex < 0 || request.pageRectangle.isEmpty() || request.imageSize.isEmpty() || pixels > MAXIMUM_PIXELS)
    {
        return;
    }

    cancelJob();

    // The worker holds the document: the shared pointer of the GUI of the tool (none in a test without a GUI)
    PDFDocumentPointer document = PDFAppEditContentDisplayAccess::getDocument(context);
    if (!document)
    {
        m_errorText = tr("The preview is not available without the document of the tool.");
        Q_EMIT failed();
        return;
    }

    auto job = std::make_shared<PDFAppEditContentPreviewJob>();
    job->link = m_link;
    job->document = std::move(document);
    job->snapshot = context->getSession()->createSnapshot();
    job->request = request;

    // The color management of the application, as Output Preview takes it (a copy, without the proof)
    const PDFDocumentToolGUI* gui = context->getGUI();
    if (PDFAppToolHost* host = gui ? gui->getHost() : nullptr)
    {
        job->cmsSettings = PDFOutputPreviewSettings::create(job->document.data(), host->createEnvironment().rendering).cmsSettings.withoutProofing();
    }

    m_job = job;
    m_jobRequest = request;
    PDFExecutionPolicy::startTask(QThreadPool::globalInstance(), PDFAppEditContentPreviewTask(std::move(job)));
}

void PDFAppEditContentPreview::clear()
{
    cancelJob();
    const bool hadImage = !m_image.isNull();
    m_image = QImage();
    m_imageRequest = PDFAppEditContentPreviewRequest();
    m_errorText.clear();
    if (hadImage && !PDFDeferredDeletion::isRetired(this))
    {
        Q_EMIT imageChanged();
    }
}

bool PDFAppEditContentPreview::isRendering() const
{
    return m_job != nullptr;
}

void PDFAppEditContentPreview::cancelJob()
{
    if (m_job)
    {
        m_job->cancel();
        m_job.reset();
    }
    m_jobRequest = PDFAppEditContentPreviewRequest();
}

void PDFAppEditContentPreview::onRenderFinished(const std::shared_ptr<PDFAppEditContentPreviewJob>& job)
{
    PDFThreadAnnotations::happensAfter(job.get());
    PDFThreadAnnotations::forget(job.get());

    // A job of an older request (or canceled) is dropped
    if (job != m_job || job->isOperationCanceled() || PDFDeferredDeletion::isRetired(this))
    {
        return;
    }
    m_job.reset();
    m_jobRequest = PDFAppEditContentPreviewRequest();

    if (job->image.isNull())
    {
        m_errorText = job->errorText.isEmpty() ? tr("The page cannot be rendered.") : job->errorText;
        Q_EMIT failed();
        return;
    }

    m_image = std::move(job->image);
    m_imageRequest = job->request;
    m_errorText.clear();
    Q_EMIT imageChanged();
}

void PDFAppEditContentPreview::onPreviewActiveChanged(bool isActive)
{
    // Switched off: the job ends and the picture goes (the canvas asks for a new one when it is switched on)
    if (!isActive)
    {
        clear();
    }
}

void PDFAppEditContentPreview::retire()
{
    // No late picture is taken; emits nothing
    m_link->detach();
    cancelJob();
}

}   // namespace pdf

#include "moc_pdfappeditcontentpreview.cpp"
