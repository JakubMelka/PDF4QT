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

#include "pdftoolabstractapplication.h"
#include "pdfdocumentreader.h"

#include <QCommandLineParser>

namespace pdftool
{

class PDFToolHelpApplication : public PDFToolAbstractApplication
{
public:
    PDFToolHelpApplication() : PDFToolAbstractApplication(true) { }

    virtual QString getStandardString(StandardString standardString) const override;
    virtual int execute(const PDFToolOptions& options) override;
    virtual Options getOptionsFlags() const override;
};

static PDFToolHelpApplication s_helpApplication;

QString PDFToolHelpApplication::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "help";

        case Name:
            return PDFToolTranslationContext::tr("Help");

        case Description:
            return PDFToolTranslationContext::tr("Show list of all available commands.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolHelpApplication::execute(const PDFToolOptions& options)
{
    PDFOutputFormatter formatter(options.outputStyle, options.outputCodec);
    formatter.beginDocument("help", PDFToolTranslationContext::tr("PDFTool help"));
    formatter.endl();

    formatter.beginTable("commands", PDFToolTranslationContext::tr("List of available commands"));

    // Table header
    formatter.beginTableHeaderRow("header");
    formatter.writeTableHeaderColumn("command", PDFToolTranslationContext::tr("Command"));
    formatter.writeTableHeaderColumn("tool", PDFToolTranslationContext::tr("Tool"));
    formatter.writeTableHeaderColumn("description", PDFToolTranslationContext::tr("Description"));
    formatter.endTableHeaderRow();

    struct Info
    {
        bool operator<(const Info& other) const
        {
            return command < other.command;
        }

        QString command;
        QString name;
        QString description;
    };

    std::vector<Info> infos;
    for (PDFToolAbstractApplication* application : PDFToolApplicationStorage::getApplications())
    {
        Info info;

        info.command = application->getStandardString(Command);
        info.name = application->getStandardString(Name);
        info.description = application->getStandardString(Description);

        infos.emplace_back(qMove(info));
    }
    qSort(infos);

    for (const Info& info : infos)
    {
        formatter.beginTableRow("command");
        formatter.writeTableColumn("command", info.command);
        formatter.writeTableColumn("name", info.name);
        formatter.writeTableColumn("description", info.description);
        formatter.endTableRow();
    }

    formatter.endTable();

    formatter.endl();
    formatter.beginHeader("text-output", PDFToolTranslationContext::tr("Text Encoding"));

    formatter.writeText("header", PDFToolTranslationContext::tr("When you redirect console to a file, then specific codec is used to transform output text to target encoding. UTF-8 encoding is used by default. For XML output, you should use only UTF-8 codec. Available codecs:"));
    formatter.endl();

    QList<QByteArray> codecs = QTextCodec::availableCodecs();
    QStringList codecNames;
    for (const QByteArray& codecName : codecs)
    {
        codecNames << QString::fromLatin1(codecName);
    }
    formatter.writeText("codecs", codecNames.join(", "));
    formatter.endl();
    formatter.writeText("default-codec", PDFToolTranslationContext::tr("Suggested codec: UTF-8 or %1").arg(QString::fromLatin1(QTextCodec::codecForLocale()->name())));

    formatter.endHeader();

    formatter.endDocument();

    PDFConsole::writeText(formatter.getString(), options.outputCodec);
    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolHelpApplication::getOptionsFlags() const
{
    return ConsoleFormat;
}

PDFToolAbstractApplication::PDFToolAbstractApplication(bool isDefault)
{
    PDFToolApplicationStorage::registerApplication(this, isDefault);
}

void PDFToolAbstractApplication::initializeCommandLineParser(QCommandLineParser* parser) const
{
    Options optionFlags = getOptionsFlags();

    if (optionFlags.testFlag(ConsoleFormat))
    {
        parser->addOption(QCommandLineOption("console-format", "Console output text format (valid values: text|xml|html).", "console-format", "text"));
        parser->addOption(QCommandLineOption("text-codec", QString("Text codec used when writing text output to redirected standard output. UTF-8 is default."), "text-codec", "UTF-8"));
    }

    if (optionFlags.testFlag(OpenDocument))
    {
        parser->addOption(QCommandLineOption("pswd", "Password for encrypted document.", "password"));
        parser->addPositionalArgument("document", "Processed document.");
        parser->addOption(QCommandLineOption("no-permissive-reading", "Do not attempt to fix damaged documents."));
    }

    if (optionFlags.testFlag(SignatureVerification))
    {
        parser->addOption(QCommandLineOption("ver-no-user-cert", "Disable user certificate store."));
        parser->addOption(QCommandLineOption("ver-no-sys-cert", "Disable system certificate store."));
        parser->addOption(QCommandLineOption("ver-no-cert-check", "Disable certificate validation."));
        parser->addOption(QCommandLineOption("ver-details", "Print details (including certificate chain, if found)."));
        parser->addOption(QCommandLineOption("ver-ignore-exp-date", "Ignore certificate expiration date."));
        parser->addOption(QCommandLineOption("ver-date-format", "Console output date/time format (valid values: short|long|iso|rfc2822).", "ver-date-format", "short"));
    }

    if (optionFlags.testFlag(XmlExport))
    {
        parser->addOption(QCommandLineOption("xml-export-streams", "Export streams as hexadecimally encoded data. By default, stream data are not exported."));
        parser->addOption(QCommandLineOption("xml-export-streams-as-text", "Export streams as text, if possible."));
        parser->addOption(QCommandLineOption("xml-use-indent", "Use automatic indent when writing output xml file."));
        parser->addOption(QCommandLineOption("xml-always-binary", "Do not try to attempt transform strings to text."));
    }
}

PDFToolOptions PDFToolAbstractApplication::getOptions(QCommandLineParser* parser) const
{
    PDFToolOptions options;

    QStringList positionalArguments = parser->positionalArguments();

    Options optionFlags = getOptionsFlags();
    if (optionFlags.testFlag(ConsoleFormat))
    {
        QString consoleFormat = parser->value("console-format");
        if (consoleFormat == "text")
        {
            options.outputStyle = PDFOutputFormatter::Style::Text;
        }
        else if (consoleFormat == "xml")
        {
            options.outputStyle = PDFOutputFormatter::Style::Xml;
        }
        else if (consoleFormat == "html")
        {
            options.outputStyle = PDFOutputFormatter::Style::Html;
        }
        else
        {
            if (!consoleFormat.isEmpty())
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Unknown console format '%1'. Defaulting to text console format.").arg(consoleFormat), options.outputCodec);
            }

            options.outputStyle = PDFOutputFormatter::Style::Text;
        }

        options.outputCodec = parser->value("text-codec");
    }

    if (optionFlags.testFlag(OpenDocument))
    {
        options.document = positionalArguments.isEmpty() ? QString() : positionalArguments.front();
        options.password = parser->isSet("pswd") ? parser->value("password") : QString();
        options.permissiveReading = !parser->isSet("no-permissive-reading");
    }

    if (optionFlags.testFlag(SignatureVerification))
    {
        options.verificationUseUserCertificates = !parser->isSet("ver-no-user-cert");
        options.verificationUseSystemCertificates = !parser->isSet("ver-no-sys-cert");
        options.verificationOmitCertificateCheck = parser->isSet("ver-no-cert-check");
        options.verificationPrintCertificateDetails = parser->isSet("ver-details");
        options.verificationIgnoreExpirationDate = parser->isSet("ver-ignore-exp-date");

        QString dateFormat = parser->value("ver-date-format");
        if (dateFormat == "short")
        {
            options.verificationDateFormat = Qt::DefaultLocaleShortDate;
        }
        else if (dateFormat == "long")
        {
            options.verificationDateFormat = Qt::DefaultLocaleLongDate;
        }
        else if (dateFormat == "iso")
        {
            options.verificationDateFormat = Qt::ISODate;
        }
        else if (dateFormat == "rfc2822")
        {
            options.verificationDateFormat = Qt::RFC2822Date;
        }
        else if (!dateFormat.isEmpty())
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Unknown console date/time format '%1'. Defaulting to short date/time format.").arg(dateFormat), options.outputCodec);
        }
    }

    if (optionFlags.testFlag(XmlExport))
    {
        options.xmlExportStreams = parser->isSet("xml-export-streams");
        options.xmlExportStreamsAsText = parser->isSet("xml-export-streams-as-text");
        options.xmlUseIndent = parser->isSet("xml-use-indent");
        options.xmlAlwaysBinaryStrings = parser->isSet("xml-always-binary");
    }

    return options;
}

bool PDFToolAbstractApplication::readDocument(const PDFToolOptions& options, pdf::PDFDocument& document)
{
    bool isFirstPasswordAttempt = true;
    auto passwordCallback = [&options, &isFirstPasswordAttempt](bool* ok) -> QString
    {
        *ok = isFirstPasswordAttempt;
        isFirstPasswordAttempt = false;
        return options.password;
    };
    pdf::PDFDocumentReader reader(nullptr, passwordCallback, options.permissiveReading);
    document = reader.readFromFile(options.document);

    switch (reader.getReadingResult())
    {
        case pdf::PDFDocumentReader::Result::OK:
            break;

        case pdf::PDFDocumentReader::Result::Cancelled:
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Invalid password provided."), options.outputCodec);
            return false;
        }

        case pdf::PDFDocumentReader::Result::Failed:
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Error occured during document reading. %1").arg(reader.getErrorMessage()), options.outputCodec);
            return false;
        }

