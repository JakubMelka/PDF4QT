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

#include "pdfappprint.h"
#include "pdfapplocale.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappmainwindow.h"
#include "pdfappprintdialog.h"
#include "pdfsettingdefinitions.h"

#include "pdfannotation.h"
#include "pdfcolorspaces.h"
#include "pdfconstants.h"
#include "pdfexception.h"
#include "pdffont.h"
#include "pdfmeshqualitysettings.h"
#include "pdfmessagebox.h"
#include "pdfoptionalcontent.h"
#include "pdfpainter.h"
#include "pdfpainterutils.h"
#include "pdfsecurityhandler.h"
#include "pdftextlayout.h"

#include <QCheckBox>
#include <QLocale>
#include <QPainter>
#include <QPrinterInfo>
#include <QPushButton>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <map>

#if defined(Q_OS_WIN) && defined(PDF4QT_APP_PRINTER_PROPERTIES)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winspool.h>
#include <QtPrintSupport/private/qprintengine_win_p.h>
#endif

namespace pdf
{

/// Helpers of the printing
class PDFAppPrintHelper
{
public:
    PDFAppPrintHelper() = delete;

    /// Width of the message boxes (958:28033)
    static constexpr int MESSAGE_BOX_WIDTH = 440;

    /// Points of an inch
    static constexpr qreal POINTS_PER_INCH = 72.0;

    /// The printer is the default printer of the system (it is listed first)
    static bool isDefaultPrinter(const PDFAppPrinterInfo& info) { return info.isDefault; }

