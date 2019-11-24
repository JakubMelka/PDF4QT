//    Copyright (C) 2019 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFOUTLINE_H
#define PDFOUTLINE_H

#include "pdfglobal.h"
#include "pdfobject.h"
#include "pdfaction.h"

#include <QSharedPointer>

#include <set>

namespace pdf
{
class PDFDocument;

/// Outline item
class PDFOutlineItem
{
public:
    explicit PDFOutlineItem() = default;

    const QString& getTitle() const { return m_title; }
    void setTitle(const QString& title) { m_title = title; }

    size_t getChildCount() const { return m_children.size(); }
    const PDFOutlineItem* getChild(size_t index) const { return m_children[index].get(); }
    void addChild(QSharedPointer<PDFOutlineItem> child) { m_children.emplace_back(qMove(child)); }

    static QSharedPointer<PDFOutlineItem> parse(const PDFDocument* document, const PDFObject& root);

    const PDFAction* getAction() const;
    void setAction(const PDFActionPtr& action);

    QColor getTextColor() const;
    void setTextColor(const QColor& textColor);

    bool isFontItalics() const;
    void setFontItalics(bool fontItalics);

    bool isFontBold() const;
    void setFontBold(bool fontBold);

    PDFObjectReference getStructureElement() const;
    void setStructureElement(PDFObjectReference structureElement);

private:
    static void parseImpl(const PDFDocument* document,
                          PDFOutlineItem* parent,
                          PDFObjectReference currentItem,
                          std::set<PDFObjectReference>& visitedOutlineItems);

    QString m_title;
    std::vector<QSharedPointer<PDFOutlineItem>> m_children;
    PDFActionPtr m_action;
    PDFObjectReference m_structureElement;
    QColor m_textColor;
    bool m_fontItalics = false;
    bool m_fontBold = false;
};

}   // namespace pdf

#endif // PDFOUTLINE_H
