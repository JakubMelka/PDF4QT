// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "sanescannerbackend.h"

#include <sane/saneopts.h>

#include <QByteArray>
#include <QObject>

#include <cstring>

namespace pdfplugin
{

SaneScannerBackend::SaneScannerBackend()
{
    SANE_Int versionCode = 0;
    m_initialized = sane_init(&versionCode, nullptr) == SANE_STATUS_GOOD;
}

SaneScannerBackend::~SaneScannerBackend()
{
    if (m_initialized)
    {
        sane_exit();
    }
}

QString SaneScannerBackend::backendName() const
{
    return QStringLiteral("SANE");
}

std::vector<ScannerDevice> SaneScannerBackend::devices(QString* errorMessage)
{
    std::vector<ScannerDevice> result;
    if (errorMessage)
    {
        errorMessage->clear();
    }

    if (!m_initialized)
    {
        if (errorMessage)
        {
            *errorMessage = QObject::tr("Failed to initialize SANE.");
        }
        return result;
    }

    const SANE_Device** deviceList = nullptr;
    const SANE_Status status = sane_get_devices(&deviceList, SANE_FALSE);
    if (status != SANE_STATUS_GOOD)
    {
        if (errorMessage)
        {
            *errorMessage = QString::fromLocal8Bit(sane_strstatus(status));
        }
        return result;
    }

    for (int i = 0; deviceList && deviceList[i]; ++i)
    {
        const SANE_Device* saneDevice = deviceList[i];
        ScannerDevice device;
        device.id = QString::fromLocal8Bit(saneDevice->name);
        device.name = QString::fromLocal8Bit(saneDevice->name);
        device.vendor = QString::fromLocal8Bit(saneDevice->vendor);
        device.model = QString::fromLocal8Bit(saneDevice->model);
        result.push_back(std::move(device));
    }

    return result;
}

QStringList SaneScannerBackend::sources(const QString& deviceId)
{
    QStringList result;
    if (!m_initialized)
    {
        return result;
    }

    SANE_Handle handle = nullptr;
    if (sane_open(deviceId.toLocal8Bit().constData(), &handle) != SANE_STATUS_GOOD)
    {
        return result;
    }

    const int option = findOption(handle, SANE_NAME_SCAN_SOURCE);
    if (option >= 0)
    {
        const SANE_Option_Descriptor* descriptor = sane_get_option_descriptor(handle, option);
        if (descriptor && descriptor->constraint_type == SANE_CONSTRAINT_STRING_LIST && descriptor->constraint.string_list)
        {
            for (int i = 0; descriptor->constraint.string_list[i]; ++i)
            {
                result << QString::fromLocal8Bit(descriptor->constraint.string_list[i]);
            }
        }
    }

    sane_close(handle);
    return result;
}

ScanResult SaneScannerBackend::scan(const ScanSettings& settings)
{
    ScanResult result;
    if (!m_initialized)
    {
        result.errorMessage = QObject::tr("Failed to initialize SANE.");
        return result;
    }

    SANE_Handle handle = nullptr;
    SANE_Status status = sane_open(settings.deviceId.toLocal8Bit().constData(), &handle);
    if (status != SANE_STATUS_GOOD)
    {
        result.errorMessage = QString::fromLocal8Bit(sane_strstatus(status));
        return result;
    }

    setOptionInt(handle, SANE_NAME_SCAN_RESOLUTION, settings.resolutionDpi);
    setOptionString(handle, SANE_NAME_SCAN_MODE, colorModeToScannerName(settings.colorMode));
    if (!settings.source.isEmpty())
    {
        setOptionString(handle, SANE_NAME_SCAN_SOURCE, settings.source);
    }

    for (int pageIndex = 0; pageIndex < settings.pageCount; ++pageIndex)
    {
        status = sane_start(handle);
        if (status == SANE_STATUS_NO_DOCS && pageIndex > 0)
        {
            break;
        }
        if (status != SANE_STATUS_GOOD)
        {
            result.errorMessage = QString::fromLocal8Bit(sane_strstatus(status));
            break;
        }

        QString errorMessage;
        QImage image = readImage(handle, settings.resolutionDpi, &errorMessage);
        sane_cancel(handle);

        if (image.isNull())
        {
            result.errorMessage = errorMessage.isEmpty() ? QObject::tr("SANE returned an empty image.") : errorMessage;
            break;
        }

        ScannedPage page;
        page.image = std::move(image);
        page.dpiX = settings.resolutionDpi;
        page.dpiY = settings.resolutionDpi;
        result.pages.push_back(std::move(page));
    }

    sane_close(handle);
    return result;
}

bool SaneScannerBackend::setOptionInt(SANE_Handle handle, const char* name, int value)
{
    const int option = findOption(handle, name);
    if (option < 0)
    {
        return false;
    }

    const SANE_Option_Descriptor* descriptor = sane_get_option_descriptor(handle, option);
    if (descriptor && descriptor->type == SANE_TYPE_FIXED)
    {
        SANE_Fixed saneValue = SANE_FIX(value);
        return sane_control_option(handle, option, SANE_ACTION_SET_VALUE, &saneValue, nullptr) == SANE_STATUS_GOOD;
    }
    else
    {
        SANE_Int saneValue = value;
        return sane_control_option(handle, option, SANE_ACTION_SET_VALUE, &saneValue, nullptr) == SANE_STATUS_GOOD;
    }
}

bool SaneScannerBackend::setOptionString(SANE_Handle handle, const char* name, const QString& value)
{
    const int option = findOption(handle, name);
    if (option < 0)
    {
        return false;
    }

    QByteArray bytes = value.toLocal8Bit();
    return sane_control_option(handle, option, SANE_ACTION_SET_VALUE, bytes.data(), nullptr) == SANE_STATUS_GOOD;
}

int SaneScannerBackend::findOption(SANE_Handle handle, const char* name) const
{
    const SANE_Option_Descriptor* optionCountDescriptor = sane_get_option_descriptor(handle, 0);
    if (!optionCountDescriptor)
    {
        return -1;
    }

    SANE_Int optionCount = 0;
    if (sane_control_option(handle, 0, SANE_ACTION_GET_VALUE, &optionCount, nullptr) != SANE_STATUS_GOOD)
    {
        return -1;
    }

    for (int option = 1; option < optionCount; ++option)
    {
        const SANE_Option_Descriptor* descriptor = sane_get_option_descriptor(handle, option);
        if (descriptor && descriptor->name && qstrcmp(descriptor->name, name) == 0)
        {
            return option;
        }
    }

    return -1;
}

QImage SaneScannerBackend::readImage(SANE_Handle handle, int dpi, QString* errorMessage)
{
    SANE_Parameters parameters;
    SANE_Status status = sane_get_parameters(handle, &parameters);
    if (status != SANE_STATUS_GOOD)
    {
        if (errorMessage)
        {
            *errorMessage = QString::fromLocal8Bit(sane_strstatus(status));
        }
        return QImage();
    }

    if (parameters.lines <= 0 || parameters.pixels_per_line <= 0)
    {
        if (errorMessage)
        {
            *errorMessage = QObject::tr("The scanner did not report a valid image size.");
        }
        return QImage();
    }

    QByteArray rawData(parameters.bytes_per_line * parameters.lines, Qt::Uninitialized);
    int offset = 0;
    while (offset < rawData.size())
    {
        SANE_Int bytesRead = 0;
        status = sane_read(handle, reinterpret_cast<SANE_Byte*>(rawData.data() + offset), rawData.size() - offset, &bytesRead);
        if (status == SANE_STATUS_EOF)
        {
            break;
        }
        if (status != SANE_STATUS_GOOD)
        {
            if (errorMessage)
            {
                *errorMessage = QString::fromLocal8Bit(sane_strstatus(status));
            }
            return QImage();
        }
        if (bytesRead <= 0)
        {
            break;
        }

        offset += bytesRead;
    }

    QImage image;
    if (parameters.format == SANE_FRAME_GRAY && parameters.depth == 8)
    {
        image = QImage(parameters.pixels_per_line, parameters.lines, QImage::Format_Grayscale8);
        for (int y = 0; y < parameters.lines; ++y)
        {
            memcpy(image.scanLine(y), rawData.constData() + y * parameters.bytes_per_line, parameters.pixels_per_line);
        }
    }
    else if (parameters.format == SANE_FRAME_GRAY && parameters.depth == 1)
    {
        image = QImage(reinterpret_cast<const uchar*>(rawData.constData()),
                       parameters.pixels_per_line,
                       parameters.lines,
                       parameters.bytes_per_line,
                       QImage::Format_Mono).copy();
    }
    else if (parameters.format == SANE_FRAME_RGB && parameters.depth == 8)
    {
        image = QImage(parameters.pixels_per_line, parameters.lines, QImage::Format_RGB888);
        for (int y = 0; y < parameters.lines; ++y)
        {
            memcpy(image.scanLine(y), rawData.constData() + y * parameters.bytes_per_line, parameters.pixels_per_line * 3);
        }
    }
    else
    {
        if (errorMessage)
        {
            *errorMessage = QObject::tr("Unsupported SANE image format or bit depth.");
        }
        return QImage();
    }

    image.setDotsPerMeterX(int(dpi / 0.0254));
    image.setDotsPerMeterY(int(dpi / 0.0254));
    return image;
}

}   // namespace pdfplugin
