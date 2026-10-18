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

#include "pdfthemecolors.h"

#include <QCoreApplication>

namespace pdf
{

/// Table of the built-in themes
class PDFThemeColorsHelper
{
public:
    PDFThemeColorsHelper() = delete;

    struct Token
    {
        const char* name;
        const char* light;
        const char* dark;
    };

    /// Variables of the collection "Color" of the design in the order of the design,
    /// the values are written exactly as the design has them (modes Light and Dark).
    static constexpr std::array<Token, PDFThemeColors::ColorCount> TOKENS =
    {
        Token{ "bg/canvas", "#ffffff", "#12161c" },
        Token{ "bg/sidebar", "#f4f4f4", "#171b22" },
        Token{ "bg/titlebar", "#ececec", "#0d1015" },
        Token{ "bg/surface", "#ffffff", "#1a1f27" },
        Token{ "bg/subtle", "#f8fafc", "#1e242d" },
        Token{ "bg/muted", "#f7f7f7", "#20262f" },
        Token{ "bg/track", "#e6e6e6", "#0f1318" },
        Token{ "bg/accent", "#1a73e8", "#3b82f6" },
        Token{ "bg/accent-subtle", "#1a73e821", "#5b9dff2e" },
        Token{ "bg/accent-subtle-hover", "#1a73e830", "#5b9dff3d" },
        Token{ "bg/accent-subtle-pressed", "#1a73e842", "#5b9dff4d" },
        Token{ "bg/overlay", "#ffffff33", "#ffffff0a" },
        Token{ "bg/hover", "#0f172a0f", "#ffffff12" },
        Token{ "bg/pressed", "#0f172a1c", "#ffffff1f" },
        Token{ "border/default", "#e2e8f0", "#2e3642" },
        Token{ "border/strong", "#cbd5e1", "#465164" },
        Token{ "border/divider", "#ececec", "#262d37" },
        Token{ "border/separator", "#d9d9d9", "#2e3642" },
        Token{ "border/focus", "#1a73e8", "#5b9dff" },
        Token{ "text/primary", "#0f172a", "#e8edf3" },
        Token{ "text/secondary", "#334155", "#c3cbd6" },
        Token{ "text/tertiary", "#475569", "#a3adbb" },
        Token{ "text/muted", "#5f6b7e", "#8f9aab" },
        Token{ "text/accent", "#1558b8", "#7fb0ff" },
        Token{ "text/accent-pressed", "#0f4796", "#a8c8ff" },
        Token{ "text/danger", "#b91c1c", "#fca5a5" },
        Token{ "icon/default", "#475569", "#a3adbb" },
        Token{ "icon/muted", "#64748b", "#8f9aab" },
        Token{ "icon/strong", "#0f172a", "#e8edf3" },
        Token{ "icon/accent", "#1a73e8", "#7fb0ff" },
        Token{ "icon/on-accent", "#ffffff", "#ffffff" },
        Token{ "icon/pdf", "#ef4444", "#f87171" },
        Token{ "icon/danger", "#ef4444", "#f87171" },
        Token{ "icon/sun", "#f59e0b", "#fbbf24" },
        Token{ "effect/shadow", "#00000014", "#00000073" },
        Token{ "accent/blue/bg", "#eff6ff", "#16233a" },
        Token{ "accent/blue/bg-hover", "#dbeafe", "#1b2c48" },
        Token{ "accent/blue/bg-pressed", "#cddffd", "#213656" },
        Token{ "accent/blue/border", "#bfdbfe", "#1e3a66" },
        Token{ "accent/blue/border-strong", "#93c5fd", "#2b5190" },
        Token{ "accent/blue/icon", "#1d4ed8", "#8ab4ff" },
        Token{ "accent/violet/bg", "#faf5ff", "#22193a" },
        Token{ "accent/violet/bg-hover", "#f3e8ff", "#2a1f48" },
        Token{ "accent/violet/bg-pressed", "#ecdffe", "#332656" },
        Token{ "accent/violet/border", "#e9d5ff", "#3b2a66" },
        Token{ "accent/violet/border-strong", "#d8b4fe", "#563c91" },
        Token{ "accent/violet/icon", "#7e22ce", "#c9a6ff" },
        Token{ "accent/amber/bg", "#fff7ed", "#2a1d12" },
        Token{ "accent/amber/bg-hover", "#ffedd5", "#352416" },
        Token{ "accent/amber/bg-pressed", "#fee4c4", "#402c1b" },
        Token{ "accent/amber/border", "#fed7aa", "#5a3715" },
        Token{ "accent/amber/border-strong", "#fdba74", "#7c4a1b" },
        Token{ "accent/amber/icon", "#c2410c", "#fdba74" },
        Token{ "accent/red/bg", "#fef2f2", "#2c1618" },
        Token{ "accent/red/bg-hover", "#fee2e2", "#381b1e" },
        Token{ "accent/red/bg-pressed", "#fdd5d5", "#442125" },
        Token{ "accent/red/border", "#fecaca", "#5c2328" },
        Token{ "accent/red/border-strong", "#fca5a5", "#802f36" },
        Token{ "accent/red/icon", "#b91c1c", "#fca5a5" },
        Token{ "accent/teal/bg", "#f0fdfa", "#0f2624" },
        Token{ "accent/teal/bg-hover", "#dafbf3", "#13302d" },
        Token{ "accent/teal/bg-pressed", "#c0f5e8", "#173b37" },
        Token{ "accent/teal/border", "#ccfbf1", "#134e4a" },
        Token{ "accent/teal/border-strong", "#5eead4", "#1a6b64" },
        Token{ "accent/teal/icon", "#0f766e", "#5eead4" },
        Token{ "accent/green/bg", "#f0fdf4", "#10251a" },
        Token{ "accent/green/bg-hover", "#dcfce7", "#142f21" },
        Token{ "accent/green/bg-pressed", "#cdf7da", "#193a28" },
        Token{ "accent/green/border", "#bbf7d0", "#1a4d2e" },
        Token{ "accent/green/border-strong", "#86efac", "#236b3f" },
        Token{ "accent/green/icon", "#15803d", "#86efac" },
        Token{ "accent/neutral/bg", "#f5f5f5", "#1e242d" },
        Token{ "accent/neutral/bg-hover", "#ebedf0", "#262d37" },
        Token{ "accent/neutral/bg-pressed", "#e4e8ee", "#2d3541" },
        Token{ "accent/neutral/border", "#e2e8f0", "#2e3642" },
        Token{ "accent/neutral/border-strong", "#cbd5e1", "#465164" },
        Token{ "accent/neutral/icon", "#475569", "#c3cbd6" },
        Token{ "brand/logo-red", "#c53620", "#c53620" },
        Token{ "brand/logo-green", "#6cbe45", "#6cbe45" },
        Token{ "bg/scrim", "#0f172ab8", "#000000b8" },
        Token{ "sample/black", "#000000", "#000000" },
        Token{ "sample/white", "#ffffff", "#ffffff" },
        Token{ "reading/paper/page", "#ffffff", "#ffffff" },
        Token{ "reading/paper/text", "#111111", "#111111" },
        Token{ "reading/sepia/page", "#f4ecd8", "#f4ecd8" },
        Token{ "reading/sepia/text", "#5b4636", "#5b4636" },
        Token{ "reading/soft-gray/page", "#e8e8e8", "#e8e8e8" },
        Token{ "reading/soft-gray/text", "#2b2b2b", "#2b2b2b" },
        Token{ "reading/mint/page", "#cce8cf", "#cce8cf" },
        Token{ "reading/mint/text", "#1b2a20", "#1b2a20" },
        Token{ "reading/night/page", "#1e2228", "#1e2228" },
        Token{ "reading/night/text", "#d6d9de", "#d6d9de" },
        Token{ "reading/black/page", "#000000", "#000000" },
        Token{ "reading/black/text", "#f2f2f2", "#f2f2f2" },
        Token{ "reading/terminal/page", "#0b0b10", "#0b0b10" },
        Token{ "reading/terminal/text", "#00ff7f", "#00ff7f" },
        Token{ "reading/amber/page", "#0b0b10", "#0b0b10" },
        Token{ "reading/amber/text", "#ffb000", "#ffb000" },
        Token{ "reading/high-contrast/page", "#000000", "#000000" },
        Token{ "reading/high-contrast/text", "#ffff00", "#ffff00" },
        Token{ "reading/custom/page", "#102a43", "#102a43" },
        Token{ "reading/custom/text", "#f0f4f8", "#f0f4f8" },
        Token{ "sample/gray-dark", "#4b5563", "#4b5563" },
        Token{ "sample/gray-mid", "#9ca3af", "#9ca3af" },
        Token{ "sample/gray-light", "#d1d5db", "#d1d5db" },
        Token{ "sample/blue", "#2563eb", "#2563eb" },
        Token{ "sample/orange", "#f59e0b", "#f59e0b" },
        Token{ "sample/blue-soft", "#9dbbf5", "#9dbbf5" },
        Token{ "sample/orange-soft", "#f7cd8b", "#f7cd8b" },
        Token{ "annotation/yellow", "#ffd60a", "#ffd60a" },
        Token{ "annotation/orange", "#ff9500", "#ff9500" },
        Token{ "annotation/red", "#ff3b30", "#ff3b30" },
        Token{ "annotation/pink", "#ff2d92", "#ff2d92" },
        Token{ "annotation/purple", "#af52de", "#af52de" },
        Token{ "annotation/blue", "#0a84ff", "#0a84ff" },
        Token{ "annotation/green", "#34c759", "#34c759" },
        Token{ "annotation/black", "#000000", "#000000" },
        Token{ "annotation/white", "#ffffff", "#ffffff" },
        Token{ "ink/cyan", "#00aeef", "#00aeef" },
        Token{ "ink/magenta", "#ec008c", "#ec008c" },
        Token{ "ink/yellow", "#ffe600", "#ffe600" },
        Token{ "ink/black", "#231f20", "#231f20" },
        Token{ "ink/spot-orange", "#f26b21", "#f26b21" },
        Token{ "ink/spot-green", "#00a651", "#00a651" },
        Token{ "ink/alarm", "#ff0000", "#ff0000" },
        Token{ "heat/low", "#2b6cb0", "#2b6cb0" },
        Token{ "heat/mid", "#38a169", "#38a169" },
        Token{ "heat/high", "#ecc94b", "#ecc94b" },
        Token{ "heat/max", "#e53e3e", "#e53e3e" },
        Token{ "diff/removed", "#ff3232", "#ff3232" },
        Token{ "diff/added", "#7dfa00", "#7dfa00" },
        Token{ "diff/replaced", "#ff781e", "#ff781e" },
        Token{ "diff/moved", "#2391ff", "#2391ff" },
        Token{ "bg/accent-hover", "#1d4ed8", "#2563eb" },
        Token{ "bg/accent-pressed", "#0f4796", "#1d4ed8" },
        Token{ "text/disabled", "#94a3b8", "#525e72" },
        Token{ "icon/disabled", "#94a3b8", "#525e72" },
        Token{ "search/match", "#a5f3fc", "#a5f3fc" },
        Token{ "search/match-current", "#22d3ee", "#22d3ee" },
        Token{ "search/match-border", "#0891b2", "#0891b2" },
        Token{ "bg/danger", "#c42b1c", "#c42b1c" },
        Token{ "bg/danger-pressed", "#b22a1e", "#b22a1e" },
        Token{ "window/macos-close", "#ff5f57", "#ff5f57" },
        Token{ "window/macos-minimize", "#febc2e", "#febc2e" },
        Token{ "window/macos-zoom", "#28c840", "#28c840" },
        Token{ "window/macos-inactive", "#dddddd", "#4d4d4d" }
    };
};

PDFThemeColors::PDFThemeColors() :
    PDFThemeColors(getBuiltinThemeId(Scheme::Light), QString::fromLatin1(QT_TRANSLATE_NOOP("pdf::PDFThemeColors", "Light")), Scheme::Light)
{
    m_isNameTranslated = true;
}

QString PDFThemeColors::getName() const
{
    // The built-in themes can be created before the translators of the
    // application are installed, so their names are translated, when they are asked for
    return m_isNameTranslated ? QCoreApplication::translate("pdf::PDFThemeColors", m_name.toUtf8().constData()) : m_name;
}

PDFThemeColors::PDFThemeColors(QString id, QString name, Scheme scheme) :
    m_id(std::move(id)),
    m_name(std::move(name)),
    m_scheme(scheme)
{
    for (size_t i = 0; i < PDFThemeColorsHelper::TOKENS.size(); ++i)
    {
        const PDFThemeColorsHelper::Token& token = PDFThemeColorsHelper::TOKENS[i];
        m_colors[i] = parseTokenValue(scheme == Scheme::Dark ? token.dark : token.light).rgba();
    }
}

QColor PDFThemeColors::getColor(Color color) const
{
    if (color < 0 || color >= ColorCount)
    {
        return getLeakColor();
    }

    return QColor::fromRgba(m_colors[color]);
}

void PDFThemeColors::setColor(Color color, QColor value)
{
    if (color >= 0 && color < ColorCount)
    {
        m_colors[color] = value.rgba();
    }
}

QByteArray PDFThemeColors::getTokenName(Color color)
{
    if (color < 0 || color >= ColorCount)
    {
        return QByteArray();
    }

    return QByteArray(PDFThemeColorsHelper::TOKENS[color].name);
}

PDFThemeColors::Color PDFThemeColors::getColorFromTokenName(const QByteArray& tokenName)
{
    for (size_t i = 0; i < PDFThemeColorsHelper::TOKENS.size(); ++i)
    {
        if (tokenName == PDFThemeColorsHelper::TOKENS[i].name)
        {
            return static_cast<Color>(i);
        }
    }

    return ColorCount;
}

QByteArray PDFThemeColors::getBuiltinTokenValue(Color color, Scheme scheme)
{
    if (color < 0 || color >= ColorCount)
    {
        return QByteArray();
    }

    const PDFThemeColorsHelper::Token& token = PDFThemeColorsHelper::TOKENS[color];
    return QByteArray(scheme == Scheme::Dark ? token.dark : token.light);
}

std::vector<PDFThemeColors> PDFThemeColors::getBuiltinThemes()
{
    std::vector<PDFThemeColors> themes;
    themes.emplace_back(getBuiltinThemeId(Scheme::Light), QString::fromLatin1(QT_TRANSLATE_NOOP("pdf::PDFThemeColors", "Light")), Scheme::Light);
    themes.emplace_back(getBuiltinThemeId(Scheme::Dark), QString::fromLatin1(QT_TRANSLATE_NOOP("pdf::PDFThemeColors", "Dark")), Scheme::Dark);

    for (PDFThemeColors& theme : themes)
    {
        theme.m_isNameTranslated = true;
    }

    return themes;
}

QString PDFThemeColors::getBuiltinThemeId(Scheme scheme)
{
    return scheme == Scheme::Dark ? QStringLiteral("dark") : QStringLiteral("light");
}

QColor PDFThemeColors::getLeakColor()
{
    return QColor(0xA0, 0x00, 0xFF);
}

QPalette PDFThemeColors::createLeakPalette()
{
    const QBrush brush(getLeakColor());

    QPalette palette;
    for (int group = 0; group < QPalette::NColorGroups; ++group)
    {
        for (int role = 0; role < QPalette::NColorRoles; ++role)
        {
            if (role == QPalette::NoRole)
            {
                continue;
            }

            palette.setBrush(static_cast<QPalette::ColorGroup>(group), static_cast<QPalette::ColorRole>(role), brush);
        }
    }

    return palette;
}

QColor PDFThemeColors::parseTokenValue(const char* value)
{
    // The design writes the opacity as the last byte (#rrggbbaa), Qt as the first one
    const QByteArray text(value);
    bool ok = false;

    if (text.size() == 7)
    {
        const uint rgb = text.mid(1).toUInt(&ok, 16);
        return ok ? QColor::fromRgb(rgb) : getLeakColor();
    }

    if (text.size() == 9)
    {
        const uint rgb = text.mid(1, 6).toUInt(&ok, 16);
        bool alphaOk = false;
        const uint alpha = text.mid(7, 2).toUInt(&alphaOk, 16);

        if (ok && alphaOk)
        {
            QColor color = QColor::fromRgb(rgb);
            color.setAlpha(static_cast<int>(alpha));
            return color;
        }
    }

    return getLeakColor();
}

}   // namespace pdf