    /// Items of the job from the index to the end (the pages, which were not printed)
    static std::vector<PDFAppPrintJob::Item> getRemainingItems(const std::vector<PDFAppPrintJob::Item>& items, size_t index)
    {
        return std::vector<PDFAppPrintJob::Item>(items.begin() + std::min(index, items.size()), items.end());
    }
};

PDFAppPrinters::Provider& PDFAppPrinters::getProvider()
{
    static Provider provider;
    return provider;
}

PDFAppPrinters::PropertiesHandler& PDFAppPrinters::getPropertiesHandler()
{
    static PropertiesHandler handler;
    return handler;
}

void PDFAppPrinters::setProvider(Provider provider)
{
    getProvider() = std::move(provider);
}

void PDFAppPrinters::setPropertiesHandler(PropertiesHandler handler)
{
    getPropertiesHandler() = std::move(handler);
}

std::vector<PDFAppPrinterInfo> PDFAppPrinters::getPrinters()
{
    std::vector<PDFAppPrinterInfo> printers;
    if (const Provider& provider = getProvider())
    {
        printers = provider();
    }
    else
    {
        const QString defaultName = QPrinterInfo::defaultPrinterName();
        for (const QPrinterInfo& printerInfo : QPrinterInfo::availablePrinters())
        {
            PDFAppPrinterInfo info;
            info.name = printerInfo.printerName();
            info.location = printerInfo.location();
            info.description = printerInfo.description();
            info.isDefault = info.name == defaultName;
            info.pageSizes = printerInfo.supportedPageSizes();
            info.defaultPageSize = printerInfo.defaultPageSize();

            switch (printerInfo.state())
            {
                case QPrinter::Idle:
                    info.stateText = tr("Ready");
                    break;

                case QPrinter::Active:
                    info.stateText = tr("Printing");
                    break;

                case QPrinter::Aborted:
                    info.isReady = false;
                    info.stateText = tr("Stopped");
                    break;

                case QPrinter::Error:
                    info.isReady = false;
                    info.stateText = tr("Not responding");
                    break;
            }

            if (!info.name.isEmpty())
            {
                printers.push_back(std::move(info));
            }
        }
    }

    // The default printer first, the others in the order of the system
    std::stable_partition(printers.begin(), printers.end(), &PDFAppPrintHelper::isDefaultPrinter);
    return printers;
}

std::unique_ptr<QPrinter> PDFAppPrinters::createPrinter(const PDFAppPrinterInfo& info, QPrinter::PrinterMode mode)
{
    std::unique_ptr<QPrinter> printer;
    if (!info.outputFile.isEmpty())
    {
        printer = std::make_unique<QPrinter>(mode);
        printer->setOutputFormat(QPrinter::PdfFormat);
        printer->setOutputFileName(info.outputFile);
    }
    else
    {
        printer = std::make_unique<QPrinter>(QPrinterInfo::printerInfo(info.name), mode);
    }

    printer->setDocName(QString());
    printer->setFullPage(true);
    printer->setCopyCount(1);
    return printer;
}

bool PDFAppPrinters::hasPrinterProperties(const PDFAppPrinterInfo& info)
{
    if (getPropertiesHandler())
    {
        return true;
    }

#if defined(Q_OS_WIN) && defined(PDF4QT_APP_PRINTER_PROPERTIES)
    return info.outputFile.isEmpty() && !info.name.isEmpty();
#else
    Q_UNUSED(info);
    return false;
#endif
}

bool PDFAppPrinters::isPrinterPropertiesSupported()
{
    if (getPropertiesHandler())
    {
        return true;
    }

#if defined(Q_OS_WIN) && defined(PDF4QT_APP_PRINTER_PROPERTIES)
    return true;
#else
    return false;
#endif
}

bool PDFAppPrinters::showPrinterProperties(QWidget* parent, QPrinter* printer, const PDFAppPrinterInfo& info)
{
    if (!printer)
    {
        return false;
    }

    if (const PropertiesHandler& handler = getPropertiesHandler())
    {
        return handler(parent, printer, info);
    }

    if (!info.outputFile.isEmpty())
    {
        return false;
    }

    return showNativePrinterProperties(parent, printer, info.name);
}

bool PDFAppPrinters::showNativePrinterProperties(QWidget* parent, QPrinter* printer, const QString& printerName)
{
#if defined(Q_OS_WIN) && defined(PDF4QT_APP_PRINTER_PROPERTIES)
    if (printer->outputFormat() != QPrinter::NativeFormat || printerName.isEmpty())
    {
        return false;
    }

    QWin32PrintEngine* engine = dynamic_cast<QWin32PrintEngine*>(printer->printEngine());
    if (!engine)
    {
        return false;
    }

    std::wstring name = printerName.toStdWString();
    HANDLE printerHandle = nullptr;
    if (!::OpenPrinterW(name.data(), &printerHandle, nullptr) || !printerHandle)
    {
        return false;
    }

    const HWND window = parent ? reinterpret_cast<HWND>(parent->window()->winId()) : nullptr;
    bool isAccepted = false;

    // Size of the DEVMODE of the driver (the public part and the private data)
    const LONG size = ::DocumentPropertiesW(window, printerHandle, name.data(), nullptr, nullptr, 0);
    if (size > LONG(sizeof(DEVMODEW)) / 2)
    {
        HGLOBAL devModeHandle = ::GlobalAlloc(GHND, SIZE_T(size));
        DEVMODEW* devMode = devModeHandle ? static_cast<DEVMODEW*>(::GlobalLock(devModeHandle)) : nullptr;

        // The settings accepted before (a previous Printer properties… of this
        // printer, held by the engine) are the start: the private settings of the
        // driver (tray, quality, ...) are not reset to the defaults
        HGLOBAL previousHandle = engine->globalDevMode();
        const DEVMODEW* previous = previousHandle ? static_cast<const DEVMODEW*>(::GlobalLock(previousHandle)) : nullptr;
        if (devMode)
        {
            // The defaults of the driver (merged with the previous settings), then
            // the settings of the dialog, which the driver shows as the start
            const DWORD startMode = previous ? DWORD(DM_IN_BUFFER | DM_OUT_BUFFER) : DWORD(DM_OUT_BUFFER);
            bool isStarted = ::DocumentPropertiesW(window, printerHandle, name.data(), devMode, const_cast<DEVMODEW*>(previous), startMode) == IDOK;
            if (!isStarted && previous)
            {
                isStarted = ::DocumentPropertiesW(window, printerHandle, name.data(), devMode, nullptr, DM_OUT_BUFFER) == IDOK;
            }

            if (isStarted)
            {
                devMode->dmOrientation = printer->pageLayout().orientation() == QPageLayout::Landscape ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
                devMode->dmFields |= DM_ORIENTATION;

                const int paperId = printer->pageLayout().pageSize().windowsId();
                if (paperId > 0)
                {
                    devMode->dmPaperSize = short(paperId);
                    devMode->dmFields |= DM_PAPERSIZE;
                }

                devMode->dmColor = printer->colorMode() == QPrinter::GrayScale ? DMCOLOR_MONOCHROME : DMCOLOR_COLOR;
                devMode->dmFields |= DM_COLOR;

                isAccepted = ::DocumentPropertiesW(window, printerHandle, name.data(), devMode, devMode, DM_IN_BUFFER | DM_OUT_BUFFER | DM_IN_PROMPT) == IDOK;
            }
            ::GlobalUnlock(devModeHandle);
        }
        if (previous)
        {
            ::GlobalUnlock(previousHandle);
        }

        if (isAccepted)
        {
            // The engine takes the global DEVMODE over (as QPrintDialog of Qt does);
            // it only unlocks the previous one, which this function has given it
            engine->setGlobalDevMode(nullptr, devModeHandle);
            if (previousHandle && previousHandle != devModeHandle)
            {
                ::GlobalFree(previousHandle);
            }
        }
        else if (devModeHandle)
        {
            ::GlobalFree(devModeHandle);
        }
    }

    ::ClosePrinter(printerHandle);
    return isAccepted;
#else
    Q_UNUSED(parent);
    Q_UNUSED(printer);
    Q_UNUSED(printerName);
    return false;
#endif
}

QList<QPageSize> PDFAppPrinters::getStandardPageSizes()
{
    return { QPageSize(QPageSize::A4), QPageSize(QPageSize::Letter), QPageSize(QPageSize::Legal), QPageSize(QPageSize::A3),
             QPageSize(QPageSize::A5), QPageSize(QPageSize::B5), QPageSize(QPageSize::Tabloid), QPageSize(QPageSize::Executive) };
}

QPageSize PDFAppPrinters::getLocaleDefaultPageSize()
{
    // The paper is a regional setting of the system, not of the language of the
    // user interface (O17: an English interface in Czechia prints on A4)
    return getDefaultPageSize(PDFAppLocale::getRegionalLocale().territory());
}

QPageSize PDFAppPrinters::getDefaultPageSize(QLocale::Territory territory)
{
    if (territory == QLocale::UnitedStates || territory == QLocale::Canada || territory == QLocale::Mexico ||
        territory == QLocale::Chile || territory == QLocale::Colombia || territory == QLocale::Philippines)
    {
        return QPageSize(QPageSize::Letter);
    }
    return QPageSize(QPageSize::A4);
}

QPageLayout PDFAppPrinters::getPrintableLayout(const QPageLayout& layout)
{
    // The printer of a job prints a full page (setFullPage): the paint rectangle of
    // its layout is the whole paper, the hardware margins are its minimal margins
    const QMarginsF minimumMargins = layout.minimumMargins();
    QPageLayout printable(layout.pageSize(), layout.orientation(), minimumMargins, layout.units(), minimumMargins);
    printable.setMode(QPageLayout::StandardMode);
    return printable;
}

class PDFAppPrintRenderer::Implementation
{
public:
    explicit Implementation(PDFDocumentPointer pdfDocument, const PDFCMSSettings& cmsSettings, PDFRenderer::Features rendererFeatures) :
        document(std::move(pdfDocument)),
        fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT),
        optionalContentActivity(document.data(), OCUsage::Print, nullptr),
        cmsManager(nullptr),
        annotationManager(&fontCache, &iccProfileCache, &cmsManager, &optionalContentActivity, PDFMeshQualitySettings(), rendererFeatures, PDFAnnotationManager::Target::Print, nullptr),
        features(rendererFeatures)
    {
        // The document is not modified: the caches and the annotations only read it
        PDFModifiedDocument modifiedDocument(const_cast<PDFDocument*>(document.data()), &optionalContentActivity);
        fontCache.setDocument(modifiedDocument);
        iccProfileCache.setDocument(document.data());
        cmsManager.setDocument(document.data());
        cmsManager.setSettings(cmsSettings);
        annotationManager.setDocument(modifiedDocument);
    }

