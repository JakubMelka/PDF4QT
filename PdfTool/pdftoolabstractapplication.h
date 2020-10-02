//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFTOOLABSTRACTAPPLICATION_H
#define PDFTOOLABSTRACTAPPLICATION_H

#include "pdfoutputformatter.h"
#include "pdfdocument.h"

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
    QString outputCodec;

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
    Qt::DateFormat verificationDateFormat = Qt::DefaultLocaleShortDate;

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
        ErrorNoDocumentSpecified,
        ErrorDocumentReading,
        ErrorInvalidArguments,
        ErrorFailedWriteToFile
    };

    enum StandardString
    {
        Command,        ///< Command, by which is this application invoked
        Name,           ///< Name of application
        Description     ///< Description (what this application does)
    };

    enum Option
    {
        ConsoleFormat           = 0x0001,       ///< Set format of console output (text, xml or html)
        OpenDocument            = 0x0002,       ///< Flags for document reading
        SignatureVerification   = 0x0004,       ///< Flags for signature verification,
        XmlExport               = 0x0008,       ///< Flags for xml export
        Attachments             = 0x0010,       ///< Flags for attachments manipulating
    };
    Q_DECLARE_FLAGS(Options, Option)

    virtual QString getStandardString(StandardString standardString) const = 0;
    virtual int execute(const PDFToolOptions& options) = 0;
    virtual Options getOptionsFlags() const = 0;

    void initializeCommandLineParser(QCommandLineParser* parser) const;
    PDFToolOptions getOptions(QCommandLineParser* parser) const;

    bool readDocument(const PDFToolOptions& options, pdf::PDFDocument& document);
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

#endif // PDFTOOLABSTRACTAPPLICATION_H
