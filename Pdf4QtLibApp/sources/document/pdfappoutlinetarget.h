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

#ifndef PDFAPPOUTLINETARGET_H
#define PDFAPPOUTLINETARGET_H

#include "pdfappglobal.h"

#include "pdfaction.h"
#include "pdfmessagebox.h"

#include <QObject>
#include <QPointer>

class QSortFilterProxyModel;
class QStandardItemModel;

namespace pdf
{

class PDFAppDocumentView;
class PDFAppFloatingBar;
class PDFAppFloatingBarIcon;
class PDFAppPageLinkTool;
class PDFDocument;
class PDFIconButton;
class PDFLabel;
class PDFLineEdit;
class PDFToolManager;
class PDFTreeView;
class PDFWidgetTool;

/// Picking of the target of an outline item on a page (Set Target of the Outline
/// panel, step 4 package C): the step 2 of Link to page - PDFAppPageLinkTool in its
/// target-only mode - with a floating bar 12 px below the toolbar (the target, the
/// instruction, close). The user navigates freely (scroll, the page field, the
/// thumbnails), a click (Whole page, Page width, Page height, Position, Visible
/// content, its width and height) or a rectangle (Area) picks the target; Esc,
/// the close button, another tool or cancel() end the picking without a target.
///
/// The picked target is emitted from the event loop (the tool is not turned off
/// from its own mouse event); the picking has ended before targetPicked. Every
/// slot survives the deletion of the view and of the picker.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppOutlineTargetPicker : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAppOutlineTargetPicker(PDFAppDocumentView* view, QObject* parent);
    virtual ~PDFAppOutlineTargetPicker() override;

    /// The type is picked on a page (Fit, FitH, FitV, FitR, XYZ, FitB, FitBH, FitBV)
    static bool isPickedType(DestinationType type);

    /// Starts the picking of the target of the type for the item \p itemTitle;
    /// Position keeps the zoom of the reader with \p keepZoom. A running picking
    /// is canceled first. \returns false, when the picking cannot start.
    bool start(DestinationType type, bool keepZoom, const QString& itemTitle);

    /// Ends the picking without a target (emits canceled, when it was active)
    void cancel();

    bool isActive() const { return m_isActive; }
    DestinationType getType() const { return m_type; }

    /// Parts (tests)
    PDFAppPageLinkTool* getTool() const;
    PDFAppFloatingBar* getBar() const;
    PDFLabel* getInstructionLabel() const;
    PDFIconButton* getCloseButton() const;

    /// Picks the target as a click (\p point) or a drawn rectangle (\p rectangle,
    /// Area) on the page would; the target is emitted from the event loop
    void pickTarget(PDFInteger pageIndex, const QPointF& point, const QRectF& rectangle);

Q_SIGNALS:
    /// The target has been picked (the picking has ended)
    void targetPicked(pdf::PDFDestination destination);

    /// The picking has ended without a target
    void canceled();

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void createBar();
    void updateTexts();
    void finish();
    void onToolTargetPicked(pdf::PDFDestination destination);
    void onPickedQueued();
    void onActiveToolChanged(pdf::PDFWidgetTool* tool);
    void onCloseClicked();
    PDFToolManager* getToolManager() const;
    QWidget* getCanvasWidget() const;

    QPointer<PDFAppDocumentView> m_view;
    QPointer<PDFAppPageLinkTool> m_tool;
    QPointer<PDFToolManager> m_toolManager;     ///< Tool manager, to which the tool has been added
    QPointer<QWidget> m_canvas;                 ///< Canvas with the event filter of Esc
    QPointer<PDFAppFloatingBar> m_bar;
    QPointer<PDFAppFloatingBarIcon> m_icon;
    QPointer<PDFLabel> m_name;
    QPointer<PDFLabel> m_instruction;
    QPointer<PDFIconButton> m_closeButton;
    DestinationType m_type = DestinationType::Fit;
    QString m_itemTitle;
    QString m_statusHint;
    PDFDestination m_pickedDestination;
    bool m_isActive = false;
    bool m_isStarting = false;
    bool m_isPickPending = false;
};

/// Dialog Named Destination… of Set Target of an outline item (a dialog of the
/// design built on PDFMessageBox, no badge): the search field and the list of the
/// named destinations of the document (sorted, at most MAXIMUM_NAMES, the names
/// shown as plain text without control characters and shortened, names longer
/// than PDFAppAnnotationOperations::MAXIMUM_OUTLINE_TITLE_LENGTH left out), an
/// empty state for a document without them. Set Target (Enter, a double click)
/// accepts the current row, Cancel / Esc reject. The dialog keeps the names, not
/// the document.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppNamedDestinationDialog : public PDFMessageBox
{
    Q_OBJECT

private:
    using BaseClass = PDFMessageBox;

public:
    /// Most names listed
    static constexpr int MAXIMUM_NAMES = 10000;

    /// Longest name shown (the rest is elided)
    static constexpr int MAXIMUM_SHOWN_LENGTH = 200;

    /// Width of the card
    static constexpr int CARD_WIDTH = 440;

    /// Height of the list
    static constexpr int LIST_HEIGHT = 240;

    explicit PDFAppNamedDestinationDialog(const PDFDocument* document, const QString& itemTitle, QWidget* parent);
    virtual ~PDFAppNamedDestinationDialog() override;

    /// Names of the named destinations of the document, which an item can target
    /// (sorted, at most MAXIMUM_NAMES)
    static QList<QByteArray> getNames(const PDFDocument* document);

    /// Name shown for the user: Latin-1 / UTF-8 text, control and format
    /// characters replaced, at most MAXIMUM_SHOWN_LENGTH characters
    static QString getDisplayName(const QByteArray& name);

    /// Name of the current row (empty if none)
    QByteArray getSelectedName() const;

    /// Makes the row of the name current (false, when it is not listed or hidden)
    bool selectName(const QByteArray& name);

    /// Number of the listed names
    int getNameCount() const;

    PDFLineEdit* getSearchField() const;
    PDFTreeView* getList() const;
    PDFLabel* getEmptyLabel() const;
    QPushButton* getSetButton() const;
    QPushButton* getCancelButton() const;

private:
    void onFilterTextChanged(const QString& text);
    void onCurrentChanged();
    void onSetClicked();
    void onDoubleClicked();

    QPointer<PDFLineEdit> m_searchField;
    QPointer<PDFTreeView> m_list;
    QPointer<QStandardItemModel> m_model;
    QPointer<QSortFilterProxyModel> m_filterModel;
    QPointer<PDFLabel> m_emptyLabel;
    QPointer<QPushButton> m_setButton;
    QPointer<QPushButton> m_cancelButton;
};

}   // namespace pdf

#endif // PDFAPPOUTLINETARGET_H
