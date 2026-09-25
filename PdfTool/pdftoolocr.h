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

#ifndef PDFTOOLOCR_H
#define PDFTOOLOCR_H

#include "pdftoolabstractapplication.h"

namespace pdf
{
class PDFOCRModelManager;
}

namespace pdftool
{

/// Command line recognition of the text of scanned documents (phase 7 of OCR_PLAN.md).
/// A single document, or a batch of documents, is recognized by PDFOCRDocumentRunner,
/// the invisible text layer is written into a copy of the document, the text can be
/// exported into text, hOCR, ALTO and TSV files and the results can be saved as a
/// project of the OCR dialog of the editor. The language models are never downloaded
/// by this command (see the command 'ocr-models').
class PDFToolOCR : public PDFToolAbstractApplication
{
public:
    virtual QString getStandardString(StandardString standardString) const override;
    virtual int execute(const PDFToolOptions& options) override;
    virtual Options getOptionsFlags() const override;

    /// Adds the options of the command to the parser
    static void initializeCommandLineParser(QCommandLineParser* parser);

    /// Reads the options of the command. An invalid value is reported by
    /// PDFToolOCROptions::invalidArgument, never replaced by a default.
    static void readOptions(QCommandLineParser* parser, PDFToolOCROptions& options);

private:
    /// Files of the exports of a single document
    struct ExportTargets
    {
        QString text;
        QString hocr;
        QString alto;
        QString tsv;

        bool isEmpty() const { return text.isEmpty() && hocr.isEmpty() && alto.isEmpty() && tsv.isEmpty(); }
        QStringList getFiles() const;
    };

    /// Outcome of a single document
    struct DocumentReport
    {
        enum class Status
        {
            Success,
            Skipped,
            Failed
        };

        QString input;
        QString output;
        Status status = Status::Failed;
        QString message;
        int pageCount = 0;
        int resultPages = 0;
        int failedPages = 0;
        int wordCount = 0;
        int reviewWords = 0;
        qint64 elapsedMilliseconds = 0;
    };

    /// Recognizes a single document. Returns the exit code of the document.
    int processDocument(const PDFToolOptions& options,
                        pdf::PDFOCRModelManager* modelManager,
                        const QString& inputFile,
                        const QString& outputFile,
                        const ExportTargets& exports,
                        bool printPages,
                        DocumentReport* report);

    /// Creates the configuration: the configuration of the project (or the default one)
    /// changed by the options. Returns false and fills the error, if it is not complete.
    static bool createConfiguration(const PDFToolOptions& options, const pdf::PDFOCRProject* project, pdf::PDFOCRConfiguration* configuration, QString* errorMessage);

    /// Reads the lines of a UTF-8 text file (empty lines are skipped)
    static bool readLines(const QString& fileName, QStringList* lines, QString* errorMessage);

    /// Returns the files of the batch (a directory or a file mask), sorted
    static QStringList getBatchFiles(const QString& batch, QString* errorMessage);

    /// Returns true, if both paths refer to the same file
    static bool isSameFile(const QString& first, const QString& second);

    int executeBatch(const PDFToolOptions& options, pdf::PDFOCRModelManager* modelManager);
};

/// Management of the language models of the OCR from the command line: list, install
/// (only with an explicit consent to the download, https and SHA-256 verified), remove
class PDFToolOCRModels : public PDFToolAbstractApplication
{
public:
    virtual QString getStandardString(StandardString standardString) const override;
    virtual int execute(const PDFToolOptions& options) override;
    virtual Options getOptionsFlags() const override;

    static void initializeCommandLineParser(QCommandLineParser* parser);
    static void readOptions(QCommandLineParser* parser, PDFToolOCROptions& options);
};

}   // namespace pdftool

#endif // PDFTOOLOCR_H