    PDFDocumentPointer document;
    PDFFontCache fontCache;
    PDFICCProfileCache iccProfileCache;
    PDFOptionalContentActivity optionalContentActivity;
    PDFCMSManager cmsManager;
    PDFAnnotationManager annotationManager;
    PDFRenderer::Features features;
};

PDFAppPrintRenderer::PDFAppPrintRenderer(PDFDocumentPointer document, const PDFCMSSettings& cmsSettings, PDFRenderer::Features features) :
    m_implementation(std::make_unique<Implementation>(std::move(document), cmsSettings, features))
{

}

PDFAppPrintRenderer::~PDFAppPrintRenderer() = default;

void PDFAppPrintRenderer::renderPage(QPainter* painter, PDFInteger pageIndex, const QRectF& targetRect, const PDFOperationControl* control)
{
    Implementation* implementation = m_implementation.get();
    const PDFDocument* document = implementation->document.data();
    const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
    const PDFPage* page = catalog && pageIndex >= 0 && pageIndex < PDFInteger(catalog->getPageCount()) ? catalog->getPage(pageIndex) : nullptr;
    if (!painter || !page || !targetRect.isValid())
    {
        return;
    }

    const PDFCMSPointer cms = implementation->cmsManager.getCurrentCMS();
    PDFRenderer renderer(document, &implementation->fontCache, &implementation->iccProfileCache, cms.data(),
                         &implementation->optionalContentActivity, implementation->features, PDFMeshQualitySettings());
    renderer.setOperationControl(control);

    PDFPrecompiledPage compiledPage;
    renderer.compile(&compiledPage, pageIndex);
    if (PDFOperationControl::isOperationCancelled(control))
    {
        return;
    }

    const QTransform matrix = PDFRenderer::createPagePointToDevicePointMatrix(page, targetRect);
    compiledPage.draw(painter, page->getCropBox(), matrix, implementation->features, 1.0);

    // Annotations marked as printed (widgets of the forms with their values)
    PDFColorConvertor convertor = cms->getColorConvertor();
    PDFRenderer::applyFeaturesToColorConvertor(implementation->features, convertor);
    PDFTextLayoutGetter textLayoutGetter(nullptr, pageIndex);
    QList<PDFRenderError> errors;
    implementation->annotationManager.drawPage(painter, pageIndex, &compiledPage, textLayoutGetter, matrix, convertor, errors);
}

QSizeF PDFAppPrintRenderer::getPageSize(const PDFPage* page)
{
    if (!page)
    {
        return QSizeF();
    }

    const QSizeF size = page->getRotatedMediaBox().size();
    if (!std::isfinite(size.width()) || !std::isfinite(size.height()) || size.width() <= 0.0 || size.height() <= 0.0)
    {
        return QSizeF();
    }
    return size;
}

