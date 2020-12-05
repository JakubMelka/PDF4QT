//    Copyright (C) 2020 Jakub Melka
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
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFADVANCEDTOOLS_H
#define PDFADVANCEDTOOLS_H

#include "pdfwidgettool.h"
#include "pdfannotation.h"

class QActionGroup;

namespace pdf
{

/// Tool that creates 'sticky note' annotations. Multiple types of sticky
/// notes are available, user can select a type of sticky note. When
/// user select a point, popup window appears and user can enter a text.
class PDFFORQTLIBSHARED_EXPORT PDFCreateStickyNoteTool : public PDFWidgetTool
{
    Q_OBJECT

private:
    using BaseClass = PDFWidgetTool;

public:
    explicit PDFCreateStickyNoteTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QActionGroup* actionGroup, QObject* parent);

protected:
    virtual void updateActions() override;

private:
    void onActionTriggered(QAction* action);
    void onPointPicked(PDFInteger pageIndex, QPointF pagePoint);

    PDFToolManager* m_toolManager;
    QActionGroup* m_actionGroup;
    PDFPickTool* m_pickTool;
    TextAnnotationIcon m_icon;
};

class PDFFORQTLIBSHARED_EXPORT PDFCreateAnnotationTool : public PDFWidgetTool
{
    Q_OBJECT

private:
    using BaseClass = PDFWidgetTool;

public:
    explicit PDFCreateAnnotationTool(PDFDrawWidgetProxy* proxy, QAction* action, QObject* parent);

protected:
    virtual void updateActions() override;
};

/// Tool that creates url link annotation. Multiple types of link highlights
/// are available, user can select a link highlight. When link annotation
/// is clicked, url address is triggered.
class PDFFORQTLIBSHARED_EXPORT PDFCreateHyperlinkTool : public PDFCreateAnnotationTool
{
    Q_OBJECT

private:
    using BaseClass = PDFCreateAnnotationTool;

public:
    explicit PDFCreateHyperlinkTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent);

    LinkHighlightMode getHighlightMode() const;
    void setHighlightMode(const LinkHighlightMode& highlightMode);

private:
    void onRectanglePicked(pdf::PDFInteger pageIndex, QRectF pageRectangle);

    PDFToolManager* m_toolManager;
    PDFPickTool* m_pickTool;
    LinkHighlightMode m_highlightMode = LinkHighlightMode::Outline;
};

/// Tool that creates free text note without callout line.
class PDFFORQTLIBSHARED_EXPORT PDFCreateFreeTextTool : public PDFCreateAnnotationTool
{
    Q_OBJECT

private:
    using BaseClass = PDFCreateAnnotationTool;

public:
    explicit PDFCreateFreeTextTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent);

private:
    void onRectanglePicked(pdf::PDFInteger pageIndex, QRectF pageRectangle);

    PDFToolManager* m_toolManager;
    PDFPickTool* m_pickTool;
};

} // namespace pdf

#endif // PDFADVANCEDTOOLS_H
