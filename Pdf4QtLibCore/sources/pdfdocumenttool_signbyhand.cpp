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

#include "pdfdocumenttool_signbyhand.h"
#include "pdfdocmdppermissions.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentpageoperations.h"
#include "pdfform.h"
#include "pdfoperationcontrol.h"
#include "pdfpagedrawing.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

namespace pdf
{

/// Helpers of Sign by Hand
class PDFDocumentToolSignByHandHelper
{
public:
    PDFDocumentToolSignByHandHelper() = delete;

    using Tool = PDFDocumentToolSignByHand;

    /// The drawing of the run: the JSON of the parameter elements (MCP), else the value of the
    /// parameter drawing (the panels); \p errorMessage gets the reason of a JSON, which is not valid
    static std::optional<PDFPageDrawing> getDrawing(const QVariantMap& parameters, QString* errorMessage)
    {
        const QString elements = parameters.value(QString::fromLatin1(Tool::PARAMETER_ELEMENTS)).toString();
        if (!elements.isEmpty())
        {
            QJsonParseError parseError;
            const QJsonDocument json = QJsonDocument::fromJson(elements.toUtf8(), &parseError);
            if (parseError.error != QJsonParseError::NoError || !json.isArray())
            {
                *errorMessage = Tool::tr("The elements are not a JSON array.");
                return std::nullopt;
            }
            return PDFPageDrawing::fromJson(json.array(), errorMessage);
        }

        const QVariant value = parameters.value(QString::fromLatin1(Tool::PARAMETER_DRAWING));
        return value.canConvert<PDFPageDrawing>() ? value.value<PDFPageDrawing>() : PDFPageDrawing();
    }
};

QString PDFDocumentToolSignByHand::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolSignByHand::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;

    PDFDocumentToolParameterDefinitions definitions;
    definitions.push_back(Definition::createValue(tr, QString::fromLatin1(PARAMETER_DRAWING), QT_TR_NOOP("Drawing"), QT_TR_NOOP("The drawing on the pages"),
                                                  QVariant::fromValue(PDFPageDrawing())).setSensitive(true));
    definitions.push_back(Definition::createString(tr, QString::fromLatin1(PARAMETER_ELEMENTS), QT_TR_NOOP("Elements"),
                                                   QT_TR_NOOP("JSON array of the elements of the drawing: text, freehand-curve, line, rectangle, rounded-rectangle, dot, accept-mark, reject-mark; no images"),
                                                   QString()).setStructuredData(true).setMaximumLength(MAXIMUM_ELEMENTS_LENGTH).setSensitive(true));
    return definitions;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolSignByHand::checkGivenParameters(const QVariantMap& given) const
{
    // An assistant gives the elements (Q6.7); the drawing of the panels is never a parameter of MCP
    std::vector<PDFDocumentToolParameterError> errors = PDFDocumentTool::checkGivenParameters(given);
    const QString drawingParameter = QString::fromLatin1(PARAMETER_DRAWING);
    const QString elementsParameter = QString::fromLatin1(PARAMETER_ELEMENTS);
    const bool hasElements = !given.value(elementsParameter).toString().isEmpty();
    if (given.contains(drawingParameter) && hasElements)
    {
        errors.push_back({ elementsParameter, tr("Give either the elements or the drawing, not both.") });
    }
    else if (!given.contains(drawingParameter) && !hasElements)
    {
        errors.push_back({ elementsParameter, tr("Give the elements of the drawing.") });
    }
    else if (hasElements)
    {
        QString errorMessage;
        if (!PDFDocumentToolSignByHandHelper::getDrawing(given, &errorMessage))
        {
            errors.push_back({ elementsParameter, errorMessage });
        }
    }
    return errors;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolSignByHand::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    std::vector<PDFDocumentToolParameterError> errors = PDFDocumentTool::validate(context, parameters);
    if (!errors.empty())
    {
        return errors;
    }

    const PDFDocument* document = context.getDocument();
    if (document->getCatalog()->getPageCount() == 0)
    {
        errors.push_back({ QString(), tr("The document has no pages.") });
        return errors;
    }

    const bool hasElements = !parameters.value(QString::fromLatin1(PARAMETER_ELEMENTS)).toString().isEmpty();
    const QString parameter = QString::fromLatin1(hasElements ? PARAMETER_ELEMENTS : PARAMETER_DRAWING);
    QString errorMessage;
    const std::optional<PDFPageDrawing> drawing = PDFDocumentToolSignByHandHelper::getDrawing(parameters, &errorMessage);
    if (!drawing)
    {
        errors.push_back({ parameter, errorMessage });
    }
    else if (drawing->isEmpty())
    {
        // 435:8908: the confirmation is Disabled until something is drawn
        errors.push_back({ parameter, tr("Draw your signature.") });
    }
    else if (!drawing->isValid(document, &errorMessage))
    {
        errors.push_back({ parameter, errorMessage });
    }
    return errors;
}

PDFDocumentToolResult PDFDocumentToolSignByHand::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    Q_UNUSED(progress);

