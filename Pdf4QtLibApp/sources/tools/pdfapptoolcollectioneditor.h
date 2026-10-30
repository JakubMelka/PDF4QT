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

#ifndef PDFAPPTOOLCOLLECTIONEDITOR_H
#define PDFAPPTOOLCOLLECTIONEDITOR_H

#include "pdfappglobal.h"
#include "pdfappcontext.h"
#include "pdfapptoolcollections.h"

#include "pdfdocumenttoolsuggestions.h"

#include "pdfappcollectioniconchooser.h"
#include "pdfappcollectiontoollist.h"
#include "pdfappdocumentinfobar.h"
#include "pdfapppagelayout.h"
#include "pdfappworkspace.h"

#include "pdfcomponentbuttons.h"
#include "pdffluentwidgets.h"

#include <QPointer>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QWidget>

#include <optional>
#include <vector>

class QPushButton;
class QVBoxLayout;

namespace pdf
{

class PDFAppCard;
class PDFAppCollectionIconChooser;
class PDFAppCollectionToolList;
class PDFAppInfoBar;
class PDFAppParagraph;
class PDFIconButton;
class PDFLabel;
class PDFLineEdit;
class PDFTag;
class PDFTextLink;
class PDFToolRow;

/// Edited values of a collection (the draft of the collections editor): only
/// Save writes them (plan of step 6, 4.4; owner decision Q4 - the draft is kept
/// in the history entry of the page, no question when the page is left)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolCollectionDraftState
{
public:
    QString name;
    QString iconName;
    QStringList toolIds;

    bool operator==(const PDFAppToolCollectionDraftState&) const = default;

    /// Values for the view state of the history entry
    QVariantMap toVariantMap() const;

    /// Values of a view state; untrusted (another version wrote it, or it was
    /// changed): the name bounded, an icon outside of the allowed icons is the
    /// default icon, unknown and duplicate tools left out. std::nullopt for a
    /// state without a draft or of a wrong type.
    static std::optional<PDFAppToolCollectionDraftState> fromVariantMap(const QVariantMap& map);
};

/// Identity of the stored collection, from which a draft kept in the history
/// started (owner decision O25): a custom collection by its row and the time of
/// its last change, a built-in collection by its id and the row of its override
/// (0 = none) with its time. A restored draft is compared with the shown
/// collection: another collection drops the draft, a changed one (another
/// instance, an id reused by a new collection - tool_collection has no
/// AUTOINCREMENT) shows Reload / Keep my changes.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolCollectionDraftIdentity
{
public:
    QString builtinId;
    qint64 rowId = 0;
    qint64 modified = 0;

    /// The user has not changed the name: it follows the stored name (the
    /// translated name of a built-in collection after a change of the language
    /// is not a rename, review RC6-2)
    bool isNameFollowingBase = false;

    bool operator==(const PDFAppToolCollectionDraftIdentity&) const = default;

    /// Writes the identity into the values of the draft of the view state
    void insertInto(QVariantMap& map) const;

    /// Identity of the values of a view state; untrusted: std::nullopt for a state
    /// without an identity (an older version) or with values of wrong types or out
    /// of range - such a draft is treated as a draft of a changed collection
    static std::optional<PDFAppToolCollectionDraftIdentity> fromVariantMap(const QVariantMap& map);
};

/// Editor of a collection (Figma Manage collections 283:1888, editor-column
/// 284:2210): the header (name, tag Built-in, Reset to default), the card with the
/// name and its help or error, the icon chooser, the tools of the collection with
/// their order (PDFAppCollectionToolList), Add a tool (search of the catalog by
/// PDFDocumentToolSearch, at most MAXIMUM_RESULTS results without the tools of the
/// collection; without a text the suggestions of PDFDocumentToolSuggestions over
/// the tools of the collection and their categories), the actions Delete
/// collection / Cancel / Save collection and the note below the card.
///
/// The editor holds the draft: every edit changes only the draft (draftChanged),
/// Save is requested by saveRequested and written by the page in one transaction.
/// A collection changed by another instance (setStoredCollection) is followed
/// at once, when the draft has no change; with a change the draft stays and the
/// info bar "This collection was changed in another window" offers Reload / Keep
/// my changes. A collection deleted elsewhere turns the draft into a new
/// collection (Save creates it again). Widgets only in the GUI thread.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolCollectionEditor : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Most results of the search of Add a tool and most suggestions
    static constexpr int MAXIMUM_RESULTS = 8;
    static constexpr int MAXIMUM_SUGGESTIONS = 3;

