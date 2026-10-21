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

#ifndef PDFAPPABOUTPAGE_H
#define PDFAPPABOUTPAGE_H

#include "pdfappglobal.h"
#include "pdfappaboutinfo.h"
#include "pdfappinfowidgets.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"

#include <QAbstractTableModel>
#include <QPointer>
#include <QPushButton>

#include <vector>

namespace pdf
{

/// Model of the table Third-party libraries (Library row 110:2158): the columns
/// Library (Semi Bold 12 text/primary), Version (text/secondary), Purpose
/// (text/tertiary) and License (text/secondary, aligned to the trailing edge).
/// The colors are the tokens of the active theme at the time of the query.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppLibraryModel : public QAbstractTableModel
{
    Q_OBJECT

private:
    using BaseClass = QAbstractTableModel;

public:
    enum Column
    {
        LibraryColumn,
        VersionColumn,
        PurposeColumn,
        LicenseColumn,
        ColumnCount
    };

    explicit PDFAppLibraryModel(std::vector<PDFAppThirdPartyLibrary> libraries, QObject* parent = nullptr);
    virtual ~PDFAppLibraryModel() override;

    virtual int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    virtual QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    const std::vector<PDFAppThirdPartyLibrary>& getLibraries() const { return m_libraries; }

private:
    std::vector<PDFAppThirdPartyLibrary> m_libraries;
};

/// Table of the third-party libraries: the column Purpose is hidden, when the
/// view is too narrow for it (the minimal window)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppLibraryView : public PDFTreeView
{
    Q_OBJECT

private:
    using BaseClass = PDFTreeView;

public:
    /// Widths of the fixed columns (the texts 150 / 90 / 110 of the design with
    /// the gaps 12; each cell has the padding 10 on both sides, so the column of
    /// the library is 10 + 150 + 12 - 10) and the narrowest view showing Purpose
    static constexpr int LIBRARY_WIDTH = 162;
    static constexpr int VERSION_WIDTH = 102;
    static constexpr int LICENSE_WIDTH = 132;
    static constexpr int MINIMUM_PURPOSE_WIDTH = 120;

    explicit PDFAppLibraryView(QWidget* parent = nullptr);
    virtual ~PDFAppLibraryView() override;

    /// Applies the widths of the columns (after the model is set)
    void updateColumns();

protected:
    virtual void resizeEvent(QResizeEvent* event) override;
};

/// Page About (pdf4qt-about 110:2163): the application (logo, version, facts,
/// website and GitHub), the author, the third-party libraries (anchor
/// #third-party), the build information with Copy build info, the license and
/// the links. The build information names the database and the folder of the
/// logs, or says, that nothing is written in the portable mode; it is updated,
/// when the state of the storage changes.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAboutPage : public PDFAppContentPage
{
    Q_OBJECT

private:
    using BaseClass = PDFAppContentPage;

public:
    /// Fragment of the route of the third-party libraries
    static constexpr const char* THIRD_PARTY_ANCHOR = "third-party";

    explicit PDFAppAboutPage(PDFAppContext* context, QWidget* parent = nullptr);
    virtual ~PDFAppAboutPage() override;

    virtual bool setRoute(const PDFAppRoute& route) override;
    virtual QString getTitle() const override;
    virtual QString getHeaderTitle() const override;
    virtual QString getHeaderSubtitle() const override;

    /// Address of the license texts of the third-party libraries (Third-party notices)
    static QUrl getThirdPartyNoticesUrl();

    /// Parts of the page (tests)
    QWidget* getAppCard() const { return m_appCard; }
    QWidget* getAuthorSection() const { return m_authorSection; }
    QWidget* getLibrariesSection() const { return m_librariesSection; }
    PDFAppLibraryView* getLibraryView() const { return m_libraryView; }
    PDFKeyValueList* getBuildInfo() const { return m_buildInfo; }
    QPushButton* getCopyButton() const { return m_copyButton; }
    QPushButton* getWebsiteButton() const { return m_websiteButton; }
    QPushButton* getGitHubButton() const { return m_gitHubButton; }
    QPushButton* getProfileButton() const { return m_profileButton; }
    QPushButton* getSponsorButton() const { return m_sponsorButton; }
    PDFTextLink* getNoticesLink() const { return m_noticesLink; }
    PDFTextLink* getLicenseLink() const { return m_licenseLink; }
    QList<PDFLinkRow*> getLinkRows() const;
    PDFAppColumns* getColumns() const { return m_columns; }

    /// Rows shown in the build information
    const QList<PDFAppBuildInfoRow>& getBuildRows() const { return m_buildRows; }

private:
    QWidget* createAppCard(QWidget* parent);
    QWidget* createAuthorSection(QWidget* parent);
    QWidget* createLibrariesSection(QWidget* parent);
    QWidget* createBuildSection(QWidget* parent);
    QWidget* createLicenseSection(QWidget* parent);
    QWidget* createLinksSection(QWidget* parent);
    PDFLinkRow* addLinkRow(QWidget* card, const QString& title, const QString& subtitle, const QString& iconName);

    void updateBuildInfo();
    void onStorageStateChanged();
    void onCopyClicked();

    QPointer<PDFAppColumns> m_columns;
    QPointer<QWidget> m_appCard;
    QPointer<QWidget> m_authorSection;
    QPointer<QWidget> m_librariesSection;
    QPointer<PDFAppLibraryView> m_libraryView;
    QPointer<PDFKeyValueList> m_buildInfo;
    QPointer<QPushButton> m_copyButton;
    QPointer<QPushButton> m_websiteButton;
    QPointer<QPushButton> m_gitHubButton;
    QPointer<QPushButton> m_profileButton;
    QPointer<QPushButton> m_sponsorButton;
    QPointer<PDFTextLink> m_noticesLink;
    QPointer<PDFTextLink> m_licenseLink;
    QList<QPointer<PDFLinkRow>> m_linkRows;
    QList<PDFAppBuildInfoRow> m_buildRows;
};

/// Factory of the page About
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAboutPageFactory : public PDFAppPageFactory
{
public:
    virtual PDFAppPage* createPage(PDFAppContext* context, QWidget* parent) const override;
};

}   // namespace pdf

#endif // PDFAPPABOUTPAGE_H
