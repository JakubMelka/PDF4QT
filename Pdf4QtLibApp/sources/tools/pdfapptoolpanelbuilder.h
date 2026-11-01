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

#ifndef PDFAPPTOOLPANELBUILDER_H
#define PDFAPPTOOLPANELBUILDER_H

#include "pdfappglobal.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"

#include "pdfaccenthue.h"
#include "pdfcomponentdelegates.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QToolButton>
#include <QWidget>

#include <map>
#include <vector>

class QAbstractItemModel;
class QBoxLayout;
class QVBoxLayout;

namespace pdf
{

class PDFAppSettingControl;
class PDFAppToolParameterModel;
class PDFDocumentToolGUI;
class PDFListView;

/// Row of a panel bound to a Bool parameter: a step of the tool, a kind of hidden
/// data, an extra item of the export
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolFlagItem
{
public:
    QString parameter;          ///< Bool parameter of the tool
    QString title;              ///< Translated title
    QString description;        ///< Translated description
    QString iconName;           ///< Icon of the tile
    QString offDescription;     ///< Description of a step, which is off ("Will be skipped", 319:7386); empty = the same
    QString groupCaption;       ///< Caption of a group of rows, which starts by this row (339:9500 "FROM THE TAGS…"), only in a menu
};

/// Menu of an Enum parameter (Order 339:8765): the button of the toolbar says the title of
/// the option, the panel has one row per option; a row writes the parameter into the model
/// and closes the panel. A panel of more parameters (Format and Encoding 339:9546, each
/// with its caption, \p addGroup) stays open: the user chooses in both groups.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolEnumMenu : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAppToolEnumMenu(PDFAppToolParameterModel* model, const QString& parameter, const QString& titleFormat, QToolButton* button, PDFAppToolPanelPopup* panel, QObject* parent);
    virtual ~PDFAppToolEnumMenu() override;

    QToolButton* getButton() const { return m_button; }
    PDFAppToolPanelPopup* getPanel() const { return m_panel; }

    /// Adds a further parameter to the panel below a divider, with its caption
    void addGroup(const QString& parameter, const QString& caption);

    /// Row of the option, nullptr = none
    PDFTileRow* getRow(const QString& optionId) const;

private:
    void addRows(const QString& parameter);
    void refresh();
    void onRowClicked();
    void onParameterChanged(const QString& name);

    QPointer<PDFAppToolParameterModel> m_model;
    QStringList m_parameters;   ///< The first one gives the title of the button
    QString m_titleFormat;
    QPointer<QToolButton> m_button;
    QPointer<PDFAppToolPanelPopup> m_panel;
    std::map<QString, QPointer<PDFTileRow>> m_rows;
};

/// Menu of more Bool parameters (Steps 303:4560, Include 339:8765): the rows toggle
/// their parameters and the panel stays open; the button says how many are on by its
/// format ("Steps: %1 of %2", "Include: %1 extra": %2 is optional), an empty format leaves
/// it to the tool
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolFlagsMenu : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// \param rowKind Kind of the rows: PanelStep 46 of 303:4560, MenuItem 44 of 339:9492
    explicit PDFAppToolFlagsMenu(PDFAppToolParameterModel* model, std::vector<PDFAppToolFlagItem> items, const QString& titleFormat, QToolButton* button, PDFAppToolPanelPopup* panel, PDFTileRow::Kind rowKind, QObject* parent);
    virtual ~PDFAppToolFlagsMenu() override;

    QToolButton* getButton() const { return m_button; }
    PDFAppToolPanelPopup* getPanel() const { return m_panel; }

    /// Number of the parameters, which are on
    int getCheckedCount() const;

    /// Writes the values of all the parameters as one undo step (Restore defaults, Select all, Clear)
    void setAll(bool isChecked);
    void restoreDefaults();

private:
    void refresh();
    void onRowClicked();
    void onParameterChanged(const QString& name);

    QPointer<PDFAppToolParameterModel> m_model;
    std::vector<PDFAppToolFlagItem> m_items;
    QString m_titleFormat;
    QPointer<QToolButton> m_button;
    QPointer<PDFAppToolPanelPopup> m_panel;
    std::map<QString, QPointer<PDFTileRow>> m_rows;
};

/// The steps of the right panel (steps 319:7343): a card of rows, which show the Bool
/// parameters: on - the check mark and the tile of the accent, off - the mark minus,
/// the muted tile and the description of a skipped step
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolStepList : public PDFAppToolCard
{
    Q_OBJECT

private:
    using BaseClass = PDFAppToolCard;

public:
    explicit PDFAppToolStepList(PDFAppToolParameterModel* model, std::vector<PDFAppToolFlagItem> items, QWidget* parent);
    virtual ~PDFAppToolStepList() override;

    PDFTileRow* getRow(const QString& parameter) const;

private:
    void refresh();
    void onParameterChanged(const QString& name);

    QPointer<PDFAppToolParameterModel> m_model;
    std::vector<PDFAppToolFlagItem> m_items;
    std::map<QString, QPointer<PDFTileRow>> m_rows;
};

