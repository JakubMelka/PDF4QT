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

#ifndef ICCTESTPROFILE_H
#define ICCTESTPROFILE_H

// An ICC profile of the tests, written byte by byte (big endian, ICC.1:2001-04): a CMYK output profile with a
// known small gamut, so a test knows from the specification, which colors are out of the gamut (soft proofing
// of step 7 of phase 2 through MCP; the same profile as SoftProofTestProfile of UnitTestsSoftProof).

#include <QByteArray>
#include <QtGlobal>

#include <cmath>
#include <utility>
#include <vector>

namespace pdftest
{

class IccTestProfile
{
public:
    IccTestProfile() = delete;

    /// A CMYK output profile (class prtr, PCS Lab, version 2.1) with the description \p description. Its
    /// gamut: L* = 100 (1 - K), a* = 40 (2 M - 1), b* = 40 (2 Y - 1) (C has no effect) - every color with
    /// |a*| <= 40 and |b*| <= 40 is printable (a neutral gray), a saturated green of sRGB (a* about -86) is not.
    static QByteArray createCmykProfile(const QByteArray& description)
    {
        std::vector<std::pair<QByteArray, QByteArray>> tags;
        tags.emplace_back("desc", createDescription(description));
        tags.emplace_back("wtpt", createWhitePoint());
        tags.emplace_back("A2B0", createDeviceToLab());
        tags.emplace_back("B2A0", createLabToDevice());
        return createProfile("prtr", "CMYK", "Lab ", tags);
    }

private:
    static void appendU32(QByteArray& data, quint32 value)
    {
        data.append(char((value >> 24) & 0xFF));
        data.append(char((value >> 16) & 0xFF));
        data.append(char((value >> 8) & 0xFF));
        data.append(char(value & 0xFF));
    }

    static void appendU16(QByteArray& data, quint16 value)
    {
        data.append(char((value >> 8) & 0xFF));
        data.append(char(value & 0xFF));
    }

    static void appendS15Fixed16(QByteArray& data, double value)
    {
        appendU32(data, quint32(qint32(std::lround(value * 65536.0))));
    }

    static void pad(QByteArray& data)
    {
        while (data.size() % 4 != 0)
        {
            data.append(char(0));
        }
    }

    static quint16 encode(double value01)
    {
        return quint16(std::lround(qBound(0.0, value01, 1.0) * 65535.0));
    }

    /// textDescriptionType (ICC.1:2001-04, 6.5.17)
    static QByteArray createDescription(const QByteArray& text)
    {
        QByteArray data("desc");
        appendU32(data, 0);
        appendU32(data, quint32(text.size() + 1));
        data.append(text);
        data.append(char(0));
        appendU32(data, 0);     // Unicode language code
        appendU32(data, 0);     // Unicode count
        appendU16(data, 0);     // ScriptCode code
        data.append(char(0));   // ScriptCode count
        data.append(QByteArray(67, char(0)));
        return data;
    }

    /// XYZType of the white point D50
    static QByteArray createWhitePoint()
    {
        QByteArray data("XYZ ");
        appendU32(data, 0);
        appendS15Fixed16(data, 0.9642);
        appendS15Fixed16(data, 1.0);
        appendS15Fixed16(data, 0.8249);
        return data;
    }

