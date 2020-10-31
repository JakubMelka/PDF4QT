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
#include "pdfutils.h"

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
        parser->addOption(QCommandLineOption("console-format", "Console output text format (valid values: text|xml|html).", "format", "text"));
        parser->addOption(QCommandLineOption("text-codec", QString("Text codec used when writing text output to redirected standard output. UTF-8 is default."), "text codec", "UTF-8"));
    }

    if (optionFlags.testFlag(DateFormat))
    {
        parser->addOption(QCommandLineOption("date-format", "Console output date/time format (valid values: short|long|iso|rfc2822).", "date format", "short"));
    }

    if (optionFlags.testFlag(OpenDocument))
    {
        parser->addOption(QCommandLineOption("pswd", "Password for encrypted document.", "password"));
        parser->addPositionalArgument("document", "Processed document.");
        parser->addOption(QCommandLineOption("no-permissive-reading", "Do not attempt to fix damaged documents."));
    }

    if (optionFlags.testFlag(Separate))
    {
        parser->addPositionalArgument("pattern", "Page pattern, must contain '%' character if multiple pages are selected.");
    }

    if (optionFlags.testFlag(SignatureVerification))
    {
        parser->addOption(QCommandLineOption("ver-no-user-cert", "Disable user certificate store."));
        parser->addOption(QCommandLineOption("ver-no-sys-cert", "Disable system certificate store."));
        parser->addOption(QCommandLineOption("ver-no-cert-check", "Disable certificate validation."));
        parser->addOption(QCommandLineOption("ver-details", "Print details (including certificate chain, if found)."));
        parser->addOption(QCommandLineOption("ver-ignore-exp-date", "Ignore certificate expiration date."));
    }

    if (optionFlags.testFlag(XmlExport))
    {
        parser->addOption(QCommandLineOption("xml-export-streams", "Export streams as hexadecimally encoded data. By default, stream data are not exported."));
        parser->addOption(QCommandLineOption("xml-export-streams-as-text", "Export streams as text, if possible."));
        parser->addOption(QCommandLineOption("xml-use-indent", "Use automatic indent when writing output xml file."));
        parser->addOption(QCommandLineOption("xml-always-binary", "Do not try to attempt transform strings to text."));
    }

    if (optionFlags.testFlag(Attachments))
    {
        parser->addOption(QCommandLineOption("att-save-n", "Save the specified file attached in document. File name is, by default, same as attachment, it can be changed by a switch.", "number", QString()));
        parser->addOption(QCommandLineOption("att-save-file", "Save the specified file attached in document. File name is, by default, same as attachment, it can be changed by a switch.", "file", QString()));
        parser->addOption(QCommandLineOption("att-save-all", "Save all attachments to target directory."));
        parser->addOption(QCommandLineOption("att-target-dir", "Target directory to which is attachment saved.", "directory", QString()));
        parser->addOption(QCommandLineOption("att-target-file", "File, to which is attachment saved.", "target", QString()));
    }

    if (optionFlags.testFlag(ComputeHashes))
    {
        parser->addOption(QCommandLineOption("compute-hashes", "Compute hashes (MD5, SHA1, SHA256...) of document."));
    }

    if (optionFlags.testFlag(PageSelector))
    {
        parser->addOption(QCommandLineOption("page-first", "First page of page range.", "number"));
        parser->addOption(QCommandLineOption("page-last", "Last page of page range.", "number"));
        parser->addOption(QCommandLineOption("page-select", "Choose arbitrary pages, in form '1,5,3,7-11,-29,43-.'.", "number"));
    }

    if (optionFlags.testFlag(TextAnalysis))
    {
        parser->addOption(QCommandLineOption("text-analysis-alg", "Text analysis algorithm (auto - select automatically, layout - perform automatic layout algorithm, content - simple content stream reading order, structure - use tagged document structure).", "algorithm", "auto"));
    }

    if (optionFlags.testFlag(TextShow))
    {
        parser->addOption(QCommandLineOption("text-show-page-numbers", "Show page numbers in extracted text."));
        parser->addOption(QCommandLineOption("text-show-struct-title", "Show title extracted from structure tree."));
        parser->addOption(QCommandLineOption("text-show-struct-lang", "Show language extracted from structure tree."));
        parser->addOption(QCommandLineOption("text-show-struct-alt-desc", "Show alternative description extracted from structure tree."));
        parser->addOption(QCommandLineOption("text-show-struct-expanded-form", "Show expanded form extracted from structure tree."));
        parser->addOption(QCommandLineOption("text-show-struct-act-text", "Show actual text extracted from structure tree."));
        parser->addOption(QCommandLineOption("text-show-phoneme", "Show phoneme extracted from structure tree."));
    }

    if (optionFlags.testFlag(VoiceSelector))
    {
        parser->addOption(QCommandLineOption("voice-name", "Choose voice name for text-to-speech engine.", "name"));
        parser->addOption(QCommandLineOption("voice-gender", "Choose voice gender for text-to-speech engine.", "gender"));
        parser->addOption(QCommandLineOption("voice-age", "Choose voice age for text-to-speech engine.", "age"));
        parser->addOption(QCommandLineOption("voice-lang-code", "Choose voice language code for text-to-speech engine.", "code"));
    }

    if (optionFlags.testFlag(TextSpeech))
    {
        parser->addOption(QCommandLineOption("audio-format", "Audio fromat, valid values are wav/mp3.", "audio format", "mp3"));
        parser->addOption(QCommandLineOption("mark-page-numbers", "Mark page numbers in audio stream."));
        parser->addOption(QCommandLineOption("say-page-numbers", "Say page numbers."));
        parser->addOption(QCommandLineOption("say-struct-titles", "Say titles extracted from structure tree (only for tagged pdf)."));
        parser->addOption(QCommandLineOption("say-struct-alt-desc", "Say alternative descriptions extracted from structure tree (only for tagged pdf)."));
        parser->addOption(QCommandLineOption("say-struct-exp-form", "Say expanded form extracted from structure tree (only for tagged pdf)."));
        parser->addOption(QCommandLineOption("say-struct-act-text", "Say actual text extracted from structure tree (only for tagged pdf)."));
    }

    if (optionFlags.testFlag(CharacterMaps))
    {
        parser->addOption(QCommandLineOption("character-maps", "Show character maps for embedded fonts."));
    }

    if (optionFlags.testFlag(ImageWriterSettings))
    {
        parser->addOption(QCommandLineOption("image-format", "Image format. Common formats as png, jpeg, are supported.", "format", "png"));
        parser->addOption(QCommandLineOption("image-subtype", "Image format subtype. Some image formats can have this setting.", "subtype"));
        parser->addOption(QCommandLineOption("image-compress-lvl", "Image compression level. Different formats can have different meaning.", "level", "9"));
        parser->addOption(QCommandLineOption("image-quality", "Image quality. Different formats can have different meaning.", "quality", "100"));
        parser->addOption(QCommandLineOption("image-optimized-write", "Use optimized write mode."));
        parser->addOption(QCommandLineOption("image-progressive-scan-write", "Use image progressive scan mode."));
    }

    if (optionFlags.testFlag(ImageExportSettingsFiles))
    {
        parser->addOption(QCommandLineOption("image-output-dir", "Output directory, where images are saved.", "dir"));
        parser->addOption(QCommandLineOption("image-template-fn", "Template file name, must contain '%' character, must not contain suffix.", "template file name", "Image_%"));
    }

    if (optionFlags.testFlag(ImageExportSettingsResolution))
    {
        parser->addOption(QCommandLineOption("image-res-mode", "Image resolution mode (valid values are dpi|pixel). Dpi is default.", "mode", "dpi"));
        parser->addOption(QCommandLineOption("image-res-dpi", "DPI resolution of target image.", "dpi"));
        parser->addOption(QCommandLineOption("image-res-pixel", "Pixel resolution of target image.", "pixel"));
    }

    if (optionFlags.testFlag(ColorManagementSystem))
    {
        parser->addOption(QCommandLineOption("cms", "Color management system. Valid values are generic|lcms.", "cms", "lcms"));
        parser->addOption(QCommandLineOption("cms-accuracy", "Accuracy of cms system. Valid values are low|medium|high. Higher accuracy means higher memory consumption.", "accuracy", "medium"));
        parser->addOption(QCommandLineOption("cms-intent", "Rendering intent. Valid values are auto|perceptual|abs|rel|saturation.", "intent", "auto"));
        parser->addOption(QCommandLineOption("cms-black-compensated", "Black point compensation.", "bool", "1"));
        parser->addOption(QCommandLineOption("cms-white-paper-trans", "Transform also color of paper using cms.", "bool", "0"));
        parser->addOption(QCommandLineOption("cms-profile-output", "Output color profile.", "profile"));
        parser->addOption(QCommandLineOption("cms-profile-gray", "Gray color profile for gray device.", "profile"));
        parser->addOption(QCommandLineOption("cms-profile-rgb", "RGB color profile for RGB device.", "profile"));
        parser->addOption(QCommandLineOption("cms-profile-cmyk", "CMYK color profile for CMYK device.", "profile"));
        parser->addOption(QCommandLineOption("cms-profile-dir", "External directory containing color profiles.", "directory"));
    }

    if (optionFlags.testFlag(RenderFlags))
    {
        const pdf::PDFRenderer::Features defaultFeatures = pdf::PDFRenderer::getDefaultFeatures();
        for (const PDFToolOptions::RenderFeatureInfo& info : PDFToolOptions::getRenderFeatures())
        {
            parser->addOption(QCommandLineOption(info.option, info.description, "bool", defaultFeatures.testFlag(info.feature) ? "1" : "0"));
        }

        parser->addOption(QCommandLineOption("render-hw-accel", "Use hardware acceleration (using GPU).", "bool", "1"));
        parser->addOption(QCommandLineOption("render-show-page-stat", "Show page rendering statistics."));
        parser->addOption(QCommandLineOption("render-msaa-samples", "MSAA sample count for GPU rendering.", "samples", "4"));
        parser->addOption(QCommandLineOption("render-rasterizers", "Number of rasterizer contexts.", "rasterizers", QString::number(pdf::PDFRasterizerPool::getDefaultRasterizerCount())));
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

    if (optionFlags.testFlag(DateFormat))
    {
        QString dateFormat = parser->value("date-format");
        if (dateFormat == "short")
        {
            options.outputDateFormat = Qt::DefaultLocaleShortDate;
        }
        else if (dateFormat == "long")
        {
            options.outputDateFormat = Qt::DefaultLocaleLongDate;
        }
        else if (dateFormat == "iso")
        {
            options.outputDateFormat = Qt::ISODate;
        }
        else if (dateFormat == "rfc2822")
        {
            options.outputDateFormat = Qt::RFC2822Date;
        }
        else if (!dateFormat.isEmpty())
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Unknown console date/time format '%1'. Defaulting to short date/time format.").arg(dateFormat), options.outputCodec);
        }
    }

    if (optionFlags.testFlag(OpenDocument))
    {
        options.document = positionalArguments.isEmpty() ? QString() : positionalArguments.front();
        options.password = parser->isSet("pswd") ? parser->value("pswd") : QString();
        options.permissiveReading = !parser->isSet("no-permissive-reading");
    }

    if (optionFlags.testFlag(Separate))
    {
        options.separatePagePattern = positionalArguments.size() >= 2 ? positionalArguments[1] : QString();
    }

    if (optionFlags.testFlag(SignatureVerification))
    {
        options.verificationUseUserCertificates = !parser->isSet("ver-no-user-cert");
        options.verificationUseSystemCertificates = !parser->isSet("ver-no-sys-cert");
        options.verificationOmitCertificateCheck = parser->isSet("ver-no-cert-check");
        options.verificationPrintCertificateDetails = parser->isSet("ver-details");
        options.verificationIgnoreExpirationDate = parser->isSet("ver-ignore-exp-date");
    }

    if (optionFlags.testFlag(XmlExport))
    {
        options.xmlExportStreams = parser->isSet("xml-export-streams");
        options.xmlExportStreamsAsText = parser->isSet("xml-export-streams-as-text");
        options.xmlUseIndent = parser->isSet("xml-use-indent");
        options.xmlAlwaysBinaryStrings = parser->isSet("xml-always-binary");
    }

    if (optionFlags.testFlag(Attachments))
    {
        options.attachmentsSaveNumber = parser->isSet("att-save-n") ? parser->value("att-save-n") : QString();
        options.attachmentsSaveFileName = parser->isSet("att-save-file") ? parser->value("att-save-file") : QString();
        options.attachmentsSaveAll = parser->isSet("att-save-all");
        options.attachmentsOutputDirectory = parser->isSet("att-target-dir") ? parser->value("att-target-dir") : QString();
        options.attachmentsTargetFile = parser->isSet("att-target-file") ? parser->value("att-target-file") : QString();
    }

    if (optionFlags.testFlag(ComputeHashes))
    {
        options.computeHashes = parser->isSet("compute-hashes");
    }

    if (optionFlags.testFlag(PageSelector))
    {
        options.pageSelectorFirstPage = parser->isSet("page-first") ? parser->value("page-first") : QString();
        options.pageSelectorLastPage = parser->isSet("page-last") ? parser->value("page-last") : QString();
        options.pageSelectorSelection = parser->isSet("page-select") ? parser->value("page-select") : QString();
    }

    if (optionFlags.testFlag(TextAnalysis))
    {
        QString algoritm = parser->value("text-analysis-alg");
        if (algoritm == "auto")
        {
            options.textAnalysisAlgorithm = pdf::PDFDocumentTextFlowFactory::Algorithm::Auto;
        }
        else if (algoritm == "layout")
        {
            options.textAnalysisAlgorithm = pdf::PDFDocumentTextFlowFactory::Algorithm::Layout;
        }
        else if (algoritm == "content")
        {
            options.textAnalysisAlgorithm = pdf::PDFDocumentTextFlowFactory::Algorithm::Content;
        }
        else if (algoritm == "structure")
        {
            options.textAnalysisAlgorithm = pdf::PDFDocumentTextFlowFactory::Algorithm::Structure;
        }
        else if (!algoritm.isEmpty())
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Unknown text layout analysis algorithm '%1'. Defaulting to automatic algorithm selection.").arg(algoritm), options.outputCodec);
        }
    }

    if (optionFlags.testFlag(TextShow))
    {
        options.textShowPageNumbers = parser->isSet("text-show-page-numbers");
        options.textShowStructTitles = parser->isSet("text-show-struct-title");
        options.textShowStructLanguage = parser->isSet("text-show-struct-lang");
        options.textShowStructAlternativeDescription = parser->isSet("text-show-struct-alt-desc");
        options.textShowStructExpandedForm = parser->isSet("text-show-struct-expanded-form");
        options.textShowStructActualText = parser->isSet("text-show-struct-act-text");
        options.textShowStructPhoneme = parser->isSet("text-show-phoneme");
    }

    if (optionFlags.testFlag(VoiceSelector))
    {
        options.textVoiceName = parser->isSet("voice-name") ? parser->value("voice-name") : QString();
        options.textVoiceGender = parser->isSet("voice-gender") ? parser->value("voice-gender") : QString();
        options.textVoiceAge = parser->isSet("voice-age") ? parser->value("voice-age") : QString();
        options.textVoiceLangCode = parser->isSet("voice-lang-code") ? parser->value("voice-lang-code") : QString();
    }

    if (optionFlags.testFlag(TextSpeech))
    {
        options.textSpeechAudioFormat = parser->value("audio-format");
        if (options.textSpeechAudioFormat != "wav" && options.textSpeechAudioFormat != "mp3")
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Unknown audio format '%1'. Defaulting to mp3 audio format.").arg(options.textSpeechAudioFormat), options.outputCodec);
            options.textSpeechAudioFormat = "mp3";
        }

        options.textSpeechMarkPageNumbers = parser->isSet("mark-page-numbers");
        options.textSpeechSayPageNumbers = parser->isSet("say-page-numbers");
        options.textSpeechSayStructTitles = parser->isSet("say-struct-titles");
        options.textSpeechSayStructAlternativeDescription = parser->isSet("say-struct-alt-desc");
        options.textSpeechSayStructExpandedForm = parser->isSet("say-struct-exp-form");
        options.textSpeechSayStructActualText = parser->isSet("say-struct-act-text");
    }

    if (optionFlags.testFlag(CharacterMaps))
    {
        options.showCharacterMapsForEmbeddedFonts = parser->isSet("character-maps");
    }

    if (optionFlags.testFlag(ImageWriterSettings))
    {
        // Image format
        QByteArray imageWriterFormat = parser->value("image-format").toLatin1();
        if (!options.imageWriterSettings.getFormats().contains(imageWriterFormat))
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Image format '%1' is not supported. Defaulting to png.").arg(QString::fromLatin1(imageWriterFormat)), options.outputCodec);
            imageWriterFormat = "png";
        }
        Q_ASSERT(options.imageWriterSettings.getFormats().contains(imageWriterFormat));

        options.imageWriterSettings.selectFormat(imageWriterFormat);

        // Image subtype
        if (parser->isSet("image-subtype"))
        {
            QByteArray imageWriterSubtype = parser->value("image-subtype").toLatin1();
            if (options.imageWriterSettings.getSubtypes().contains(imageWriterSubtype))
            {
                options.imageWriterSettings.setCurrentSubtype(imageWriterSubtype);
            }
            else
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Image format subtype '%1' is not supported.").arg(QString::fromLatin1(imageWriterSubtype)), options.outputCodec);
            }
        }

        // Compression level
        if (parser->isSet("image-compress-lvl"))
        {
            QString valueText = parser->value("image-compress-lvl");

            bool ok = false;
            int value = valueText.toInt(&ok);
            if (ok)
            {
                if (options.imageWriterSettings.isOptionSupported(QImageIOHandler::CompressionRatio))
                {
                    options.imageWriterSettings.setCompression(value);
                }
                else
                {
                    PDFConsole::writeError(PDFToolTranslationContext::tr("Image compression for current format is not supported."), options.outputCodec);
                }
            }
            else
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Invalid compression level '%1'.").arg(valueText), options.outputCodec);
            }
        }

        // Quality
        if (parser->isSet("image-quality"))
        {
            QString valueText = parser->value("image-quality");

            bool ok = false;
            int value = valueText.toInt(&ok);
            if (ok)
            {
                if (options.imageWriterSettings.isOptionSupported(QImageIOHandler::Quality))
                {
                    options.imageWriterSettings.setQuality(value);
                }
                else
                {
                    PDFConsole::writeError(PDFToolTranslationContext::tr("Image quality settings for current format is not supported."), options.outputCodec);
                }
            }
            else
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Invalid image quality '%1'.").arg(valueText), options.outputCodec);
            }
        }

        options.imageWriterSettings.setOptimizedWrite(false);
        options.imageWriterSettings.setProgressiveScanWrite(false);

        if (parser->isSet("image-optimized-write"))
        {
            if (options.imageWriterSettings.isOptionSupported(QImageIOHandler::OptimizedWrite))
            {
                options.imageWriterSettings.setOptimizedWrite(true);
            }
            else
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Optimized write is not supported."), options.outputCodec);
            }
        }

        if (parser->isSet("image-progressive-scan-write"))
        {
            if (options.imageWriterSettings.isOptionSupported(QImageIOHandler::ProgressiveScanWrite))
            {
                options.imageWriterSettings.setProgressiveScanWrite(true);
            }
            else
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Progressive scan write is not supported."), options.outputCodec);
            }
        }
    }

    if (optionFlags.testFlag(ImageExportSettingsFiles))
    {
        QFileInfo documentFileInfo(options.document);
        QString outputDir = documentFileInfo.path();

        if (parser->isSet("image-output-dir"))
        {
            outputDir = parser->value("image-output-dir");
        }

        options.imageExportSettings.setDirectory(outputDir);
        options.imageExportSettings.setFileTemplate(parser->value("image-template-fn"));
    }

    if (optionFlags.testFlag(ImageExportSettingsResolution))
    {
        QString resMode = parser->value("image-res-mode").toLower();
        if (resMode == "dpi")
        {
            options.imageExportSettings.setResolutionMode(pdf::PDFPageImageExportSettings::ResolutionMode::DPI);
        }
        else if (resMode == "pixel")
        {
            options.imageExportSettings.setResolutionMode(pdf::PDFPageImageExportSettings::ResolutionMode::Pixels);
        }
        else
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Invalid image resolution mode '%1'. Defaulting to dpi.").arg(resMode), options.outputCodec);
            options.imageExportSettings.setResolutionMode(pdf::PDFPageImageExportSettings::ResolutionMode::DPI);
        }

        if (parser->isSet("image-res-dpi"))
        {
            if (options.imageExportSettings.getResolutionMode() != pdf::PDFPageImageExportSettings::ResolutionMode::DPI)
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Cannot set dpi value, resolution mode must be dpi."), options.outputCodec);
            }

            bool ok = false;
            int dpi = parser->value("image-res-dpi").toInt(&ok);
            if (ok)
            {
                int boundedDpi = qBound(pdf::PDFPageImageExportSettings::getMinDPIResolution(), dpi, pdf::PDFPageImageExportSettings::getMaxDPIResolution());

                if (boundedDpi != dpi)
                {
                    PDFConsole::writeError(PDFToolTranslationContext::tr("Dpi must be in range from %1 to %2. Defaulting to %3.").arg(pdf::PDFPageImageExportSettings::getMinDPIResolution()).arg(pdf::PDFPageImageExportSettings::getMaxDPIResolution()).arg(boundedDpi), options.outputCodec);
                }

                options.imageExportSettings.setDpiResolution(boundedDpi);
            }
            else
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Invalid image dpi value '%1'.").arg(parser->value("image-res-dpi")), options.outputCodec);
            }
        }

        if (parser->isSet("image-res-pixel"))
        {
            if (options.imageExportSettings.getResolutionMode() != pdf::PDFPageImageExportSettings::ResolutionMode::Pixels)
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Cannot set pixel value, resolution mode must be pixel."), options.outputCodec);
            }

            bool ok = false;
            int pixel = parser->value("image-res-pixel").toInt(&ok);
            if (ok)
            {
                int boundedPixel = qBound(pdf::PDFPageImageExportSettings::getMinPixelResolution(), pixel, pdf::PDFPageImageExportSettings::getMaxPixelResolution());

                if (boundedPixel != pixel)
                {
                    PDFConsole::writeError(PDFToolTranslationContext::tr("Pixel value must be in range from %1 to %2. Defaulting to %3.").arg(pdf::PDFPageImageExportSettings::getMinPixelResolution()).arg(pdf::PDFPageImageExportSettings::getMaxPixelResolution()).arg(boundedPixel), options.outputCodec);
                }

                options.imageExportSettings.setPixelResolution(boundedPixel);
            }
            else
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Invalid image pixel value '%1'.").arg(parser->value("image-res-pixel")), options.outputCodec);
            }
        }
    }

    if (optionFlags.testFlag(ColorManagementSystem))
    {
        pdf::PDFCMSManager cmsManager(nullptr);
        options.cmsSettings = cmsManager.getDefaultSettings();

        QString cms = parser->value("cms");
        if (cms == "generic")
        {
            options.cmsSettings.system = pdf::PDFCMSSettings::System::Generic;
        }
        else if (cms == "lcms")
        {
            options.cmsSettings.system = pdf::PDFCMSSettings::System::LittleCMS2;
        }
        else
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Unknown color management system '%1'. Defaulting to lcms.").arg(cms), options.outputCodec);
            options.cmsSettings.system = pdf::PDFCMSSettings::System::LittleCMS2;
        }

        QString accuracy = parser->value("cms-accuracy");
        if (accuracy == "medium")
        {
            options.cmsSettings.accuracy = pdf::PDFCMSSettings::Accuracy::Medium;
        }
        else if (accuracy == "low")
        {
            options.cmsSettings.accuracy = pdf::PDFCMSSettings::Accuracy::Low;
        }
        else if (accuracy == "high")
        {
            options.cmsSettings.accuracy = pdf::PDFCMSSettings::Accuracy::High;
        }
        else
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Uknown color management system accuracy '%1'. Defaulting to medium.").arg(accuracy), options.outputCodec);
            options.cmsSettings.accuracy = pdf::PDFCMSSettings::Accuracy::Medium;
        }

        QString intent = parser->value("cms-intent");
        if (intent == "auto")
        {
            options.cmsSettings.intent = pdf::RenderingIntent::Auto;
        }
        else if (intent == "perceptual")
        {
            options.cmsSettings.intent = pdf::RenderingIntent::Perceptual;
        }
        else if (intent == "abs")
        {
            options.cmsSettings.intent = pdf::RenderingIntent::AbsoluteColorimetric;
        }
        else if (intent == "rel")
        {
            options.cmsSettings.intent = pdf::RenderingIntent::RelativeColorimetric;
        }
        else if (intent == "saturation")
        {
            options.cmsSettings.intent = pdf::RenderingIntent::Saturation;
        }
        else
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Uknown color management system rendering intent '%1'. Defaulting to auto.").arg(intent), options.outputCodec);
            options.cmsSettings.intent = pdf::RenderingIntent::Auto;
        }

        if (parser->isSet("cms-black-compensated"))
        {
            options.cmsSettings.isBlackPointCompensationActive = parser->value("cms-black-compensated").toInt();
        }

        if (parser->isSet("cms-white-paper-trans"))
        {
            options.cmsSettings.isWhitePaperColorTransformed = parser->value("cms-white-paper-trans").toInt();
        }

        auto setProfile = [&parser, &options](QString settings, QString& profile)
        {
            if (parser->isSet(settings))
            {
                profile = parser->value(settings);
            }
        };

        setProfile("cms-profile-output", options.cmsSettings.outputCS);
        setProfile("cms-profile-gray", options.cmsSettings.deviceGray);
        setProfile("cms-profile-rgb", options.cmsSettings.deviceRGB);
        setProfile("cms-profile-cmyk", options.cmsSettings.deviceCMYK);
        setProfile("cms-profile-dir", options.cmsSettings.profileDirectory);
    }

    if (optionFlags.testFlag(RenderFlags))
    {
        for (const PDFToolOptions::RenderFeatureInfo& info : PDFToolOptions::getRenderFeatures())
        {
            QString textValue = parser->value(info.option);

            bool ok = false;
            bool value = textValue.toInt(&ok);

            if (ok)
            {
                options.renderFeatures.setFlag(info.feature, value);
            }
            else
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Uknown bool value '%1'. Default value is used.").arg(textValue), options.outputCodec);
            }
        }

        QString textValue = parser->value("render-hw-accel");
        bool ok = false;
        bool value = textValue.toInt(&ok);
        if (ok)
        {
            options.renderUseHardwareRendering = value;
        }
        else
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Uknown bool value '%1'. GPU rendering is used as default.").arg(textValue), options.outputCodec);
        }

        textValue = parser->value("render-msaa-samples");
        options.renderMSAAsamples = textValue.toInt(&ok);
        if (!ok)
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Uknown MSAA sample count '%1'. 4 samples are used as default.").arg(textValue), options.outputCodec);
            options.renderMSAAsamples = 4;
        }

        textValue = parser->value("render-rasterizers");
        options.renderRasterizerCount = textValue.toInt(&ok);
        if (!ok)
        {
            options.renderRasterizerCount = pdf::PDFRasterizerPool::getDefaultRasterizerCount();
            PDFConsole::writeError(PDFToolTranslationContext::tr("Uknown rasterizer count '%1'. %2 rasterizers are used as default.").arg(textValue).arg(options.renderRasterizerCount), options.outputCodec);
        }
        int correctedRasterizerCount = pdf::PDFRasterizerPool::getCorrectedRasterizerCount(options.renderRasterizerCount);
        if (correctedRasterizerCount != options.renderRasterizerCount)
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Invalid raterizer count: %1. Correcting to use %2 rasterizers.").arg(options.renderRasterizerCount).arg(correctedRasterizerCount), options.outputCodec);
            options.renderRasterizerCount = correctedRasterizerCount;
        }

        options.renderShowPageStatistics = parser->isSet("render-show-page-stat");
    }

    return options;
}

