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
#include "pdfobjectutils.h"

#include <algorithm>

namespace pdf
{

PDFDocMDPPermissions::Level PDFDocMDPPermissions::getLevel(const PDFDocument* document)
{
    // RB5 point 3 of step 3b: a signed field with /Lock /P restricts the document as DocMDP does
    return getStricterLevel(getCertificationLevel(document), getFieldLockLevel(document));
}

PDFDocMDPPermissions::Level PDFDocMDPPermissions::getStricterLevel(Level first, Level second)
{
    if (first == Level::NotCertified)
    {
        return second;
    }
    if (second == Level::NotCertified)
    {
        return first;
    }
    return std::min(first, second);
}

PDFDocMDPPermissions::Level PDFDocMDPPermissions::getFieldLockLevel(const PDFDocument* document)
{
    if (!document)
    {
        return Level::NotCertified;
    }

    // 12.7.2, Table 224: the fields of the document are the trees of /AcroForm /Fields
    const PDFDictionary* trailer = document->getTrailerDictionary();
    const PDFDictionary* catalog = trailer ? document->getDictionaryFromObject(trailer->get("Root")) : nullptr;
    const PDFDictionary* form = catalog ? document->getDictionaryFromObject(catalog->get("AcroForm")) : nullptr;
    const PDFObject& fieldsObject = form ? document->getObject(form->get("Fields")) : PDFObject();
    if (!fieldsObject.isArray())
    {
        return Level::NotCertified;
    }

    Level level = Level::NotCertified;
    PDFObjectWalk walk(MAXIMUM_FIELD_DEPTH);
    const PDFArray* fields = fieldsObject.getArray();
    for (size_t i = 0; i < fields->getCount(); ++i)
    {
        collectFieldLocks(document, fields->getItem(i), QByteArray(), false, 0, &walk, &level);
    }
    return level;
}

void PDFDocMDPPermissions::collectFieldLocks(const PDFDocument* document, const PDFObject& field, const QByteArray& inheritedType, bool isInheritedSigned,
                                             int depth, PDFObjectWalk* walk, Level* level)
{
    // One walk of the whole tree: a cycle of /Kids or a field shared by two parents is read once
    if (!walk->enter(field, depth))
    {
        return;
    }
    const PDFDictionary* dictionary = document->getDictionaryFromObject(field);
    if (!dictionary)
    {
        return;
    }

    // /FT and /V are inheritable (Table 226); a signature field is signed by its value, the
    // signature dictionary (12.7.5.5 /V, Table 255)
    const PDFObject& typeObject = document->getObject(dictionary->get("FT"));
    const QByteArray type = typeObject.isName() ? typeObject.getString() : inheritedType;
    const bool hasValue = dictionary->hasKey("V");
    const bool isSigned = hasValue ? document->getDictionaryFromObject(dictionary->get("V")) != nullptr : isInheritedSigned;

    // 12.7.5.5, the lock dictionary of a signature field: /P (PDF 2.0) has the values of DocMDP and
    // no default - without it the lock changes no permission of the document; it applies, when the
    // field is signed. A /P of a wrong type or an unknown value counts as level 2, as DocMDP does.
    if (type == "Sig" && isSigned)
    {
        const PDFDictionary* lock = document->getDictionaryFromObject(dictionary->get("Lock"));
        if (lock && lock->hasKey("P") && !document->getObject(lock->get("P")).isNull())
        {
            const PDFObject& permissionObject = document->getObject(lock->get("P"));
            Level lockLevel = Level::FormFilling;
            if (permissionObject.isInt())
            {
                switch (permissionObject.getInteger())
                {
                    case 1:
                        lockLevel = Level::NoChanges;
                        break;
                    case 3:
                        lockLevel = Level::FormFillingAndAnnotations;
                        break;
                    default:
                        break;
                }
            }
            *level = getStricterLevel(*level, lockLevel);
        }
    }

    const PDFObject& kidsObject = document->getObject(dictionary->get("Kids"));
    if (kidsObject.isArray())
    {
        const PDFArray* kids = kidsObject.getArray();
        for (size_t i = 0; i < kids->getCount(); ++i)
        {
            collectFieldLocks(document, kids->getItem(i), type, isSigned, depth + 1, walk, level);
        }
    }
}

PDFDocMDPPermissions::Level PDFDocMDPPermissions::getCertificationLevel(const PDFDocument* document)
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

bool PDFDocMDPPermissions::isCertificationRemovedBy(const PDFDocument* document, const PDFDocumentToolOutputTraits& tool)
{
    // Table 257: no level allows a change of the pages; a tool, which leaves the pages of the
    // open document as they are (a new document, files, an analysis, a rewrite of the file),
    // keeps the document certified
    return document && tool.isPageChange && !isPageChangeAllowed(document);
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
