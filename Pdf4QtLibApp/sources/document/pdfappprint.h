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

#ifndef PDFAPPPRINT_H
#define PDFAPPPRINT_H

#include "pdfappglobal.h"

#include "pdfcms.h"
#include "pdfdocument.h"
#include "pdfoperationcontrol.h"
#include "pdfrenderer.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QImage>
#include <QLocale>
#include <QObject>
#include <QPageLayout>
#include <QPageSize>
#include <QPointer>
#include <QPrinter>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

class QPainter;
class QTimer;

namespace pdf
{

class PDFAppDocument;
class PDFAppDocumentOperation;
class PDFAppDocumentTab;
class PDFAppPrintDialog;
class PDFMessageBox;
class PDFPage;

/// A printer, which the print dialog offers
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPrinterInfo
{
public:
    QString name;                       ///< Name of the printer (QPrinterInfo::printerName)
    QString location;                   ///< Location ("Office, 2nd floor"), may be empty
    QString description;                ///< Description of the system, may be empty
    bool isDefault = false;             ///< Default printer of the system
    bool isReady = true;                ///< The printer is idle or printing (not stopped / in error)
    QString stateText;                  ///< State for the dialog ("Ready", "Printing", ...)
    QList<QPageSize> pageSizes;         ///< Supported paper sizes (empty = the standard sizes)
    QPageSize defaultPageSize;          ///< Default paper size (invalid = the size of the locale)
    QString outputFile;                 ///< Tests: the printer writes a PDF file instead of printing

    /// The port of the printer asks for a file (Windows PORTPROMPT: - Microsoft Print
    /// to PDF, Microsoft XPS Document Writer): the driver would show its own save
    /// dialog from the worker of the job, so the file is chosen before the job
    bool isPortPrompt = false;
    QString outputSuffix;               ///< Suffix of the file of a port prompt ("pdf", "oxps", "prn")
};

/// Printers of the system for the print dialog. The tests replace them by a
/// provider (printers writing PDF files). Printer properties of the driver are
/// available on Windows (DocumentPropertiesW on the DEVMODE of the QPrinter).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPrinters
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppPrinters)

public:
    PDFAppPrinters() = delete;

    using Provider = std::function<std::vector<PDFAppPrinterInfo>()>;
    using PropertiesHandler = std::function<bool(QWidget*, QPrinter*, const PDFAppPrinterInfo&)>;

    /// Printers of the system (or of the provider); the default printer first
    static std::vector<PDFAppPrinterInfo> getPrinters();

    /// Replaces the printers of the system (tests); an empty provider restores them
    static void setProvider(Provider provider);

    /// Replaces the native dialog of the printer properties (tests)
    static void setPropertiesHandler(PropertiesHandler handler);

    /// Creates the printer of the info (a PDF writer for an info with an output file)
    static std::unique_ptr<QPrinter> createPrinter(const PDFAppPrinterInfo& info, QPrinter::PrinterMode mode);

    /// The native settings of the driver can be shown for the printer (Windows,
    /// a printer of the system or a test handler)
    static bool hasPrinterProperties(const PDFAppPrinterInfo& info);

    /// The platform has the native settings of a driver (Windows, or a test handler):
    /// the print dialog keeps Printer properties… in its place, disabled for a printer
    /// without them and without a printer (stable button order, note 1237:166095);
    /// elsewhere the button is hidden
    static bool isPrinterPropertiesSupported();

    /// Shows the native settings of the driver of the printer (DocumentPropertiesW)
    /// and applies them to \p printer. \returns true, when the user has accepted them.
    static bool showPrinterProperties(QWidget* parent, QPrinter* printer, const PDFAppPrinterInfo& info);

    /// Standard paper sizes (a printer without its own list)
    static QList<QPageSize> getStandardPageSizes();

    /// Default paper size of the region of the system (PDFAppLocale::getRegionalLocale,
    /// not the language of the user interface - O17)
    static QPageSize getLocaleDefaultPageSize();

    /// Default paper size of a territory (Letter in the United States, Canada and some
    /// countries of America, A4 elsewhere)
    static QPageSize getDefaultPageSize(QLocale::Territory territory);

    /// Layout of the printable area of the paper of \p layout: the paper without
    /// the minimal margins of the printer. The printer prints a full page (its
    /// paint rectangle is the whole paper), Fit to page fills this area.
    static QPageLayout getPrintableLayout(const QPageLayout& layout);

