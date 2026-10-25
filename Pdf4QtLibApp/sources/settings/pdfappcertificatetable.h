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

#ifndef PDFAPPCERTIFICATETABLE_H
#define PDFAPPCERTIFICATETABLE_H

#include "pdfappglobal.h"
#include "pdfapphomedialogs.h"
#include "pdftrustedcertificaterepository.h"

#include "pdfcertificatestore.h"
#include "pdffluentwidgets.h"

#include <QAbstractTableModel>
#include <QByteArray>
#include <QDateTime>
#include <QFutureWatcher>
#include <QPointer>
#include <QString>
#include <QWidget>

#include <vector>

class QPushButton;

namespace pdf
{

class PDFAppContext;

/// A certificate of the table of the trusted certificates (Settings › Security,
/// 147:3781). Its texts come from the certificate - untrusted data: one line
/// without control and format characters, at most MAXIMUM_TEXT_LENGTH characters.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCertificateEntry
{
public:
    enum class Type
    {
        User,   ///< A row of the table trusted_certificate (can be removed)
        System  ///< A certificate of the system store (read only)
    };

    static constexpr int MAXIMUM_TEXT_LENGTH = 256;

    Type type = Type::User;
    qint64 id = 0;              ///< Row of the table trusted_certificate (User)
    QByteArray data;            ///< DER data (may be damaged or empty)
    bool isDamaged = false;     ///< The data are not a certificate
    QString name;               ///< Common name, organization or e-mail
    QString organization;
    QString issuer;
    QDateTime validFrom;
    QDateTime validTo;

    /// Entry of the data (a damaged entry, when the data are not a certificate)
    static PDFAppCertificateEntry create(Type type, qint64 id, const QByteArray& data);

    /// Entry of a parsed certificate (the system store)
    static PDFAppCertificateEntry create(Type type, const PDFCertificateInfo& info);

    /// Text of a certificate as one safe line (control, format and bidirectional
    /// characters removed, cut to MAXIMUM_TEXT_LENGTH)
    static QString sanitizeText(const QString& text);

    /// SHA-256 fingerprint of the data in groups of four hexadecimal digits
    static QString getFingerprint(const QByteArray& data);
};

/// Certificates of the system store and the number of the certificates of the
/// Adobe Approved Trust List, read in a worker thread
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSystemCertificates
{
public:
    std::vector<PDFAppCertificateEntry> entries;
    int trustListCount = 0;
    bool isSystemStoreUsed = false;
};

/// Model of the table (columns Type, Certificate, Organization, Valid from, Valid
/// to): the certificates of the user first, then the system certificates sorted
/// by their names
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCertificateTableModel : public QAbstractTableModel
{
    Q_OBJECT

private:
    using BaseClass = QAbstractTableModel;

public:
    enum Column
    {
        TypeColumn,
        NameColumn,
        OrganizationColumn,
        ValidFromColumn,
        ValidToColumn,
        ColumnCount
    };

    explicit PDFAppCertificateTableModel(QObject* parent);
    virtual ~PDFAppCertificateTableModel() override;

    virtual int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    virtual QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    /// Replaces the rows (a model reset)
    void setEntries(std::vector<PDFAppCertificateEntry> userEntries, std::vector<PDFAppCertificateEntry> systemEntries);

    const std::vector<PDFAppCertificateEntry>& getEntries() const { return m_entries; }
    const PDFAppCertificateEntry* getEntry(int row) const;

    /// Number of the rows of the type
    int getCount(PDFAppCertificateEntry::Type type) const;

    /// Row of the certificate of the user \p id, or of the system certificate of
    /// the data (-1 = none)
    int findUserRow(qint64 id) const;
    int findSystemRow(const QByteArray& data) const;

    /// Text of the type ("User", "System")
    static QString getTypeText(PDFAppCertificateEntry::Type type);

private:
    std::vector<PDFAppCertificateEntry> m_entries;
};

/// Reads the system certificates and the trust list in a worker thread
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSystemCertificatesTask
{
public:
    using Provider = PDFCertificateEntries (*)();

    explicit PDFAppSystemCertificatesTask(bool isSystemStoreUsed, Provider provider);

    PDFAppSystemCertificates operator()() const;

private:
    bool m_isSystemStoreUsed;
    Provider m_provider;
};

/// Table of the trusted certificates of Settings › Security (147:3781, owner
/// decision Q4): a card with the certificates of the user (table
/// trusted_certificate, also damaged rows) and - with the setting
/// signatures.useSystemStore - the certificates of the system store (read in a
/// worker thread), a line with the number of the certificates of the Adobe
/// Approved Trust List, the status "1 of 5 selected" and Remove. Only the
/// certificates of the user can be removed, after a confirmation naming their
/// subjects and SHA-256 fingerprints; adding stays in the Signatures panel of a
/// document. The table follows the changes of the database (also of another
/// instance) by refresh, keeping the selection.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCertificateTable : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Rows shown without the scroll bar of the view
    static constexpr int MAXIMUM_VISIBLE_ROWS = 8;