    /// Width of the name field and of the icon chooser beside each other
    /// (283:1888: 318 + 24 + 418); a narrower card has the icons below the name
    static constexpr int NAME_AND_ICON_THRESHOLD = 640;

    /// Width of the card, below which Cancel and Save are below Delete collection
    static constexpr int ACTIONS_THRESHOLD = 460;

    /// Height of the buttons of the actions (284:2441)
    static constexpr int BUTTON_HEIGHT = 36;

    /// Line height of the texts of 11 px (Inter, line height normal): the help of
    /// the name 284:2224, the caption of the suggestions and the note 284:2463
    /// (QLabel would take the line spacing of the font, 14)
    static constexpr int CAPTION_LINE_HEIGHT = 13;

    explicit PDFAppToolCollectionEditor(PDFAppContext* context, QWidget* parent = nullptr);
    virtual ~PDFAppToolCollectionEditor() override;

    /// Shows the stored collection (its draft has no change); nullptr = a new
    /// collection with the tools \p newTools (?tool= of the route)
    void setCollection(const PDFAppToolCollection* collection, const QStringList& newTools = QStringList());

    /// The editor shows a new collection (also a collection deleted by another
    /// instance, while its draft was changed)
    bool isNewCollection() const { return m_isNew; }

    /// The shown collection (its route id, built-in id and row id; empty / 0 for a new one)
    const QString& getRouteId() const { return m_routeId; }
    const QString& getBuiltinId() const { return m_builtinId; }
    qint64 getRowId() const { return m_rowId; }
    bool isBuiltin() const { return !m_builtinId.isEmpty(); }

    /// Draft and the stored values, from which it started
    const PDFAppToolCollectionDraftState& getDraft() const { return m_draft; }
    const PDFAppToolCollectionDraftState& getBase() const { return m_base; }

    /// Time of the last change of the stored collection, which the editor has seen
    /// (a change reported by Reload / Keep my changes, else the base of the draft):
    /// Save is refused, when the stored row has another time (PDFToolCollectionDraft::expectedModified)
    qint64 getSeenModified() const { return m_pendingStored ? m_pendingStored->modified : m_baseModified; }

    /// Replaces the draft (a draft restored from the history); invalid values are
    /// sanitized as by PDFAppToolCollectionDraftState::fromVariantMap
    void setDraft(const PDFAppToolCollectionDraftState& draft);

    /// Identity of the stored collection, from which the draft started (O25)
    PDFAppToolCollectionDraftIdentity getDraftIdentity() const;

    /// Restores a draft of the history (O25). The editor of a new collection takes
    /// it. A draft of another collection than the shown one (\p identity) is
    /// dropped: \returns false. An untouched name follows the stored name. A draft
    /// of a changed collection (or without an identity) is kept and, when it
    /// differs from the shown collection \p stored, the info bar offers Reload /
    /// Keep my changes.
    bool restoreDraft(const PDFAppToolCollectionDraftState& draft, const std::optional<PDFAppToolCollectionDraftIdentity>& identity,
                      const PDFAppToolCollection* stored);

    /// The stored names have changed (another instance): the check of a duplicate
    /// name is done again (it is done once per change of the name otherwise)
    void refreshNameCheck();

    /// The draft differs from the stored collection
    bool isModified() const;

