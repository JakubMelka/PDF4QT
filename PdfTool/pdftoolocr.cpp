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

#include "pdftoolocr.h"

#include "pdfocrmodelmanager.h"
#include "pdfocrexport.h"
#include "pdfocrengine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QEventLoop>
#include <QCommandLineParser>
#include <QRegularExpression>

#include <map>
#include <cmath>
#include <algorithm>

namespace pdftool
{

static PDFToolOCR s_ocrApplication;
static PDFToolOCRModels s_ocrModelsApplication;

namespace
{

/// Remembers the first invalid value of an option
void reportInvalidValue(PDFToolOCROptions& options, const QString& option, const QString& value, const QString& validValues)
{
    if (options.invalidArgument.isEmpty())
    {
        options.invalidArgument = PDFToolTranslationContext::tr("Invalid value '%1' of the option '--%2'. Valid values are %3.").arg(value, option, validValues);
    }
}

/// Parses the profile of the language models
bool parseProfile(const QString& value, pdf::PDFOCRModelProfile* profile)
{
    for (pdf::PDFOCRModelProfile item : pdf::PDFOCRConfiguration::getProfiles())
    {
        if (pdf::PDFOCRConfiguration::getProfileIdentifier(item) == value)
        {
            *profile = item;
            return true;
        }
    }
    return false;
}

/// Splits the languages given as "ces+eng" or "ces,eng"
QStringList splitLanguages(const QString& value)
{
    QStringList languages;
    for (const QString& part : value.split(QRegularExpression(QStringLiteral("[+,]")), Qt::SkipEmptyParts))
    {
        const QString language = part.trimmed();
        if (!language.isEmpty() && !languages.contains(language))
        {
            languages << language;
        }
    }
    return languages;
}

QString formatMilliseconds(qint64 milliseconds)
{
    return QString::number(double(milliseconds) / 1000.0, 'f', 1);
}

}   // namespace

// -------------------------------------------------------------------------
// Command 'ocr'
// -------------------------------------------------------------------------

QString PDFToolOCR::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "ocr";

        case Name:
            return PDFToolTranslationContext::tr("Text Recognition (OCR)");

