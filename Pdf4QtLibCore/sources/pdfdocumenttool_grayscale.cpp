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

#include "pdfdocumenttool_grayscale.h"
#include "pdfdocumenttool_extractimages.h"

#include "pdfdbgheap.h"

namespace pdf
{

QString PDFDocumentToolGrayscale::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolGrayscale::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;

    PDFDocumentToolParameterDefinitions definitions;
    addOptionDefinitions(&definitions, Purpose::Grayscale);

    // 308:6911: Target DPI 0 = the resolution unchanged, JPEG 85
    addProfileDefinitions(&definitions, nullptr, 0);

    // The excluded images belong to one document (R4-20: "exclude" in the core and in MCP)
    definitions.push_back(Definition::createString(tr, QString::fromLatin1(PARAMETER_EXCLUDE), QT_TR_NOOP("Excluded images"),
                                                   QT_TR_NOOP("Images, which keep their colors, separated by commas: the ids of document.list_images (\"148 0\"); empty = every color image is converted"),
                                                   QString(), MAXIMUM_IMAGES_LENGTH).setStored(false));
    return definitions;
}

PDFImageOptimizer::Settings PDFDocumentToolGrayscale::createGrayscaleSettings(const QVariantMap& parameters)
{
    // The options and the profile without a prefix are read as the gray profile of Optimize Images
    QVariantMap optimizeParameters = parameters;
    optimizeParameters.insert(QString::fromLatin1(PARAMETER_COLOR_MODE), QString::fromLatin1(COLOR_GRAYSCALE));
    for (const char* field : { FIELD_ALGORITHM, FIELD_TARGET_DPI, FIELD_RESAMPLE, FIELD_JPEG_QUALITY, FIELD_JPEG2000_RATE, FIELD_PNG_PREDICTOR })
    {
        const QString name = getProfileParameterName(nullptr, field);
        if (parameters.contains(name))
        {
            for (const char* profile : { PROFILE_COLOR, PROFILE_GRAY, PROFILE_BITONAL })
            {
                optimizeParameters.insert(getProfileParameterName(profile, field), parameters.value(name));
            }
        }
    }

    PDFImageOptimizer::Settings settings = createSettings(optimizeParameters);
    settings.autoMode = true;
    settings.colorMode = PDFImageOptimizer::ColorMode::Grayscale;
    settings.onlyColorImages = true;
    return settings;
}

std::vector<PDFDocumentToolOptimizeImages::ImageException> PDFDocumentToolGrayscale::parseExclude(const QString& text, QString* errorMessage)
{
    // The one syntax of a list of ids (Extract Images "exclude" too)
    std::vector<ImageException> exceptions;
    for (const PDFDocumentImageList::Key& key : PDFDocumentToolExtractImages::parseKeys(text, errorMessage))
    {
        ImageException exception;
        exception.key = key;
        exception.enabled = false;
        exceptions.push_back(std::move(exception));
    }
    return exceptions;
}

QString PDFDocumentToolGrayscale::writeExclude(const std::vector<PDFDocumentImageList::Key>& keys)
{
    return PDFDocumentToolExtractImages::writeKeys(keys);
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolGrayscale::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    std::vector<PDFDocumentToolParameterError> errors = PDFDocumentTool::validate(context, parameters);
    if (!errors.empty())
    {
        return errors;
    }

    const QString name = QString::fromLatin1(PARAMETER_EXCLUDE);
    QString errorMessage;
    const std::vector<ImageException> exceptions = parseExclude(parameters.value(name).toString(), &errorMessage);
    if (errorMessage.isEmpty())
    {
        checkImageExceptions(context.getDocument(), exceptions, &errorMessage);
    }
    if (!errorMessage.isEmpty())
    {
        errors.push_back({ name, errorMessage });
    }
    return errors;
}

PDFDocumentToolResult PDFDocumentToolGrayscale::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    QString errorMessage;
    const std::vector<ImageException> exceptions = parseExclude(parameters.value(QString::fromLatin1(PARAMETER_EXCLUDE)).toString(), &errorMessage);
    if (!errorMessage.isEmpty())
    {
        return PDFDocumentToolResult::createFailed(errorMessage);
    }
    return runOptimization(context, createGrayscaleSettings(parameters), exceptions, Purpose::Grayscale, control, progress);
}

}   // namespace pdf
