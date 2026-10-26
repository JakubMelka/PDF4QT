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

#include "pdfappcollectioniconchooser.h"
#include "pdfappbuiltintoolcollections.h"
#include "pdfapppagelayout.h"

#include "pdfcomponentbuttons.h"

#include <QButtonGroup>
#include <QEvent>

namespace pdf
{

PDFAppCollectionIconChooser::PDFAppCollectionIconChooser(QWidget* parent) :
    BaseClass(parent),
    m_iconName(PDFAppBuiltinToolCollections::getDefaultIconName())
{
    setObjectName(QStringLiteral("collectionIconChooser"));

    PDFAppFlowLayout* layout = new PDFAppFlowLayout(this, SPACING, SPACING);
    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);

    for (const QString& iconName : PDFAppBuiltinToolCollections::getIconChoices())
    {
        PDFIconChoice* choice = new PDFIconChoice(iconName, getIconText(iconName), this);
        choice->setObjectName(QStringLiteral("icon-") + iconName);
        m_group->addButton(choice);
        layout->addWidget(choice);
        m_choices.emplace_back(choice);
    }

    connect(m_group.data(), &QButtonGroup::buttonToggled, this, &PDFAppCollectionIconChooser::onButtonToggled);
    setIconName(m_iconName);
}

PDFAppCollectionIconChooser::~PDFAppCollectionIconChooser()
{
    // The buttons are deleted by QWidget: no signal reaches a half destroyed chooser
    if (m_group)
    {
        m_group->disconnect(this);
    }
}

QString PDFAppCollectionIconChooser::getIconName() const
{
    return m_iconName;
}

void PDFAppCollectionIconChooser::setIconName(const QString& iconName)
{
    const QString validName = PDFAppBuiltinToolCollections::isValidIconName(iconName) ? iconName : PDFAppBuiltinToolCollections::getDefaultIconName();
    m_iconName = validName;

    m_isUpdating = true;
    if (PDFIconChoice* choice = getChoice(validName))
    {
        choice->setSelected(true);
    }
    m_isUpdating = false;
}

PDFIconChoice* PDFAppCollectionIconChooser::getChoice(const QString& iconName) const
{
    for (const QPointer<PDFIconChoice>& choice : m_choices)
    {
        if (choice && choice->getIconName() == iconName)
        {
            return choice.data();
        }
    }
    return nullptr;
}

QString PDFAppCollectionIconChooser::getIconText(const QString& iconName)
{
    // Meaning of the icon, which the user chooses (the names of the icons are technical)
    if (iconName == QLatin1String("rotate-cw"))
    {
        return tr("Rotate");
    }
    if (iconName == QLatin1String("camera"))
    {
        return tr("Camera");
    }
    if (iconName == QLatin1String("mail"))
    {
        return tr("Mail");
    }
    if (iconName == QLatin1String("pen-tool"))
    {
        return tr("Pen");
    }
    if (iconName == QLatin1String("briefcase"))
    {
        return tr("Briefcase");
    }
    if (iconName == QLatin1String("printer"))
    {
        return tr("Printer");
    }
    if (iconName == QLatin1String("download"))
    {
        return tr("Download");
    }
    if (iconName == QLatin1String("folder"))
    {
        return tr("Folder");
    }
    if (iconName == QLatin1String("star"))
    {
        return tr("Star");
    }
    if (iconName == QLatin1String("zap"))
    {
        return tr("Lightning");
    }
    return tr("Icon");
}

void PDFAppCollectionIconChooser::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::LanguageChange)
    {
        retranslate();
    }
}

void PDFAppCollectionIconChooser::retranslate()
{
    for (const QPointer<PDFIconChoice>& choice : m_choices)
    {
        if (choice)
        {
            const QString text = getIconText(choice->getIconName());
            choice->setText(text);
            choice->setToolTip(text);
        }
    }
}

void PDFAppCollectionIconChooser::onButtonToggled(QAbstractButton* button, bool isChecked)
{
    PDFIconChoice* choice = qobject_cast<PDFIconChoice*>(button);
    if (!isChecked || !choice || m_isUpdating)
    {
        return;
    }

    m_iconName = choice->getIconName();
    Q_EMIT iconChosen(m_iconName);
}

}   // namespace pdf