        case Description:
            return PDFToolTranslationContext::tr("Recognize the text of scanned pages, write an invisible text layer into a copy of the document and export the text.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

PDFToolAbstractApplication::Options PDFToolOCR::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | PageSelector | OCR;
}

QStringList PDFToolOCR::ExportTargets::getFiles() const
{
    QStringList files;
    for (const QString& file : { text, hocr, alto, tsv })
    {
        if (!file.isEmpty())
        {
            files << file;
        }
    }
    return files;
}

void PDFToolOCR::initializeCommandLineParser(QCommandLineParser* parser)
{
    // Output
    parser->addOption(QCommandLineOption({ "o", "output" }, "Output document with the text layer. The source document is never overwritten. Not used with '--export-only'.", "file"));

    // Recognition
    parser->addOption(QCommandLineOption("engine", "Identifier of the OCR engine.", "engine", "tesseract"));
    parser->addOption(QCommandLineOption("languages", "Languages of the text, in the order of their importance, for example ces+eng.", "languages"));
    parser->addOption(QCommandLineOption("profile", "Profile of the language models (fast|standard|best). The models of the profile must be installed, see the command 'ocr-models'.", "profile"));
    parser->addOption(QCommandLineOption("layout", "Layout of the page: a page segmentation mode 0-13, or auto|auto-osd|single-column|vertical-block|single-block|single-line|single-word|sparse|sparse-osd|raw-line.", "layout"));
    parser->addOption(QCommandLineOption("dpi", "Resolution of the recognition (150-1200 DPI).", "dpi"));
    parser->addOption(QCommandLineOption("allow-reduced-dpi", "Recognize the pages too large for the resolution with a reduced resolution. Without it, such a page is an error."));
    parser->addOption(QCommandLineOption("engine-parameter", "Parameter of the engine in the form name=value. Can be repeated.", "parameter"));

    // Preprocessing
    parser->addOption(QCommandLineOption("deskew", "Straighten a slightly skewed page before the recognition (the visible page is not changed)."));
    parser->addOption(QCommandLineOption("denoise", "Remove a mild noise before the recognition."));
    parser->addOption(QCommandLineOption("auto-orientation", "Detect the orientation of the page."));
    parser->addOption(QCommandLineOption("rotation", "Rotation of the page for the recognition (0|90|180|270).", "degrees"));
    parser->addOption(QCommandLineOption("binarization", "Binarization of the image (auto|otsu|adaptive-otsu|sauvola).", "method"));
    parser->addOption(QCommandLineOption("invert", "Invert a light text on a dark background."));
    parser->addOption(QCommandLineOption("no-blank-detection", "Recognize also the pages detected as blank."));

    // Existing text
    parser->addOption(QCommandLineOption("existing-text", "Policy of the pages with an existing text: skip (recognize only the pages without text), replace-own (replace the text layer created by PDF4QT), review-only (recognize, but never write into the PDF), regions (recognize the regions of the project).", "policy", "skip"));
    parser->addOption(QCommandLineOption("mixed-pages", "Pages with both text and images, or with an ambiguous content: skip, mask (recognize a scan with a small existing text masked, the existing text is kept), review-only (recognize them for the export only).", "policy", "skip"));

    // Restrictions and hints
    parser->addOption(QCommandLineOption("whitelist", "Characters, which can be recognized (empty = all).", "characters"));
    parser->addOption(QCommandLineOption("blacklist", "Characters, which are never recognized.", "characters"));
    parser->addOption(QCommandLineOption("user-words", "UTF-8 text file with the words of the documents (one word per line), a hint of the recognition and accepted words of the review.", "file"));
    parser->addOption(QCommandLineOption("user-patterns", "UTF-8 text file with the patterns of the words (one pattern per line).", "file"));
    parser->addOption(QCommandLineOption("review-threshold", "Words with a lower confidence (0-100) require a review.", "score"));
    parser->addOption(QCommandLineOption("no-dictionary-review", "Words not found in the dictionary of the language model do not require a review."));

    // Compression
    parser->addOption(QCommandLineOption("compression", "Compression of the scanned images of the written pages: off, lossless (pixel identical, JBIG2 generic region / CCITT G4 / Flate), bitonal (gray and color scans of a text are converted to black and white, lossy).", "mode"));
    parser->addOption(QCommandLineOption("compression-encoding", "Encoding of the black and white images (smallest|jbig2|ccittg4|flate).", "encoding"));
    parser->addOption(QCommandLineOption("bitonal-algorithm", "Conversion of the scans of a text into black and white (automatic|adaptive|manual).", "algorithm"));
    parser->addOption(QCommandLineOption("threshold", "Threshold of the manual conversion into black and white (0-255).", "threshold"));
    parser->addOption(QCommandLineOption("compress-shared-images", "Compress also the images, which are drawn on the pages, which are not written."));

    // Exports
    parser->addOption(QCommandLineOption("export-txt", "Export the recognized text into a UTF-8 text file.", "file"));
    parser->addOption(QCommandLineOption("export-hocr", "Export the recognized text with its geometry into a hOCR file.", "file"));
    parser->addOption(QCommandLineOption("export-alto", "Export the recognized text with its geometry into an ALTO file.", "file"));
    parser->addOption(QCommandLineOption("export-tsv", "Export the recognized text with its geometry into a TSV file of Tesseract.", "file"));
    parser->addOption(QCommandLineOption("export-dpi", "Resolution of the coordinates of the hOCR, ALTO and TSV exports. Zero means the resolution of the recognition.", "dpi", "0"));
    parser->addOption(QCommandLineOption("export-only", "Only export the recognized text, do not write the document."));
    parser->addOption(QCommandLineOption("only-reviewed", "Write and export only the reviewed words (confirmed or corrected in a project)."));

    // Project
    parser->addOption(QCommandLineOption("project", "OCR project of the editor. Its configuration is used (the options change it) and its results and corrections are used for the pages with the same content.", "file"));
    parser->addOption(QCommandLineOption("save-project", "Save the results as an OCR project of the editor.", "file"));

    // Operation
    parser->addOption(QCommandLineOption("keep-review-data", "Store the review data (original texts, confidences) in the document."));
    parser->addOption(QCommandLineOption("workers", "Number of the pages recognized in parallel.", "count"));
    parser->addOption(QCommandLineOption("memory-budget", "Memory budget of the page rasters and of the language models in MB.", "megabytes"));
    parser->addOption(QCommandLineOption("page-timeout", "Time limit of the recognition of a page in seconds (0 = no limit).", "seconds"));
    parser->addOption(QCommandLineOption("ocr-data-dir", "Directory of the downloaded and imported language models (default: the directory shared with the editor).", "directory"));
    parser->addOption(QCommandLineOption("allow-page-errors", "Write the document and the exports even if the recognition of some pages failed. Without it, nothing is written then."));
    parser->addOption(QCommandLineOption("quiet", "Do not report the progress."));

    // Batch
    parser->addOption(QCommandLineOption("batch", "Recognize all PDF files of the directory, or the files matching the mask (for example scans/*.pdf). The files are processed one after another, the pages in parallel.", "directory or mask"));
    parser->addOption(QCommandLineOption("output-dir", "Output directory of the batch.", "directory"));
    parser->addOption(QCommandLineOption("suffix", "Suffix of the output files of the batch.", "suffix", "_ocr"));
    parser->addOption(QCommandLineOption("batch-export", "Exports of the batch, written into the output directory (comma separated list of txt|hocr|alto|tsv).", "formats"));
    parser->addOption(QCommandLineOption("skip-existing", "Skip the files of the batch, whose outputs already exist."));
    parser->addOption(QCommandLineOption("continue-on-error", "Continue with the next file of the batch after an error. The exit code reports the error anyway."));
}

void PDFToolOCR::readOptions(QCommandLineParser* parser, PDFToolOCROptions& options)
{
    using Configuration = pdf::PDFOCRConfiguration;

    options.output = parser->value("output");

    auto addChange = [&options](std::function<void(Configuration&)> change)
    {
        options.configurationChanges.push_back(std::move(change));
    };

    auto readInteger = [&](const char* option, int minimum, int maximum, const QString& validValues, int* value)
    {
        const QString text = parser->value(option);
        bool ok = false;
        const int number = text.toInt(&ok);
        if (!ok || number < minimum || number > maximum)
        {
            reportInvalidValue(options, QString::fromLatin1(option), text, validValues);
            return false;
        }
        *value = number;
        return true;
    };

    auto readDouble = [&](const char* option, double minimum, double maximum, const QString& validValues, double* value)
    {
        const QString text = parser->value(option);
        bool ok = false;
        const double number = text.toDouble(&ok);
        if (!ok || !std::isfinite(number) || number < minimum || number > maximum)
        {
            reportInvalidValue(options, QString::fromLatin1(option), text, validValues);
            return false;
        }
        *value = number;
        return true;
    };

    // Recognition
    if (parser->isSet("engine"))
    {
        const QString engine = parser->value("engine");
        addChange([engine](Configuration& configuration) { configuration.engineId = engine; });
    }

    if (parser->isSet("languages"))
    {
        const QStringList languages = splitLanguages(parser->value("languages"));
        if (languages.isEmpty())
        {
            reportInvalidValue(options, "languages", parser->value("languages"), "language codes joined by '+', for example ces+eng");
        }
        addChange([languages](Configuration& configuration) { configuration.languages = languages; });
    }

    if (parser->isSet("profile"))
    {
        pdf::PDFOCRModelProfile profile = pdf::PDFOCRModelProfile::Fast;
        if (!parseProfile(parser->value("profile"), &profile))
        {
            reportInvalidValue(options, "profile", parser->value("profile"), "fast|standard|best");
        }
        addChange([profile](Configuration& configuration) { configuration.profile = profile; });
    }

    if (parser->isSet("layout"))
    {
        static const std::map<QString, pdf::PDFOCRLayout> layouts =
        {
            { QStringLiteral("auto"), pdf::PDFOCRLayout::Automatic },
            { QStringLiteral("auto-osd"), pdf::PDFOCRLayout::AutomaticWithOrientation },
            { QStringLiteral("single-column"), pdf::PDFOCRLayout::SingleColumn },
            { QStringLiteral("vertical-block"), pdf::PDFOCRLayout::VerticalBlock },
            { QStringLiteral("single-block"), pdf::PDFOCRLayout::SingleBlock },
            { QStringLiteral("single-line"), pdf::PDFOCRLayout::SingleLine },
            { QStringLiteral("single-word"), pdf::PDFOCRLayout::SingleWord },
            { QStringLiteral("sparse"), pdf::PDFOCRLayout::SparseText },
            { QStringLiteral("sparse-osd"), pdf::PDFOCRLayout::SparseTextWithOrientation },
            { QStringLiteral("raw-line"), pdf::PDFOCRLayout::RawLine }
        };

        const QString value = parser->value("layout");
        std::optional<pdf::PDFOCRLayout> layout;
        bool ok = false;
        const int number = value.toInt(&ok);
        if (ok && number >= int(pdf::PDFOCRLayout::OrientationOnly) && number <= int(pdf::PDFOCRLayout::RawLine))
        {
            layout = pdf::PDFOCRLayout(number);
        }
        else if (auto it = layouts.find(value); it != layouts.cend())
        {
            layout = it->second;
        }

        if (layout)
        {
            const pdf::PDFOCRLayout selectedLayout = *layout;
            addChange([selectedLayout](Configuration& configuration) { configuration.layout = selectedLayout; });
        }
        else
        {
            reportInvalidValue(options, "layout", value, "0-13|auto|auto-osd|single-column|vertical-block|single-block|single-line|single-word|sparse|sparse-osd|raw-line");
        }
    }

    if (parser->isSet("dpi"))
    {
        double dpi = 0.0;
        if (readDouble("dpi", Configuration::MinimumDpi, Configuration::MaximumDpi, QStringLiteral("%1-%2").arg(Configuration::MinimumDpi).arg(Configuration::MaximumDpi), &dpi))
        {
            addChange([dpi](Configuration& configuration) { configuration.dpi = dpi; });
        }
    }

    options.allowReducedResolution = parser->isSet("allow-reduced-dpi");

    for (const QString& parameter : parser->values("engine-parameter"))
    {
        const qsizetype separator = parameter.indexOf(QChar('='));
        if (separator <= 0)
        {
            reportInvalidValue(options, "engine-parameter", parameter, "name=value");
            continue;
        }

        const QString name = parameter.left(separator).trimmed();
        const QString value = parameter.mid(separator + 1);
        addChange([name, value](Configuration& configuration) { configuration.engineParameters[name] = value; });
    }

    // Preprocessing
    if (parser->isSet("deskew"))
    {
        addChange([](Configuration& configuration) { configuration.preprocessing.deskew = true; });
    }
    if (parser->isSet("denoise"))
    {
        addChange([](Configuration& configuration) { configuration.preprocessing.denoise = true; });
    }
    if (parser->isSet("auto-orientation"))
    {
        addChange([](Configuration& configuration) { configuration.preprocessing.autoOrientation = true; });
    }
    if (parser->isSet("invert"))
    {
        addChange([](Configuration& configuration) { configuration.preprocessing.invert = true; });
    }
    if (parser->isSet("no-blank-detection"))
    {
        addChange([](Configuration& configuration) { configuration.detectBlankPages = false; });
    }
    if (parser->isSet("rotation"))
    {
        const QString value = parser->value("rotation");
        bool ok = false;
        const int rotation = value.toInt(&ok);
        if (!ok || (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270))
        {
            reportInvalidValue(options, "rotation", value, "0|90|180|270");
        }
        addChange([rotation](Configuration& configuration) { configuration.preprocessing.rotation = rotation; });
    }
    if (parser->isSet("binarization"))
    {
        static const std::map<QString, pdf::PDFOCRBinarization> binarizations =
        {
            { QStringLiteral("auto"), pdf::PDFOCRBinarization::Automatic },
            { QStringLiteral("otsu"), pdf::PDFOCRBinarization::Otsu },
            { QStringLiteral("adaptive-otsu"), pdf::PDFOCRBinarization::AdaptiveOtsu },
            { QStringLiteral("sauvola"), pdf::PDFOCRBinarization::Sauvola }
        };

        const QString value = parser->value("binarization");
        auto it = binarizations.find(value);
        if (it == binarizations.cend())
        {
            reportInvalidValue(options, "binarization", value, "auto|otsu|adaptive-otsu|sauvola");
        }
        else
        {
            const pdf::PDFOCRBinarization binarization = it->second;
            addChange([binarization](Configuration& configuration) { configuration.preprocessing.binarization = binarization; });
        }
    }

    // Existing text
    {
        const QString value = parser->value("existing-text");
        std::optional<pdf::PDFOCRExistingTextPolicy> policy;
        if (value == "skip")
        {
            policy = pdf::PDFOCRExistingTextPolicy::OnlyPagesWithoutText;
        }
        else if (value == "replace-own")
        {
            policy = pdf::PDFOCRExistingTextPolicy::ReplaceOwnLayer;
        }
        else if (value == "review-only")
        {
            policy = pdf::PDFOCRExistingTextPolicy::ReviewOnly;
        }
        else if (value == "regions")
        {
            policy = pdf::PDFOCRExistingTextPolicy::AddInRegions;
        }

        if (!policy)
        {
            reportInvalidValue(options, "existing-text", value, "skip|replace-own|review-only|regions");
        }
        else if (parser->isSet("existing-text"))
        {
            // The policy of a project is kept, unless the option is given
            const pdf::PDFOCRExistingTextPolicy selectedPolicy = *policy;
            addChange([selectedPolicy](Configuration& configuration) { configuration.existingTextPolicy = selectedPolicy; });
        }
    }

    {
        const QString value = parser->value("mixed-pages");
        if (value == "skip")
        {
            options.decisionPolicy = pdf::PDFOCRDocumentRunner::DecisionPolicy::Skip;
        }
        else if (value == "mask" || value == "recognize")
        {
            options.decisionPolicy = pdf::PDFOCRDocumentRunner::DecisionPolicy::MaskExistingText;
        }
        else if (value == "review-only")
        {
            options.decisionPolicy = pdf::PDFOCRDocumentRunner::DecisionPolicy::ReviewOnly;
        }
        else
        {
            reportInvalidValue(options, "mixed-pages", value, "skip|mask|review-only");
        }
    }

    // Restrictions and hints
    if (parser->isSet("whitelist"))
    {
        const QString whitelist = parser->value("whitelist");
        addChange([whitelist](Configuration& configuration) { configuration.characterWhitelist = whitelist; });
    }
    if (parser->isSet("blacklist"))
    {
        const QString blacklist = parser->value("blacklist");
        addChange([blacklist](Configuration& configuration) { configuration.characterBlacklist = blacklist; });
    }
    options.userWordsFile = parser->value("user-words");
    options.userPatternsFile = parser->value("user-patterns");
    if (parser->isSet("review-threshold"))
    {
        double threshold = 0.0;
        if (readDouble("review-threshold", 0.0, 100.0, "0-100", &threshold))
        {
            addChange([threshold](Configuration& configuration) { configuration.reviewThreshold = threshold; });
        }
    }
    if (parser->isSet("no-dictionary-review"))
    {
        addChange([](Configuration& configuration) { configuration.reviewOutsideDictionary = false; });
    }

    // Compression
    if (parser->isSet("compression"))
    {
        const QString value = parser->value("compression");
        std::optional<pdf::PDFOCRCompressionMode> mode;
        if (value == "off")
        {
            mode = pdf::PDFOCRCompressionMode::Off;
        }
        else if (value == "lossless")
        {
            mode = pdf::PDFOCRCompressionMode::Lossless;
        }
        else if (value == "bitonal")
        {
            mode = pdf::PDFOCRCompressionMode::BitonalTextScans;
        }

        if (mode)
        {
            const pdf::PDFOCRCompressionMode selectedMode = *mode;
            addChange([selectedMode](Configuration& configuration) { configuration.compression.mode = selectedMode; });
        }
        else
        {
            reportInvalidValue(options, "compression", value, "off|lossless|bitonal");
        }
    }
    if (parser->isSet("compression-encoding"))
    {
        static const std::map<QString, pdf::PDFOCRBitonalEncoding> encodings =
        {
            { QStringLiteral("smallest"), pdf::PDFOCRBitonalEncoding::Smallest },
            { QStringLiteral("jbig2"), pdf::PDFOCRBitonalEncoding::JBIG2 },
            { QStringLiteral("ccittg4"), pdf::PDFOCRBitonalEncoding::CCITTGroup4 },
            { QStringLiteral("flate"), pdf::PDFOCRBitonalEncoding::Flate }
        };

        const QString value = parser->value("compression-encoding");
        auto it = encodings.find(value);
        if (it == encodings.cend())
        {
            reportInvalidValue(options, "compression-encoding", value, "smallest|jbig2|ccittg4|flate");
        }
        else
        {
            const pdf::PDFOCRBitonalEncoding encoding = it->second;
            addChange([encoding](Configuration& configuration) { configuration.compression.bitonalEncoding = encoding; });
        }
    }
    if (parser->isSet("bitonal-algorithm"))
    {
        static const std::map<QString, pdf::PDFOCRThresholdMethod> methods =
        {
            { QStringLiteral("automatic"), pdf::PDFOCRThresholdMethod::Automatic },
            { QStringLiteral("adaptive"), pdf::PDFOCRThresholdMethod::Adaptive },
            { QStringLiteral("manual"), pdf::PDFOCRThresholdMethod::Manual }
        };

        const QString value = parser->value("bitonal-algorithm");
        auto it = methods.find(value);
        if (it == methods.cend())
        {
            reportInvalidValue(options, "bitonal-algorithm", value, "automatic|adaptive|manual");
        }
        else
        {
            const pdf::PDFOCRThresholdMethod method = it->second;
            addChange([method](Configuration& configuration) { configuration.compression.thresholdMethod = method; });
        }
    }
    if (parser->isSet("threshold"))
    {
        int threshold = 0;
        if (readInteger("threshold", 0, 255, "0-255", &threshold))
        {
            // A threshold without an explicit algorithm selects the manual conversion
            const bool setManual = !parser->isSet("bitonal-algorithm");
            addChange([threshold, setManual](Configuration& configuration)
            {
                configuration.compression.manualThreshold = threshold;
                if (setManual)
                {
                    configuration.compression.thresholdMethod = pdf::PDFOCRThresholdMethod::Manual;
                }
            });
        }
    }
    if (parser->isSet("compress-shared-images"))
    {
        addChange([](Configuration& configuration) { configuration.compression.compressSharedImages = true; });
    }

    // Exports
    options.exportText = parser->value("export-txt");
    options.exportHocr = parser->value("export-hocr");
    options.exportAlto = parser->value("export-alto");
    options.exportTsv = parser->value("export-tsv");
    readDouble("export-dpi", 0.0, Configuration::MaximumDpi, QStringLiteral("0-%1").arg(Configuration::MaximumDpi), &options.exportDpi);
    if (options.exportDpi > 0.0 && options.exportDpi < 1.0)
    {
        reportInvalidValue(options, "export-dpi", parser->value("export-dpi"), QStringLiteral("0 or 1-%1").arg(Configuration::MaximumDpi));
    }
    options.exportOnly = parser->isSet("export-only");
    options.onlyReviewed = parser->isSet("only-reviewed");

    // Project
    options.project = parser->value("project");
    options.saveProject = parser->value("save-project");

    // Operation
    options.keepReviewData = parser->isSet("keep-review-data");
    if (parser->isSet("workers"))
    {
        int workers = 0;
        if (readInteger("workers", 1, 64, "1-64", &workers))
        {
            addChange([workers](Configuration& configuration) { configuration.workerCount = workers; });
        }
    }
    if (parser->isSet("memory-budget"))
    {
        int megabytes = 0;
        if (readInteger("memory-budget", 64, 1024 * 1024, "64-1048576", &megabytes))
        {
            addChange([megabytes](Configuration& configuration) { configuration.memoryBudget = qint64(megabytes) << 20; });
        }
    }
    if (parser->isSet("page-timeout"))
    {
        int seconds = 0;
        if (readInteger("page-timeout", 0, 24 * 3600, "0-86400", &seconds))
        {
            addChange([seconds](Configuration& configuration) { configuration.pageTimeoutSeconds = seconds; });
        }
    }
    options.dataDirectory = parser->value("ocr-data-dir");
    options.allowPageErrors = parser->isSet("allow-page-errors");
    options.quiet = parser->isSet("quiet");

    // Batch
    options.batch = parser->value("batch");
    options.outputDirectory = parser->value("output-dir");
    options.suffix = parser->value("suffix");
    options.skipExisting = parser->isSet("skip-existing");
    options.continueOnError = parser->isSet("continue-on-error");
    if (parser->isSet("batch-export"))
    {
        for (const QString& format : parser->value("batch-export").split(QChar(','), Qt::SkipEmptyParts))
        {
            const QString trimmedFormat = format.trimmed();
            if (trimmedFormat != "txt" && trimmedFormat != "hocr" && trimmedFormat != "alto" && trimmedFormat != "tsv")
            {
                reportInvalidValue(options, "batch-export", parser->value("batch-export"), "a comma separated list of txt|hocr|alto|tsv");
            }
            else if (!options.batchExports.contains(trimmedFormat))
            {
                options.batchExports << trimmedFormat;
            }
        }
    }
}

bool PDFToolOCR::readLines(const QString& fileName, QStringList* lines, QString* errorMessage)
{
    QFile file(fileName);
    if (!file.open(QFile::ReadOnly | QFile::Text))
    {
        *errorMessage = PDFToolTranslationContext::tr("Cannot read the file '%1'. %2").arg(QDir::toNativeSeparators(fileName), file.errorString());
        return false;
    }

    for (const QString& line : QString::fromUtf8(file.readAll()).split(QChar('\n')))
    {
        const QString trimmedLine = line.trimmed();
        if (!trimmedLine.isEmpty())
        {
            *lines << trimmedLine;
        }
    }
    return true;
}

bool PDFToolOCR::createConfiguration(const PDFToolOptions& options, const pdf::PDFOCRProject* project, pdf::PDFOCRConfiguration* configuration, QString* errorMessage)
{
    *configuration = project ? project->configuration : pdf::PDFOCRConfiguration();
    for (const auto& change : options.ocr.configurationChanges)
    {
        change(*configuration);
    }

    if (!options.ocr.userWordsFile.isEmpty())
    {
        QStringList words;
        if (!readLines(options.ocr.userWordsFile, &words, errorMessage))
        {
            return false;
        }
        for (const QString& word : words)
        {
            if (!configuration->userWords.contains(word))
            {
                configuration->userWords << word;
            }
        }
    }

    if (!options.ocr.userPatternsFile.isEmpty())
    {
        QStringList patterns;
        if (!readLines(options.ocr.userPatternsFile, &patterns, errorMessage))
        {
            return false;
        }
        for (const QString& pattern : patterns)
        {
            if (!configuration->userPatterns.contains(pattern))
            {
                configuration->userPatterns << pattern;
            }
        }
    }

    if (configuration->languages.isEmpty())
    {
        *errorMessage = PDFToolTranslationContext::tr("No language of the text is selected. Use the option '--languages', for example '--languages ces+eng'.");
        return false;
    }

    // The compression of a project refers to the images of its session, the preview
    // exclusions cannot be applied to another run
    configuration->compression.excludedImages.clear();
    return true;
}

bool PDFToolOCR::isSameFile(const QString& first, const QString& second)
{
    const QFileInfo firstInfo(first);
    const QFileInfo secondInfo(second);
    if (firstInfo.exists() && secondInfo.exists())
    {
        return firstInfo.canonicalFilePath().compare(secondInfo.canonicalFilePath(), Qt::CaseInsensitive) == 0;
    }
    return firstInfo.absoluteFilePath().compare(secondInfo.absoluteFilePath(), Qt::CaseInsensitive) == 0;
}

QStringList PDFToolOCR::getBatchFiles(const QString& batch, QString* errorMessage)
{
    QStringList files;
    const QFileInfo batchInfo(batch);

    QDir directory;
    QStringList nameFilters;
    if (batchInfo.isDir())
    {
        directory = QDir(batchInfo.absoluteFilePath());
        nameFilters << QStringLiteral("*.pdf");
    }
    else
    {
        directory = batchInfo.absoluteDir();
        nameFilters << batchInfo.fileName();
    }

    if (!directory.exists())
    {
        *errorMessage = PDFToolTranslationContext::tr("The directory '%1' of the batch does not exist.").arg(QDir::toNativeSeparators(directory.absolutePath()));
        return files;
    }

    for (const QFileInfo& fileInfo : directory.entryInfoList(nameFilters, QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase))
    {
        files << fileInfo.absoluteFilePath();
    }

    if (files.isEmpty())
    {
        *errorMessage = PDFToolTranslationContext::tr("No file of the batch '%1' was found.").arg(QDir::toNativeSeparators(batch));
    }
    return files;
}

int PDFToolOCR::execute(const PDFToolOptions& options)
{
    const PDFToolOCROptions& ocr = options.ocr;

    if (!ocr.invalidArgument.isEmpty())
    {
        PDFConsole::writeError(ocr.invalidArgument, options.outputCodec);
        return ErrorInvalidArguments;
    }

    const bool isBatch = !ocr.batch.isEmpty();
    const ExportTargets exports = { ocr.exportText, ocr.exportHocr, ocr.exportAlto, ocr.exportTsv };

    // Combinations of the options
    QString argumentError;
    if (isBatch)
    {
        if (!options.document.isEmpty())
        {
            argumentError = PDFToolTranslationContext::tr("A document cannot be given together with '--batch'.");
        }
        else if (ocr.outputDirectory.isEmpty())
        {
            argumentError = PDFToolTranslationContext::tr("The batch needs the output directory '--output-dir'.");
        }
        else if (!ocr.output.isEmpty() || !exports.isEmpty())
        {
            argumentError = PDFToolTranslationContext::tr("The options '--output' and '--export-*' cannot be used with '--batch', use '--output-dir' and '--batch-export'.");
        }
        else if (!ocr.project.isEmpty() || !ocr.saveProject.isEmpty())
        {
            argumentError = PDFToolTranslationContext::tr("A project belongs to a single document, it cannot be used with '--batch'.");
        }
        else if (options.isPageRangeSet())
        {
            argumentError = PDFToolTranslationContext::tr("A page range cannot be used with '--batch'.");
        }
        else if (ocr.exportOnly && ocr.batchExports.isEmpty())
        {
            argumentError = PDFToolTranslationContext::tr("The option '--export-only' needs at least one export ('--batch-export').");
        }
        else if (ocr.suffix.isEmpty() && !ocr.exportOnly)
        {
            QString directoryError;
            const QStringList files = getBatchFiles(ocr.batch, &directoryError);
            if (!files.isEmpty() && QFileInfo(files.front()).absoluteDir() == QDir(ocr.outputDirectory))
            {
                argumentError = PDFToolTranslationContext::tr("An empty suffix in the directory of the source files would overwrite them.");
            }
        }
    }
    else
    {
        if (options.document.isEmpty())
        {
            argumentError = PDFToolTranslationContext::tr("No document specified. Give a document, or a batch by '--batch'.");
        }
        else if (!ocr.batchExports.isEmpty() || !ocr.outputDirectory.isEmpty())
        {
            argumentError = PDFToolTranslationContext::tr("The options '--output-dir' and '--batch-export' can be used only with '--batch'.");
        }
        else if (ocr.exportOnly && !ocr.output.isEmpty())
        {
            argumentError = PDFToolTranslationContext::tr("The option '--export-only' does not write the document, remove the option '--output'.");
        }
        else if (ocr.exportOnly && exports.isEmpty() && ocr.saveProject.isEmpty())
        {
            argumentError = PDFToolTranslationContext::tr("The option '--export-only' needs at least one export ('--export-txt', '--export-hocr', '--export-alto', '--export-tsv') or '--save-project'.");
        }
        else if (!ocr.exportOnly && ocr.output.isEmpty())
        {
            argumentError = PDFToolTranslationContext::tr("No output document specified. Use the option '--output', or '--export-only' to export the text only.");
        }
        else if (!ocr.output.isEmpty() && isSameFile(ocr.output, options.document))
        {
            argumentError = PDFToolTranslationContext::tr("The output document must differ from the source document, the source document is never overwritten.");
        }
        else
        {
            for (const QString& file : exports.getFiles())
            {
                if (isSameFile(file, options.document) || (!ocr.output.isEmpty() && isSameFile(file, ocr.output)))
                {
                    argumentError = PDFToolTranslationContext::tr("The export '%1' would overwrite a document.").arg(QDir::toNativeSeparators(file));
                    break;
                }
            }
        }
    }

    if (!argumentError.isEmpty())
    {
        PDFConsole::writeError(argumentError, options.outputCodec);
        return ErrorInvalidArguments;
    }

    // The models are shared with the editor, the command never downloads a model
    pdf::PDFOCRModelManager modelManager(nullptr);
    if (!ocr.dataDirectory.isEmpty())
    {
        modelManager.setUserDirectory(ocr.dataDirectory);
    }
    modelManager.loadBundledCatalog();
    modelManager.refresh();

    if (isBatch)
    {
        return executeBatch(options, &modelManager);
    }

    DocumentReport report;
    return processDocument(options, &modelManager, options.document, ocr.output, exports, true, &report);
}

int PDFToolOCR::processDocument(const PDFToolOptions& options,
                                pdf::PDFOCRModelManager* modelManager,
                                const QString& inputFile,
                                const QString& outputFile,
                                const ExportTargets& exports,
                                bool printPages,
                                DocumentReport* report)
{
    const PDFToolOCROptions& ocr = options.ocr;

    pdf::PDFOCRDocumentRunner::FileTask task;
    task.inputFile = inputFile;
    task.password = options.password;
    task.outputFile = outputFile;
    task.exportText = exports.text;
    task.exportHocr = exports.hocr;
    task.exportAlto = exports.alto;
    task.exportTsv = exports.tsv;
    task.exportDpi = ocr.exportDpi;
    task.onlyReviewed = ocr.onlyReviewed;
    task.keepReviewData = ocr.keepReviewData;
    task.projectFile = ocr.project;
    task.saveProjectFile = ocr.saveProject;
    task.allowPageErrors = ocr.allowPageErrors;
    task.configure = [&options](const pdf::PDFOCRProject* project, pdf::PDFOCRConfiguration* configuration)
    {
        QString errorMessage;
        createConfiguration(options, project, configuration, &errorMessage);
        return errorMessage;
    };
    if (options.isPageRangeSet())
    {
        task.selectPages = [&options](pdf::PDFInteger pageCount, std::vector<pdf::PDFInteger>* pages)
        {
            QString errorMessage;
            *pages = options.getPageRange(pageCount, errorMessage, true);
            return errorMessage;
        };
    }

    pdf::PDFOCRDocumentRunner::Settings settings;
    settings.decisionPolicy = ocr.decisionPolicy;
    settings.allowReducedResolution = ocr.allowReducedResolution;
    settings.modelManager = modelManager;

    auto progress = [&](int finished, int total, const pdf::PDFOCRPageResult* page)
    {
        if (ocr.quiet || !page)
        {
            return;
        }

        QString text = PDFToolTranslationContext::tr("[%1/%2] Page %3: %4").arg(finished).arg(total).arg(page->pageIndex + 1).arg(pdf::PDFOCRDocumentRunner::getPageStateName(page->state));
        if (page->state == pdf::PDFOCRPageState::Error && page->error)
        {
            text += QStringLiteral(" - ") + page->error.message;
        }
        PDFConsole::writeError(text, options.outputCodec);
    };

    pdf::PDFOCRDocumentRunner::FileResult fileResult = pdf::PDFOCRDocumentRunner::processFile(task, settings, progress, nullptr);
    const pdf::PDFOCRDocumentRunner::Result& result = fileResult.result;
    const pdf::PDFOCRConfidenceStatistics statistics = result.getStatistics();

    report->input = inputFile;
    report->output = outputFile;
    report->pageCount = fileResult.pageCount;
    report->resultPages = result.getResultPageCount();
    report->failedPages = result.getFailedPageCount();
    report->wordCount = statistics.wordCount;
    report->reviewWords = statistics.reviewRequiredCount;
    report->elapsedMilliseconds = fileResult.elapsedMilliseconds;
    report->message = fileResult.message;
    switch (fileResult.status)
    {
        case pdf::PDFOCRDocumentRunner::FileResult::Status::Success:
            report->status = DocumentReport::Status::Success;
            break;
        case pdf::PDFOCRDocumentRunner::FileResult::Status::Skipped:
            report->status = DocumentReport::Status::Skipped;
            break;
        case pdf::PDFOCRDocumentRunner::FileResult::Status::Failed:
            report->status = DocumentReport::Status::Failed;
            break;
    }

    if (!printPages)
    {
        return fileResult.status == pdf::PDFOCRDocumentRunner::FileResult::Status::Success ? ExitSuccess : ExitFailure;
    }

    QString message = fileResult.message;
    if (fileResult.result.errorCode == pdf::PDFOCRErrorCode::MissingModel)
    {
        message += QChar('\n') + PDFToolTranslationContext::tr("The command line never downloads a language model. Install the language in the editor (Tools > Manage OCR Languages), or by the command 'PdfTool ocr-models install <language> --profile <profile> --accept-download'.");
    }
    if (fileResult.stage == pdf::PDFOCRDocumentRunner::FileResult::Stage::PageErrors)
    {
        message += QChar(' ') + PDFToolTranslationContext::tr("Use '--allow-page-errors' to write the results of the other pages.");
    }

    for (const QString& warning : fileResult.warnings)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Warning: %1").arg(warning), options.outputCodec);
    }

