//    Copyright (C) 2023 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfbookmarkmanager.h"
#include "pdfaction.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

namespace pdfviewer
{

class PDFBookmarkManagerHelper
{
public:
    constexpr PDFBookmarkManagerHelper() = delete;

    static QJsonObject convertBookmarkToJson(const PDFBookmarkManager::Bookmark& bookmark)
    {
        QJsonObject json;
        json["isAuto"] = bookmark.isAuto;
        json["name"] = bookmark.name;
        json["pageIndex"] = bookmark.pageIndex;
        return json;
    }

    static PDFBookmarkManager::Bookmark convertJsonToBookmark(const QJsonObject& json)
    {
        PDFBookmarkManager::Bookmark bookmark;
        bookmark.isAuto = json["isAuto"].toBool();
        bookmark.name = json["name"].toString();
        bookmark.pageIndex = json["pageIndex"].toInt();
        return bookmark;
    }

    static QJsonObject convertBookmarksToJson(const PDFBookmarkManager::Bookmarks& bookmarks)
    {
        QJsonArray jsonArray;

        for (const auto& bookmark : bookmarks.bookmarks)
        {
            jsonArray.append(convertBookmarkToJson(bookmark));
        }

        QJsonObject jsonObject;
        jsonObject["bookmarks"] = jsonArray;
        return jsonObject;
    }

    static PDFBookmarkManager::Bookmarks convertBookmarksFromJson(const QJsonObject& object)
    {
        PDFBookmarkManager::Bookmarks bookmarks;

        QJsonArray jsonArray = object["bookmarks"].toArray();

        for (const auto& jsonValue : jsonArray)
        {
            bookmarks.bookmarks.push_back(convertJsonToBookmark(jsonValue.toObject()));
        }

        return bookmarks;
    }

    static QJsonDocument convertBookmarksMapToJsonDocument(const std::map<QString, PDFBookmarkManager::Bookmarks>& bookmarksMap)
    {
        QJsonObject mainObject;
        for (const auto& pair : bookmarksMap)
        {
            mainObject[pair.first] = convertBookmarksToJson(pair.second);
        }
        return QJsonDocument(mainObject);
    }

    static std::map<QString, PDFBookmarkManager::Bookmarks> convertBookmarksMapFromJsonDocument(const QJsonDocument &doc)
    {
        std::map<QString, PDFBookmarkManager::Bookmarks> container;
        QJsonObject mainObject = doc.object();

        for (auto it = mainObject.begin(); it != mainObject.end(); ++it)
        {
            container[it.key()] = convertBookmarksFromJson(it.value().toObject());
        }

        return container;
    }
};

PDFBookmarkManager::PDFBookmarkManager(QObject* parent) :
    BaseClass(parent)
{

}

void PDFBookmarkManager::setDocument(const pdf::PDFModifiedDocument& document)
{
    Q_EMIT bookmarksAboutToBeChanged();

    const bool init = !m_document;
    m_document = document.getDocument();

    QString key;

    if (document.hasPreserveView() && m_document)
    {
        // Pass the key
        key = QString::fromLatin1(m_document->getSourceDataHash().toHex());

        if (m_bookmarks.count(m_currentKey) && m_currentKey != key)
        {
            m_bookmarks[key] = m_bookmarks[m_currentKey];
            m_bookmarks.erase(m_currentKey);
        }
    }

    if (init && m_document)
    {
        key = QString::fromLatin1(m_document->getSourceDataHash().toHex());
    }

    if (key.isEmpty())
    {
        key = "generic";
    }

    m_currentKey = key;

    if (document.hasReset() && !document.hasPreserveView())
    {
        regenerateAutoBookmarks();
    }

    Q_EMIT bookmarksChanged();
}

void PDFBookmarkManager::saveToFile(QString fileName)
{
    QJsonDocument doc = PDFBookmarkManagerHelper::convertBookmarksMapToJsonDocument(m_bookmarks);

    // Příklad zápisu do souboru
    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(doc.toJson());
        file.close();
    }
}

bool PDFBookmarkManager::loadFromFile(QString fileName)
{
    QFile file(fileName);
    if (file.open(QIODevice::ReadOnly))
    {
        QJsonDocument loadedDoc = QJsonDocument::fromJson(file.readAll());
        file.close();

        m_bookmarks = PDFBookmarkManagerHelper::convertBookmarksMapFromJsonDocument(loadedDoc);
        return true;
    }

    return false;
}

int PDFBookmarkManager::getBookmarkCount() const
{
    if (m_bookmarks.count(m_currentKey))
    {
        return m_bookmarks.at(m_currentKey).bookmarks.size();
    }

    return 0;
}

PDFBookmarkManager::Bookmark PDFBookmarkManager::getBookmark(int index) const
{
    if (m_bookmarks.count(m_currentKey))
    {
        return m_bookmarks.at(m_currentKey).bookmarks.at(index);
    }

    return Bookmark();
}

void PDFBookmarkManager::regenerateAutoBookmarks()
{
    if (!m_document)
    {
        return;
    }

    // Create bookmarks for all main chapters
    Bookmarks& bookmarks = m_bookmarks[m_currentKey];

    for (auto it = bookmarks.bookmarks.begin(); it != bookmarks.bookmarks.end();)
    {
        if (it->isAuto)
        {
            it = bookmarks.bookmarks.erase(it);
        }
        else
        {
            ++it;
        }
    }

    if (auto outlineRoot = m_document->getCatalog()->getOutlineRootPtr())
    {
        size_t childCount = outlineRoot->getChildCount();
        for (size_t i = 0; i < childCount; ++i)
        {
            Bookmark bookmark;
            bookmark.isAuto = true;
            bookmark.pageIndex = pdf::PDFCatalog::INVALID_PAGE_INDEX;

            const pdf::PDFOutlineItem* child = outlineRoot->getChild(i);
            const pdf::PDFAction* action = child->getAction();

            if (action)
            {
                for (const pdf::PDFAction* currentAction : action->getActionList())
                {
                    if (currentAction->getType() != pdf::ActionType::GoTo)
                    {
                        continue;
                    }

                    const pdf::PDFActionGoTo* typedAction = dynamic_cast<const pdf::PDFActionGoTo*>(currentAction);
                    pdf::PDFDestination destination = typedAction->getDestination();
                    if (destination.getDestinationType() == pdf::DestinationType::Named)
                    {
                        if (const pdf::PDFDestination* targetDestination = m_document->getCatalog()->getNamedDestination(destination.getName()))
                        {
                            destination = *targetDestination;
                        }
                    }

                    if (destination.getDestinationType() != pdf::DestinationType::Invalid &&
                        destination.getPageReference() != pdf::PDFObjectReference())
                    {
                        const size_t pageIndex = m_document->getCatalog()->getPageIndexFromPageReference(destination.getPageReference());
                        if (pageIndex != pdf::PDFCatalog::INVALID_PAGE_INDEX)
                        {
                            bookmark.pageIndex = pageIndex;
                            bookmark.name = child->getTitle();
                        }
                    }
                }
            }

            if (bookmark.pageIndex != pdf::PDFCatalog::INVALID_PAGE_INDEX)
            {
                bookmarks.bookmarks.emplace_back(std::move(bookmark));
            }
        }
    }
}

}   // namespace pdf
