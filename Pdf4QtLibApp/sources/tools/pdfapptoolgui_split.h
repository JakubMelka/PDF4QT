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

#ifndef PDFAPPTOOLGUI_SPLIT_H
#define PDFAPPTOOLGUI_SPLIT_H

#include "pdfappglobal.h"
#include "pdfapptoolgui_pages.h"
#include "pdfapptooloutputfiles.h"
#include "pdfdocumenttool_split.h"

#include <QPointer>

namespace pdf
{

class PDFAppSettingControl;

/// The tools pages.split and pages.extract (package A of step 3a of phase 2, plan 9.2)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUISplit
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppToolGUISplit)

public:
    PDFAppToolGUISplit() = delete;

    /// Registers what is not registered yet: the tools in PDFDocumentToolRegistry and their
    /// GUI in PDFDocumentToolGUIRegistry (PDFAppBuiltinTools::registerAll)
    static void registerSplitTools();

    /// The paths of the documents open in the application (R14: a file of a run never replaces
    /// them), canonical; for PDFAppToolRunEnvironment::protectedFilePaths
    static QStringList getOpenDocumentPaths(const PDFDocumentToolGUI* gui);
};

/// Split (pages.split, Figma 383:3315 ... 403:29978): the pages of the selection of the left
/// panel are separated into files by the rule of the toolbar (Split: Every page, Every N pages,
/// At selected pages, At bookmarks, By file size, Odd and even pages; the number of pages per
/// file; Optimize). The left panel lists the files to create (the rows of the pending list of
/// the base: the number, the name, "Pages 1–12"), the grid shows each page with the file it
/// goes to; At selected pages a click on a tile sets or removes the cut before it. The right
/// panel has the field of the rule (the cuts, the bookmarks, the size), Output files and the
/// Summary. The files are computed by the core (PDFDocumentToolSplit::createFiles, the names by
/// PDFDocumentToolFileOutput::createFileNames) - the same as the run.
///
/// Deviations without a frame (Q17 of step 2, said in the report): the grid of the pages is the
/// one grid of the base, not the cards of the files of 383:3315; the list of the bookmarks of
/// 383:6771 is the text field of their numbers.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSplitToolGUI : public PDFAppPageMarkingToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppPageMarkingToolGUI;

public:
    explicit PDFAppSplitToolGUI(QObject* parent = nullptr);
    virtual ~PDFAppSplitToolGUI() override;

    /// The files of the values now (empty with an error)
    const std::vector<PDFDocumentToolFileOutput::File>& getFiles() const { return m_files; }
    const QString& getFilesError() const { return m_filesError; }

    PDFAppToolOutputFilesSection* getOutputFiles() const { return m_outputFiles; }
    PDFKeyValueList* getSummary() const { return m_summary.data(); }

    virtual QString getConfirmText() const override;
    virtual bool isConfirmEnabled() const override;
    virtual QString getStatusText() const override;
    virtual void fillResultInput(PDFAppToolResultInput* input) const override;
    virtual void prepareEnvironment(PDFAppToolRunEnvironment* environment) const override;
    virtual void clickPage(int page, Qt::KeyboardModifiers modifiers) override;

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;

    virtual QSize getSlotSize() const override;
    virtual QString getSelectionLabel() const override;
    virtual QString getPendingTitle() const override;
    virtual QString getGridHeaderText() const override;
    virtual QString getGridHintText() const override;
    virtual std::vector<PDFInteger> getPendingPages() const override;
    virtual PDFAppToolPendingPagesModel::Row createPendingRow(PDFInteger page) const override;
    virtual PDFAppToolPageGridModel::Tile createTile(PDFInteger page) const override;
    virtual bool isPendingListShown(const std::vector<PDFInteger>& pendingPages) const override;
    virtual void onPendingLinkActivated(PDFInteger page) override;
    virtual void onTargetsUpdated() override;
    virtual void updatePanels() override;
    virtual PDFPageSelection::Mode getEntryModeWithoutSelection() const override;

private:
    QString getRule() const;

    /// The template gives one name to every file (383:9088)
    bool isEveryNameEqual() const;

    /// The name of the document without its extension ("Handbook")
    QString getSourceBaseName() const;

    std::vector<PDFDocumentToolFileOutput::File> m_files;
    std::vector<int> m_fileOfPage;  ///< Index of the file of each page, -1 = none
    QString m_filesError;
    QPointer<PDFAppToolOutputFilesSection> m_outputFiles;
    QPointer<PDFAppSettingControl> m_pagesPerFile;
    QPointer<QWidget> m_cutsSection;
    QPointer<QWidget> m_bookmarksSection;
    QPointer<QWidget> m_sizeSection;
    QPointer<PDFAppToolCallout> m_sizeCallout;
    QPointer<PDFKeyValueList> m_summary;
};

/// Extract Pages (pages.extract, Figma 385:9405 ... 403:31676): the pages of the selection are
/// copied, in the order of the document (K49), as a new document (Output: One file, R2) or as a
/// file per page (One file per page, R3 with Output files). The grid shows the pages, the
/// extracted ones marked "extract"; the source never changes - a locked document may be
/// extracted (4.7).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppExtractToolGUI : public PDFAppPageMarkingToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppPageMarkingToolGUI;

public:
    explicit PDFAppExtractToolGUI(QObject* parent = nullptr);
    virtual ~PDFAppExtractToolGUI() override;

    /// One file per page is chosen
    bool isFilePerPage() const;

    PDFAppToolOutputFilesSection* getOutputFiles() const { return m_outputFiles; }
    PDFKeyValueList* getSummary() const { return m_summary.data(); }

    virtual QString getConfirmText() const override;
    virtual bool isConfirmEnabled() const override;
    virtual QString getStatusText() const override;
    virtual void fillResultInput(PDFAppToolResultInput* input) const override;
    virtual void prepareEnvironment(PDFAppToolRunEnvironment* environment) const override;

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;

    virtual QSize getSlotSize() const override;
    virtual QString getSelectionLabel() const override;
    virtual QString getPendingTitle() const override;
    virtual QString getGridHeaderText() const override;
    virtual QString getGridHintText() const override;
    virtual std::vector<PDFInteger> getPendingPages() const override;
    virtual PDFAppToolPendingPagesModel::Row createPendingRow(PDFInteger page) const override;
    virtual PDFAppToolPageGridModel::Tile createTile(PDFInteger page) const override;
    virtual bool isPendingListShown(const std::vector<PDFInteger>& pendingPages) const override;
    virtual void onPendingLinkActivated(PDFInteger page) override;
    virtual void updatePanels() override;

private:
    QPointer<PDFAppToolOutputFilesSection> m_outputFiles;
    QPointer<PDFKeyValueList> m_summary;
    QString m_namesError;
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_SPLIT_H