QPageLayout::Orientation PDFAppPrintRenderer::getPaperOrientation(const PDFPage* page, PDFAppPrintOrientation orientation)
{
    switch (orientation)
    {
        case PDFAppPrintOrientation::Portrait:
            return QPageLayout::Portrait;

        case PDFAppPrintOrientation::Landscape:
            return QPageLayout::Landscape;

        case PDFAppPrintOrientation::Auto:
            break;
    }

    const QSizeF size = getPageSize(page);
    return size.isValid() && size.width() > size.height() ? QPageLayout::Landscape : QPageLayout::Portrait;
}

QRectF PDFAppPrintRenderer::getTargetRect(const QSizeF& pageSize, const QRectF& paperRect, const QRectF& printableRect, qreal unitsPerPoint,
                                          PDFAppPrintScaling scaling, int customScale)
{
    if (!pageSize.isValid() || pageSize.isEmpty() || !paperRect.isValid() || unitsPerPoint <= 0.0)
    {
        return QRectF();
    }

    QRectF result;
    switch (scaling)
    {
        case PDFAppPrintScaling::FitToPage:
        {
            // The printable area of the printer (the whole paper, if it has no margins)
            const QRectF area = printableRect.isValid() ? printableRect : paperRect;
            result.setSize(pageSize.scaled(area.size(), Qt::KeepAspectRatio));
            result.moveCenter(area.center());
            break;
        }

        case PDFAppPrintScaling::ActualSize:
        case PDFAppPrintScaling::Custom:
        {
            const qreal scale = scaling == PDFAppPrintScaling::ActualSize ? 1.0 : qBound(PDFAppPrintSettings::MINIMUM_SCALE, customScale, PDFAppPrintSettings::MAXIMUM_SCALE) / 100.0;
            result.setSize(pageSize * (scale * unitsPerPoint));
            result.moveCenter(paperRect.center());
            break;
        }
    }

    return result;
}

PDFRenderer::Features PDFAppPrintRenderer::getFeatures(bool isGrayscale, bool isRealText)
{
    PDFRenderer::Features features = PDFRenderer::Features(PDFRenderer::Antialiasing | PDFRenderer::TextAntialiasing | PDFRenderer::SmoothImages |
                                                           PDFRenderer::ClipToCropBox | PDFRenderer::DisplayAnnotations);
    if (isGrayscale)
    {
        features.setFlag(PDFRenderer::ColorAdjust_Grayscale);
        features.setFlag(PDFRenderer::ColorAdjust_Annotations);
    }
    if (isRealText)
    {
        features.setFlag(PDFRenderer::RealText);
    }
    return features;
}

QImage PDFAppPrintRenderer::renderPreview(PDFDocumentPointer document, PDFCMSSettings cmsSettings, PDFInteger pageIndex, PDFAppPrintSettings settings,
                                          QRectF paperRect, QRectF printableRect, QSize imageSize, bool isRealText)
{
    if (document.isNull() || imageSize.isEmpty() || !paperRect.isValid() || paperRect.width() <= 0.0)
    {
        return QImage();
    }

    try
    {
        const PDFCatalog* catalog = document->getCatalog();
        const PDFPage* page = pageIndex >= 0 && pageIndex < PDFInteger(catalog->getPageCount()) ? catalog->getPage(pageIndex) : nullptr;
        if (!page)
        {
            return QImage();
        }

        QImage image(imageSize, QImage::Format_ARGB32_Premultiplied);
        if (image.isNull())
        {
            return QImage();
        }
        image.fill(Qt::white);

        // The paper in points maps to the image
        const qreal scale = qreal(imageSize.width()) / paperRect.width();
        const QRectF imagePaperRect(QPointF(0, 0), QSizeF(imageSize));
        const QRectF imagePrintableRect(printableRect.left() * scale, printableRect.top() * scale, printableRect.width() * scale, printableRect.height() * scale);
        const QRectF targetRect = getTargetRect(getPageSize(page), imagePaperRect, imagePrintableRect, scale, settings.scaling, settings.customScale);

        PDFAppPrintRenderer renderer(document, cmsSettings, getFeatures(settings.isGrayscale, isRealText));
        QPainter painter(&image);
        painter.setClipRect(imagePaperRect);
        renderer.renderPage(&painter, pageIndex, targetRect, nullptr);
        painter.end();

        if (settings.isGrayscale)
        {
            image = image.convertToFormat(QImage::Format_Grayscale8);
        }
        return image;
    }
    catch (const PDFException&)
    {
        return QImage();
    }
    catch (const std::bad_alloc&)
    {
        return QImage();
    }
}

std::vector<PDFAppPrintJob::Item> PDFAppPrintJob::getItems(const PDFAppPrintSettings& settings)
{
    std::vector<Item> items;
    const int copies = qBound(1, settings.copies, PDFAppPrintSettings::MAXIMUM_COPIES);
    items.reserve(settings.pages.size() * size_t(copies));

    if (settings.collate)
    {
        // 1 2 3, 1 2 3
        for (int copy = 0; copy < copies; ++copy)
        {
            for (PDFInteger pageIndex : settings.pages)
            {
                items.push_back(Item{ pageIndex, copy });
            }
        }
    }
    else
    {
        // 1 1, 2 2, 3 3
        for (PDFInteger pageIndex : settings.pages)
        {
            for (int copy = 0; copy < copies; ++copy)
            {
                items.push_back(Item{ pageIndex, copy });
            }
        }
    }
    return items;
}