    /// The collection changed or deleted (nullptr) by another instance (a refresh of
    /// the page). Without a change of the draft the editor shows the new values
    /// (a deleted collection: \returns false - the page leaves it); with a change the
    /// info bar is shown. \returns true, when the editor still shows the collection.
    bool setStoredCollection(const PDFAppToolCollection* collection);

    /// The stored values of the shown collection have changed without a change of
    /// the user (the translated name of a built-in collection after a change of the
    /// language): an unchanged draft follows them, a changed draft stays
    void rebase(const PDFAppToolCollection& collection);

    /// Data of the suggestions (the counters of the pairs of the tools of the
    /// collection, the setting tools.suggestNext)
    void setSuggestionData(const std::vector<PDFDocumentToolPair>& pairs, bool isSuggestNextEnabled);

    /// Number of the custom collections (a new collection over the limit cannot be saved)
    void setCustomCollectionCount(int count);

    /// Error of the last Save shown above the actions (empty = none)
    void setSaveError(const QString& error);
    QString getSaveError() const;

    /// Error of the name (a duplicate name refused by Save), empty = none
    void setNameError(const QString& error);
    QString getNameMessage() const;
    bool isNameInvalid() const;

    /// Text of the search of Add a tool
    QString getSearchText() const;
    void setSearchText(const QString& text);

    /// The draft can be saved (a name, a tool, not a duplicate, the limit); \p reason
    /// gets the reason, why it cannot
    bool canSave(QString* reason = nullptr) const;

    /// Fills the draft with the definition of the built-in collection (Reset to default)
    void resetToDefault();

    /// Returns the draft to the stored values (Cancel of a stored collection)
    void revert();

    /// Adds the tool at the end of the draft (Add a tool). \returns false for an
    /// invalid tool, a tool of the collection or a deleted editor.
    bool addTool(const QString& toolId);

    /// Tools offered by Add a tool (the results of the search or the suggestions)
    QStringList getOfferedTools() const;

    /// Parts of the editor (tests)
    PDFAppHeaderText* getTitleLabel() const { return m_titleLabel; }
    PDFTag* getBuiltinTag() const { return m_builtinTag; }
    PDFTextLink* getResetLink() const { return m_resetLink; }
    PDFLineEdit* getNameField() const { return m_nameField; }
    PDFAppCollectionIconChooser* getIconChooser() const { return m_iconChooser; }
    PDFAppCollectionToolList* getToolList() const { return m_toolList; }
    PDFLabel* getToolsHint() const { return m_toolsHint; }
    PDFLineEdit* getSearchField() const { return m_searchField; }
    PDFLabel* getOfferCaption() const { return m_offerCaption; }
    QPushButton* getDeleteButton() const { return m_deleteButton; }
    QPushButton* getCancelButton() const { return m_cancelButton; }
    QPushButton* getSaveButton() const { return m_saveButton; }
    PDFAppInfoBar* getInfoBar() const { return m_infoBar; }
    PDFAppCard* getCard() const { return m_card; }
    PDFLabel* getNoteLabel() const { return m_noteLabel; }
    PDFToolRow* findOfferedToolRow(const QString& toolId) const;
    PDFIconButton* findAddButton(const QString& toolId) const;

Q_SIGNALS:
    /// The draft has changed (the page keeps nothing; its view state reads the draft)
    void draftChanged();

    /// Save collection
    void saveRequested();

    /// Delete collection (a custom collection)
    void deleteRequested();

    /// Cancel of a new collection: the page leaves the editor (a stored collection
    /// is reverted by the editor itself)
    void leaveRequested();

    /// The user has activated a tool of the collection (the page opens it)
    void toolActivated(const QString& toolId);

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    enum class InfoBarKind
    {
        None,
        Changed,
        Deleted
    };

    QWidget* createHeader();
    QWidget* createNameAndIcon(QWidget* parent);
    QWidget* createToolsSection(QWidget* parent);
    QWidget* createAddSection(QWidget* parent);
    QWidget* createActions(QWidget* parent);