    /// The beginning of a lut16Type (6.5.7): the channels, the grid, the identity matrix, two entries of
    /// each table and the identity input tables
    static QByteArray createLutStart(int inputs, int outputs, int gridPoints)
    {
        QByteArray data("mft2");
        appendU32(data, 0);
        data.append(char(inputs));
        data.append(char(outputs));
        data.append(char(gridPoints));
        data.append(char(0));
        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0; column < 3; ++column)
            {
                appendS15Fixed16(data, row == column ? 1.0 : 0.0);
            }
        }
        appendU16(data, 2);
        appendU16(data, 2);
        for (int i = 0; i < inputs; ++i)
        {
            appendU16(data, 0);
            appendU16(data, 0xFFFF);
        }
        return data;
    }

    static void appendOutputTables(QByteArray& data, int outputs)
    {
        for (int i = 0; i < outputs; ++i)
        {
            appendU16(data, 0);
            appendU16(data, 0xFFFF);
        }
    }

    /// The legacy 16-bit encoding of Lab of version 2 (L* 0..100 = 0..0xFF00, a* b* -128..127.996 = 0..0xFFFF)
    static void appendLab(QByteArray& data, double l, double a, double b)
    {
        appendU16(data, quint16(std::lround(qBound(0.0, l, 100.0) * 652.8)));
        appendU16(data, quint16(std::lround(qBound(0.0, (a + 128.0) * 256.0, 65535.0))));
        appendU16(data, quint16(std::lround(qBound(0.0, (b + 128.0) * 256.0, 65535.0))));
    }

    /// A2B0: CMYK to Lab, 2 grid points (the mapping is linear, so the interpolation is exact)
    static QByteArray createDeviceToLab()
    {
        QByteArray data = createLutStart(4, 3, 2);
        for (int c = 0; c < 2; ++c)
        {
            for (int m = 0; m < 2; ++m)
            {
                for (int y = 0; y < 2; ++y)
                {
                    for (int k = 0; k < 2; ++k)
                    {
                        Q_UNUSED(c);
                        appendLab(data, 100.0 * (1 - k), 40.0 * (2 * m - 1), 40.0 * (2 * y - 1));
                    }
                }
            }
        }
        appendOutputTables(data, 3);
        return data;
    }

    /// B2A0: Lab to CMYK, the inverse of A2B0 clamped to the device, 17 grid points per channel
    static QByteArray createLabToDevice()
    {
        constexpr int gridPoints = 17;
        QByteArray data = createLutStart(3, 4, gridPoints);
        for (int i = 0; i < gridPoints; ++i)
        {
            const double l = (65535.0 * i / (gridPoints - 1)) / 652.8;
            for (int j = 0; j < gridPoints; ++j)
            {
                const double a = (65535.0 * j / (gridPoints - 1)) / 256.0 - 128.0;
                for (int k = 0; k < gridPoints; ++k)
                {
                    const double b = (65535.0 * k / (gridPoints - 1)) / 256.0 - 128.0;
                    appendU16(data, 0);
                    appendU16(data, encode((a / 40.0 + 1.0) / 2.0));
                    appendU16(data, encode((b / 40.0 + 1.0) / 2.0));
                    appendU16(data, encode(1.0 - l / 100.0));
                }
            }
        }
        appendOutputTables(data, 4);
        return data;
    }

    /// The header (7.2), the tag table (7.3) and the tags (each at an offset divisible by 4)
    static QByteArray createProfile(const QByteArray& deviceClass, const QByteArray& colorSpace, const QByteArray& pcs, const std::vector<std::pair<QByteArray, QByteArray>>& tags)
    {
        const quint32 tableSize = 4 + 12 * quint32(tags.size());
        const quint32 offset = 128 + tableSize;
        QByteArray table;
        appendU32(table, quint32(tags.size()));
        QByteArray tagData;
        for (const auto& [signature, content] : tags)
        {
            appendU32(table, (quint32(uchar(signature[0])) << 24) | (quint32(uchar(signature[1])) << 16) | (quint32(uchar(signature[2])) << 8) | quint32(uchar(signature[3])));
            appendU32(table, offset + quint32(tagData.size()));
            appendU32(table, quint32(content.size()));
            tagData.append(content);
            pad(tagData);
        }

        QByteArray header;
        appendU32(header, 128 + tableSize + quint32(tagData.size()));
        appendU32(header, 0);               // CMM
        appendU32(header, 0x02100000);      // Version 2.1
        header.append(deviceClass);
        header.append(colorSpace);
        header.append(pcs);
        header.append(QByteArray(12, char(0)));     // Date
        header.append("acsp");
        header.append(QByteArray(4 + 4 + 4 + 4 + 8 + 4, char(0)));     // Platform, flags, manufacturer, model, attributes, intent
        appendS15Fixed16(header, 0.9642);   // Illuminant D50
        appendS15Fixed16(header, 1.0);
        appendS15Fixed16(header, 0.8249);
        header.append(QByteArray(4, char(0)));      // Creator
        header.append(QByteArray(44, char(0)));     // Profile id and reserved
        Q_ASSERT(header.size() == 128);
        return header + table + tagData;
    }
};

}   // namespace pdftest

#endif // ICCTESTPROFILE_H
