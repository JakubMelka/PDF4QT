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

#ifndef TESTDOCUMENTTOOL_H
#define TESTDOCUMENTTOOL_H

#include "pdfcatalog.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttool.h"
#include "pdfdocumenttoolparameter.h"
#include "pdfexception.h"
#include "pdfoperationcontrol.h"
#include "pdfprogress.h"

#include <QJsonArray>
#include <QThread>

#include <atomic>
#include <functional>
#include <new>
#include <optional>
#include <stdexcept>

namespace pdftest
{

/// Tool of the tests (step 6, package D; reused by MCP in step 7): it has the
/// catalog id review.statistics, counts the selected pages and has one parameter
/// of each type. It is registered only by the tests (never by the applications).
///   mode = count   - the pages of "pages" are counted (Analysis: facts and data)
///   mode = title   - a new version of the document with the title "title" (Changed)
///   mode = new-document - a new document with the title "title" (New; only setResultKindModes)
///   mode = files   - one file of the result: the text "title" (outputs; only setResultKindModes)
///   mode = nothing - NothingToDo
///   mode = fail    - Failed
///   mode = throw, throw-memory, throw-std - throws PDFException / std::bad_alloc /
///                    std::runtime_error from execute and computePreview (the checked run converts it)
/// The cancellation is checked before each page; the progress has a step per page
/// and "repeat" (the pages are counted "repeat" times). setHold keeps execute and
/// computePreview at their start until it is released or the run is canceled. The id
/// of another tool of the catalog gives the test tool the signature impact, the
/// preselected result and the texts of that tool (the result dialog of the shell).
class TestDocumentTool : public pdf::PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdftest::TestDocumentTool)

public:
    static constexpr const char* TOOL_ID = "review.statistics";

    explicit TestDocumentTool(QString id = QString::fromLatin1(TOOL_ID)) : m_id(std::move(id)) { }

    QString getId() const override { return m_id; }

    pdf::PDFDocumentToolParameterDefinitions getParameters() const override
    {
        using Definition = pdf::PDFDocumentToolParameterDefinition;

        pdf::PDFDocumentToolParameterDefinitions definitions;
        definitions.push_back(Definition::createPageSelection(tr, QStringLiteral("pages"), QT_TR_NOOP("Pages"), QT_TR_NOOP("Pages to count")));
        std::vector<pdf::PDFDocumentToolParameterOption> modes = {
            { QStringLiteral("count"), QT_TR_NOOP("Count pages"), QT_TR_NOOP("Counts the selected pages"), QStringLiteral("hash") },
            { QStringLiteral("title"), QT_TR_NOOP("Set title") },
            { QStringLiteral("nothing"), QT_TR_NOOP("Nothing to do") },
            { QStringLiteral("fail"), QT_TR_NOOP("Fail") },
            { QStringLiteral("throw"), QT_TR_NOOP("Throw") },
            { QStringLiteral("throw-memory"), QT_TR_NOOP("Throw bad_alloc") },
            { QStringLiteral("throw-std"), QT_TR_NOOP("Throw std::exception") },
        };
        if (m_hasResultKindModes)
        {
            modes.push_back({ QStringLiteral("new-document"), QT_TR_NOOP("New document") });
            modes.push_back({ QStringLiteral("files"), QT_TR_NOOP("Files") });
        }
        definitions.push_back(Definition::createEnum(tr, QStringLiteral("mode"), QT_TR_NOOP("Mode"), QT_TR_NOOP("What the tool does"), std::move(modes), QStringLiteral("count")));
        definitions.push_back(Definition::createBool(tr, QStringLiteral("detailed"), QT_TR_NOOP("Detailed"), QT_TR_NOOP("List the page numbers"), false));
        definitions.push_back(Definition::createInteger(tr, QStringLiteral("repeat"), QT_TR_NOOP("Repeat"), QT_TR_NOOP("Number of passes"), 1, 1, 100));
        definitions.push_back(Definition::createReal(tr, QStringLiteral("scale"), QT_TR_NOOP("Scale"), nullptr, 1.0, 0.25, 4.0));
        definitions.push_back(Definition::createString(tr, QStringLiteral("title"), QT_TR_NOOP("Title"), QT_TR_NOOP("Title of the document"), QStringLiteral("Statistics"), 200));
        definitions.push_back(Definition::createColor(tr, QStringLiteral("color"), QT_TR_NOOP("Color"), nullptr, QColor(0x25, 0x63, 0xEB)));
        definitions.push_back(Definition::createFilePath(tr, QStringLiteral("reportFile"), QT_TR_NOOP("Report file"), nullptr));
        definitions.push_back(Definition::createFolderPath(tr, QStringLiteral("outputFolder"), QT_TR_NOOP("Output folder"), nullptr));
        definitions.push_back(Definition::createFileNameTemplate(tr, QStringLiteral("fileName"), QT_TR_NOOP("File name"), nullptr, QStringLiteral("{name}-statistics.txt")));
        definitions.push_back(Definition::createSecret(tr, QStringLiteral("password"), QT_TR_NOOP("Password"), nullptr));
        return definitions;
    }

    pdf::PDFDocumentToolResult execute(const pdf::PDFDocumentToolContext& context,
                                       const QVariantMap& parameters,
                                       const pdf::PDFOperationControl* control,
                                       pdf::PDFProgress* progress) const override
    {
        ++m_executeCount;
        if (m_executeCallback)
        {
            m_executeCallback();
        }
        if (!waitWhileHeld(control))
        {
            return pdf::PDFDocumentToolResult::createStopped();
        }

        return compute(context, parameters, control, progress);
    }

    bool hasPreview() const override { return m_hasPreview; }
    bool isPreviewFinal() const override { return m_isPreviewFinal; }

    pdf::PDFDocumentToolResult computePreview(const pdf::PDFDocumentToolContext& context, const QVariantMap& parameters, const pdf::PDFOperationControl* control, pdf::PDFProgress* progress) const override
    {
        ++m_previewCount;
        if (!waitWhileHeld(control))
        {
            return pdf::PDFDocumentToolResult::createStopped();
        }

        if (m_isPreviewFinal)
        {
            return compute(context, parameters, control, progress);
        }

        Q_UNUSED(control);
        Q_UNUSED(progress);
        throwByMode(parameters.value(QStringLiteral("mode")).toString());

        pdf::PDFDocumentToolResult result;
        const pdf::PDFDocument* document = context.getDocument();
        result.facts.push_back({ QStringLiteral("pages"), QStringLiteral("Pages"), QString::number(document ? document->getCatalog()->getPageCount() : 0) });
        return result;
    }

    /// Number of the calls of execute (the checked run does not call it for invalid input)
    int getExecuteCount() const { return m_executeCount.load(); }

    /// Number of the calls of computePreview
    int getPreviewCount() const { return m_previewCount.load(); }

    /// The worker waits at the start of execute and computePreview (until a cancellation)
    void setHold(bool hold) { m_hold.store(hold); }

    /// Number of the workers waiting at the moment
    int getHeldCount() const { return m_heldCount.load(); }

    void setPreview(bool hasPreview, bool isPreviewFinal) { m_hasPreview = hasPreview; m_isPreviewFinal = isPreviewFinal; }

    /// The modes of a new document and of a file of the result are offered (the shell of
    /// the tools; the schema of the tool in the other tests stays as it is)
    void setResultKindModes(bool hasResultKindModes) { m_hasResultKindModes = hasResultKindModes; }

    /// The files of the mode "files" (empty = one text file of the parameter "title")
    void setOutputFiles(std::vector<pdf::PDFDocumentToolOutputFile> files) { m_outputFiles = std::move(files); }

    /// Called at the start of execute in the thread of the run (a change of the document
    /// under the run); set before the first run
    void setExecuteCallback(std::function<void()> callback) { m_executeCallback = std::move(callback); }