    void retranslate();
    void applyDraftToWidgets();
    void updateState();
    void updateOffers();
    void updateInfoBar();
    void updateInfoBarGeometry();
    void setInfoBarKind(InfoBarKind kind);
    void notifyDraftChanged();
    QString getDuplicateNameError() const;
    QStringList computeSuggestions() const;
    static PDFAppToolCollectionDraftState getStoredState(const PDFAppToolCollection& collection);

    void onNameEdited(const QString& text);
    void onIconChosen(const QString& iconName);
    void onToolsChanged(const QStringList& toolIds);
    void onSearchTextChanged(const QString& text);
    void onSearchReturnPressed();
    void onOfferClicked();
    void onResetClicked();
    void onDeleteClicked();
    void onCancelClicked();
    void onSaveClicked();
    void onInfoBarAction(const QString& infoBarId, const QString& actionId);
    void onInfoBarCloseRequested(const QString& infoBarId);

    struct Offer
    {
        QString toolId;
        QPointer<QWidget> widget;
        QPointer<PDFToolRow> toolRow;
        QPointer<PDFIconButton> addButton;
    };

    QPointer<PDFAppContext> m_context;

    // The shown collection
    QString m_routeId;
    QString m_builtinId;
    qint64 m_rowId = 0;
    qint64 m_baseRowId = 0;         ///< Row of the stored collection of the base of the draft (kept, when it is deleted)
    qint64 m_baseModified = 0;      ///< Time of the last change of the stored collection of the base
    bool m_isNew = true;
    PDFAppToolCollectionDraftState m_base;
    PDFAppToolCollectionDraftState m_draft;
    std::optional<PDFAppToolCollection> m_pendingStored;
    InfoBarKind m_infoBarKind = InfoBarKind::None;

    // Data of the suggestions and of the checks
    std::vector<PDFDocumentToolPair> m_pairs;
    bool m_isSuggestNextEnabled = true;
    int m_customCollectionCount = 0;
    QString m_saveError;
    QString m_nameError;

    /// Result of the last check of a duplicate name and what it was checked for
    /// (one query of the database per change of the name, review RC6-2)
    mutable QString m_duplicateNameError;
    mutable QString m_duplicateCheckKey;
    mutable bool m_isDuplicateCheckValid = false;

    bool m_isNameTouched = false;
    bool m_isUpdating = false;

    // Widgets
    QPointer<QVBoxLayout> m_layout;
    QPointer<PDFAppHeaderText> m_titleLabel;
    QPointer<PDFTag> m_builtinTag;
    QPointer<PDFTextLink> m_resetLink;
    QPointer<QWidget> m_infoBarSlot;
    QPointer<PDFAppInfoBar> m_infoBar;
    QPointer<PDFAppCard> m_card;
    QPointer<PDFLabel> m_nameLabel;
    QPointer<PDFLineEdit> m_nameField;
    QPointer<PDFLabel> m_nameHelp;
    QPointer<PDFLabel> m_iconLabel;
    QPointer<PDFAppCollectionIconChooser> m_iconChooser;
    QPointer<PDFLabel> m_toolsTitle;
    QPointer<PDFLabel> m_toolsHint;
    QPointer<PDFAppCollectionToolList> m_toolList;
    QPointer<PDFLabel> m_addTitle;
    QPointer<PDFLineEdit> m_searchField;
    QPointer<PDFLabel> m_offerCaption;
    QPointer<QWidget> m_offerContainer;
    QPointer<QVBoxLayout> m_offerLayout;
    std::vector<Offer> m_offers;
    QPointer<PDFLabel> m_saveErrorLabel;
    QPointer<QPushButton> m_deleteButton;
    QPointer<QPushButton> m_cancelButton;
    QPointer<QPushButton> m_saveButton;
    QPointer<PDFLabel> m_noteLabel;
};

}   // namespace pdf

#endif // PDFAPPTOOLCOLLECTIONEDITOR_H