PDFAppPrintJob::Result PDFAppPrintJob::run(Data data)
{
    Result result;
    const std::shared_ptr<PDFAppPrintJobState> state = data.state;
    QPrinter* printer = data.printer.get();
    const PDFDocument* document = data.document.data();
    if (!state || !printer || !document)
    {
        result.errorMessage = tr("Nothing can be printed.");
        return result;
    }

    const std::vector<Item> items = getItems(data.settings);
    const PDFCatalog* catalog = document->getCatalog();
    const PDFInteger pageCount = PDFInteger(catalog->getPageCount());

    if (items.empty())
    {
        result.isCompleted = true;
        return result;
    }

    try
    {
        PDFAppPrintRenderer renderer(data.document, data.cmsSettings, PDFAppPrintRenderer::getFeatures(data.settings.isGrayscale, data.isRealText));

        printer->setFullPage(true);
        printer->setCopyCount(1);
        printer->setColorMode(data.settings.isGrayscale ? QPrinter::GrayScale : QPrinter::Color);
        if (data.settings.pageSize.isValid())
        {
            printer->setPageSize(data.settings.pageSize);
        }

        const PDFPage* firstPage = items.front().pageIndex >= 0 && items.front().pageIndex < pageCount ? catalog->getPage(items.front().pageIndex) : nullptr;
        printer->setPageOrientation(PDFAppPrintRenderer::getPaperOrientation(firstPage, data.settings.orientation));

        QPainter painter;
        if (!painter.begin(printer))
        {
            result.errorMessage = tr("The printer could not be started.");
            result.remainingItems = items;
            return result;
        }

        for (size_t i = 0; i < items.size(); ++i)
        {
            state->currentItem.store(int(i));
            if (state->isCancelRequested.load())
            {
                printer->abort();
                result.isCanceled = true;
                result.printedCount = int(i);
                return result;
            }

            const Item& item = items[i];
            const PDFPage* page = item.pageIndex >= 0 && item.pageIndex < pageCount ? catalog->getPage(item.pageIndex) : nullptr;

            if (i > 0)
            {
                // The paper of the next page (the orientation of its own in Auto)
                printer->setPageOrientation(PDFAppPrintRenderer::getPaperOrientation(page, data.settings.orientation));
                if (!printer->newPage())
                {
                    painter.end();
                    result.errorMessage = tr("The printer did not accept the next page.");
                    result.printedCount = int(i);
                    result.remainingItems = PDFAppPrintHelper::getRemainingItems(items, i);
                    return result;
                }
            }

            const qreal resolution = qMax(1, printer->resolution());
            const QPageLayout layout = printer->pageLayout();
            const QRectF paperRect = layout.fullRectPixels(int(resolution));
            const QRectF printableRect = PDFAppPrinters::getPrintableLayout(layout).paintRectPixels(int(resolution));
            const QRectF targetRect = PDFAppPrintRenderer::getTargetRect(PDFAppPrintRenderer::getPageSize(page), paperRect, printableRect,
                                                                         resolution / PDFAppPrintHelper::POINTS_PER_INCH, data.settings.scaling, data.settings.customScale);
            if (page && targetRect.isValid())
            {
                PDFPainterStateGuard guard(&painter);
                painter.setClipRect(paperRect);
                renderer.renderPage(&painter, item.pageIndex, targetRect, state.get());
            }

            if (printer->printerState() == QPrinter::Error || printer->printerState() == QPrinter::Aborted)
            {
                painter.end();
                if (state->isCancelRequested.load())
                {
                    result.isCanceled = true;
                    result.printedCount = int(i);
                    return result;
                }
                result.errorMessage = tr("The printer stopped responding.");
                result.printedCount = int(i);
                result.remainingItems = PDFAppPrintHelper::getRemainingItems(items, i);
                return result;
            }

            state->printedCount.store(int(i + 1));
        }

        if (!painter.end())
        {
            result.errorMessage = tr("The printer did not accept the end of the job.");
            result.printedCount = int(items.size());
            return result;
        }

        result.isCompleted = true;
        result.printedCount = int(items.size());
    }
    catch (const PDFException& exception)
    {
        result.errorMessage = exception.getMessage();
        result.printedCount = state->printedCount.load();
        result.remainingItems = PDFAppPrintHelper::getRemainingItems(items, size_t(qMax(0, result.printedCount)));
    }
    catch (const std::bad_alloc&)
    {
        result.errorMessage = tr("There is not enough memory to print the document.");
        result.printedCount = state->printedCount.load();
        result.remainingItems = PDFAppPrintHelper::getRemainingItems(items, size_t(qMax(0, result.printedCount)));
    }

    return result;
}

