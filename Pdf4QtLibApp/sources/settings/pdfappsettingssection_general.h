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

#ifndef PDFAPPSETTINGSSECTION_GENERAL_H
#define PDFAPPSETTINGSSECTION_GENERAL_H

#include "pdfappglobal.h"
#include "pdfapphomedialogs.h"
#include "pdfappsettingssection.h"

#include <QPointer>

class QFrame;
class QPushButton;

namespace pdf
{

class PDFAppSettingRow;
class PDFKeyValueList;

/// Clear the tool history? (941:136741): the warning badge, the text with the
/// number of the recorded runs, the facts Deleted / Kept and Clear Tool History
/// (Secondary, leading) and Cancel (Primary, the default). The texts follow the
/// changes of the database (another instance records or clears runs) while the
/// dialog is open; without a recorded run Clear Tool History is disabled. The
/// dialog only answers: the General section clears the history, when it is
/// accepted.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppClearToolHistoryDialog : public PDFAppCardDialog
{
    Q_OBJECT

private:
    using BaseClass = PDFAppCardDialog;

public:
    /// Width of the card of 941:136881
    static constexpr int CARD_WIDTH = 520;

    explicit PDFAppClearToolHistoryDialog(PDFAppContext* context, QWidget* parent);
    virtual ~PDFAppClearToolHistoryDialog() override;

    PDFKeyValueList* getFacts() const { return m_facts; }
    QPushButton* getClearButton() const { return m_clearButton; }
    QPushButton* getCancelButton() const { return m_cancelButton; }

    /// Number of the recorded runs shown
    qint64 getRunCount() const { return m_runCount; }

    /// Reads the number of the runs again and updates the texts
    void updateTexts();

protected:
    virtual void changeEvent(QEvent* event) override;

private:
    void onDatabaseChanged(const QString& topic, const QString& key);

    QPointer<PDFAppContext> m_context;
    QPointer<QFrame> m_factsFrame;
    QPointer<PDFKeyValueList> m_facts;
    QPointer<QPushButton> m_clearButton;
    QPointer<QPushButton> m_cancelButton;
    qint64 m_runCount = 0;
};

/// Section General (146:2843, package A of step 5): the generic rows of the
/// definitions (Language, Color scheme, Recent files, the sidebar, Suggest tools,
/// Check for updates) and the custom row Tool history of the group Tools (the
/// number of the recorded runs and Clear history…, which opens
/// PDFAppClearToolHistoryDialog). The row Language says, when the translation of
/// the chosen language could not be loaded (English is shown then).
///
/// The language itself is applied by PDFAppTranslator of the context (also a change
/// of another instance); a lowered Recent files limit trims the history by the
/// context (PDFAppContext::applyRecentLimit).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppGeneralSettingsSection : public PDFAppGenericSettingsSection
{
    Q_OBJECT

private:
    using BaseClass = PDFAppGenericSettingsSection;

public:
    explicit PDFAppGeneralSettingsSection(PDFAppContext* context, const QString& sectionId, QWidget* parent = nullptr);
    virtual ~PDFAppGeneralSettingsSection() override;

    /// Anchor of the row Tool history (/settings/general#tool-history)
    static QString getToolHistoryAnchor() { return QStringLiteral("tool-history"); }

    /// Base description of the row Tool history (without the number of the runs)
    static QString getToolHistoryDescription();

    /// Text of the number of the runs: "37 runs recorded", "no runs recorded"
    static QString getRunCountText(qint64 runs);

    /// Row Tool history and its button Clear history…
    PDFAppSettingRow* getToolHistoryRow() const { return m_toolHistoryRow; }
    QPushButton* getClearHistoryButton() const { return m_clearHistoryButton; }

    /// Dialog Clear the tool history? (nullptr = not open)
    PDFAppClearToolHistoryDialog* getClearHistoryDialog() const { return m_clearHistoryDialog; }

    /// Opens the dialog Clear the tool history? (false = no run is recorded)
    bool openClearHistoryDialog();

    /// Number of the recorded runs shown by the row Tool history
    qint64 getShownRunCount() const { return m_shownRunCount; }

    virtual void refresh(const QString& topic, const QString& key) override;

protected:
    virtual void finishContent() override;

private:
    void updateToolHistory();
    void updateLanguageRow();
    void onClearHistoryClicked();
    void onClearHistoryDialogFinished(int result);

    QPointer<PDFAppSettingRow> m_toolHistoryRow;
    QPointer<QPushButton> m_clearHistoryButton;
    QPointer<PDFAppClearToolHistoryDialog> m_clearHistoryDialog;
    qint64 m_shownRunCount = 0;
};

class PDF4QTLIBAPPSHARED_EXPORT PDFAppGeneralSettingsSectionFactory : public PDFAppSettingsSectionFactory
{
public:
    virtual PDFAppSettingsSection* createSection(PDFAppContext* context, const QString& sectionId, QWidget* parent) const override;
};

}   // namespace pdf

#endif // PDFAPPSETTINGSSECTION_GENERAL_H