    // Table of the pages
    if (!result.records.empty())
    {
        PDFOutputFormatter formatter(options.outputStyle);
        formatter.beginDocument("ocr", PDFToolTranslationContext::tr("Text recognition of %1").arg(QDir::toNativeSeparators(inputFile)));
        formatter.endl();

        formatter.beginTable("pages", PDFToolTranslationContext::tr("Pages"));
        formatter.beginTableHeaderRow("header");
        formatter.writeTableHeaderColumn("page", PDFToolTranslationContext::tr("Page"), Qt::AlignRight);
        formatter.writeTableHeaderColumn("label", PDFToolTranslationContext::tr("Label"));
        formatter.writeTableHeaderColumn("state", PDFToolTranslationContext::tr("State"));
        formatter.writeTableHeaderColumn("source", PDFToolTranslationContext::tr("Source"));
        formatter.writeTableHeaderColumn("words", PDFToolTranslationContext::tr("Words"), Qt::AlignRight);
        formatter.writeTableHeaderColumn("confidence", PDFToolTranslationContext::tr("Mean confidence"), Qt::AlignRight);
        formatter.writeTableHeaderColumn("review", PDFToolTranslationContext::tr("To review"), Qt::AlignRight);
        formatter.writeTableHeaderColumn("dictionary", PDFToolTranslationContext::tr("Not in dictionary"), Qt::AlignRight);
        formatter.writeTableHeaderColumn("time", PDFToolTranslationContext::tr("Time [s]"), Qt::AlignRight);
        formatter.writeTableHeaderColumn("note", PDFToolTranslationContext::tr("Note"));
        formatter.endTableHeaderRow();

        for (const pdf::PDFOCRDocumentRunner::PageRecord& record : result.records)
        {
            QString source;
            switch (record.source)
            {
                case pdf::PDFOCRDocumentRunner::PageSource::Recognized:
                    source = PDFToolTranslationContext::tr("recognized");
                    break;
                case pdf::PDFOCRDocumentRunner::PageSource::Project:
                    source = PDFToolTranslationContext::tr("project");
                    break;
                case pdf::PDFOCRDocumentRunner::PageSource::Skipped:
                    source = PDFToolTranslationContext::tr("not recognized");
                    break;
            }

            QStringList notes;
            if (record.reviewOnly)
            {
                notes << PDFToolTranslationContext::tr("review and export only");
            }
            if (record.masked)
            {
                notes << PDFToolTranslationContext::tr("existing text masked");
            }
            if (!record.message.isEmpty())
            {
                notes << record.message;
            }

            const bool hasResult = record.state == pdf::PDFOCRPageState::Done || record.state == pdf::PDFOCRPageState::NoText;
            formatter.beginTableRow("page", int(record.pageIndex + 1));
            formatter.writeTableColumn("page", QString::number(record.pageIndex + 1), Qt::AlignRight);
            formatter.writeTableColumn("label", record.pageLabel);
            formatter.writeTableColumn("state", pdf::PDFOCRDocumentRunner::getPageStateName(record.state));
            formatter.writeTableColumn("source", source);
            formatter.writeTableColumn("words", hasResult ? QString::number(record.statistics.wordCount) : QString(), Qt::AlignRight);
            formatter.writeTableColumn("confidence", hasResult && record.statistics.meanScore ? QString::number(*record.statistics.meanScore, 'f', 1) : QString(), Qt::AlignRight);
            formatter.writeTableColumn("review", hasResult ? QString::number(record.statistics.reviewRequiredCount) : QString(), Qt::AlignRight);
            formatter.writeTableColumn("dictionary", hasResult && record.statistics.dictionaryCheckedCount > 0 ? QString::number(record.statistics.outsideDictionaryCount) : QString(), Qt::AlignRight);
            formatter.writeTableColumn("time", record.source == pdf::PDFOCRDocumentRunner::PageSource::Recognized ? formatMilliseconds(record.elapsedMilliseconds) : QString(), Qt::AlignRight);
            formatter.writeTableColumn("note", notes.join(QStringLiteral("; ")));
            formatter.endTableRow();
        }
        formatter.endTable();
        formatter.endl();

        formatter.beginHeader("summary", PDFToolTranslationContext::tr("Summary"));
        formatter.writeText("pages", PDFToolTranslationContext::tr("Pages with a result: %1 of %2, failed: %3.").arg(report->resultPages).arg(result.records.size()).arg(report->failedPages));
        formatter.writeText("words", PDFToolTranslationContext::tr("Words: %1, words to review: %2, mean confidence: %3.")
                                         .arg(statistics.wordCount).arg(statistics.reviewRequiredCount)
                                         .arg(statistics.meanScore ? QString::number(*statistics.meanScore, 'f', 1) : PDFToolTranslationContext::tr("unknown")));
        if (result.summary.totalPages > 0)
        {
            formatter.writeText("time", PDFToolTranslationContext::tr("Recognition time: %1 s, workers: %2.").arg(formatMilliseconds(result.summary.elapsedMilliseconds)).arg(result.summary.workerCount));
        }
        formatter.endHeader();
        formatter.endDocument();

        PDFConsole::writeText(formatter.getString(), options.outputCodec);
    }