private:
    static Provider& getProvider();
    static PropertiesHandler& getPropertiesHandler();
    static bool showNativePrinterProperties(QWidget* parent, QPrinter* printer, const QString& printerName);

    /// Windows: the port of the printer is PORTPROMPT: (GetPrinter level 2; QPrinterInfo
    /// does not tell it), \p suffix gets the suffix of the file of its driver
    static bool isPortPromptPrinter(const QString& printerName, QString* suffix);
};

/// Orientation of the paper
enum class PDFAppPrintOrientation
{
    Auto,       ///< Each page on the paper of its own orientation
    Portrait,
    Landscape
};

/// Scaling of the pages onto the paper
enum class PDFAppPrintScaling
{
    FitToPage,  ///< The page fills the printable area
    ActualSize, ///< 1 point = 1/72 inch, centered on the paper
    Custom      ///< The percentage of the actual size
};

/// Settings of a print job (the print dialog fills them)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPrintSettings
{
public:
    static constexpr int MAXIMUM_COPIES = 999;
    static constexpr int MINIMUM_SCALE = 10;
    static constexpr int MAXIMUM_SCALE = 400;

    std::vector<PDFInteger> pages;      ///< Page indices (from 0) in the order of printing
    int copies = 1;
    bool collate = true;                ///< Copies one after another (else the copies of each page together)
    PDFAppPrintOrientation orientation = PDFAppPrintOrientation::Auto;
    PDFAppPrintScaling scaling = PDFAppPrintScaling::FitToPage;
    int customScale = 100;              ///< Percent of the actual size (Custom)
    bool isGrayscale = false;
    QPageSize pageSize;                 ///< Paper
};

/// Geometry and rendering of a page onto a paper, shared by the print job and the
/// preview of the dialog (the same picture). The renderer has its own caches and
/// its own color management (settings copied from the tab), it is created and used
/// in one worker thread.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPrintRenderer
{
public:
    explicit PDFAppPrintRenderer(PDFDocumentPointer document, const PDFCMSSettings& cmsSettings, PDFRenderer::Features features);
    ~PDFAppPrintRenderer();

    PDFAppPrintRenderer(const PDFAppPrintRenderer&) = delete;
    PDFAppPrintRenderer& operator=(const PDFAppPrintRenderer&) = delete;

    /// Draws the page into the rectangle of the painter (device coordinates)
    void renderPage(QPainter* painter, PDFInteger pageIndex, const QRectF& targetRect, const PDFOperationControl* control);

    /// Size of the page in points as it is printed (the rotated media box)
    static QSizeF getPageSize(const PDFPage* page);

    /// Orientation of the paper of the page
    static QPageLayout::Orientation getPaperOrientation(const PDFPage* page, PDFAppPrintOrientation orientation);

    /// Rectangle of the page on the paper: \p paperRect and \p printableRect are in
    /// the units of the device, \p unitsPerPoint converts a point of the page to them
    static QRectF getTargetRect(const QSizeF& pageSize, const QRectF& paperRect, const QRectF& printableRect, qreal unitsPerPoint,
                                PDFAppPrintScaling scaling, int customScale);

    /// Features of the rendering of a print (annotations, real text by the setting, grayscale)
    static PDFRenderer::Features getFeatures(bool isGrayscale, bool isRealText);

    /// Renders the preview of the page on its paper (worker): the paper of \p paperSize
    /// (points, oriented) with the margins of \p printableRect (points) in \p imageSize
    static QImage renderPreview(PDFDocumentPointer document, PDFCMSSettings cmsSettings, PDFInteger pageIndex, PDFAppPrintSettings settings,
                                QRectF paperRect, QRectF printableRect, QSize imageSize, bool isRealText);

private:
    class Implementation;
    std::unique_ptr<Implementation> m_implementation;
};

/// State of a running print job shared by the job and the GUI (atomic values)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPrintJobState : public PDFOperationControl
{
public:
    virtual bool isOperationCancelled() const override { return isCancelRequested.load(); }

    std::atomic<bool> isCancelRequested = false;
    std::atomic<int> printedCount = 0;      ///< Printed pages (page x copy)
    std::atomic<int> currentItem = 0;       ///< Index of the page being printed (into the items of the job)
    int totalCount = 0;                     ///< All pages of the job (page x copy)
};

/// Print job: draws the pages onto the QPrinter in a worker thread (the snapshot
/// of the document), the copies are printed by the job (collated or not), the
/// paper of each page follows the orientation; a printer, which refuses a page,
/// stops the job and the rest is reported.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPrintJob
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppPrintJob)

