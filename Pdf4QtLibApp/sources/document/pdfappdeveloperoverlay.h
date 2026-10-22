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

#ifndef PDFAPPDEVELOPEROVERLAY_H
#define PDFAPPDEVELOPEROVERLAY_H

#include "pdfappglobal.h"

#include "pdfrenderer.h"

#include <QObject>
#include <QPointer>

namespace pdf
{

class PDFAppContext;
class PDFAppDocumentView;

/// Developer view of a document tab (Settings › Advanced › Developer): with the
/// setting advanced.developerMode the canvas draws the overlays of the
/// settings advanced.showTextBlocks / advanced.showTextLines (the blocks and the
/// lines of the text layout, numbered) and advanced.showRenderTimes (the times
/// of the compilation and of the drawing of each page). The overlays are the
/// renderer features DebugTextBlocks, DebugTextLines and DisplayTimes of the
/// proxy of the canvas; the other features (of the view) are kept. The settings
/// are read at each change of the settings (also from another instance).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDeveloperOverlay : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAppDeveloperOverlay(PDFAppContext* context, PDFAppDocumentView* view, QObject* parent);
    virtual ~PDFAppDeveloperOverlay() override;

    /// Features of the overlays, which the settings asked for at their last change
    PDFRenderer::Features getRequestedFeatures() const { return m_requested; }

    /// Mask of the features, which the overlay controls
    static PDFRenderer::Features getOverlayFeatures();

    /// Applies the settings to the proxy of the view
    void apply();

private:
    void onSettingChanged(const QString& topic, const QString& key);
    void onEverythingChanged();
    void onPageImageChanged();
    PDFRenderer::Features readRequestedFeatures() const;

    QPointer<PDFAppContext> m_context;
    QPointer<PDFAppDocumentView> m_view;
    PDFRenderer::Features m_requested;
    bool m_isApplying = false;
};

}   // namespace pdf

#endif // PDFAPPDEVELOPEROVERLAY_H
