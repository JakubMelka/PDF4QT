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

#include "pdfdocmdppermissions.h"
#include "pdfdocument.h"
#include "pdfdocumentmerger.h"
#include "pdfdocumenttool.h"

#include <algorithm>

namespace pdf
{

PDFDocMDPPermissions::Level PDFDocMDPPermissions::getLevel(const PDFDocument* document)
{
    if (!document)
    {
        return Level::NotCertified;
    }

    // 12.8.2.2: /Perms /DocMDP of the catalog is the certification signature; a hostile entry of
    // a wrong type (or a reference to nothing) is no certification
    const PDFDictionary* trailer = document->getTrailerDictionary();
    const PDFDictionary* catalog = trailer ? document->getDictionaryFromObject(trailer->get("Root")) : nullptr;
    const PDFDictionary* permissions = catalog ? document->getDictionaryFromObject(catalog->get("Perms")) : nullptr;
    if (!permissions || !permissions->hasKey("DocMDP") || document->getObject(permissions->get("DocMDP")).isNull())
    {
        return Level::NotCertified;
    }

    // Table 255, Table 257: /P of the transform parameters of the signature reference, whose
    // transform method is DocMDP (the first reference without a method, as the old reading did);
    // the default of /P is 2, also for a value of a wrong type or an unknown one
    PDFInteger value = 2;
    if (const PDFDictionary* signature = document->getDictionaryFromObject(permissions->get("DocMDP")))
    {
        const PDFObject& referencesObject = document->getObject(signature->get("Reference"));
        const PDFArray* references = referencesObject.isArray() ? referencesObject.getArray() : nullptr;
        const PDFDictionary* docMDPReference = nullptr;
        for (size_t i = 0; references && i < references->getCount() && !docMDPReference; ++i)
        {
            const PDFDictionary* reference = document->getDictionaryFromObject(references->getItem(i));
            const PDFObject& method = reference ? document->getObject(reference->get("TransformMethod")) : PDFObject();
            if (reference && method.isName() && method.getString() == "DocMDP")
            {
                docMDPReference = reference;
            }
        }
        if (!docMDPReference && references && references->getCount() > 0)
        {
            docMDPReference = document->getDictionaryFromObject(references->getItem(0));
        }

        const PDFDictionary* transformParameters = docMDPReference ? document->getDictionaryFromObject(docMDPReference->get("TransformParams")) : nullptr;
        const PDFObject& levelObject = transformParameters ? document->getObject(transformParameters->get("P")) : PDFObject();
        if (levelObject.isInt())
        {
            value = levelObject.getInteger();
        }
    }

    switch (value)
    {
        case 1:
            return Level::NoChanges;
        case 3:
            return Level::FormFillingAndAnnotations;
        default:
            return Level::FormFilling;
    }
}

bool PDFDocMDPPermissions::isPageChangeAllowed(Level level)
{
    return level == Level::NotCertified;
}

bool PDFDocMDPPermissions::isPageChangeAllowed(const PDFDocument* document)
{
    return isPageChangeAllowed(getLevel(document));
}

PDFDocument PDFDocMDPPermissions::removeCertification(const PDFDocument& document, PDFInteger* signaturesRemoved)
{
    return PDFDocumentMerger::removeSignatures(document, signaturesRemoved);
}

void PDFDocMDPPermissions::addCertificationRemoved(PDFDocumentToolResult* result, PDFInteger signaturesRemoved)
{
    result->data.insert(QStringLiteral("certificationRemoved"), true);
    result->data.insert(QStringLiteral("certificationSignaturesRemoved"), qint64(signaturesRemoved));
    result->data.insert(QStringLiteral("signatureImpact"), QStringLiteral("lost-copy-only"));

    const bool hasFact = std::any_of(result->facts.cbegin(), result->facts.cend(), [](const PDFDocumentToolFact& fact) { return fact.id == QLatin1String("certification"); });
    if (!hasFact)
    {
        result->facts.push_back({ QStringLiteral("certification"), tr("Certification"), tr("Removed: a certified document allows no change of its pages, save the result as a copy") });
    }
}

}   // namespace pdf