PDFAppPrintController::PDFAppPrintController(PDFAppDocumentTab* tab, QObject* parent) :
    BaseClass(parent),
    m_tab(tab),
    m_document(tab ? tab->getDocument() : nullptr)
{
    m_progressTimer = new QTimer(this);
    m_progressTimer->setInterval(PROGRESS_INTERVAL);
    connect(m_progressTimer, &QTimer::timeout, this, &PDFAppPrintController::onProgressTimeout);

    if (PDFAppDocumentView* view = tab ? tab->getView() : nullptr)
    {
        connect(view, &PDFAppDocumentView::infoBarActionTriggered, this, &PDFAppPrintController::onInfoBarActionTriggered);
    }
}

PDFAppPrintController::~PDFAppPrintController()
{
    // The job keeps its printer and its snapshot; it is stopped after its page
    if (m_state)
    {
        m_state->isCancelRequested.store(true);
    }
    if (m_watcher)
    {
        m_watcher->disconnect(this);
    }
    if (m_operation)
    {
        m_operation->finish();
    }

    for (QDialog* dialog : { static_cast<QDialog*>(m_dialog.data()), static_cast<QDialog*>(m_errorDialog.data()) })
    {
        if (dialog)
        {
            dialog->disconnect(this);
            dialog->hide();
            dialog->deleteLater();
        }
    }
}

PDFAppPrintDialog* PDFAppPrintController::getDialog() const
{
    return m_dialog.data();
}

PDFMessageBox* PDFAppPrintController::getErrorDialog() const
{
    return m_errorDialog.data();
}

bool PDFAppPrintController::isPrintAllowed(const PDFDocument* document)
{
    const PDFSecurityHandler* securityHandler = document ? document->getStorage().getSecurityHandler() : nullptr;
    // Bit 3 allows printing; bit 12 adds the high resolution only together with it
    return document && (!securityHandler || securityHandler->isAllowed(PDFSecurityHandler::Permission::PrintLowResolution));
}

bool PDFAppPrintController::isHighResolutionAllowed(const PDFDocument* document)
{
    const PDFSecurityHandler* securityHandler = document ? document->getStorage().getSecurityHandler() : nullptr;
    return isPrintAllowed(document) && (!securityHandler || securityHandler->isAllowed(PDFSecurityHandler::Permission::PrintHighResolution));
}

bool PDFAppPrintController::canPrint() const
{
    const PDFAppDocument* document = m_document.data();
    return document && document->isReady() && document->getPageCount() > 0 && isPrintAllowed(document->getDocument().data()) &&
           !isPrinting() && !m_dialog && !m_errorDialog;
}

void PDFAppPrintController::showPrintDialog()
{
    if (!canPrint())
    {
        return;
    }

    std::vector<PDFInteger> pages;
    if (PDFAppDocumentView* view = m_tab ? m_tab->getView() : nullptr)
    {
        pages = view->getSelectedPages();
    }
    openDialog(pages, 1);
}

void PDFAppPrintController::openDialog(const std::vector<PDFInteger>& initialPages, int copies)
{
    // An open editor of a form field is printed too (step 4, package D: it is
    // committed first - the dialog takes the focus, which keeps the editor open)
    QPointer<PDFAppPrintController> guard(this);
    if (m_document)
    {
        m_document->commitPendingEdits();
        if (!guard)
        {
            return;
        }
    }

    PDFAppDocument* document = m_document.data();
    PDFAppDocumentView* view = m_tab ? m_tab->getView() : nullptr;
    if (!document || !document->isReady() || !view)
    {
        return;
    }

    PDFCMSSettings cmsSettings;
    if (const PDFCMSManager* cmsManager = view->getCMSManager())
    {
        cmsSettings = cmsManager->getSettings();
    }

    PDFAppPrintDialog* dialog = new PDFAppPrintDialog(document, qMax<PDFInteger>(0, view->getDisplayedPageIndex()), initialPages, cmsSettings, m_tab->window());
    PDFAppMainWindow::prepareDialog(dialog, m_tab);
    dialog->setCopies(copies);
    connect(dialog, &QDialog::finished, this, &PDFAppPrintController::onDialogFinished);
    m_dialog = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
    Q_EMIT stateChanged();
}

void PDFAppPrintController::onDialogFinished(int result)
{
    PDFAppPrintDialog* dialog = m_dialog.data();
    if (!dialog || sender() != dialog)
    {
        return;
    }
    m_dialog = nullptr;

    std::shared_ptr<QPrinter> printer = dialog->getPrinter();
    const PDFAppPrintSettings settings = dialog->getSettings();
    const int printerIndex = dialog->getPrinterIndex();
    const QString printerName = printerIndex >= 0 && printerIndex < int(dialog->getPrinters().size()) ? dialog->getPrinters()[size_t(printerIndex)].name : QString();

    QPointer<PDFAppPrintController> guard(this);
    Q_EMIT stateChanged();
    if (!guard)
    {
        return;
    }

    if (result == QDialog::Accepted && printer && !settings.pages.empty())
    {
        startJob(std::move(printer), settings, printerName);
    }
}