    const PDFDocumentToolInputDocument& input = context.documents.front();
    QString errorMessage;
    const std::optional<PDFPageDrawing> drawing = PDFDocumentToolSignByHandHelper::getDrawing(parameters, &errorMessage);
    if (!drawing || drawing->isEmpty())
    {
        return PDFDocumentToolResult::createFailed(errorMessage.isEmpty() ? tr("Draw your signature.") : errorMessage);
    }

    if (PDFOperationControl::isOperationCanceled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    // 5.5: the drawing goes into the content of the pages - no annotation, no field, no signature
    PDFDocumentBuilder builder(input.document.data());
    if (!drawing->appendToPages(&builder, &errorMessage))
    {
        return PDFDocumentToolResult::createFailed(errorMessage);
    }
    PDFDocument document = builder.build();

    // 4.2, Table 257: no level of a certification allows a change of the content of a page - the
    // certification is removed, the result is only a copy (isCertificationRemovedBy, owner 13. 11.)
    const PDFDocumentToolOutputTraits traits = getOutputTraits(parameters);
    const bool isCertificationRemoved = PDFDocMDPPermissions::isCertificationRemovedBy(input.document.data(), traits);
    PDFInteger certificationSignaturesRemoved = 0;
    if (isCertificationRemoved)
    {
        document = PDFDocMDPPermissions::removeCertification(document, &certificationSignaturesRemoved);
    }

    PDFDocumentToolResult result;
    PDFDocumentToolOutputDocument output;
    output.kind = PDFDocumentToolOutputDocument::Kind::Changed;
    output.inputIndex = 0;
    output.document = PDFDocumentPointer(new PDFDocument(std::move(document)));
    output.flags = PDFModifiedDocument::ModificationFlags(PDFModifiedDocument::Reset);
    const QString baseName = QFileInfo(input.fileName).completeBaseName();
    output.suggestedFileName = (baseName.isEmpty() ? QStringLiteral("document") : baseName) + QStringLiteral("_signed_by_hand.pdf");
    result.documents.push_back(std::move(output));

    // The facts of 435:10812 ("Elements: 4 on pages 3 and 308", "Kind: Drawing – not a digital ID
    // signature") and the data of MCP; the pages by the one text of the page lists
    const std::vector<PDFInteger> pageIndices = drawing->getPageIndices();
    const int elementCount = int(drawing->getElements().size());
    QString elementsText;
    if (pageIndices.size() == 1)
    {
        elementsText = tr("%Ln on page %1", "elements on a page", elementCount).arg(pageIndices.front() + 1);
    }
    else if (pageIndices.size() == 2)
    {
        elementsText = tr("%Ln on pages %1 and %2", "elements on two pages", elementCount).arg(pageIndices.front() + 1).arg(pageIndices.back() + 1);
    }
    else
    {
        elementsText = tr("%Ln on pages %1", "elements on more pages", elementCount).arg(PDFDocumentPageOperations::formatPageNumbers(pageIndices));
    }
    result.facts.push_back({ QString::fromLatin1(FACT_DRAWN), tr("Elements"), elementsText });
    result.facts.push_back({ QString::fromLatin1(FACT_KIND), tr("Kind"), tr("Drawing – not a digital ID signature") });
    result.data.insert(QString::fromLatin1(DATA_ELEMENT_COUNT), elementCount);
    QJsonArray pages;
    for (const PDFInteger pageIndex : pageIndices)
    {
        pages.append(qint64(pageIndex + 1));
    }
    result.data.insert(QString::fromLatin1(DATA_PAGES), pages);

    // Q18 of step 3: the declarations stay, the result says so; the drawing is no tagged content
    const QStringList declarations = getConformanceDeclarations(input.document.data());
    if (!declarations.isEmpty())
    {
        const QString text = declarations.contains(QStringLiteral("PDF/UA")) ? tr("The drawing is not tagged; %1 is still declared").arg(declarations.join(QStringLiteral(", ")))
                                                                            : tr("%1 is still declared, the drawing is not checked against it").arg(declarations.join(QStringLiteral(", ")));
        result.facts.push_back({ QString::fromLatin1(FACT_CONFORMANCE), tr("Conformance"), text });
        result.data.insert(QStringLiteral("conformanceKept"), QJsonArray::fromStringList(declarations));
    }

    result.data.insert(QStringLiteral("signatureCount"), PDFForm::getSignatureCount(input.document.data()));
    result.data.insert(QStringLiteral("signatureImpact"), QStringLiteral("incremental-save"));
    result.data.insert(QStringLiteral("certified"), isCertificationRemoved);
    if (isCertificationRemoved)
    {
        PDFDocMDPPermissions::addCertificationRemoved(&result, certificationSignaturesRemoved);
    }
    return result;
}

}   // namespace pdf