bool PDFToolAbstractApplication::readDocument(const PDFToolOptions& options, pdf::PDFDocument& document, QByteArray* sourceData)
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
        {
            if (sourceData)
            {
                *sourceData = reader.getSource();
            }
            break;
        }

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

std::vector<pdf::PDFInteger> PDFToolOptions::getPageRange(pdf::PDFInteger pageCount, QString& errorMessage, bool zeroBased) const
{
    QStringList parts;

    const bool hasFirst = !pageSelectorFirstPage.isEmpty();
    const bool hasLast = !pageSelectorLastPage.isEmpty();
    const bool hasSelection = !pageSelectorSelection.isEmpty();

    if (hasFirst && hasLast)
    {
        parts << QString("%1-%2").arg(pageSelectorFirstPage, pageSelectorLastPage);
    }
    else if (hasFirst)
    {
        parts << QString("%1-").arg(pageSelectorFirstPage);
    }
    else if (hasLast)
    {
        parts << QString("-%1").arg(pageSelectorLastPage);
    }

    if (hasSelection)
    {
        parts << pageSelectorSelection;
    }

    if (parts.empty())
    {
        parts << "-";
    }

    QString partsString = parts.join(",");
    pdf::PDFClosedIntervalSet result = pdf::PDFClosedIntervalSet::parse(1, pageCount, partsString, &errorMessage);
    std::vector<pdf::PDFInteger> pageIndices = result.unfold();

    if (zeroBased)
    {
        std::for_each(pageIndices.begin(), pageIndices.end(), [](auto& index) { --index; });
    }

    return pageIndices;
}

std::vector<PDFToolOptions::RenderFeatureInfo> PDFToolOptions::getRenderFeatures()
{
    return {
        RenderFeatureInfo{ "render-antialiasing", "Antialiasing for lines, shapes, etc.", pdf::PDFRenderer::Antialiasing },
        RenderFeatureInfo{ "render-text-antialiasing", "Antialiasing for text outlines.", pdf::PDFRenderer::TextAntialiasing },
        RenderFeatureInfo{ "render-smooth-img", "Smooth image transformation (slower, but better quality images).", pdf::PDFRenderer::SmoothImages },
        RenderFeatureInfo{ "render-ignore-opt-content", "Ignore optional content settings (draw everything).", pdf::PDFRenderer::IgnoreOptionalContent },
        RenderFeatureInfo{ "render-clip-to-crop-box", "Clip page graphics to crop box.", pdf::PDFRenderer::ClipToCropBox },
        RenderFeatureInfo{ "render-invert-colors", "Invert all colors.", pdf::PDFRenderer::InvertColors },
        RenderFeatureInfo{ "render-display-annot", "Display annotations.", pdf::PDFRenderer::DisplayAnnotations }
    };
}

}   // pdftool