    if (!fileResult.messages.isEmpty())
    {
        PDFConsole::writeError(fileResult.messages.join(QChar('\n')), options.outputCodec);
    }
    if (!message.isEmpty())
    {
        PDFConsole::writeError(message, options.outputCodec);
    }

    using Stage = pdf::PDFOCRDocumentRunner::FileResult::Stage;
    switch (fileResult.stage)
    {
        case Stage::None:
            return ExitSuccess;
        case Stage::Reading:
            return ErrorDocumentReading;
        case Stage::Permissions:
            return ErrorPermissions;
        case Stage::Project:
        case Stage::Configuration:
            return ErrorInvalidArguments;
        case Stage::Recognition:
        case Stage::PageErrors:
            return ExitFailure;
        case Stage::NothingToWrite:
            return ErrorNoText;
        case Stage::Writing:
            return ErrorFailedWriteToFile;
    }

    return ExitFailure;
}

int PDFToolOCR::executeBatch(const PDFToolOptions& options, pdf::PDFOCRModelManager* modelManager)
{
    const PDFToolOCROptions& ocr = options.ocr;

    QString errorMessage;
    const QStringList files = getBatchFiles(ocr.batch, &errorMessage);
    if (files.isEmpty())
    {
        PDFConsole::writeError(errorMessage, options.outputCodec);
        return ErrorInvalidArguments;
    }

    QDir outputDirectory(ocr.outputDirectory);
    if (!outputDirectory.exists() && !QDir().mkpath(outputDirectory.absolutePath()))
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("The output directory '%1' cannot be created.").arg(QDir::toNativeSeparators(outputDirectory.absolutePath())), options.outputCodec);
        return ErrorFailedWriteToFile;
    }

    std::vector<DocumentReport> reports;
    bool hasFailure = false;
    int index = 0;
    for (const QString& file : files)
    {
        ++index;
        const QString baseName = outputDirectory.absoluteFilePath(QFileInfo(file).completeBaseName() + ocr.suffix);
        const QString outputFile = ocr.exportOnly ? QString() : baseName + QStringLiteral(".pdf");

        ExportTargets exports;
        if (ocr.batchExports.contains(QStringLiteral("txt")))
        {
            exports.text = baseName + QStringLiteral(".txt");
        }
        if (ocr.batchExports.contains(QStringLiteral("hocr")))
        {
            exports.hocr = baseName + QStringLiteral(".") + pdf::PDFOCRStructuredExporter::getFileSuffix(pdf::PDFOCRStructuredExporter::Format::Hocr);
        }
        if (ocr.batchExports.contains(QStringLiteral("alto")))
        {
            exports.alto = baseName + QStringLiteral(".") + pdf::PDFOCRStructuredExporter::getFileSuffix(pdf::PDFOCRStructuredExporter::Format::Alto);
        }
        if (ocr.batchExports.contains(QStringLiteral("tsv")))
        {
            exports.tsv = baseName + QStringLiteral(".") + pdf::PDFOCRStructuredExporter::getFileSuffix(pdf::PDFOCRStructuredExporter::Format::Tsv);
        }

        DocumentReport report;
        report.input = file;
        report.output = outputFile;

        QStringList outputs = exports.getFiles();
        if (!outputFile.isEmpty())
        {
            outputs.prepend(outputFile);
        }

        if (!ocr.quiet)
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("File %1/%2: %3").arg(index).arg(files.size()).arg(QDir::toNativeSeparators(file)), options.outputCodec);
        }

        const bool overwritesSource = std::any_of(outputs.cbegin(), outputs.cend(), [&file](const QString& output) { return isSameFile(output, file); });
        if (overwritesSource)
        {
            report.status = DocumentReport::Status::Failed;
            report.message = PDFToolTranslationContext::tr("The output would overwrite the source file.");
            hasFailure = true;
        }
        else if (ocr.skipExisting && std::all_of(outputs.cbegin(), outputs.cend(), [](const QString& output) { return QFileInfo::exists(output); }))
        {
            report.status = DocumentReport::Status::Skipped;
            report.message = PDFToolTranslationContext::tr("The outputs already exist.");
        }
        else
        {
            processDocument(options, modelManager, file, outputFile, exports, false, &report);
            hasFailure = hasFailure || report.status == DocumentReport::Status::Failed;
        }

        reports.push_back(report);

        if (hasFailure && !ocr.continueOnError)
        {
            break;
        }
    }

    // Summary table of the files
    PDFOutputFormatter formatter(options.outputStyle);
    formatter.beginDocument("ocr-batch", PDFToolTranslationContext::tr("Text recognition of the batch %1").arg(QDir::toNativeSeparators(ocr.batch)));
    formatter.endl();

    formatter.beginTable("files", PDFToolTranslationContext::tr("Files"));
    formatter.beginTableHeaderRow("header");
    formatter.writeTableHeaderColumn("file", PDFToolTranslationContext::tr("File"));
    formatter.writeTableHeaderColumn("status", PDFToolTranslationContext::tr("Status"));
    formatter.writeTableHeaderColumn("pages", PDFToolTranslationContext::tr("Pages"), Qt::AlignRight);
    formatter.writeTableHeaderColumn("results", PDFToolTranslationContext::tr("With result"), Qt::AlignRight);
    formatter.writeTableHeaderColumn("failed", PDFToolTranslationContext::tr("Failed"), Qt::AlignRight);
    formatter.writeTableHeaderColumn("words", PDFToolTranslationContext::tr("Words"), Qt::AlignRight);
    formatter.writeTableHeaderColumn("review", PDFToolTranslationContext::tr("To review"), Qt::AlignRight);
    formatter.writeTableHeaderColumn("time", PDFToolTranslationContext::tr("Time [s]"), Qt::AlignRight);
    formatter.writeTableHeaderColumn("message", PDFToolTranslationContext::tr("Message"));
    formatter.endTableHeaderRow();

    int succeeded = 0;
    int skipped = 0;
    int failed = 0;
    int reference = 0;
    for (const DocumentReport& report : reports)
    {
        QString status;
        switch (report.status)
        {
            case DocumentReport::Status::Success:
                status = PDFToolTranslationContext::tr("OK");
                ++succeeded;
                break;
            case DocumentReport::Status::Skipped:
                status = PDFToolTranslationContext::tr("Skipped");
                ++skipped;
                break;
            case DocumentReport::Status::Failed:
                status = PDFToolTranslationContext::tr("Failed");
                ++failed;
                break;
        }

        formatter.beginTableRow("file", ++reference);
        formatter.writeTableColumn("file", QDir::toNativeSeparators(report.input));
        formatter.writeTableColumn("status", status);
        formatter.writeTableColumn("pages", QString::number(report.pageCount), Qt::AlignRight);
        formatter.writeTableColumn("results", QString::number(report.resultPages), Qt::AlignRight);
        formatter.writeTableColumn("failed", QString::number(report.failedPages), Qt::AlignRight);
        formatter.writeTableColumn("words", QString::number(report.wordCount), Qt::AlignRight);
        formatter.writeTableColumn("review", QString::number(report.reviewWords), Qt::AlignRight);
        formatter.writeTableColumn("time", formatMilliseconds(report.elapsedMilliseconds), Qt::AlignRight);
        formatter.writeTableColumn("message", report.message);
        formatter.endTableRow();
    }
    formatter.endTable();
    formatter.endl();

    const int notProcessed = int(files.size() - reports.size());
    QString summary = PDFToolTranslationContext::tr("Files: %1, succeeded: %2, skipped: %3, failed: %4.").arg(files.size()).arg(succeeded).arg(skipped).arg(failed);
    if (notProcessed > 0)
    {
        summary += QChar(' ') + PDFToolTranslationContext::tr("%n file(s) were not processed because of the error (use '--continue-on-error').", nullptr, notProcessed);
    }
    formatter.writeText("summary", summary);
    formatter.endDocument();

    PDFConsole::writeText(formatter.getString(), options.outputCodec);

    // Any failed file is a failure of the batch, even with --continue-on-error
    return hasFailure ? ExitFailure : ExitSuccess;
}