void PDFAppPrintController::startJob(std::shared_ptr<QPrinter> printer, const PDFAppPrintSettings& settings, const QString& printerName)
{
    PDFAppDocument* document = m_document.data();
    PDFAppDocumentView* view = m_tab ? m_tab->getView() : nullptr;
    if (!document || !document->isReady() || isPrinting())
    {
        return;
    }

    PDFAppPrintJob::Data data;
    data.document = document->getDocument();
    data.printer = std::move(printer);
    data.settings = settings;
    if (view && view->getCMSManager())
    {
        data.cmsSettings = view->getCMSManager()->getSettings();
    }
    PDFAppContext* context = m_tab ? m_tab->getContext() : nullptr;
    data.isRealText = context && context->getDatabase() && context->getSettings().getBool(PDFSettingKeys::renderingRealText());
    data.state = std::make_shared<PDFAppPrintJobState>();
    data.printer->setDocName(document->getTitle());

    m_items = PDFAppPrintJob::getItems(settings);
    data.state->totalCount = int(m_items.size());
    m_state = data.state;
    m_printerName = printerName;
    m_lastCopies = settings.copies;
    m_jobTimer.start();

    // The document stays usable: the job prints its snapshot. The document
    // announces the operation (operationsChanged): a slot may delete the tab.
    QPointer<PDFAppPrintController> guard(this);
    PDFAppDocumentOperation* operation = document->beginOperation(PDFAppDocumentOperationKind::Print, tr("Printing on %1").arg(printerName), false);
    if (!guard)
    {
        return;
    }
    m_operation = operation;
    connect(m_operation, &PDFAppDocumentOperation::cancelRequested, this, &PDFAppPrintController::stop);

    QFutureWatcher<PDFAppPrintJob::Result>* watcher = new QFutureWatcher<PDFAppPrintJob::Result>(this);
    m_watcher = watcher;
    connect(watcher, &QFutureWatcher<PDFAppPrintJob::Result>::finished, this, &PDFAppPrintController::onJobFinished);
    watcher->setFuture(QtConcurrent::run(&PDFAppPrintJob::run, std::move(data)));

    m_progressTimer->start();
    updateInfoBar();
    if (guard)
    {
        Q_EMIT stateChanged();
    }
}

void PDFAppPrintController::stop()
{
    if (m_state)
    {
        m_state->isCancelRequested.store(true);
        updateInfoBar();
    }
}

void PDFAppPrintController::onProgressTimeout()
{
    updateInfoBar();
}

void PDFAppPrintController::updateInfoBar()
{
    PDFAppDocumentView* view = m_tab ? m_tab->getView() : nullptr;
    if (!view || !m_state)
    {
        return;
    }

    const int total = qMax(1, m_state->totalCount);
    const int printed = qBound(0, m_state->printedCount.load(), total);
    const int current = qBound(0, m_state->currentItem.load(), total - 1);
    const int progress = printed * 100 / total;

    // The document announces the progress of the operation (a slot may delete the tab)
    if (m_operation)
    {
        QPointer<PDFAppPrintController> guard(this);
        m_operation->setProgress(progress);
        if (!guard || !m_state)
        {
            return;
        }
    }

    QString remaining;
    if (m_state->isCancelRequested.load())
    {
        remaining = tr("stopping after this page");
    }
    else if (printed >= 2 && m_jobTimer.isValid())
    {
        const qint64 remainingMs = m_jobTimer.elapsed() * (total - printed) / printed;
        const qint64 minutes = (remainingMs + 30000) / 60000;
        remaining = minutes < 1 ? tr("less than a minute left") : (minutes == 1 ? tr("about a minute left") : tr("about %Ln minutes left", nullptr, int(qMin<qint64>(minutes, 100000))));
    }
    else
    {
        remaining = tr("estimating the time");
    }

    QString pageText;
    if (size_t(current) < m_items.size())
    {
        const PDFAppPrintJob::Item& item = m_items[size_t(current)];
        pageText = tr("Page %1 of copy %2").arg(view->getPageLabel(item.pageIndex)).arg(item.copy + 1);
    }

    PDFAppInfoBarRequest request;
    request.id = getInfoBarId();
    request.tone = PDFAppInfoBarTone::Progress;
    request.iconName = QStringLiteral("printer");
    request.title = tr("Printing on %1").arg(m_printerName);
    request.text = tr("%1 · %2 of %3 pages · %4").arg(pageText).arg(printed).arg(total).arg(remaining);
    request.actions = { PDFAppInfoBarAction{ QStringLiteral("stop"), tr("Stop"), QStringLiteral("stop") } };
    request.isClosable = false;
    request.progress = progress;
    view->showInfoBar(request);
}

void PDFAppPrintController::hideInfoBar()
{
    if (PDFAppDocumentView* view = m_tab ? m_tab->getView() : nullptr)
    {
        view->hideInfoBar(getInfoBarId());
    }
}

void PDFAppPrintController::onInfoBarActionTriggered(const QString& infoBarId, const QString& actionId)
{
    if (infoBarId == getInfoBarId() && actionId == QStringLiteral("stop"))
    {
        stop();
    }
}

