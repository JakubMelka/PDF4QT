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

#ifndef PDFAPPSYSTEMINFO_H
#define PDFAPPSYSTEMINFO_H

#include "pdfappglobal.h"

#include <QByteArray>
#include <QHash>
#include <QString>

#include <vector>

namespace pdf
{

/// Mount point of a file system (a network one for PDFAppNetworkPaths)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMount
{
public:
    QString mountPoint;         ///< Absolute path with '/' separators
    QString fileSystemType;     ///< "nfs4", "cifs", "fuse.sshfs", ...
};

/// Recognizes paths on network file systems without touching them: Home, Open
/// and Recent never contact a network share by themselves (no existence check,
/// no PDF count, no thumbnail) - a server, which does not answer, would block a
/// worker for a long time and a share may be on a metered or a slow connection.
/// The check is done, when the user opens the item (an explicit action).
///
/// A path is a network path when:
/// - its text says so (isNetworkPathByName): a UNC path (\\server\share,
///   //server/share), a URL with a scheme (smb://, nfs://, ...) or a mount of
///   GVFS (/run/user/<uid>/gvfs/...);
/// - on Windows, its drive is a mapped network drive (GetDriveTypeW = DRIVE_REMOTE,
///   a query of the local table of the redirector, which never contacts the
///   server; asked once per drive letter and classifier);
/// - on Linux, it lies below a mount point of a network file system of
///   /proc/self/mountinfo (a local file of the kernel); on macOS below a mount
///   without MNT_LOCAL (getmntinfo with MNT_NOWAIT, no file system is asked).
///
/// A row copied from the database of another computer is classified by the same
/// rules (a drive letter, which does not exist here, is not remote - it is a
/// missing local file). Create one classifier for a refresh of a page (the mount
/// table is read once by the constructor). Any thread.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppNetworkPaths
{
public:
    /// Reads the mounts of the system (Linux, macOS); drives of Windows are asked lazily
    PDFAppNetworkPaths();

    /// Classifier without any query of the system (tests): the network mounts and
    /// the letters of the remote drives ("ZY")
    PDFAppNetworkPaths(std::vector<PDFAppMount> networkMounts, const QString& remoteDrives);

    /// The path lies on a network file system (see the class)
    bool isNetworkPath(const QString& path) const;

    /// Network mounts used by the classifier (sorted, the longest mount point first)
    const std::vector<PDFAppMount>& getNetworkMounts() const { return m_mounts; }

    /// The text of the path denotes a network location (UNC, URL, GVFS), see the class
    static bool isNetworkPathByName(const QString& path);

    /// The type of a file system of /proc/self/mountinfo is a network one
    static bool isNetworkFileSystem(const QString& type);

    /// Network mounts of a text of /proc/self/mountinfo (octal escapes decoded,
    /// malformed lines skipped, at most MAXIMUM_MOUNTS lines)
    static std::vector<PDFAppMount> parseMountInfo(const QByteArray& text);

    /// Most lines of the mount table, which are read
    static constexpr int MAXIMUM_MOUNTS = 10000;

private:
    bool isRemoteDrive(QChar letter) const;

    std::vector<PDFAppMount> m_mounts;
    bool m_isSystemQueried = true;              ///< false = test classifier (m_remoteDrives)
    QString m_remoteDrives;
    mutable QHash<QChar, bool> m_driveCache;    ///< Windows: drive letter -> remote
};

/// Information about processes of the system
class PDF4QTLIBAPPSHARED_EXPORT PDFAppProcessInfo
{
public:
    PDFAppProcessInfo() = delete;

    /// A process of the id exists (also a process of another user). A reused id
    /// of a finished process looks running - callers combine it with a time limit.
    static bool isProcessRunning(qint64 processId);
};

}   // namespace pdf

#endif // PDFAPPSYSTEMINFO_H
