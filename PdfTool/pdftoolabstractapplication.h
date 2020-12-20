//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFTOOLABSTRACTAPPLICATION_H
#define PDFTOOLABSTRACTAPPLICATION_H

#include "pdfoutputformatter.h"
#include "pdfdocument.h"
#include "pdfdocumenttextflow.h"
#include "pdfrenderer.h"
#include "pdfcms.h"
#include "pdfoptimizer.h"

#include <QtGlobal>
#include <QString>
#include <QDateTime>
#include <QCoreApplication>

#include <vector>

class QCommandLineParser;

namespace pdftool
{

struct PDFToolTranslationContext
{
    Q_DECLARE_TR_FUNCTIONS(PDFToolTranslationContext)
};

struct PDFToolOptions
{
    // For option 'ConsoleFormat'
    PDFOutputFormatter::Style outputStyle = PDFOutputFormatter::Style::Text;
    QString outputCodec = "UTF-8";

    // For option 'DateFormat'
    Qt::DateFormat outputDateFormat = Qt::DefaultLocaleShortDate;

    // For option 'OpenDocument'
    QString document;
    QString password;
    bool permissiveReading = true;

    // For option 'SignatureVerification'
    bool verificationUseUserCertificates = true;
    bool verificationUseSystemCertificates = true;
    bool verificationOmitCertificateCheck = false;
    bool verificationPrintCertificateDetails = false;
    bool verificationIgnoreExpirationDate = false;

    // For option 'XMLExport'
    bool xmlExportStreams = false;
    bool xmlExportStreamsAsText = false;
    bool xmlUseIndent = false;
    bool xmlAlwaysBinaryStrings = false;

    // For option 'Attachments'
    QString attachmentsSaveNumber;
    QString attachmentsSaveFileName;
    QString attachmentsOutputDirectory;
    QString attachmentsTargetFile;
    bool attachmentsSaveAll = false;

    // For option 'ComputeHashes'
    bool computeHashes = false;

    // For option 'PageSelector'
    QString pageSelectorFirstPage;
    QString pageSelectorLastPage;
    QString pageSelectorSelection;

    // For option 'TextAnalysis'
    pdf::PDFDocumentTextFlowFactory::Algorithm textAnalysisAlgorithm = pdf::PDFDocumentTextFlowFactory::Algorithm::Auto;

    // For option 'TextShow'
    bool textShowPageNumbers = false;
    bool textShowStructTitles = false;
    bool textShowStructLanguage = false;
    bool textShowStructAlternativeDescription = false;
    bool textShowStructExpandedForm = false;
    bool textShowStructActualText = false;
    bool textShowStructPhoneme = false;

    // For option 'VoiceSelector'
    QString textVoiceName;
    QString textVoiceGender;
    QString textVoiceAge;
    QString textVoiceLangCode;

    // For option 'TextSpeech'
    bool textSpeechMarkPageNumbers = false;
    bool textSpeechSayPageNumbers = false;
    bool textSpeechSayStructTitles = false;
    bool textSpeechSayStructAlternativeDescription = false;
    bool textSpeechSayStructExpandedForm = false;
    bool textSpeechSayStructActualText = false;
    QString textSpeechAudioFormat = "mp3";

    // For option 'CharacterMaps'
    bool showCharacterMapsForEmbeddedFonts = false;

    // For option 'ImageWriterSettings'
    pdf::PDFImageWriterSettings imageWriterSettings;

    // For option 'ImageExportSettings'
    pdf::PDFPageImageExportSettings imageExportSettings;

    // For option 'ColorManagementSystem'
    pdf::PDFCMSSettings cmsSettings;

    // For option 'RenderFlags'
    pdf::PDFRenderer::Features renderFeatures = pdf::PDFRenderer::getDefaultFeatures();
    bool renderUseHardwareRendering = true;
    bool renderShowPageStatistics = false;
    int renderMSAAsamples = 4;
    int renderRasterizerCount = pdf::PDFRasterizerPool::getDefaultRasterizerCount();

    // For option 'Separate'
    QString separatePagePattern;

    // For option 'Unite'
    QStringList uniteFiles;

    // For option 'Optimize'
    pdf::PDFOptimizer::OptimizationFlags optimizeFlags = pdf::PDFOptimizer::None;

    // For option 'CertStore'
    bool certStoreEnumerateSystemCertificates = false;
    bool certStoreEnumerateUserCertificates = true;

    // For option 'CertStoreInstall'
    QString certificateStoreInstallCertificateFile;

    /// Returns page range. If page range is invalid, then \p errorMessage is empty.
    /// \param pageCount Page count
    /// \param[out] errorMessage Error message
    /// \param zeroBased Convert to zero based page range?
    std::vector<pdf::PDFInteger> getPageRange(pdf::PDFInteger pageCount, QString& errorMessage, bool zeroBased) const;

    struct RenderFeatureInfo
    {
        QString option;
        QString description;
        pdf::PDFRenderer::Feature feature;
    };

    /// Returns a list of available renderer features
    static std::vector<RenderFeatureInfo> getRenderFeatures();

