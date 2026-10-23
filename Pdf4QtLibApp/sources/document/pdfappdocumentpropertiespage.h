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

#ifndef PDFAPPDOCUMENTPROPERTIESPAGE_H
#define PDFAPPDOCUMENTPROPERTIESPAGE_H

#include "pdfappglobal.h"
#include "pdfapppage.h"
#include "pdfappdocumentpropertiesdata.h"

#include <QFutureWatcher>
#include <QPointer>
#include <QTimer>

#include <atomic>
#include <memory>
#include <vector>

class QLabel;
class QPushButton;
class QScrollArea;
class QStandardItemModel;
class QVBoxLayout;

namespace pdf
{

class PDFAppDocument;
class PDFAppDocumentStatusBar;
class PDFAppDocumentTab;
class PDFLabel;
class PDFNavList;
class PDFPageThumbnail;
class PDFPlainTextEdit;
class PDFPropertyRow;

/// Bar of the page Document properties (properties-bar 236:9026): 52 px,
/// bg/subtle with the border at the bottom
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentPropertiesBar : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int HEIGHT = 52;

    explicit PDFAppDocumentPropertiesBar(QWidget* parent);
    virtual ~PDFAppDocumentPropertiesBar() override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
};

/// Page Document properties of a document tab (/document/{id}/properties/{section},
/// Figma 236:8882 … 236:9574): the bar with Back to document and Copy all, the
/// navigation with the summary of the document and the five sections Properties
/// (general), Security, Fonts, Display and print, XMP metadata, and the status
/// bar of the tab. The page replaces the view of the tab (the view and the
/// document stay); Back to document returns to it.
///
/// The texts of the Info dictionary (Title, Subject, Author, Keywords and the
/// custom properties) and the XMP packet are edited here: each change is one
/// undo step through PDFAppDocument::commit; a locked document (or a document,
/// which another operation blocks) cannot be edited. The fonts are scanned in a
/// worker thread once per version of the document (canceled, when the page goes).
/// The page follows the changes of its document (undo, redo, reload).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentPropertiesPage : public PDFAppPage
{
    Q_OBJECT

private:
    using BaseClass = PDFAppPage;

public:
    /// Sections of the page in the order of the navigation
    enum class Section
    {
        General,
        Security,
        Fonts,
        Display,
        Xmp
    };

    explicit PDFAppDocumentPropertiesPage(PDFAppContext* context, QWidget* parent = nullptr);
    virtual ~PDFAppDocumentPropertiesPage() override;

    /// Ids of the sections in the route (general, security, fonts, display, xmp)
    static QStringList getSectionIds();

    virtual bool setRoute(const PDFAppRoute& route) override;
    virtual QString getTitle() const override;
    virtual bool hasSearchHeader() const override;
    virtual QVariantMap saveViewState() const override;
    virtual void restoreViewState(const QVariantMap& state) override;

    Section getSection() const { return m_section; }
    QString getSectionId() const;

    /// Tab of the page (nullptr, when the page is not in a document tab)
    PDFAppDocumentTab* getDocumentTab() const;

    /// Text of Copy all (every section; the fonts, when they have been scanned)
    QString getCopyText() const;

    /// The fonts are being scanned
    bool isFontScanRunning() const;

    /// Fonts of the last scan
    const std::vector<PDFAppFontInfo>& getFonts() const { return m_fonts; }

    /// Widget of the shown section (nullptr while it is being built)
    QWidget* getSectionWidget() const { return m_sectionWidget.data(); }

    /// Editor of the XMP packet (section xmp)
    PDFPlainTextEdit* getMetadataEditor() const;

    /// The page may change the document (ready, not locked, not blocked)
    bool isEditable() const;

    /// Rebuilds the shown section now (tests; normally after the events)
    void rebuildNow();

Q_SIGNALS:
    /// A scan of the fonts has finished
    void fontsScanned();

private:
    void buildSkeleton();
    void scheduleRebuild();
    void rebuildSection();
    QWidget* createSectionHeader(const QString& title, const QString& subtitle, QWidget* parent);
    QWidget* createGroup(const PDFAppPropertyGroup& group, QWidget* parent);
    QWidget* createPermissionsGroup(QWidget* parent);
    QWidget* buildGeneral(QWidget* parent);
    QWidget* buildSecurity(QWidget* parent);
    QWidget* buildFonts(QWidget* parent);
    QWidget* buildDisplay(QWidget* parent);
    QWidget* buildXmp(QWidget* parent);
    void updateSummary();
    void updateMetadataState();
    void showStatusMessage(const QString& text);
    bool commitDocument(PDFDocumentPointer document, const QString& description);
    QString getFontsSubtitle() const;

    void onNavIndexChanged(int index);
    void onBackClicked();
    void onCopyAllClicked();
    void onPropertyEdited(const QString& value);
    void onRowEditingChanged(bool isEditing);
    void onDocumentChanged();
    void onDocumentStateChanged();
    void onRebuildTimeout();
    void startFontScan();
    void onFontScanFinished();
    void onMetadataTextChanged();
    void onApplyMetadata();
    void onDiscardMetadata();
    void onCreateDefaultMetadata();
    void onThemeChanged();

    QPointer<PDFAppDocumentTab> m_tab;
    QPointer<PDFAppDocument> m_document;
    Section m_section = Section::General;

    QPointer<PDFAppDocumentPropertiesBar> m_bar;
    QPointer<QPushButton> m_backButton;
    QPointer<QPushButton> m_copyAllButton;
    QPointer<PDFPageThumbnail> m_thumbnail;
    QPointer<PDFLabel> m_fileName;
    QPointer<PDFLabel> m_fileMeta;
    QPointer<PDFNavList> m_navigation;
    QPointer<QScrollArea> m_scrollArea;
    QPointer<QWidget> m_content;
    QPointer<QVBoxLayout> m_contentLayout;
    QPointer<QWidget> m_sectionWidget;
    QPointer<PDFAppDocumentStatusBar> m_statusBar;

    // Section XMP
    QPointer<PDFPlainTextEdit> m_metadataEditor;
    QPointer<QPushButton> m_createMetadataButton;
    QPointer<QPushButton> m_applyMetadataButton;
    QPointer<QPushButton> m_discardMetadataButton;
    QPointer<PDFLabel> m_metadataError;
    QString m_metadataText;
    QString m_metadataErrorText;    ///< Text of the editor, which the error is about
    bool m_hasMetadata = false;
    bool m_isMetadataModified = false;

    // Fonts (worker thread)
    QPointer<QFutureWatcher<std::vector<PDFAppFontInfo>>> m_fontWatcher;
    std::shared_ptr<std::atomic_bool> m_fontScanCancel;
    std::vector<PDFAppFontInfo> m_fonts;
    quint64 m_fontsVersion = 0;
    quint64 m_fontScanVersion = 0;
    bool m_hasFonts = false;

    QTimer m_rebuildTimer;
    bool m_isRebuildPending = false;
    int m_pendingScroll = -1;

    /// The shown section has been built for a document, which could be changed
    /// (editable rows): a change of the state of the document rebuilds the
    /// section only, when this changes (not with each step of an operation)
    bool m_isSectionEditable = false;
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTPROPERTIESPAGE_H