private:
    bool waitWhileHeld(const pdf::PDFOperationControl* control) const
    {
        ++m_heldCount;
        while (m_hold.load())
        {
            if (pdf::PDFOperationControl::isOperationCancelled(control))
            {
                --m_heldCount;
                return false;
            }
            QThread::msleep(1);
        }
        --m_heldCount;
        return true;
    }

    pdf::PDFDocumentToolResult compute(const pdf::PDFDocumentToolContext& context, const QVariantMap& parameters, const pdf::PDFOperationControl* control, pdf::PDFProgress* progress) const
    {
        const QString mode = parameters.value(QStringLiteral("mode")).toString();
        if (mode == QLatin1String("nothing"))
        {
            return pdf::PDFDocumentToolResult::createNothingToDo(QStringLiteral("Nothing to count"));
        }
        if (mode == QLatin1String("fail"))
        {
            return pdf::PDFDocumentToolResult::createFailed(QStringLiteral("Failed on purpose"));
        }
        throwByMode(mode);

        const pdf::PDFDocument* document = context.getDocument();
        if (!document)
        {
            return pdf::PDFDocumentToolResult::createFailed(QStringLiteral("No document"));
        }

        if (mode == QLatin1String("title") || mode == QLatin1String("new-document"))
        {
            pdf::PDFDocumentBuilder builder(document);
            builder.setDocumentTitle(parameters.value(QStringLiteral("title")).toString());

            pdf::PDFDocumentToolOutputDocument output;
            output.kind = mode == QLatin1String("title") ? pdf::PDFDocumentToolOutputDocument::Kind::Changed : pdf::PDFDocumentToolOutputDocument::Kind::New;
            output.inputIndex = 0;
            output.document = pdf::PDFDocumentPointer(new pdf::PDFDocument(builder.build()));
            output.flags = pdf::PDFModifiedDocument::Reset;
            output.suggestedFileName = QStringLiteral("titled.pdf");

            pdf::PDFDocumentToolResult result;
            result.documents.push_back(output);
            result.facts.push_back({ QStringLiteral("title"), QStringLiteral("Title"), parameters.value(QStringLiteral("title")).toString() });
            return result;
        }

        if (mode == QLatin1String("files"))
        {
            pdf::PDFDocumentToolOutputFile output;
            output.suggestedFileName = QStringLiteral("extracted.txt");
            output.data = parameters.value(QStringLiteral("title")).toString().toUtf8();
            output.mimeType = QStringLiteral("text/plain");

            pdf::PDFDocumentToolResult result;
            if (m_outputFiles.empty())
            {
                result.outputs.push_back(output);
            }
            else
            {
                result.outputs = m_outputFiles;
            }
            result.facts.push_back({ QStringLiteral("size"), QStringLiteral("Size"), QString::number(result.outputs.front().data.size()) });
            return result;
        }

        QString errorMessage;
        const std::optional<pdf::PDFDocumentToolPageSelection> selection = pdf::PDFDocumentToolPageSelection::fromVariant(parameters.value(QStringLiteral("pages")), &errorMessage);
        const std::vector<pdf::PDFInteger> pages = selection ? selection->getPageIndices(document, &errorMessage) : std::vector<pdf::PDFInteger>();
        if (pages.empty())
        {
            return pdf::PDFDocumentToolResult::createFailed(errorMessage);
        }

        const qint64 repeat = parameters.value(QStringLiteral("repeat")).toLongLong();
        if (progress)
        {
            progress->start(size_t(pages.size()) * size_t(repeat), pdf::ProgressStartupInfo());
        }

        qint64 counted = 0;
        for (qint64 pass = 0; pass < repeat; ++pass)
        {
            for (size_t i = 0; i < pages.size(); ++i)
            {
                if (pdf::PDFOperationControl::isOperationCancelled(control))
                {
                    return pdf::PDFDocumentToolResult::createStopped();
                }

                ++counted;
                if (progress)
                {
                    progress->step();
                }
            }
        }

        if (progress)
        {
            progress->finish();
        }

        pdf::PDFDocumentToolResult result;
        result.message = QStringLiteral("%1 pages counted").arg(counted);
        result.facts.push_back({ QStringLiteral("pages"), QStringLiteral("Pages"), QString::number(pages.size()) });
        result.steps.push_back({ QString(), QStringLiteral("Pages counted"), pdf::PDFDocumentToolStep::State::Done });
        result.data.insert(QStringLiteral("pageCount"), qint64(document->getCatalog()->getPageCount()));
        result.data.insert(QStringLiteral("selectedPages"), qint64(pages.size()));
        result.data.insert(QStringLiteral("counted"), counted);
        if (parameters.value(QStringLiteral("detailed")).toBool())
        {
            QJsonArray numbers;
            for (const pdf::PDFInteger page : pages)
            {
                numbers.append(qint64(page + 1));
            }
            result.data.insert(QStringLiteral("pages"), numbers);
        }
        return result;
    }

    /// Modes throw, throw-memory and throw-std throw the exception
    static void throwByMode(const QString& mode)
    {
        if (mode == QLatin1String("throw"))
        {
            throw pdf::PDFException(QStringLiteral("Thrown on purpose"));
        }
        if (mode == QLatin1String("throw-memory"))
        {
            throw std::bad_alloc();
        }
        if (mode == QLatin1String("throw-std"))
        {
            throw std::runtime_error("thrown on purpose");
        }
    }

    QString m_id;
    bool m_hasPreview = true;
    bool m_isPreviewFinal = false;
    bool m_hasResultKindModes = false;
    std::vector<pdf::PDFDocumentToolOutputFile> m_outputFiles;
    std::function<void()> m_executeCallback;
    std::atomic<bool> m_hold = false;
    mutable std::atomic<int> m_heldCount = 0;
    mutable std::atomic<int> m_previewCount = 0;
    mutable std::atomic<int> m_executeCount = 0;
};

/// Operation control, which cancels the operation at its n-th question (thread safe)
class TestOperationControl : public pdf::PDFOperationControl
{
public:
    explicit TestOperationControl(int cancelAt) : m_cancelAt(cancelAt) { }

    bool isOperationCancelled() const override
    {
        return m_cancelAt >= 0 && ++m_questions > m_cancelAt;
    }

    int getQuestionCount() const { return m_questions.load(); }

private:
    int m_cancelAt = -1;
    mutable std::atomic<int> m_questions = 0;
};

}   // namespace pdftest

#endif // TESTDOCUMENTTOOL_H