void PDFAppPrintController::onJobFinished()
{
    QFutureWatcher<PDFAppPrintJob::Result>* watcher = m_watcher.data();
    if (!watcher || sender() != watcher)
    {
        return;
    }

    m_lastResult = watcher->future().result();
    m_watcher = nullptr;
    watcher->deleteLater();
    m_progressTimer->stop();
    m_state.reset();
    hideInfoBar();

    // The document announces the finished operation (a slot may delete the tab)
    QPointer<PDFAppPrintController> guard(this);
    if (PDFAppDocumentOperation* operation = m_operation.data())
    {
        m_operation = nullptr;
        operation->finish();
        if (!guard)
        {
            return;
        }
    }

    PDFAppDocumentView* view = m_tab ? m_tab->getView() : nullptr;
    if (view && view->getStatusBar())
    {
        if (m_lastResult.isCompleted)
        {
            view->getStatusBar()->showMessage((m_lastResult.printedCount == 1 ? tr("1 page sent to %1") : tr("%Ln pages sent to %1", nullptr, m_lastResult.printedCount)).arg(m_printerName), 5000);
        }
        else if (m_lastResult.isCanceled)
        {
            view->getStatusBar()->showMessage(tr("Printing stopped"), 5000);
        }
    }

    Q_EMIT stateChanged();
    if (!guard)
    {
        return;
    }

    if (!m_lastResult.isCompleted && !m_lastResult.isCanceled)
    {
        showErrorDialog(m_lastResult);
        if (!guard)
        {
            return;
        }
    }
    Q_EMIT printFinished();
}

void PDFAppPrintController::showErrorDialog(const PDFAppPrintJob::Result& result)
{
    if (m_errorDialog)
    {
        return;
    }

    const int total = int(m_items.size());
    const QString text = result.remainingItems.empty() ? tr("%1 stopped at the end of the job. The pages may not have been printed.").arg(m_printerName)
                                                       : tr("%1 stopped after %2 of %3 pages. The rest was not sent to the printer.").arg(m_printerName).arg(result.printedCount).arg(total);
    PDFMessageBox* dialog = new PDFMessageBox(PDFThemeIcons::Badge::Critical, tr("Printing stopped"), text, QDialogButtonBox::NoButton, m_tab ? m_tab->window() : nullptr);
    dialog->setObjectName(QStringLiteral("printErrorDialog"));
    dialog->setCardWidth(PDFAppPrintHelper::MESSAGE_BOX_WIDTH);
    dialog->setDetailedText(result.errorMessage);
    PDFAppMainWindow::prepareDialog(dialog, m_tab);

    QPushButton* closeButton = dialog->addActionButton(tr("Close"), PDFMessageBox::ActionPosition::Trailing, result.remainingItems.empty());
    closeButton->setObjectName(QStringLiteral("closeButton"));
    connect(closeButton, &QPushButton::clicked, dialog, &QDialog::reject);
    if (!result.remainingItems.empty())
    {
        QPushButton* restButton = dialog->addActionButton(tr("Print the Rest…"), PDFMessageBox::ActionPosition::Trailing, true);
        restButton->setObjectName(QStringLiteral("printRestButton"));
        connect(restButton, &QPushButton::clicked, dialog, &QDialog::accept);
    }
    connect(dialog, &QDialog::finished, this, &PDFAppPrintController::onErrorDialogFinished);

    m_errorDialog = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
    Q_EMIT stateChanged();
}

void PDFAppPrintController::onErrorDialogFinished(int result)
{
    if (sender() != m_errorDialog.data())
    {
        return;
    }
    m_errorDialog = nullptr;

    QPointer<PDFAppPrintController> guard(this);
    Q_EMIT stateChanged();
    if (!guard || result != QDialog::Accepted || m_lastResult.remainingItems.empty())
    {
        return;
    }

    // The pages, which were not printed, with the copies they still need
    std::map<PDFInteger, int> copiesOfPages;
    for (const PDFAppPrintJob::Item& item : m_lastResult.remainingItems)
    {
        ++copiesOfPages[item.pageIndex];
    }

    std::vector<PDFInteger> pages;
    int copies = 1;
    for (const auto& [pageIndex, count] : copiesOfPages)
    {
        pages.push_back(pageIndex);
        copies = qMax(copies, count);
    }

    openDialog(pages, copies);
    if (PDFAppPrintDialog* dialog = m_dialog.data())
    {
        // The mode Range with the rest (physical page numbers from 1)
        QStringList parts;
        size_t i = 0;
        while (i < pages.size())
        {
            size_t j = i;
            while (j + 1 < pages.size() && pages[j + 1] == pages[j] + 1)
            {
                ++j;
            }
            parts << (i == j ? QString::number(pages[i] + 1) : QStringLiteral("%1-%2").arg(pages[i] + 1).arg(pages[j] + 1));
            i = j + 1;
        }
        dialog->setRange(parts.join(QStringLiteral(", ")));
    }
}

}   // namespace pdf
