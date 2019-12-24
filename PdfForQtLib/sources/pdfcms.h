//    Copyright (C) 2019 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt. If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFCMS_H
#define PDFCMS_H

#include "pdfglobal.h"
#include "pdfcolorspaces.h"
#include "pdfexception.h"
#include "pdfutils.h"

#include <QMutex>

#include <compare>

namespace pdf
{

/// This simple structure stores settings for color management system, and what
/// color management system should be used. At default, two color management
/// system are available - generic (which uses default imprecise color management),
/// and CMS using engine LittleCMS2, which was written by Marti Maria, and is
/// linked as separate library.
struct PDFCMSSettings
{
    /// Type of color management system
    enum class System
    {
        Generic,
        LittleCMS2
    };

    /// Controls accuracy of the color transformations. High accuracy
    /// could mean high memory consumption, but better color accuracy,
    /// low accuracy means low memory consumption and low color accuracy.
    enum class Accuracy
    {
        Low,
        Medium,
        High
    };

    bool operator==(const PDFCMSSettings&) const = default;

    System system = System::Generic;
    Accuracy accuracy = Accuracy::Medium;
    RenderingIntent intent = RenderingIntent::Auto;
    bool isBlackPointCompensationActive = true;
    bool isWhitePaperColorTransformed = false;
    QString outputCS;       ///< Output (rendering) color space
    QString deviceGray;     ///< Identifiers for color space (device gray)
    QString deviceRGB;      ///< Identifiers for color space (device RGB)
    QString deviceCMYK;     ///< Identifiers for color space (device CMYK)
};

/// Color management system base class. It contains functions to transform
/// colors from various color system to device color system. If color management
/// system can't handle color transform, it should return invalid color.
class PDFCMS
{
public:
    explicit inline PDFCMS() = default;
    virtual ~PDFCMS() = default;

    /// This function should decide, if color management system is compatible with these
    /// settings (so, it transforms colors according to this setting). If this
    /// function returns false, then this color management system should be replaced
    /// by newly created one, according these settings.
    virtual bool isCompatible(const PDFCMSSettings& settings) const = 0;

    /// Converts color in Device Gray color space to the target device
    /// color space. If error occurs, then invalid color is returned.
    /// Caller then should handle this - try to convert color as accurate
    /// as possible.
    /// \param color Single color channel value
    /// \param intent Rendering intent
    /// \param reporter Render error reporter (used, when color transform fails)
    virtual QColor getColorFromDeviceGray(const PDFColor& color, RenderingIntent intent, PDFRenderErrorReporter* reporter) const = 0;

    /// Converts color in Device RGB color space to the target device
    /// color space. If error occurs, then invalid color is returned.
    /// Caller then should handle this - try to convert color as accurate
    /// as possible.
    /// \param color Three color channel value (R,G,B channel)
    /// \param intent Rendering intent
    /// \param reporter Render error reporter (used, when color transform fails)
    virtual QColor getColorFromDeviceRGB(const PDFColor& color, RenderingIntent intent, PDFRenderErrorReporter* reporter) const = 0;

    /// Converts color in Device CMYK color space to the target device
    /// color space. If error occurs, then invalid color is returned.
    /// Caller then should handle this - try to convert color as accurate
    /// as possible.
    /// \param color Four color channel value (C,M,Y,K channel)
    /// \param intent Rendering intent
    /// \param reporter Render error reporter (used, when color transform fails)
    virtual QColor getColorFromDeviceCMYK(const PDFColor& color, RenderingIntent intent, PDFRenderErrorReporter* reporter) const = 0;

    /// Converts color in XYZ color space to the target device
    /// color space. If error occurs, then invalid color is returned.
    /// Caller then should handle this - try to convert color as accurate
    /// as possible.
    /// \param whitePoint White point of source XYZ color space
    /// \param Three color channel value (X,Y,Z channel)
    /// \param intent Rendering intent
    /// \param reporter Render error reporter (used, when color transform fails)
    virtual QColor getColorFromXYZ(const PDFColor3& whitePoint, const PDFColor& color, RenderingIntent intent, PDFRenderErrorReporter* reporter) const = 0;
};

class PDFCMSGeneric : public PDFCMS
{
public:
    explicit inline PDFCMSGeneric() = default;

