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
#ifndef ONLINE_SERVICES_MODEL_H
#define ONLINE_SERVICES_MODEL_H

#include <QtCore/QAbstractItemModel>
#include <QtCore/QSet>
#include "song.h"
#include "config.h"

class OnlineService;

class OnlineServicesModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    static OnlineServicesModel * self();

    OnlineServicesModel(QObject *parent = 0);
    ~OnlineServicesModel();
    QModelIndex index(int, int, const QModelIndex & = QModelIndex()) const;
    QModelIndex parent(const QModelIndex &) const;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const;
    int rowCount(const QModelIndex &parent = QModelIndex()) const;
    int columnCount(const QModelIndex &) const;
    QVariant data(const QModelIndex &, int) const;
    Qt::ItemFlags flags(const QModelIndex &index) const;
    QStringList filenames(const QModelIndexList &indexes) const;
    QList<Song> songs(const QModelIndexList &indexes) const;
    void clear();
    OnlineService * service(const QString &name);
    bool isEnabled() const { return enabled; }
    void setEnabled(bool e);
    void getDetails(QSet<QString> &artists, QSet<QString> &albumArtists, QSet<QString> &albums, QSet<QString> &genres);
    QMimeData * mimeData(const QModelIndexList &indexes) const;
    void toggleGrouping();
    const QSet<QString> & genres() { return srvGenres; }
    void createService(const QString &name);

public Q_SLOTS:
    void setCover(const Song &song, const QImage &img);
    void removeService(const QString &name);
    void stateChanged(const QString &name, bool state);

private:
    OnlineService * addService(const QString &name);
    void updateItemMenu();
    void load();
    int indexOf(const QString &name);
    void updateGenres();

Q_SIGNALS:
    void updateGenres(const QSet<QString> &genres);
    void error(const QString &text);

private:
    QList<OnlineService *> services;
    QSet<QString> srvGenres;
    bool enabled;

    friend class OnlineService;
};

#endif