// -------------------------------------------------------------------------
// Command 'ocr-models'
// -------------------------------------------------------------------------

QString PDFToolOCRModels::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "ocr-models";

        case Name:
            return PDFToolTranslationContext::tr("OCR Language Models");

        case Description:
            return PDFToolTranslationContext::tr("List, install or remove the language models of the text recognition.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

PDFToolAbstractApplication::Options PDFToolOCRModels::getOptionsFlags() const
{
    return ConsoleFormat | OCRModels;
}

void PDFToolOCRModels::initializeCommandLineParser(QCommandLineParser* parser)
{
    parser->addPositionalArgument("action", "Action: list|install|remove.");
    parser->addPositionalArgument("languages", "Languages of the action, for example ces eng, or ces+eng.", "[languages...]");
    parser->addOption(QCommandLineOption("engine", "Identifier of the OCR engine.", "engine", "tesseract"));
    parser->addOption(QCommandLineOption("profile", "Profile of the language models (fast|standard|best). The list shows all profiles, if it is not given.", "profile"));
    parser->addOption(QCommandLineOption("accept-download", "Consent to the download of the models (over https, verified by the SHA-256 checksums of the catalog)."));
    parser->addOption(QCommandLineOption("ocr-data-dir", "Directory of the downloaded and imported language models (default: the directory shared with the editor).", "directory"));
}

void PDFToolOCRModels::readOptions(QCommandLineParser* parser, PDFToolOCROptions& options)
{
    QStringList positionalArguments = parser->positionalArguments();
    options.modelsAction = positionalArguments.isEmpty() ? QString() : positionalArguments.takeFirst();
    for (const QString& argument : positionalArguments)
    {
        for (const QString& language : splitLanguages(argument))
        {
            if (!options.modelsLanguages.contains(language))
            {
                options.modelsLanguages << language;
            }
        }
    }

    options.modelsEngine = parser->value("engine");
    options.modelsAcceptDownload = parser->isSet("accept-download");
    options.dataDirectory = parser->value("ocr-data-dir");

    if (parser->isSet("profile"))
    {
        if (!parseProfile(parser->value("profile"), &options.modelsProfile))
        {
            reportInvalidValue(options, "profile", parser->value("profile"), "fast|standard|best");
        }
        else
        {
            // The list is restricted to the profile only, when it is given
            options.modelsProfileSet = true;
        }
    }

    if (options.modelsAction != "list" && options.modelsAction != "install" && options.modelsAction != "remove")
    {
        reportInvalidValue(options, "action", options.modelsAction, "list|install|remove");
    }
}

int PDFToolOCRModels::execute(const PDFToolOptions& options)
{
    const PDFToolOCROptions& ocr = options.ocr;
    if (!ocr.invalidArgument.isEmpty())
    {
        PDFConsole::writeError(ocr.invalidArgument, options.outputCodec);
        return ErrorInvalidArguments;
    }

    for (const QString& language : ocr.modelsLanguages)
    {
        if (!pdf::PDFOCRConfiguration::isValidLanguageIdentifier(language))
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Invalid language '%1'.").arg(language), options.outputCodec);
            return ErrorInvalidArguments;
        }
    }

    pdf::PDFOCRModelManager manager(nullptr);
    if (!ocr.dataDirectory.isEmpty())
    {
        manager.setUserDirectory(ocr.dataDirectory);
    }
    manager.loadBundledCatalog();
    manager.refresh();

    const bool isProfileSet = ocr.modelsProfileSet;
    const QString profileIdentifier = pdf::PDFOCRConfiguration::getProfileIdentifier(ocr.modelsProfile);

    if (ocr.modelsAction == "list")
    {
        PDFOutputFormatter formatter(options.outputStyle);
        formatter.beginDocument("ocr-models", PDFToolTranslationContext::tr("Language models of the OCR"));
        formatter.endl();
        formatter.writeText("built-in", PDFToolTranslationContext::tr("Built-in models: %1").arg(QDir::toNativeSeparators(manager.getBuiltInDirectory())));
        formatter.writeText("user", PDFToolTranslationContext::tr("Downloaded and imported models: %1").arg(QDir::toNativeSeparators(manager.getUserDirectory())));
        formatter.endl();

        formatter.beginTable("models", PDFToolTranslationContext::tr("Models"));
        formatter.beginTableHeaderRow("header");
        formatter.writeTableHeaderColumn("language", PDFToolTranslationContext::tr("Language"));
        formatter.writeTableHeaderColumn("name", PDFToolTranslationContext::tr("Name"));
        formatter.writeTableHeaderColumn("profile", PDFToolTranslationContext::tr("Profile"));
        formatter.writeTableHeaderColumn("state", PDFToolTranslationContext::tr("State"));
        formatter.writeTableHeaderColumn("origin", PDFToolTranslationContext::tr("Origin"));
        formatter.writeTableHeaderColumn("size", PDFToolTranslationContext::tr("Size [MB]"), Qt::AlignRight);
        formatter.endTableHeaderRow();

        int reference = 0;
        for (const pdf::PDFOCRModelInfo& model : manager.getModels())
        {
            if (model.engineId != ocr.modelsEngine ||
                (isProfileSet && model.profile != ocr.modelsProfile) ||
                (!ocr.modelsLanguages.isEmpty() && !ocr.modelsLanguages.contains(model.language)))
            {
                continue;
            }

            formatter.beginTableRow("model", ++reference);
            formatter.writeTableColumn("language", model.language);
            formatter.writeTableColumn("name", model.name);
            formatter.writeTableColumn("profile", pdf::PDFOCRConfiguration::getProfileIdentifier(model.profile));
            formatter.writeTableColumn("state", pdf::PDFOCRModelInfo::getStateName(model.state));
            formatter.writeTableColumn("origin", model.origin != pdf::PDFOCRModelOrigin::None ? pdf::PDFOCRModelInfo::getOriginName(model.origin) : QString());
            formatter.writeTableColumn("size", model.size > 0 ? QString::number(double(model.size) / (1024.0 * 1024.0), 'f', 1) : QString(), Qt::AlignRight);
            formatter.endTableRow();
        }
        formatter.endTable();
        formatter.endDocument();

        PDFConsole::writeText(formatter.getString(), options.outputCodec);
        return ExitSuccess;
    }

    if (ocr.modelsLanguages.isEmpty())
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("No language specified."), options.outputCodec);
        return ErrorInvalidArguments;
    }

    if (ocr.modelsAction == "install")
    {
        const QStringList missing = manager.getMissingModels(ocr.modelsEngine, ocr.modelsLanguages, ocr.modelsProfile);
        if (missing.isEmpty())
        {
            PDFConsole::writeText(PDFToolTranslationContext::tr("All models of the languages are already installed (profile %1).").arg(profileIdentifier), options.outputCodec);
            return ExitSuccess;
        }

        qint64 totalSize = 0;
        QStringList descriptions;
        for (const QString& modelId : missing)
        {
            const pdf::PDFOCRCatalogEntry* entry = manager.getCatalog().find(modelId);
            if (!entry)
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("The model '%1' is not in the catalog of the application.").arg(modelId), options.outputCodec);
                return ErrorInvalidArguments;
            }
            totalSize += entry->size;
            descriptions << PDFToolTranslationContext::tr("%1 (%2 MB, %3)").arg(modelId).arg(double(entry->size) / (1024.0 * 1024.0), 0, 'f', 1).arg(entry->license);
        }

        if (!ocr.modelsAcceptDownload)
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("The following models would be downloaded (%1 MB):\n%2\nUse the option '--accept-download' to confirm the download.")
                                       .arg(double(totalSize) / (1024.0 * 1024.0), 0, 'f', 1).arg(descriptions.join(QChar('\n'))), options.outputCodec);
            return ErrorInvalidArguments;
        }

        QEventLoop eventLoop;
        QStringList failures;
        int finishedCount = 0;
        QObject::connect(&manager, &pdf::PDFOCRModelManager::downloadFinished, &eventLoop, [&](const QString& modelId, bool success, const QString& message)
        {
            ++finishedCount;
            if (success)
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Installed: %1").arg(modelId), options.outputCodec);
            }
            else
            {
                failures << PDFToolTranslationContext::tr("%1: %2").arg(modelId, message);
            }
        });
        QObject::connect(&manager, &pdf::PDFOCRModelManager::downloadQueueChanged, &eventLoop, [&]()
        {
            if (!manager.isDownloading())
            {
                eventLoop.quit();
            }
        });

        manager.download(missing);
        if (manager.isDownloading())
        {
            eventLoop.exec();
        }

        if (!failures.isEmpty())
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Installation failed:\n%1").arg(failures.join(QChar('\n'))), options.outputCodec);
            return ExitFailure;
        }

        manager.refresh();
        const QStringList stillMissing = manager.getMissingModels(ocr.modelsEngine, ocr.modelsLanguages, ocr.modelsProfile);
        if (!stillMissing.isEmpty())
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("The models were not installed: %1").arg(stillMissing.join(QStringLiteral(", "))), options.outputCodec);
            return ExitFailure;
        }

        PDFConsole::writeText(PDFToolTranslationContext::tr("%n model(s) were installed.", nullptr, finishedCount), options.outputCodec);
        return ExitSuccess;
    }

    // Removal of the downloaded and imported models, never a built-in one (LANG-12)
    QStringList removed;
    for (const QString& language : ocr.modelsLanguages)
    {
        bool found = false;
        for (const pdf::PDFOCRModelInfo& model : manager.getModels())
        {
            if (model.engineId != ocr.modelsEngine || model.language != language || model.profile != ocr.modelsProfile || !model.isUsable())
            {
                continue;
            }

            found = true;
            if (model.origin == pdf::PDFOCRModelOrigin::BuiltIn)
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("The model '%1' is built into the application, it cannot be removed.").arg(model.id), options.outputCodec);
                return ErrorInvalidArguments;
            }

            const pdf::PDFOCRError error = manager.removeUserModel(model.id);
            if (error)
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("The model '%1' was not removed. %2").arg(model.id, error.message), options.outputCodec);
                return ExitFailure;
            }
            removed << model.id;
        }

        if (!found)
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("No installed model of the language '%1' in the profile %2.").arg(language, profileIdentifier), options.outputCodec);
            return ErrorInvalidArguments;
        }
    }

    PDFConsole::writeText(PDFToolTranslationContext::tr("Removed: %1").arg(removed.join(QStringLiteral(", "))), options.outputCodec);
    return ExitSuccess;
}

}   // namespace pdftool