        default:
        {
            Q_ASSERT(false);
            return false;
        }
    }

    for (const QString& warning : reader.getWarnings())
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Warning: %1").arg(warning), options.outputCodec);
    }

    return true;
}

PDFToolAbstractApplication* PDFToolApplicationStorage::getApplicationByCommand(const QString& command)
{
    for (PDFToolAbstractApplication* application : getInstance()->m_applications)
    {
        if (application->getStandardString(PDFToolAbstractApplication::Command) == command)
        {
            return application;
        }
    }

    return nullptr;
}

void PDFToolApplicationStorage::registerApplication(PDFToolAbstractApplication* application, bool isDefault)
{
    PDFToolApplicationStorage* storage = getInstance();
    storage->m_applications.push_back(application);

    if (isDefault)
    {
        storage->m_defaultApplication = application;
    }
}

PDFToolAbstractApplication* PDFToolApplicationStorage::getDefaultApplication()
{
    return getInstance()->m_defaultApplication;
}

const std::vector<PDFToolAbstractApplication*>& PDFToolApplicationStorage::getApplications()
{
    return getInstance()->m_applications;
}

PDFToolApplicationStorage* PDFToolApplicationStorage::getInstance()
{
    static PDFToolApplicationStorage storage;
    return &storage;
}

}   // pdftool
