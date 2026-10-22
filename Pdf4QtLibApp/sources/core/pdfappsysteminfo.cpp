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

#include "pdfappsysteminfo.h"

#include <QDir>
#include <QFile>

#include <algorithm>
#include <limits>

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#else
#include <cerrno>
#include <signal.h>
#include <sys/types.h>
#endif

#if defined(Q_OS_MACOS)
#include <sys/param.h>
#include <sys/ucred.h>
#include <sys/mount.h>
#endif

namespace pdf
{

/// Helpers of the classification of the paths
class PDFAppSystemInfoHelper
{
public:
    PDFAppSystemInfoHelper() = delete;

    /// Longest mount table, which is read (bytes)
    static constexpr qint64 MAXIMUM_MOUNT_TABLE_SIZE = 16 * 1024 * 1024;

    /// Path with '/' separators (a path of Windows in a database used on Linux is classified the same way)
    static QString toSlashes(const QString& path)
    {
        QString result = path;
        result.replace(QLatin1Char('\\'), QLatin1Char('/'));
        return result;
    }

    /// Decodes the octal escapes of /proc/self/mountinfo (\040 = space, \011, \012, \134)
    static QString decodeMountPath(const QByteArray& text)
    {
        QByteArray decoded;
        decoded.reserve(text.size());
        for (qsizetype i = 0; i < text.size(); ++i)
        {
            const char c = text[i];
            if (c == '\\' && i + 3 < text.size())
            {
                const char a = text[i + 1];
                const char b = text[i + 2];
                const char d = text[i + 3];
                const bool isOctal = a >= '0' && a <= '3' && b >= '0' && b <= '7' && d >= '0' && d <= '7';
                if (isOctal)
                {
                    decoded.append(char(((a - '0') << 6) | ((b - '0') << 3) | (d - '0')));
                    i += 3;
                    continue;
                }
            }
            decoded.append(c);
        }
        return QString::fromUtf8(decoded);
    }

    /// The path lies below the mount point (or is the mount point)
    static bool isBelow(const QString& path, const QString& mountPoint)
    {
        if (mountPoint.isEmpty() || mountPoint == QLatin1String("/"))
        {
            // A network root file system: everything is on the network
            return !mountPoint.isEmpty() && path.startsWith(QLatin1Char('/'));
        }
        if (!path.startsWith(mountPoint))
        {
            return false;
        }
        return path.size() == mountPoint.size() || path[mountPoint.size()] == QLatin1Char('/');
    }

    static bool isLongerMountPoint(const PDFAppMount& first, const PDFAppMount& second)
    {
        if (first.mountPoint.size() != second.mountPoint.size())
        {
            return first.mountPoint.size() > second.mountPoint.size();
        }
        return first.mountPoint < second.mountPoint;
    }

