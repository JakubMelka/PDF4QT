//    Copyright (C) 2019-2020 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFPAINTER_H
#define PDFPAINTER_H

#include "pdfutils.h"
#include "pdfpattern.h"
#include "pdfrenderer.h"
#include "pdfpagecontentprocessor.h"
#include "pdftextlayout.h"
#include "pdfsnapper.h"

#include <QPen>
#include <QBrush>

namespace pdf
{

/// Base painter, encapsulating common functionality for all PDF painters (for example,
/// direct painter, or painter, which generates list of graphic commands).
class PDFPainterBase : public PDFPageContentProcessor
{
    using BaseClass = PDFPageContentProcessor;

public:
    explicit PDFPainterBase(PDFRenderer::Features features,
                            const PDFPage* page,
                            const PDFDocument* document,
                            const PDFFontCache* fontCache,
                            const PDFCMS* cms,
                            const PDFOptionalContentActivity* optionalContentActivity,
                            QMatrix pagePointToDevicePointMatrix,
                            const PDFMeshQualitySettings& meshQualitySettings);

    virtual bool isContentSuppressedByOC(PDFObjectReference ocgOrOcmd) override;

protected:
    virtual void performUpdateGraphicsState(const PDFPageContentProcessorState& state) override;
    virtual void performBeginTransparencyGroup(ProcessOrder order, const PDFTransparencyGroup& transparencyGroup);
    virtual void performEndTransparencyGroup(ProcessOrder order, const PDFTransparencyGroup& transparencyGroup);
    virtual void setWorldMatrix(const QMatrix& matrix) = 0;
    virtual void setCompositionMode(QPainter::CompositionMode mode) = 0;

    /// Returns current pen
    const QPen& getCurrentPen() { return m_currentPen.get(this, &PDFPainterBase::getCurrentPenImpl); }

    /// Returns current brush
    const QBrush& getCurrentBrush() { return m_currentBrush.get(this, &PDFPainterBase::getCurrentBrushImpl); }

    /// Returns effective stroking alpha from transparency groups and current graphic state
    PDFReal getEffectiveStrokingAlpha() const;

    /// Returns effective filling alpha from transparency groups and current graphic state
    PDFReal getEffectiveFillingAlpha() const;

    /// Returns true, if blend mode can be set according the transparency group stack
    bool canSetBlendMode(BlendMode mode) const;

    /// Returns, if feature is turned on
    bool hasFeature(PDFRenderer::Feature feature) const { return m_features.testFlag(feature); }

    /// Is transparency group active?
    bool isTransparencyGroupActive() const { return !m_transparencyGroupDataStack.empty(); }

private:
    /// Returns current pen (implementation)
    QPen getCurrentPenImpl() const;

    /// Returns current brush (implementation)
    QBrush getCurrentBrushImpl() const;

    struct PDFTransparencyGroupPainterData
    {
        PDFTransparencyGroup group;
        PDFReal alphaStroke = 1.0;
        PDFReal alphaFill = 1.0;
        BlendMode blendMode = BlendMode::Normal;
    };