/// A check list of Bool parameters in a card (438:13119): the check box writes its
/// parameter, a click on the row toggles it, the tile follows the check box
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolChecklist : public PDFAppToolCard
{
    Q_OBJECT

private:
    using BaseClass = PDFAppToolCard;

public:
    explicit PDFAppToolChecklist(PDFAppToolParameterModel* model, std::vector<PDFAppToolFlagItem> items, QWidget* parent);
    virtual ~PDFAppToolChecklist() override;

    PDFTileRow* getRow(const QString& parameter) const;

private:
    void refresh();
    void onCheckBoxToggled(bool isChecked);
    void onRowClicked();
    void onParameterChanged(const QString& name);

    QPointer<PDFAppToolParameterModel> m_model;
    std::vector<PDFAppToolFlagItem> m_items;
    std::map<QString, QPointer<PDFTileRow>> m_rows;
};

/// PDFPageSelection bound to a PageSelection parameter (389:20177, 339:8112): the mode,
/// the range and the filters become the canonical value of the model, the pages of the
/// document and of the view come from the host. A range with an error is no value: the
/// Primary button is disabled (PDFDocumentToolGUI::setInputError). Selected is the mode at
/// the entry, when more pages are selected in the thumbnails (tools.md 9.6).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolPageSelectionBinder : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAppToolPageSelectionBinder(PDFDocumentToolGUI* gui, const QString& parameter, PDFPageSelection* selection, QObject* parent);
    virtual ~PDFAppToolPageSelectionBinder() override;

    /// Range text of a list of pages from 1, sorted ("1-3, 5")
    static QString createRangeText(const QList<int>& pages);

private:
    void updatePages();
    void updateViewPages();
    void onDocumentChanged();

    /// Writes the value: \p isUserChange an undo step of a change of the user, otherwise a
    /// value following the pages of the view
    void writeValue(bool isUserChange);
    void onUserChange();
    void onParameterChanged(const QString& name);

    QPointer<PDFDocumentToolGUI> m_gui;
    QString m_parameter;
    QPointer<PDFPageSelection> m_selection;
    bool m_isUpdating = false;
};

/// A list of pending changes (Pending rotation 389:20177, Marked for removal 388:20614):
/// the header with the count and the rows of the model of the tool drawn by
/// PDFTileRowDelegate (a document of thousands of pages); the rows are not clickable, the
/// link of a row emits linkActivated
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolPendingList : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Gap of the header and of the rows (389:20204)
    static constexpr int ROW_GAP = 10;

    /// \param title Title of the header ("Pending rotation")
    /// \param model Rows (the roles of PDFComponentRoles), owned by the tool
    /// \param parent Parent widget
    explicit PDFAppToolPendingList(const QString& title, QAbstractItemModel* model, QWidget* parent);
    virtual ~PDFAppToolPendingList() override;

    PDFListView* getView() const { return m_view; }
    PDFAppToolSectionHeader* getHeader() const { return m_header; }

Q_SIGNALS:
    /// The link of the row has been activated (Undo, Keep)
    void linkActivated(const QModelIndex& index);

private:
    void updateCount();
    void onDelegateAction(const QModelIndex& index, int action);

    QPointer<PDFAppToolSectionHeader> m_header;
    QPointer<PDFListView> m_view;
    QPointer<QAbstractItemModel> m_model;
};