    /// Largest number of the system certificates listed
    static constexpr int MAXIMUM_SYSTEM_ROWS = 5000;

    /// Certificates named by the confirmation of Remove
    static constexpr int MAXIMUM_DIALOG_CERTIFICATES = 4;

    /// Widths of the columns of the design (147:3786: the texts 64 / 220 / 84 / 84,
    /// gap 12, row padding 10; the dates are 20 px wider - the dates of the language
    /// of the user interface, O17, are longer than the ISO dates of the frame)
    static constexpr int TYPE_COLUMN_WIDTH = 86;
    static constexpr int ORGANIZATION_COLUMN_WIDTH = 232;
    static constexpr int VALID_FROM_COLUMN_WIDTH = 116;
    static constexpr int VALID_TO_COLUMN_WIDTH = 114;

    explicit PDFAppCertificateTable(PDFAppContext* context, QWidget* parent = nullptr);
    virtual ~PDFAppCertificateTable() override;

    /// The database has changed (topic and key of the notifier; both empty =
    /// everything): the certificates of the user are read again, the system
    /// certificates when signatures.useSystemStore changes
    void refresh(const QString& topic, const QString& key);

    /// Reads the certificates of the user again
    void reloadUserCertificates();

    /// Starts reading of the system certificates (and of the trust list)
    void reloadSystemCertificates();

    /// The system certificates are being read
    bool isLoadingSystemCertificates() const;

    /// Opens the confirmation of Remove of the selected certificates of the user
    /// (false = nothing to remove)
    bool openRemoveDialog();

    /// Parts (tests)
    PDFTreeView* getView() const { return m_view; }
    PDFAppCertificateTableModel* getModel() const { return m_model; }
    QPushButton* getRemoveButton() const { return m_removeButton; }
    PDFLabel* getStatusLabel() const { return m_statusLabel; }
    PDFLabel* getTrustListLabel() const { return m_trustListLabel; }
    PDFLabel* getEmptyLabel() const { return m_emptyLabel; }
    PDFAppCardDialog* getRemoveDialog() const { return m_removeDialog; }
    int getTrustListCount() const { return m_trustListCount; }

    /// Entries of the selected rows
    std::vector<PDFAppCertificateEntry> getSelectedEntries() const;

    /// Provider of the system certificates (tests); nullptr = the system store
    static void setSystemCertificatesProvider(PDFAppSystemCertificatesTask::Provider provider);

Q_SIGNALS:
    /// A certificate could not be removed (the page shows the message)
    void statusMessageRequested(const QString& message);

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;
    virtual void showEvent(QShowEvent* event) override;

private:
    void updateState();
    void updateViewHeight();
    void updateModel();
    bool isSystemStoreUsed() const;
    void onSelectionChanged();
    void onRemoveClicked();
    void onRemoveDialogFinished(int result);
    void onSystemCertificatesLoaded();

    static PDFAppSystemCertificatesTask::Provider s_provider;

    QPointer<PDFAppContext> m_context;
    QPointer<PDFAppCertificateTableModel> m_model;
    QPointer<PDFTreeView> m_view;
    QPointer<PDFLabel> m_emptyLabel;
    QPointer<PDFLabel> m_trustListLabel;
    QPointer<PDFLabel> m_statusLabel;
    QPointer<QPushButton> m_removeButton;
    QPointer<PDFAppCardDialog> m_removeDialog;
    QPointer<QFutureWatcher<PDFAppSystemCertificates>> m_systemWatcher;
    std::vector<PDFAppCertificateEntry> m_userEntries;
    std::vector<PDFAppCertificateEntry> m_systemEntries;
    std::vector<PDFTrustedCertificate> m_pendingRemoval;
    int m_trustListCount = -1;
};

}   // namespace pdf

#endif // PDFAPPCERTIFICATETABLE_H