    PDFRenderer::Features m_features;
    PDFCachedItem<QPen> m_currentPen;
    PDFCachedItem<QBrush> m_currentBrush;
    std::vector<PDFTransparencyGroupPainterData> m_transparencyGroupDataStack;
};

/// Processor, which processes PDF's page commands on the QPainter. It works with QPainter
/// and with transformation matrix, which translates page points to the device points.
/// Only basic transparency is supported, advanced transparency, such as transparency groups,
/// are not supported. Painter will try to emulate them so painting will not fail completely.
class PDFPainter : public PDFPainterBase
{
    using BaseClass = PDFPainterBase;

public:
    /// Constructs new PDFPainter object, with default parameters.
    /// \param painter Painter, on which page content is drawn
    /// \param features Features of the painter
    /// \param pagePointToDevicePointMatrix Matrix, which translates page points to device points
    /// \param page Page, which will be drawn
    /// \param document Document owning the page
    /// \param fontCache Font cache
    /// \param cms Color management system
    /// \param optionalContentActivity Activity of optional content
    /// \param meshQualitySettings Mesh quality settings
    explicit PDFPainter(QPainter* painter,
                        PDFRenderer::Features features,
                        QMatrix pagePointToDevicePointMatrix,
                        const PDFPage* page,
                        const PDFDocument* document,
                        const PDFFontCache* fontCache,
                        const PDFCMS* cms,
                        const PDFOptionalContentActivity* optionalContentActivity,
                        const PDFMeshQualitySettings& meshQualitySettings);
    virtual ~PDFPainter() override;

protected:
    virtual void performPathPainting(const QPainterPath& path, bool stroke, bool fill, bool text, Qt::FillRule fillRule) override;
    virtual void performClipping(const QPainterPath& path, Qt::FillRule fillRule) override;
    virtual void performImagePainting(const QImage& image) override;
    virtual void performMeshPainting(const PDFMesh& mesh) override;
    virtual void performSaveGraphicState(ProcessOrder order) override;
    virtual void performRestoreGraphicState(ProcessOrder order) override;
    virtual void setWorldMatrix(const QMatrix& matrix) override;
    virtual void setCompositionMode(QPainter::CompositionMode mode) override;

private:
    QPainter* m_painter;
};

/// Precompiled page contains precompiled graphic instructions of a PDF page to draw it quickly
/// on the target painter. It enables very fast drawing, because instructions are not decoded
/// and interpreted from the PDF stream, but they are just "played" on the painter.
class PDFPrecompiledPage
{
public:
    explicit inline PDFPrecompiledPage() = default;

    inline PDFPrecompiledPage(const PDFPrecompiledPage&) = default;
    inline PDFPrecompiledPage(PDFPrecompiledPage&&) = default;
    inline PDFPrecompiledPage& operator=(const PDFPrecompiledPage&) = default;
    inline PDFPrecompiledPage& operator=(PDFPrecompiledPage&&) = default;

    enum class InstructionType
    {
        Invalid,
        DrawPath,
        DrawImage,
        DrawMesh,
        Clip,
        SaveGraphicState,
        RestoreGraphicState,
        SetWorldMatrix,
        SetCompositionMode
    };

    struct Instruction
    {
        inline Instruction() = default;
        inline Instruction(InstructionType type, size_t dataIndex) :
            type(type),
            dataIndex(dataIndex)
        {

        }

        InstructionType type = InstructionType::Invalid;
        size_t dataIndex = 0;
    };

    /// Paints page onto the painter using matrix
    /// \param painter Painter, onto which is page drawn
    /// \param cropBox Page's crop box
    /// \param pagePointToDevicePointMatrix Page point to device point transformation matrix
    /// \param features Renderer features
    void draw(QPainter* painter, const QRectF& cropBox, const QMatrix& pagePointToDevicePointMatrix, PDFRenderer::Features features) const;

    void addPath(QPen pen, QBrush brush, QPainterPath path, bool isText);
    void addClip(QPainterPath path);
    void addImage(QImage image);
    void addMesh(PDFMesh mesh, PDFReal alpha);
    void addSaveGraphicState() { m_instructions.emplace_back(InstructionType::SaveGraphicState, 0); }
    void addRestoreGraphicState() { m_instructions.emplace_back(InstructionType::RestoreGraphicState, 0); }
    void addSetWorldMatrix(const QMatrix& matrix);
    void addSetCompositionMode(QPainter::CompositionMode compositionMode);

    /// Optimizes page memory allocation to contain less space
    void optimize();

    /// Inverts all colors
    void invertColors();

    /// Finalizes precompiled page
    /// \param compilingTimeNS Compiling time in nanoseconds
    /// \param errors List of rendering errors
    void finalize(qint64 compilingTimeNS, QList<PDFRenderError> errors);

    /// Returns compiling time in nanoseconds
    qint64 getCompilingTimeNS() const { return m_compilingTimeNS; }

