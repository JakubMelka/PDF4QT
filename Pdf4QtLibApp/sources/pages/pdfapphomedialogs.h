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

#ifndef PDFAPPHOMEDIALOGS_H
#define PDFAPPHOMEDIALOGS_H

#include "pdfappglobal.h"
#include "pdfappcontext.h"
#include "pdfappfiledrop.h"

#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"

#include <QCheckBox>
#include <QPushButton>

#include <QPointer>

#include <vector>

class QHBoxLayout;

namespace pdf
{

class PDFAppParagraph;

/// Dialog of the pages Home, Open and Recent: the card of the message box of the
/// library (PDFMessageBox: frameless card, shadow, scrim over the parent, Escape)
/// with the content of the frames of Main page (fields, lists, facts, added by
/// PDFMessageBox::addContent) between the head and its own row of buttons
/// (PDFMessageBox::addActionButton: the order of the design, not the order of
/// QDialogButtonBox of the platform). A form without a badge (Save as workspace,
/// Rename) hides the badge. The dialog has the width of the frame
/// (PDFMessageBox::setCardWidth); its height follows its content.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCardDialog : public PDFMessageBox
{
    Q_OBJECT

private:
    using BaseClass = PDFMessageBox;

public:
    /// \param width Width of the card of the design (520 or 440)
    explicit PDFAppCardDialog(PDFThemeIcons::Badge badge, bool hasBadge, const QString& title, const QString& text, int width, QWidget* parent);
    virtual ~PDFAppCardDialog() override;

    /// Adds a button: \p isLeading at the leading edge (a destructive action),
    /// else at the trailing edge in the order of the calls. \p isPrimary is the
    /// default button (Enter). The button does not close the dialog by itself.
    QPushButton* addActionButton(const QString& text, bool isPrimary, bool isLeading);
};

/// Dialog of a name (Rename workspace 955:150171, also a pinned location): the
/// field Name, the error below it (a name of another item) and Cancel / Rename.
/// A subclass writes the name in \p submitName.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppNameDialog : public PDFAppCardDialog
{
    Q_OBJECT

private:
    using BaseClass = PDFAppCardDialog;

public:
    explicit PDFAppNameDialog(const QString& title, const QString& text, const QString& name, const QString& actionText, QWidget* parent);
    virtual ~PDFAppNameDialog() override;

    PDFLineEdit* getNameField() const { return m_nameField; }
    QPushButton* getActionButton() const { return m_actionButton; }
    QPushButton* getCancelButton() const { return m_cancelButton; }

    /// Text of the error below the field (empty = no error)
    QString getErrorText() const;

protected:
    /// Writes the name. \returns false with the error, which the dialog shows
    virtual bool submitName(const QString& name, QString* error) = 0;

    /// Shows the error below the field (empty = hides it)
    void setError(const QString& error);

private:
    void onActionClicked();
    void onNameChanged(const QString& text);

    QPointer<PDFLineEdit> m_nameField;
    QPointer<QWidget> m_errorRow;
    QPointer<PDFAppParagraph> m_errorText;
    QPointer<QPushButton> m_actionButton;
    QPointer<QPushButton> m_cancelButton;
};

/// Rename workspace (955:150171): a name of another workspace is refused
class PDF4QTLIBAPPSHARED_EXPORT PDFAppRenameWorkspaceDialog : public PDFAppNameDialog
{
    Q_OBJECT

private:
    using BaseClass = PDFAppNameDialog;

public:
    explicit PDFAppRenameWorkspaceDialog(PDFAppContext* context, qint64 workspaceId, const QString& name, QWidget* parent);
    virtual ~PDFAppRenameWorkspaceDialog() override;

