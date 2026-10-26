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

#include <atomic>
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
///   mode = nothing - NothingToDo
///   mode = fail    - Failed
///   mode = throw, throw-memory, throw-std - throws PDFException / std::bad_alloc /
///                    std::runtime_error from execute and computePreview (the checked run converts it)
/// The cancellation is checked before each page; the progress has a step per page
/// and "repeat" (the pages are counted "repeat" times).
class TestDocumentTool : public pdf::PDFDocumentTool
{
public:
    static constexpr const char* TOOL_ID = "review.statistics";

    QString getId() const override { return QString::fromLatin1(TOOL_ID); }

    pdf::PDFDocumentToolParameterDefinitions getParameters() const override
    {
        using Definition = pdf::PDFDocumentToolParameterDefinition;

        pdf::PDFDocumentToolParameterDefinitions definitions;
        definitions.push_back(Definition::createPageSelection(QStringLiteral("pages"), QStringLiteral("Pages"), QStringLiteral("Pages to count")));
        definitions.push_back(Definition::createEnum(QStringLiteral("mode"), QStringLiteral("Mode"), QStringLiteral("What the tool does"),
                                                     {
                                                         { QStringLiteral("count"), QStringLiteral("Count pages"), QStringLiteral("Counts the selected pages"), QStringLiteral("hash") },
                                                         { QStringLiteral("title"), QStringLiteral("Set title"), QString(), QString() },
                                                         { QStringLiteral("nothing"), QStringLiteral("Nothing to do"), QString(), QString() },
                                                         { QStringLiteral("fail"), QStringLiteral("Fail"), QString(), QString() },
                                                         { QStringLiteral("throw"), QStringLiteral("Throw"), QString(), QString() },
                                                         { QStringLiteral("throw-memory"), QStringLiteral("Throw bad_alloc"), QString(), QString() },
                                                         { QStringLiteral("throw-std"), QStringLiteral("Throw std::exception"), QString(), QString() },
                                                     },
                                                     QStringLiteral("count")));
        definitions.push_back(Definition::createBool(QStringLiteral("detailed"), QStringLiteral("Detailed"), QStringLiteral("List the page numbers"), false));
        definitions.push_back(Definition::createInteger(QStringLiteral("repeat"), QStringLiteral("Repeat"), QStringLiteral("Number of passes"), 1, 1, 100));
        definitions.push_back(Definition::createReal(QStringLiteral("scale"), QStringLiteral("Scale"), QString(), 1.0, 0.25, 4.0));
        definitions.push_back(Definition::createString(QStringLiteral("title"), QStringLiteral("Title"), QStringLiteral("Title of the document"), QStringLiteral("Statistics"), 200));
        definitions.push_back(Definition::createColor(QStringLiteral("color"), QStringLiteral("Color"), QString(), QColor(0x25, 0x63, 0xEB)));
        definitions.push_back(Definition::createFilePath(QStringLiteral("reportFile"), QStringLiteral("Report file"), QString()));
        definitions.push_back(Definition::createFolderPath(QStringLiteral("outputFolder"), QStringLiteral("Output folder"), QString()));
        definitions.push_back(Definition::createFileNameTemplate(QStringLiteral("fileName"), QStringLiteral("File name"), QString(), QStringLiteral("{name}-statistics.txt")));
        definitions.push_back(Definition::createSecret(QStringLiteral("password"), QStringLiteral("Password"), QString()));
        return definitions;
    }

    pdf::PDFDocumentToolResult execute(const pdf::PDFDocumentToolContext& context,
                                       const QVariantMap& parameters,
                                       const pdf::PDFOperationControl* control,
                                       pdf::PDFProgress* progress) const override
    {
        ++m_executeCount;

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

        if (mode == QLatin1String("title"))
        {
            pdf::PDFDocumentBuilder builder(document);
            builder.setDocumentTitle(parameters.value(QStringLiteral("title")).toString());

            pdf::PDFDocumentToolOutputDocument output;
            output.kind = pdf::PDFDocumentToolOutputDocument::Kind::Changed;
            output.inputIndex = 0;
            output.document = pdf::PDFDocumentPointer(new pdf::PDFDocument(builder.build()));
            output.flags = pdf::PDFModifiedDocument::Reset;
            output.suggestedFileName = QStringLiteral("titled.pdf");

            pdf::PDFDocumentToolResult result;
            result.documents.push_back(output);
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

    bool hasPreview() const override { return true; }

    pdf::PDFDocumentToolResult computePreview(const pdf::PDFDocumentToolContext& context, const QVariantMap& parameters, const pdf::PDFOperationControl* control) const override
    {
        Q_UNUSED(control);
        throwByMode(parameters.value(QStringLiteral("mode")).toString());

        pdf::PDFDocumentToolResult result;
        const pdf::PDFDocument* document = context.getDocument();
        result.facts.push_back({ QStringLiteral("pages"), QStringLiteral("Pages"), QString::number(document ? document->getCatalog()->getPageCount() : 0) });
        return result;
    }

    /// Number of the calls of execute (the checked run does not call it for invalid input)
    int getExecuteCount() const { return m_executeCount.load(); }

private:
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