/// Builds the panels and the actions of a thin tool (plan of the first batch, chapter
/// 2.1) into the widget of the GUI: the parts of the frames of the tools, whose values
/// live in the parameter model. A simple value goes through PDFAppSettingControl and
/// the store of the model (no second binder); the menus, the steps and the check lists
/// have rows of the design and write the model the same way. The bound parts follow
/// Undo and Redo of the model. A section takes the following parts into its own layout
/// (gap 8), until the next section or \p endSection.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolPanelBuilder
{
public:
    /// Gap of a section (section-Result 293:1354: the header and the card 8 apart)
    static constexpr int SECTION_GAP = 8;

    /// Gap of the actions of the toolbar and of a label and its control (tool-actions 291:1135)
    static constexpr int ACTION_GAP = 2;
    static constexpr int LABEL_GAP = 6;

    /// Width of a segment of the toolbar (preset 871:136630: 91) and its height
    static constexpr int SEGMENT_WIDTH = 91;
    static constexpr int SEGMENT_HEIGHT = 32;

    /// \param gui GUI of the tool: its model, host and context
    /// \param widget The widget of the content, it gets the layout (null = \p layout of the caller)
    /// \param orientation Horizontal for the actions of a toolbar
    /// \param spacing Gap of the parts
    explicit PDFAppToolPanelBuilder(PDFDocumentToolGUI* gui, QWidget* widget, Qt::Orientation orientation, int spacing);

    QWidget* getWidget() const { return m_widget; }

    /// The builder has added nothing
    bool isEmpty() const { return m_itemCount == 0; }

    // ------------------------------------------------------------- toolbar

    /// Segments of an Enum parameter with its label ("Preset", 291:1135), through the binder
    PDFAppSettingControl* addSegments(const QString& parameter, const QString& label, int segmentWidth = SEGMENT_WIDTH);

    /// Button with a panel of an Enum parameter ("Order: %1"; the panel 270 wide, 339:8765)
    PDFAppToolEnumMenu* addEnumMenu(const QString& parameter, const QString& iconName, const QString& titleFormat, int panelWidth, const QString& caption = QString());

    /// Button with a panel of Bool parameters ("Steps: %1 of %2", panel-steps 520)
    PDFAppToolFlagsMenu* addFlagsMenu(std::vector<PDFAppToolFlagItem> items, const QString& iconName, const QString& titleFormat, int panelWidth, const QString& caption = QString(), PDFTileRow::Kind rowKind = PDFTileRow::Kind::PanelStep);

    /// Button with an empty panel: the tool adds its rows and reacts to them (Preset of
    /// Remove Hidden Data 438:12831, Show of Remove External Links 438:15032 - their
    /// choices are no single parameter of the core). The button is PDFAppToolPanelPopup::getButton.
    PDFAppToolPanelPopup* addMenu(const QString& objectName, const QString& iconName, const QString& text, int panelWidth);

    /// Toolbar button with a text: it runs the slot of the tool (Rotate Left, Select All)
    template<typename Receiver>
    QToolButton* addAction(const QString& text, const QString& iconName, Receiver* receiver, void (Receiver::*slot)())
    {
        QToolButton* button = createActionButton(text, iconName);
        QObject::connect(button, &QToolButton::clicked, receiver, slot);
        return button;
    }

    /// Divider of the toolbar (303:4427)
    void addActionDivider();

    // ------------------------------------------------------------- panels

    /// Header of a section, the next parts go below it with the gap 8
    PDFAppToolSectionHeader* addSection(const QString& title, const QString& hint = QString());

    /// The next parts go to the content again (gap of the panel)
    void endSection();

    /// Rows of labels and values in a card (breakdown 293:1369). The tool fills them.
    PDFKeyValueList* addKeyValues();

    /// A text in a card (result 303:4509: Regular 12 in lines of 17)
    PDFAppParagraph* addTextCard(const QString& text);

    /// Callout (295:2156, 389:20177)
    PDFAppToolCallout* addCallout(PDFAccentHue hue, const QString& iconName, const QString& title, const QString& text = QString());

    /// A note below a card (preset-description 293:1382: Regular 12 text/secondary in lines of 17)
    PDFAppParagraph* addNote(const QString& text);

    /// A hint of a left panel (389:20402, 438:14939: Regular 11 text/muted in lines of 13)
    PDFAppParagraph* addHint(const QString& text);

    /// Page selection of a PageSelection parameter with its label ("Pages to rotate")
    PDFPageSelection* addPageSelection(const QString& parameter, const QString& label);

    /// Check list of Bool parameters in a card (438:13119)
    PDFAppToolChecklist* addChecklist(std::vector<PDFAppToolFlagItem> items);

    /// Steps of the Bool parameters in a card (steps 319:7343)
    PDFAppToolStepList* addSteps(std::vector<PDFAppToolFlagItem> items);

    /// List of pending changes (389:20177)
    PDFAppToolPendingList* addPendingList(const QString& title, QAbstractItemModel* model);

    /// A control of a parameter through the binder (Custom…, 294:1340) with its value
    /// store; nullptr for a type without a control
    PDFAppSettingControl* addValueControl(const QString& parameter);

    /// Any other widget, a child of the widget of the content
    void addWidget(QWidget* widget);

    /// The added widget (a list of its own with a scroll bar) takes the rest of the height
    /// of the panel; the pending list does it itself
    void setFillsPanel(QWidget* widget);

private:
    QToolButton* createActionButton(const QString& text, const QString& iconName);
    QToolButton* createMenuButton(const QString& iconName, const QString& text);
    QBoxLayout* getCurrentLayout() const;

    QPointer<PDFDocumentToolGUI> m_gui;
    QPointer<QWidget> m_widget;
    QBoxLayout* m_layout = nullptr;
    QBoxLayout* m_sectionLayout = nullptr;
    int m_itemCount = 0;
};

}   // namespace pdf

#endif // PDFAPPTOOLPANELBUILDER_H