    /// Network mounts of the system (Linux: /proc/self/mountinfo, macOS: getmntinfo)
    static std::vector<PDFAppMount> readSystemMounts()
    {
        std::vector<PDFAppMount> mounts;
#if defined(Q_OS_LINUX)
        QFile file(QStringLiteral("/proc/self/mountinfo"));
        if (file.open(QIODevice::ReadOnly))
        {
            // A file of procfs has no size; it is read up to the limit
            mounts = PDFAppNetworkPaths::parseMountInfo(file.read(MAXIMUM_MOUNT_TABLE_SIZE));
        }
#elif defined(Q_OS_MACOS)
        struct statfs* entries = nullptr;
        const int count = getmntinfo(&entries, MNT_NOWAIT);
        for (int i = 0; entries && i < count && i < PDFAppNetworkPaths::MAXIMUM_MOUNTS; ++i)
        {
            if (!(entries[i].f_flags & MNT_LOCAL))
            {
                PDFAppMount mount;
                mount.mountPoint = QDir::cleanPath(QString::fromUtf8(entries[i].f_mntonname));
                mount.fileSystemType = QString::fromUtf8(entries[i].f_fstypename);
                mounts.push_back(std::move(mount));
            }
        }
#endif
        return mounts;
    }
};

PDFAppNetworkPaths::PDFAppNetworkPaths() :
    m_mounts(PDFAppSystemInfoHelper::readSystemMounts()),
    m_isSystemQueried(true)
{
    std::sort(m_mounts.begin(), m_mounts.end(), &PDFAppSystemInfoHelper::isLongerMountPoint);
}

PDFAppNetworkPaths::PDFAppNetworkPaths(std::vector<PDFAppMount> networkMounts, const QString& remoteDrives) :
    m_isSystemQueried(false),
    m_remoteDrives(remoteDrives.toUpper())
{
    for (PDFAppMount& mount : networkMounts)
    {
        mount.mountPoint = QDir::cleanPath(PDFAppSystemInfoHelper::toSlashes(mount.mountPoint));
        if (!mount.mountPoint.isEmpty())
        {
            m_mounts.push_back(std::move(mount));
        }
    }
    std::sort(m_mounts.begin(), m_mounts.end(), &PDFAppSystemInfoHelper::isLongerMountPoint);
}

bool PDFAppNetworkPaths::isNetworkPath(const QString& path) const
{
    if (path.isEmpty())
    {
        return false;
    }

    if (isNetworkPathByName(path))
    {
        return true;
    }

    const QString slashPath = PDFAppSystemInfoHelper::toSlashes(path);

    // A drive letter ("Z:/...", also a row of Windows read on another system)
    if (slashPath.size() >= 2 && slashPath[1] == QLatin1Char(':') && slashPath[0].isLetter() && slashPath[0].unicode() < 128)
    {
        return isRemoteDrive(slashPath[0].toUpper());
    }

    if (!slashPath.startsWith(QLatin1Char('/')))
    {
        return false;
    }

    const QString cleanPath = QDir::cleanPath(slashPath);
    for (const PDFAppMount& mount : m_mounts)
    {
        if (PDFAppSystemInfoHelper::isBelow(cleanPath, mount.mountPoint))
        {
            return true;
        }
    }
    return false;
}

bool PDFAppNetworkPaths::isNetworkPathByName(const QString& path)
{
    const QString slashPath = PDFAppSystemInfoHelper::toSlashes(path);

    // UNC paths (\\server\share, //server/share); device paths of Windows (//./, //?/)
    // are no documents, they are refused by the repositories, but they are no
    // network paths ("//?/UNC/server/share" is one)
    if (slashPath.startsWith(QLatin1String("//")))
    {
        if (slashPath.startsWith(QLatin1String("//?/UNC/"), Qt::CaseInsensitive))
        {
            return true;
        }
        return !slashPath.startsWith(QLatin1String("//./")) && !slashPath.startsWith(QLatin1String("//?/"));
    }

    // URLs of network file systems (smb://, nfs://, sftp://, webdav://, ...): a
    // scheme of at least two letters, so a drive letter "C:/" is not one
    const qsizetype schemeEnd = slashPath.indexOf(QLatin1String("://"));
    if (schemeEnd >= 2)
    {
        bool isScheme = true;
        for (qsizetype i = 0; i < schemeEnd && isScheme; ++i)
        {
            const QChar c = slashPath[i];
            isScheme = (c.isLetterOrNumber() && c.unicode() < 128) || c == QLatin1Char('+') || c == QLatin1Char('-') || c == QLatin1Char('.');
        }
        if (isScheme && slashPath[0].isLetter())
        {
            return true;
        }
    }

    // Mounts of GVFS (GNOME): /run/user/<uid>/gvfs/smb-share:server=...
    if (slashPath.startsWith(QLatin1String("/run/user/")))
    {
        const QStringList parts = slashPath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        return parts.size() >= 4 && parts[3] == QLatin1String("gvfs");
    }

    return false;
}

bool PDFAppNetworkPaths::isNetworkFileSystem(const QString& type)
{
    static const char* const types[] =
    {
        "nfs", "nfs4", "cifs", "smb", "smb2", "smb3", "smbfs", "ncpfs", "afs", "coda", "ceph", "glusterfs", "lustre", "gpfs", "beegfs",
        "orangefs", "pvfs2", "davfs", "sshfs", "9p.network", "fuse.sshfs", "fuse.gvfsd-fuse", "fuse.rclone", "fuse.glusterfs", "fuse.ceph",
        "fuse.s3fs", "fuse.smbnetfs", "fuse.curlftpfs", "fuse.davfs2", "fuse.cephfs", "fuse.juicefs", "fuse.goofys", "fuse.gcsfuse", "fuse.blobfuse",
        "fuse.blobfuse2", "fuse.lizardfs", "fuse.moosefs", "fuse.mfs"
    };

    for (const char* networkType : types)
    {
        if (type.compare(QLatin1String(networkType), Qt::CaseInsensitive) == 0)
        {
            return true;
        }
    }
    return false;
}

std::vector<PDFAppMount> PDFAppNetworkPaths::parseMountInfo(const QByteArray& text)
{
    // 36 35 98:0 /mnt1 /mnt2 rw,noatime master:1 - ext3 /dev/root rw,errors=continue
    // (field 5 = the mount point; after the separator "-": the type and the source)
    std::vector<PDFAppMount> mounts;
    const QList<QByteArray> lines = text.split('\n');
    int lineCount = 0;
    for (const QByteArray& line : lines)
    {
        if (++lineCount > MAXIMUM_MOUNTS)
        {
            break;
        }

        const QList<QByteArray> fields = line.simplified().split(' ');
        const qsizetype separator = fields.indexOf(QByteArray("-"));
        if (fields.size() < 5 || separator < 6 || separator + 1 >= fields.size())
        {
            continue;
        }

        const QString type = QString::fromUtf8(fields[separator + 1]);
        if (!isNetworkFileSystem(type))
        {
            continue;
        }

        PDFAppMount mount;
        mount.mountPoint = QDir::cleanPath(PDFAppSystemInfoHelper::decodeMountPath(fields[4]));
        mount.fileSystemType = type;
        if (mount.mountPoint.startsWith(QLatin1Char('/')))
        {
            mounts.push_back(std::move(mount));
        }
    }
    return mounts;
}

bool PDFAppNetworkPaths::isRemoteDrive(QChar letter) const
{
    if (!m_isSystemQueried)
    {
        return m_remoteDrives.contains(letter);
    }

#if defined(Q_OS_WIN)
    auto it = m_driveCache.constFind(letter);
    if (it != m_driveCache.cend())
    {
        return *it;
    }

    // The type of the drive is known locally (the redirector keeps the mapped
    // drives, a disconnected one is still DRIVE_REMOTE); the server is not asked
    const wchar_t root[] = { wchar_t(letter.unicode()), L':', L'\\', L'\0' };
    const bool isRemote = GetDriveTypeW(root) == DRIVE_REMOTE;
    m_driveCache.insert(letter, isRemote);
    return isRemote;
#else
    return false;
#endif
}

bool PDFAppProcessInfo::isProcessRunning(qint64 processId)
{
#if defined(Q_OS_WIN)
    if (processId <= 0 || processId > qint64(std::numeric_limits<DWORD>::max()))
    {
        return false;
    }

    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(processId));
    if (!process)
    {
        // A process of another user (or a protected one) exists, but cannot be opened
        return GetLastError() == ERROR_ACCESS_DENIED;
    }

    // A handle of an ended process may still exist (held by another process)
    const bool isRunning = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return isRunning;
#else
    if (processId <= 0 || processId > qint64(std::numeric_limits<pid_t>::max()))
    {
        return false;
    }
    return kill(pid_t(processId), 0) == 0 || errno == EPERM;
#endif
}

}   // namespace pdf