public:
    PDFAppPrintJob() = delete;

    /// One printed page
    class Item
    {
    public:
        PDFInteger pageIndex = 0;   ///< Page (from 0)
        int copy = 0;               ///< Copy (from 0)
    };

    /// Input of the job
    class Data
    {
    public:
        PDFDocumentPointer document;
        std::shared_ptr<QPrinter> printer;
        PDFAppPrintSettings settings;
        PDFCMSSettings cmsSettings;
        bool isRealText = false;
        std::shared_ptr<PDFAppPrintJobState> state;
    };

    /// Result of the job
    class Result
    {
    public:
        bool isCompleted = false;           ///< Every page has been sent to the printer
        bool isCanceled = false;            ///< Stopped by the user
        QString errorMessage;               ///< Error of the printer (empty for a completed / canceled job)
        int printedCount = 0;               ///< Printed pages (page x copy)
        std::vector<Item> remainingItems;   ///< Pages, which have not been printed (an error)
    };

    /// Items of the settings in the order of printing
    static std::vector<Item> getItems(const PDFAppPrintSettings& settings);

    /// Runs the job (worker thread)
    static Result run(Data data);
};

/// Printing of a document tab (package E, O14 / D1): the own print dialog of the
/// design on all platforms (PDFAppPrintDialog, 1029:24203, no printer 1029:24881),
/// the job printing a snapshot in a worker thread with the notification bar over
/// the canvas (1030:25227: Stop) and the progress on the tab (operation Print),
/// the document stays usable; the error dialog (1030:25672) offers Print the Rest…,
/// which opens the dialog again with the pages, which were not printed.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPrintController : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAppPrintController(PDFAppDocumentTab* tab, QObject* parent);
    virtual ~PDFAppPrintController() override;

    /// Id of the notification bar of the printing
    static QString getInfoBarId() { return QStringLiteral("printing"); }

    /// Interval of the update of the progress
    static constexpr int PROGRESS_INTERVAL = 250;

    /// The document may be printed: shown, the permission of the document, no
    /// printing and no dialog of the printing
    bool canPrint() const;

    /// The document allows printing (low or high resolution)
    static bool isPrintAllowed(const PDFDocument* document);

    /// The document allows printing in the high resolution
    static bool isHighResolutionAllowed(const PDFDocument* document);

    /// Opens the print dialog (pages: the selected thumbnails, else all pages)
    void showPrintDialog();

    /// A job runs
    bool isPrinting() const { return !m_watcher.isNull(); }

    /// Stops the running job (after the page being printed)
    void stop();

    /// The print dialog / the error dialog, nullptr if none is shown
    PDFAppPrintDialog* getDialog() const;
    PDFMessageBox* getErrorDialog() const;

    /// Result of the last job
    const PDFAppPrintJob::Result& getLastResult() const { return m_lastResult; }

Q_SIGNALS:
    /// The state of the command may have changed
    void stateChanged();

    /// A job has ended
    void printFinished();

private:
    void openDialog(const std::vector<PDFInteger>& initialPages, int copies);
    void onDialogFinished(int result);
    /// The file of a printer with a port prompt, asked in the thread of the user
    /// interface before the job (an empty text = canceled by the user)
    QString askPrintFile(const PDFAppPrinterInfo& info);
    void startJob(std::shared_ptr<QPrinter> printer, const PDFAppPrintSettings& settings, const QString& printerName);
    void onJobFinished();
    void onProgressTimeout();
    void onInfoBarActionTriggered(const QString& infoBarId, const QString& actionId);
    void showErrorDialog(const PDFAppPrintJob::Result& result);
    void onErrorDialogFinished(int result);
    void updateInfoBar();
    void hideInfoBar();

    QPointer<PDFAppDocumentTab> m_tab;
    QPointer<PDFAppDocument> m_document;
    QPointer<PDFAppPrintDialog> m_dialog;
    QPointer<PDFMessageBox> m_errorDialog;
    QPointer<QFutureWatcher<PDFAppPrintJob::Result>> m_watcher;
    QPointer<QTimer> m_progressTimer;
    QPointer<PDFAppDocumentOperation> m_operation;
    std::shared_ptr<PDFAppPrintJobState> m_state;
    std::vector<PDFAppPrintJob::Item> m_items;
    QString m_printerName;
    QElapsedTimer m_jobTimer;
    PDFAppPrintJob::Result m_lastResult;
    int m_lastCopies = 1;
};

}   // namespace pdf

#endif // PDFAPPPRINT_H