    /// Returns a list of rendering errors
    const QList<PDFRenderError>& getErrors() const { return m_errors; }

    /// Returns true, if page is valid (i.e. has nonzero instruction count)
    bool isValid() const { return !m_instructions.empty(); }

    /// Returns memory consumption estimate
    qint64 getMemoryConsumptionEstimate() const { return m_memoryConsumptionEstimate; }

    /// Returns paper color
    QColor getPaperColor() const { return m_paperColor; }
    void setPaperColor(QColor paperColor) { m_paperColor = paperColor; }

    PDFSnapInfo* getSnapInfo() { return &m_snapInfo; }
    const PDFSnapInfo* getSnapInfo() const { return &m_snapInfo; }

private:
    struct PathPaintData
    {
        inline PathPaintData() = default;
        inline PathPaintData(QPen pen, QBrush brush, QPainterPath path, bool isText) :
            pen(qMove(pen)),
            brush(qMove(brush)),
            path(qMove(path)),
            isText(isText)
        {

        }

        QPen pen;
        QBrush brush;
        QPainterPath path;
        bool isText = false;
    };

    struct ClipData
    {
        inline ClipData() = default;
        inline ClipData(QPainterPath path) :
            clipPath(qMove(path))
        {

        }

        QPainterPath clipPath;
    };

    struct ImageData
    {
        inline ImageData() = default;
        inline ImageData(QImage image) :
            image(qMove(image))
        {

        }

        QImage image;
    };

    struct MeshPaintData
    {
        inline MeshPaintData() = default;
        inline MeshPaintData(PDFMesh mesh, PDFReal alpha) :
            mesh(qMove(mesh)),
            alpha(alpha)
        {

        }

        PDFMesh mesh;
        PDFReal alpha = 1.0;
    };

    qint64 m_compilingTimeNS = 0;
    qint64 m_memoryConsumptionEstimate = 0;
    QColor m_paperColor = QColor(Qt::white);
    std::vector<Instruction> m_instructions;
    std::vector<PathPaintData> m_paths;
    std::vector<ClipData> m_clips;
    std::vector<ImageData> m_images;
    std::vector<MeshPaintData> m_meshes;
    std::vector<QMatrix> m_matrices;
    std::vector<QPainter::CompositionMode> m_compositionModes;
    QList<PDFRenderError> m_errors;
    PDFSnapInfo m_snapInfo;
};

/// Processor, which processes PDF's page commands and writes them to the precompiled page.
/// Precompiled page then can be used to execute these commands on QPainter.
class Pdf4QtLIBSHARED_EXPORT PDFPrecompiledPageGenerator : public PDFPainterBase
{
    using BaseClass = PDFPainterBase;

public:
    explicit inline PDFPrecompiledPageGenerator(PDFPrecompiledPage* precompiledPage,
                                                PDFRenderer::Features features,
                                                const PDFPage* page,
                                                const PDFDocument* document,
                                                const PDFFontCache* fontCache,
                                                const PDFCMS* cms,
                                                const PDFOptionalContentActivity* optionalContentActivity,
                                                const PDFMeshQualitySettings& meshQualitySettings);

protected:
    virtual void performPathPainting(const QPainterPath& path, bool stroke, bool fill, bool text, Qt::FillRule fillRule) override;
    virtual void performClipping(const QPainterPath& path, Qt::FillRule fillRule) override;
    virtual void performImagePainting(const QImage& image) override;
    virtual void performMeshPainting(const PDFMesh& mesh) override;
    virtual void performSaveGraphicState(ProcessOrder order) override;
    virtual void performRestoreGraphicState(ProcessOrder order) override;
    virtual void setWorldMatrix(const QMatrix& matrix) override;
    virtual void setCompositionMode(QPainter::CompositionMode mode) override;

private:
    PDFPrecompiledPage* m_precompiledPage;
};

}   // namespace pdf

#endif // PDFPAINTER_H
