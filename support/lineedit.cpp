/*
 * Cantata
 *
 * Copyright (c) 2011-2013 Craig Drummond <craig.p.drummond@gmail.com>
 *
 * ----
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
#include "lineedit.h"
#include <QApplication>

void LineEdit::setReadOnly(bool e)
{
    #ifdef ENABLE_KDE_SUPPORT
    KLineEdit::setReadOnly(e);
    #else
    QLineEdit::setReadOnly(e);
    #endif
    if (e) {
        QPalette p(palette());
        p.setColor(QPalette::Active, QPalette::Base, p.color(QPalette::Active, QPalette::Window));
        p.setColor(QPalette::Disabled, QPalette::Base, p.color(QPalette::Disabled, QPalette::Window));
        p.setColor(QPalette::Inactive, QPalette::Base, p.color(QPalette::Inactive, QPalette::Window));
        setPalette(p);
    } else {
        setPalette(qApp->palette());
    }
}

#ifndef ENABLE_KDE_SUPPORT

/****************************************************************************
**
** Copyright (c) 2007 Trolltech ASA <info@trolltech.com>
**
** Use, modification and distribution is allowed without limitation,
** warranty, liability or support of any kind.
**
****************************************************************************/

#include "icon.h"
#include "config.h"
#include <QToolButton>
#include <QStyle>
#include <QComboBox>
#include <QDebug>
LineEdit::LineEdit(QWidget *parent)
    : QLineEdit(parent)
{
    clearButton = new QToolButton(this);
    int iconSize=Icon::stdSize(fontMetrics().height());
    QPixmap pixmap(Icon(Qt::RightToLeft==layoutDirection() ? "edit-clear-locationbar-ltr" : "edit-clear-locationbar-rtl").pixmap(iconSize, iconSize));
    if (pixmap.isNull()) {
        pixmap=(Icon("edit-clear").pixmap(iconSize, iconSize));
    }
    clearButton->setIcon(QIcon(pixmap));
    clearButton->setIconSize(pixmap.size());
    clearButton->setCursor(Qt::ArrowCursor);
    clearButton->setStyleSheet("QToolButton { border: none; padding: 0px; }");
    clearButton->hide();
    clearButton->setFocusPolicy(Qt::NoFocus);
    connect(clearButton, SIGNAL(clicked()), this, SLOT(clear()));
    connect(this, SIGNAL(textChanged(const QString&)), this, SLOT(updateCloseButton(const QString&)));
    int frameWidth = style()->pixelMetric(QStyle::PM_DefaultFrameWidth);
    bool onCombo = parent && qobject_cast<QComboBox *>(parent);
    QString styleSheet=QLatin1String("QLineEdit { padding-right")+
                       QLatin1String(": %1px; ")+QLatin1String(onCombo ? "background: transparent " : "")+QChar('}');
    setStyleSheet(styleSheet.arg(clearButton->sizeHint().width() + frameWidth + 1));

    if (!onCombo) {
        QSize msz = minimumSizeHint();
        setMinimumSize(qMax(msz.width(), clearButton->sizeHint().height() + frameWidth * 2 + 2),
                       qMax(msz.height(), clearButton->sizeHint().height() + frameWidth * 2 + 2));
    }
}

void LineEdit::resizeEvent(QResizeEvent *)
{
    QSize sz = clearButton->sizeHint();
    int frameWidth = style()->pixelMetric(QStyle::PM_DefaultFrameWidth);
    if (Qt::RightToLeft==layoutDirection()) {
        clearButton->move(rect().left() + frameWidth, (rect().bottom() + 1 - sz.height()) / 2);
    } else {
        clearButton->move(rect().right() - frameWidth - sz.width(), (rect().bottom() + 1 - sz.height()) / 2);
    }
}

void LineEdit::updateCloseButton(const QString& text)
{
    clearButton->setVisible(!isReadOnly() && !text.isEmpty());
}

#endif
