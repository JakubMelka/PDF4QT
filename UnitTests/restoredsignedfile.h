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

#ifndef RESTOREDSIGNEDFILE_H
#define RESTOREDSIGNEDFILE_H

#include "pdfcertificatemanager.h"
#include "pdfcertificatestore.h"

#include <QByteArray>
#include <QMap>
#include <QString>

namespace pdftest
{

/// A forgery of a signed document, which is not a change of it. The attacker prepares a file, whose cross-reference
/// table (ISO 32000-2, 7.5.4) leads to the page the signer reads; a second page content lies in the file under the
/// number of the first one, no entry of a table leads to it. The signer signs by a program, which goes by the table:
/// the signature is an incremental update (7.5.6) and it covers every byte of the file. A table with a wrong count of
/// its entries, which such a program tolerates, makes the reader of PDF4QT refuse the table, the objects are restored
/// from the bytes, the last one in the file wins - and the signature, intact and over the whole file, is shown next
/// to a page the signer has never seen. A stream with a wrong /Length, which such a program tolerates too: the reader
/// goes by the table and leaves the object out, the signature is shown next to a page without a part the signer has read.
class RestoredSignedFile
{
public:
    RestoredSignedFile() = delete;

    static constexpr const char* SIGNED_TEXT = "SEEN BY THE SIGNER";
    static constexpr const char* HIDDEN_TEXT = "ATTACKER CONTENT";
    static constexpr const char* LOST_TEXT = "SECOND PART SEEN BY THE SIGNER";

    /// What is wrong in the bytes, which the signature covers
    enum class Damage
    {
        None,           ///< The file is valid for the strict reader
        TableCount,     ///< The first table has a wrong count of its entries
        StreamLength    ///< A stream of the page has a wrong length
    };

    /// The whole file, signed by the certificate
    /// \param damage What is wrong in the file
    static QByteArray create(const pdf::PDFCertificateEntry& certificate, const QString& password, Damage damage = Damage::TableCount)
    {
        const QByteArray seen = QByteArray("BT /F1 24 Tf 20 700 Td (") + SIGNED_TEXT + ") Tj ET";
        const QByteArray hidden = QByteArray("BT /F1 24 Tf 20 700 Td (") + HIDDEN_TEXT + ") Tj ET";
        const QByteArray lost = QByteArray("BT /F1 24 Tf 20 600 Td (") + LOST_TEXT + ") Tj ET";
        const QByteArray resources = "/Resources << /Font << /F1 << /Type /Font /Subtype /Type1 /BaseFont /Helvetica >> >> >>";

        QByteArray file = "%PDF-1.7\n";
        QMap<int, qsizetype> offsets;
        const auto addObject = [&](int number, const QByteArray& body)
        {
            offsets[number] = file.size();
            file += QByteArray::number(number) + " 0 obj\n" + body + "\nendobj\n";
        };
        const auto addStream = [](const QByteArray& data, qsizetype length)
        {
            return "<< /Length " + QByteArray::number(length) + " >>\nstream\n" + data + "\nendstream";
        };

        addObject(1, "<< /Type /Catalog /Pages 2 0 R >>");
        addObject(2, "<< /Type /Pages /Kids [ 3 0 R ] /Count 1 >>");
        addObject(3, "<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 612 792 ] /Contents [ 4 0 R 5 0 R ] " + resources + " >>");
        addObject(4, addStream(seen, seen.size()));
        addObject(5, addStream(lost, damage == Damage::StreamLength ? 3 : lost.size()));

        // No entry of a table leads here (the first revision with the table it has)
        file += "4 0 obj\n" + addStream(hidden, hidden.size()) + "\nendobj\n";

        const qsizetype firstTable = file.size();
        file += QByteArray("xref\n0 ") + (damage == Damage::TableCount ? "7" : "6") + "\n0000000000 65535 f \n";
        for (int number = 1; number <= 5; ++number)
        {
            file += QByteArray::number(offsets[number]).rightJustified(10, '0') + " 00000 n \n";
        }
        file += "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n" + QByteArray::number(firstTable) + "\n%%EOF\n";

        // The incremental update of the signature
        const QByteArray placeholder(16384, '0');
        addObject(1, "<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [ 20 0 R ] /SigFlags 3 >> >>");
        addObject(20, "<< /FT /Sig /T (Signature1) /V 21 0 R /Type /Annot /Subtype /Widget /Rect [ 0 0 0 0 ] /P 3 0 R >>");
        addObject(21, "<< /Type /Sig /Filter /Adobe.PPKLite /SubFilter /adbe.pkcs7.detached /ByteRange [ 0 AAAAAAAAAA BBBBBBBBBB CCCCCCCCCC ] /Contents <" + placeholder + "> >>");

        const qsizetype secondTable = file.size();
        file += "xref\n0 2\n0000000000 65535 f \n" + QByteArray::number(offsets[1]).rightJustified(10, '0') + " 00000 n \n";
        file += "20 2\n" + QByteArray::number(offsets[20]).rightJustified(10, '0') + " 00000 n \n" + QByteArray::number(offsets[21]).rightJustified(10, '0') + " 00000 n \n";
        file += "trailer\n<< /Size 22 /Root 1 0 R /Prev " + QByteArray::number(firstTable) + " >>\nstartxref\n" + QByteArray::number(secondTable) + "\n%%EOF\n";

        // The hexadecimal string with its parentheses is what the signature leaves out (12.8.1)
        const qsizetype contentsStart = file.indexOf("<" + placeholder);
        const qsizetype contentsEnd = contentsStart + placeholder.size() + 2;
        const auto setNumber = [&file](const QByteArray& mark, qsizetype value)
        {
            file.replace(file.indexOf(mark), mark.size(), QByteArray::number(value).leftJustified(mark.size(), ' '));
        };
        setNumber("AAAAAAAAAA", contentsStart);
        setNumber("BBBBBBBBBB", contentsEnd);
        setNumber("CCCCCCCCCC", file.size() - contentsEnd);

        QByteArray signature;
        if (!pdf::PDFSignatureFactory::sign(certificate, password, file.left(contentsStart) + file.mid(contentsEnd), signature))
        {
            return QByteArray();
        }

        const QByteArray hexadecimal = signature.toHex();
        if (hexadecimal.size() > placeholder.size())
        {
            return QByteArray();
        }
        file.replace(contentsStart + 1, hexadecimal.size(), hexadecimal);
        return file;
    }
};

}   // namespace pdftest

#endif // RESTOREDSIGNEDFILE_H