    virtual bool isCompatible(const PDFCMSSettings& settings) const override;
    virtual QColor getColorFromDeviceGray(const PDFColor& color, RenderingIntent intent, PDFRenderErrorReporter* reporter) const override;
    virtual QColor getColorFromDeviceRGB(const PDFColor& color, RenderingIntent intent, PDFRenderErrorReporter* reporter) const override;
    virtual QColor getColorFromDeviceCMYK(const PDFColor& color, RenderingIntent intent, PDFRenderErrorReporter* reporter) const override;
    virtual QColor getColorFromXYZ(const PDFColor3& whitePoint, const PDFColor& color, RenderingIntent intent, PDFRenderErrorReporter* reporter) const override;
};

struct PDFColorSpaceIdentifier
{
    enum class Type
    {
        Gray,
        sRGB,
        RGB,
        CMYK
    };

    Type type = Type::sRGB;
    QString name;
    QString id;
    PDFReal temperature = 6500.0;
    QPointF primaryR;
    QPointF primaryG;
    QPointF primaryB;
    PDFReal gamma = 1.0;

    /// Creates gray color space identifier
    /// \param name Name of color profile
    /// \param id Identifier of color profile
    /// \param temperature White point temperature
    /// \param gamma Gamma correction
    static PDFColorSpaceIdentifier createGray(QString name, QString id, PDFReal temperature, PDFReal gamma);

    /// Creates sRGB color space identifier
    static PDFColorSpaceIdentifier createSRGB();

    /// Creates RGB color space identifier
    /// \param name Name of color profile
    /// \param id Identifier of color profile
    /// \param temperature White point temperature
    /// \param primaryR Primary red
    /// \param primaryG Primary green
    /// \param primaryB Primary blue
    /// \param gamma Gamma correction
    static PDFColorSpaceIdentifier createRGB(QString name, QString id, PDFReal temperature, QPointF primaryR, QPointF primaryG, QPointF primaryB, PDFReal gamma);
};

using PDFColorSpaceIdentifiers = std::vector<PDFColorSpaceIdentifier>;

/// Manager, that manages current color management system and also list
/// of usable input and output color profiles. It has color profiles
/// for outout device, and color profiles for input (gray/RGB/CMYK).
/// It also handles settings, and it's changes. Constant functions
/// is save to call from multiple threads, this also holds for some
/// non-constant functions - manager is protected by mutexes.
class PDFFORQTLIBSHARED_EXPORT PDFCMSManager : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFCMSManager(QObject* parent);

    const PDFColorSpaceIdentifiers& getOutputProfiles() const;
    const PDFColorSpaceIdentifiers& getGrayProfiles() const;
    const PDFColorSpaceIdentifiers& getRGBProfiles() const;
    const PDFColorSpaceIdentifiers& getCMYKProfiles() const;

    /// Returns default color management settings
    PDFCMSSettings getDefaultSettings() const;

    /// Get translated name for color management system
    /// \param system System
    static QString getSystemName(PDFCMSSettings::System system);

signals:
    void colorManagementSystemChanged();

private:
    PDFColorSpaceIdentifiers getOutputProfilesImpl() const;
    PDFColorSpaceIdentifiers getGrayProfilesImpl() const;
    PDFColorSpaceIdentifiers getRGBProfilesImpl() const;
    PDFColorSpaceIdentifiers getCMYKProfilesImpl() const;

    PDFCMSSettings m_settings;

    mutable QMutex m_mutex;
    mutable PDFCachedItem<PDFColorSpaceIdentifiers> m_outputProfiles;
    mutable PDFCachedItem<PDFColorSpaceIdentifiers> m_grayProfiles;
    mutable PDFCachedItem<PDFColorSpaceIdentifiers> m_RGBProfiles;
    mutable PDFCachedItem<PDFColorSpaceIdentifiers> m_CMYKProfiles;
};

}   // namespace pdf

#endif // PDFCMS_H