    qint64 getWorkspaceId() const { return m_workspaceId; }

protected:
    virtual bool submitName(const QString& name, QString* error) override;

private:
    QPointer<PDFAppContext> m_context;
    qint64 m_workspaceId = 0;
};

/// Rename of a pinned location (the menu of a location; no frame of its own, the
/// layout of Rename workspace)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppRenameLocationDialog : public PDFAppNameDialog
{
    Q_OBJECT

private:
    using BaseClass = PDFAppNameDialog;

public:
    explicit PDFAppRenameLocationDialog(PDFAppContext* context, qint64 locationId, const QString& name, QWidget* parent);
    virtual ~PDFAppRenameLocationDialog() override;

    qint64 getLocationId() const { return m_locationId; }

protected:
    virtual bool submitName(const QString& name, QString* error) override;

private:
    QPointer<PDFAppContext> m_context;
    qint64 m_locationId = 0;
};

/// Save as workspace (941:136915, duplicate name 955:147918): the name, the open
/// documents with check boxes (a document, which is not saved to a file, cannot
/// be selected; unsaved changes are marked, the saved file is used), the note
/// and Cancel / Save. A name of an existing workspace shows the error and the
/// buttons Cancel / Replace; a change of the name returns to Save.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSaveWorkspaceDialog : public PDFAppCardDialog
{
    Q_OBJECT

private:
    using BaseClass = PDFAppCardDialog;

public:
    explicit PDFAppSaveWorkspaceDialog(PDFAppContext* context, const std::vector<PDFAppOpenDocument>& documents, QWidget* parent);
    virtual ~PDFAppSaveWorkspaceDialog() override;

    PDFLineEdit* getNameField() const { return m_nameField; }
    QPushButton* getSaveButton() const { return m_saveButton; }
    QPushButton* getReplaceButton() const { return m_replaceButton; }
    QPushButton* getCancelButton() const { return m_cancelButton; }
    QList<QCheckBox*> getDocumentCheckBoxes() const;
    QString getErrorText() const;
    QString getSelectionText() const;

    /// The duplicate name is shown (buttons Cancel / Replace)
    bool isDuplicateShown() const { return m_isDuplicateShown; }

    /// Id of the saved workspace (0 before it is saved)
    qint64 getSavedId() const { return m_savedId; }

    /// Documents, which are selected
    std::vector<PDFWorkspaceDocument> getSelectedDocuments() const;

private:
    void onSaveClicked();
    void onReplaceClicked();
    void onNameChanged(const QString& text);
    void onSelectionChanged();
    void save(bool replaceExisting);
    void setDuplicate(bool isDuplicate, const QString& error);
    void updateButtons();

    struct DocumentRow
    {
        QPointer<QCheckBox> checkBox;
        PDFWorkspaceDocument document;
    };

    QPointer<PDFAppContext> m_context;
    std::vector<DocumentRow> m_documents;
    QPointer<PDFLineEdit> m_nameField;
    QPointer<QWidget> m_errorRow;
    QPointer<PDFAppParagraph> m_errorText;
    QPointer<PDFLabel> m_selectionLabel;
    QPointer<QPushButton> m_saveButton;
    QPointer<QPushButton> m_replaceButton;
    QPointer<QPushButton> m_cancelButton;
    QPointer<QPushButton> m_secondaryCancelButton;
    bool m_isDuplicateShown = false;
    qint64 m_savedId = 0;
};

/// Clear the Recent history? (941:135742): what is removed, also cleared and kept
/// (facts of the repository, PDFRecentRepository::getClearHistorySummary), the
/// option Also remove the pinned items, Clear Recent History / Cancel.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppClearHistoryDialog : public PDFAppCardDialog
{
    Q_OBJECT

private:
    using BaseClass = PDFAppCardDialog;

public:
    explicit PDFAppClearHistoryDialog(PDFAppContext* context, QWidget* parent);
    virtual ~PDFAppClearHistoryDialog() override;

    QCheckBox* getPinnedCheckBox() const { return m_pinnedCheckBox; }
    PDFKeyValueList* getFacts() const { return m_facts; }
    QPushButton* getClearButton() const { return m_clearButton; }
    QPushButton* getCancelButton() const { return m_cancelButton; }

    bool isPinnedIncluded() const;

    /// Reads the summary again (the checkbox, a change of another instance)
    void updateFacts();

private:
    void onClearClicked();

    QPointer<PDFAppContext> m_context;
    QPointer<PDFKeyValueList> m_facts;
    QPointer<QCheckBox> m_pinnedCheckBox;
    QPointer<QPushButton> m_clearButton;
    QPointer<QPushButton> m_cancelButton;
};

/// Delete the workspace? (955:149658): Delete Workspace / Cancel (the default)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDeleteWorkspaceDialog : public PDFAppCardDialog
{
    Q_OBJECT

private:
    using BaseClass = PDFAppCardDialog;

public:
    explicit PDFAppDeleteWorkspaceDialog(const QString& name, int documentCount, qint64 workspaceId, QWidget* parent);
    virtual ~PDFAppDeleteWorkspaceDialog() override;

    qint64 getWorkspaceId() const { return m_workspaceId; }
    QPushButton* getDeleteButton() const { return m_deleteButton; }
    QPushButton* getCancelButton() const { return m_cancelButton; }

private:
    qint64 m_workspaceId = 0;
    QPointer<QPushButton> m_deleteButton;
    QPointer<QPushButton> m_cancelButton;
};

}   // namespace pdf

#endif // PDFAPPHOMEDIALOGS_H
