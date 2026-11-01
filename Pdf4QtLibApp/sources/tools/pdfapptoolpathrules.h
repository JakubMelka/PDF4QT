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

#ifndef PDFAPPTOOLPATHRULES_H
#define PDFAPPTOOLPATHRULES_H

#include "pdfappglobal.h"

#include <QCoreApplication>
#include <QString>

namespace pdf
{

/// Rules of the files written by the tools (plan of the first batch, chapter 7: T1,
/// T2): one place for the result dialog, for the expanded templates of a file sink
/// and for the paths of an assistant (PDFAppMcpArguments::checkPath calls them).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolPathRules
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppToolPathRules)

public:
    PDFAppToolPathRules() = delete;

    /// Longest name of a file (the limit of the common file systems)
    static constexpr int MAXIMUM_FILE_NAME_LENGTH = 255;

    /// Why a name of a file cannot be used
    enum class NameProblem
    {
        None,               ///< The name can be used
        Empty,              ///< No name (also only "." or "..")
        TooLong,            ///< More than MAXIMUM_FILE_NAME_LENGTH characters
        Folder,             ///< The name contains a separator of folders of this system
        Stream,             ///< Windows: the name contains ':' (an NTFS stream: "a.pdf::$DATA")
        ReservedDevice,     ///< A reserved name of a device ("NUL.pdf", "COM1")
        TrailingDotOrSpace, ///< Windows: the system removes a trailing dot or space, so another file would be written
        InvalidCharacter    ///< A control or bidirectional character, or a character Windows refuses
    };

    /// Checks a name of a file without a folder (untrusted: typed by the user, derived
    /// from a name of a document, expanded from a template). On every system: empty,
    /// too long, a separator '/', a control or bidirectional character (they hide the
    /// extension) and a reserved name of a device. Only on Windows, where the name means
    /// another file or the system refuses it: a backslash, ':', a trailing dot or space
    /// and the characters < > " | ? * - valid names of other systems stay valid there.
    static NameProblem checkFileName(const QString& fileName);

    /// Translated reason of a problem (empty for NameProblem::None)
    static QString getNameProblemText(NameProblem problem);

    /// The name is a reserved name of a device of Windows with any extension
    /// (CON, PRN, AUX, NUL, CONIN$, CONOUT$, COM1-9, LPT1-9, also with the superscript
    /// digits Windows accepts); refused on every system, so a file can be copied there
    static bool isReservedDeviceName(const QString& fileName);

    /// A clean absolute path contains an NTFS stream (a second ':' after the drive)
    static bool hasStream(const QString& cleanPath);

    /// The path leads to another computer (by its text or by a mount / a mapped drive of
    /// this computer, never contacted)
    static bool isNetworkPath(const QString& path);

    /// The path is a path of a device or of another computer: the rule of T1 for
    /// a path typed into a field or given by an assistant
    static bool isDeviceOrNetworkPath(const QString& path);

    /// The canonical path with the junctions of NTFS resolved: QFileInfo resolves the
    /// symbolic links, not the junctions (mklink /J needs no privilege) - a junction
    /// to another place would pass the rules of the paths otherwise. An empty text for
    /// a junction without a target or too many of them. Other systems: unchanged.
    static QString resolveJunctions(const QString& canonicalPath);
};

}   // namespace pdf

#endif // PDFAPPTOOLPATHRULES_H