    struct OptimizeFeatureInfo
    {
        QString option;
        QString description;
        pdf::PDFOptimizer::OptimizationFlag flag;
    };

    /// Returns a list of available optimize features
    static std::vector<OptimizeFeatureInfo> getOptimizeFlagInfos();
};

/// Base class for all applications
class PDFToolAbstractApplication
{
public:
    explicit PDFToolAbstractApplication(bool isDefault = false);
    virtual ~PDFToolAbstractApplication() = default;

    enum ExitCodes
    {
        ExitSuccess = EXIT_SUCCESS,
        ExitFailure = EXIT_FAILURE,
        ErrorUnknown,
        ErrorNoDocumentSpecified,
        ErrorDocumentReading,
        ErrorCertificateReading,
        ErrorInvalidArguments,
        ErrorFailedWriteToFile,
        ErrorPermissions,
        ErrorNoText,
        ErrorCOM,
        ErrorSAPI
    };

    enum StandardString
    {
        Command,        ///< Command, by which is this application invoked
        Name,           ///< Name of application
        Description     ///< Description (what this application does)
    };

    enum Option
    {
        ConsoleFormat                   = 0x00000001,       ///< Set format of console output (text, xml or html)
        OpenDocument                    = 0x00000002,       ///< Flags for document reading
        SignatureVerification           = 0x00000004,       ///< Flags for signature verification,
        XmlExport                       = 0x00000008,       ///< Flags for xml export
        Attachments                     = 0x00000010,       ///< Flags for attachments manipulating
        DateFormat                      = 0x00000020,       ///< Date format
        ComputeHashes                   = 0x00000040,       ///< Compute hashes
        PageSelector                    = 0x00000080,       ///< Select page range (or all pages)
        TextAnalysis                    = 0x00000100,       ///< Text analysis options
        TextShow                        = 0x00000200,       ///< Text extract and show options
        VoiceSelector                   = 0x00000400,       ///< Select voice from SAPI
        TextSpeech                      = 0x00000800,       ///< Text speech options
        CharacterMaps                   = 0x00001000,       ///< Character maps for embedded fonts
        ImageWriterSettings             = 0x00002000,       ///< Settings for writing images (for example, format, etc.)
        ImageExportSettingsFiles        = 0x00004000,       ///< Settings for exporting page images to files
        ImageExportSettingsResolution   = 0x00008000,       ///< Settings for resolution of exported images
        ColorManagementSystem           = 0x00010000,       ///< Color management system settings
        RenderFlags                     = 0x00020000,       ///< Render flags for page image rasterizer
        Separate                        = 0x00040000,       ///< Settings for Separate tool
        Unite                           = 0x00080000,       ///< Settings for Unite tool
        Optimize                        = 0x00100000,       ///< Settings for Optimize tool
        CertStore                       = 0x00200000,       ///< Settings for certificate store tool
        CertStoreInstall                = 0x00400000,       ///< Settings for certificate store install certificate tool
    };
    Q_DECLARE_FLAGS(Options, Option)

    virtual QString getStandardString(StandardString standardString) const = 0;
    virtual int execute(const PDFToolOptions& options) = 0;
    virtual Options getOptionsFlags() const = 0;

    void initializeCommandLineParser(QCommandLineParser* parser) const;
    PDFToolOptions getOptions(QCommandLineParser* parser) const;

protected:
    /// Tries to read the document. If document is successfully read, true is returned,
    /// if error occurs, then false is returned. Optionally, original document content
    /// can also be retrieved.
    /// \param options Options
    /// \param document Document
    /// \param[out] sourceData Pointer, to which source data are stored
    bool readDocument(const PDFToolOptions& options, pdf::PDFDocument& document, QByteArray* sourceData = nullptr);
};

/// This class stores information about all applications available. Application
/// can be selected by command string. Also, default application is available.
class PDFToolApplicationStorage
{
public:

    /// Returns application by command. If application for given command is not found,
    /// then nullptr is returned.
    /// \param command Command
    /// \returns Application for given command or nullptr
    static PDFToolAbstractApplication* getApplicationByCommand(const QString& command);

    /// Registers application to the application storage. If \p isDefault
    /// is set to true, application is registered as default application.
    /// \param application Apllication
    /// \param isDefault Is default application?
    static void registerApplication(PDFToolAbstractApplication* application, bool isDefault = false);

    /// Returns default application
    /// \returns Default application
    static PDFToolAbstractApplication* getDefaultApplication();

    /// Returns a list of available applications
    static const std::vector<PDFToolAbstractApplication*>& getApplications();

private:
    PDFToolApplicationStorage() = default;
    static PDFToolApplicationStorage* getInstance();

    std::vector<PDFToolAbstractApplication*> m_applications;
    PDFToolAbstractApplication* m_defaultApplication = nullptr;
};

}   // namespace pdftool

Q_DECLARE_OPERATORS_FOR_FLAGS(pdftool::PDFToolAbstractApplication::Options)

#endif // PDFTOOLABSTRACTAPPLICATION_H
