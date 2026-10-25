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

#include "pdfappdocumentcommands.h"
#include "pdfappcommand.h"
#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"

#include <array>

namespace pdf
{

QStringList PDFAppDocumentCommandTable::getCommands(PDFAppDocumentPackage package)
{
    using C = PDFAppCommands;

    switch (package)
    {
        case PDFAppDocumentPackage::Model:
            return { C::undo(), C::redo(), C::lockDocument(), C::editCopy() };

        case PDFAppDocumentPackage::View:
            return { C::copy(), C::selectAll(), C::deselect(), C::handTool(), C::selectText(), C::selectTable(), C::screenshot(), C::extractImage(),
                     C::zoomIn(), C::zoomOut(), C::actualSize(), C::fitPage(), C::fitWidth(), C::fitHeight(),
                     C::rotateViewLeft(), C::rotateViewRight(),
                     C::layoutSinglePage(), C::layoutContinuous(), C::layoutTwoPages(), C::layoutTwoColumns(), C::firstPageRight(),
                     C::showAnnotations(),
                     C::colorModeNormal(), C::colorModeInverted(), C::colorModeGrayscale(), C::colorModeHighContrast(), C::colorModeBitonal(), C::colorModeCustom(),
                     C::toggleSidebar(), C::fullScreen(), C::magnifier(),
                     C::firstPage(), C::lastPage(), C::nextPage(), C::previousPage(), C::nextLine(), C::previousLine(), C::goToPage() };

        case PDFAppDocumentPackage::Panels:
            return { C::addBookmark(), C::nextBookmark(), C::previousBookmark(), C::exportBookmarks(), C::importBookmarks(),
                     C::rotatePagesLeft(), C::rotatePagesRight() };

        case PDFAppDocumentPackage::Search:
            return { C::find(), C::findNext(), C::findPrevious(), C::readAloud() };

        case PDFAppDocumentPackage::File:
            return { C::save(), C::saveAs(), C::print(), C::sendMail(), C::autoRefresh() };

        case PDFAppDocumentPackage::Properties:
            return { C::properties(), C::renderingIssues() };

        case PDFAppDocumentPackage::Annotate:
            return { C::annotatePanel(), C::insertPanel(),
                     C::annotateHighlight(), C::annotateUnderline(), C::annotateStrikeout(), C::annotateSquiggly(), C::annotateHighlightComment(),
                     C::insertTextBox(), C::insertStickyNote(), C::insertLine(), C::insertPolyline(), C::insertRectangle(), C::insertPolygon(),
                     C::insertEllipse(), C::insertFreehand(), C::insertWebLink(), C::insertPageLink(), C::insertStamp(),
                     C::annotateDeleteTool(), C::annotateMarkResults() };

        case PDFAppDocumentPackage::AnnotationEdit:
            return { C::cut(), C::paste(), C::pasteInPlace(), C::deleteSelection(),
                     C::annotationProperties(), C::annotationComment(), C::annotationReply(), C::annotationGeometry(), C::annotationCopyToPages(),
                     C::annotationHide(), C::annotationSelectAll(),
                     C::annotationRotateClockwise(), C::annotationRotateCounterclockwise(), C::annotationRotate180(),
                     C::annotationFlipHorizontal(), C::annotationFlipVertical(),
                     C::annotationAlignLeft(), C::annotationAlignCenter(), C::annotationAlignRight(),
                     C::annotationAlignTop(), C::annotationAlignMiddle(), C::annotationAlignBottom(),
                     C::annotationDistributeHorizontally(), C::annotationDistributeVertically() };

        case PDFAppDocumentPackage::NotesOutline:
            return { C::outlineNewItem(), C::outlineRename(), C::outlineDelete(), C::outlineMoveUp(), C::outlineMoveDown(),
                     C::outlineIndent(), C::outlineOutdent(), C::outlineSetTarget(), C::outlineBold(), C::outlineItalic(),
                     C::outlineSetTargetWholePage(), C::outlineSetTargetPageWidth(), C::outlineSetTargetPageHeight(), C::outlineSetTargetArea(),
                     C::outlineSetTargetPosition(), C::outlineSetTargetContent(), C::outlineSetTargetContentWidth(), C::outlineSetTargetContentHeight(),
                     C::outlineSetTargetNamed(), C::outlineKeepZoom(), C::outlineKeepZoomAll() };
    }

    return QStringList();
}

std::array<PDFAppDocumentPackage, PDFAppDocumentCommandTable::PACKAGE_COUNT> PDFAppDocumentCommandTable::getPackages()
{
    return { PDFAppDocumentPackage::Model, PDFAppDocumentPackage::View, PDFAppDocumentPackage::Panels,
             PDFAppDocumentPackage::Search, PDFAppDocumentPackage::File, PDFAppDocumentPackage::Properties,
             PDFAppDocumentPackage::Annotate, PDFAppDocumentPackage::AnnotationEdit, PDFAppDocumentPackage::NotesOutline };
}

QStringList PDFAppDocumentCommandTable::getAllCommands()
{
    QStringList commands;
    for (PDFAppDocumentPackage package : getPackages())
    {
        commands << getCommands(package);
    }
    return commands;
}

bool PDFAppDocumentCommandTable::findPackage(const QString& commandId, PDFAppDocumentPackage* package)
{
    for (PDFAppDocumentPackage item : getPackages())
    {
        if (getCommands(item).contains(commandId))
        {
            if (package)
            {
                *package = item;
            }
            return true;
        }
    }
    return false;
}

bool PDFAppDocumentCommandTable::isCommandOf(PDFAppDocumentPackage package, const QString& commandId)
{
    // One set per package, built at the first call (the table is constant)
    static const std::array<QSet<QString>, PACKAGE_COUNT> sets = { createSet(PDFAppDocumentPackage::Model), createSet(PDFAppDocumentPackage::View),
                                                                   createSet(PDFAppDocumentPackage::Panels), createSet(PDFAppDocumentPackage::Search),
                                                                   createSet(PDFAppDocumentPackage::File), createSet(PDFAppDocumentPackage::Properties),
                                                                   createSet(PDFAppDocumentPackage::Annotate), createSet(PDFAppDocumentPackage::AnnotationEdit),
                                                                   createSet(PDFAppDocumentPackage::NotesOutline) };

    const size_t index = size_t(package);
    return index < sets.size() && sets[index].contains(commandId);
}

QSet<QString> PDFAppDocumentCommandTable::createSet(PDFAppDocumentPackage package)
{
    const QStringList commands = getCommands(package);
    return QSet<QString>(commands.cbegin(), commands.cend());
}

PDFAppDocumentCommandHandler::PDFAppDocumentCommandHandler(PDFAppDocumentPackage package, PDFAppDocumentTab* tab) :
    BaseClass(tab),
    m_package(package),
    m_tab(tab)
{

}

PDFAppDocumentCommandHandler::~PDFAppDocumentCommandHandler() = default;

PDFAppDocumentTab* PDFAppDocumentCommandHandler::getTab() const
{
    return m_tab.data();
}

PDFAppDocument* PDFAppDocumentCommandHandler::getDocument() const
{
    return m_tab ? m_tab->getDocument() : nullptr;
}

PDFAppDocumentView* PDFAppDocumentCommandHandler::getView() const
{
    return m_tab ? m_tab->getView() : nullptr;
}

QStringList PDFAppDocumentCommandHandler::getCommands() const
{
    return PDFAppDocumentCommandTable::getCommands(m_package);
}

bool PDFAppDocumentCommandHandler::handles(const QString& commandId) const
{
    return PDFAppDocumentCommandTable::isCommandOf(m_package, commandId);
}

bool PDFAppDocumentCommandHandler::isEnabled(const QString& commandId) const
{
    Q_UNUSED(commandId);
    const PDFAppDocument* document = getDocument();
    return document && document->isReady();
}

bool PDFAppDocumentCommandHandler::isChecked(const QString& commandId) const
{
    Q_UNUSED(commandId);
    return false;
}

}   // namespace pdf
