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

#include "playlistspage.h"
#include "playlistsmodel.h"
#include "mpdconnection.h"
#include "messagebox.h"
#include "inputdialog.h"
#include "localize.h"
#include "icons.h"
#include "stdactions.h"
#include "actioncollection.h"
#include "mpdconnection.h"
#include <QMenu>
#ifndef ENABLE_KDE_SUPPORT
#include <QStyle>
#endif

PlaylistsPage::PlaylistsPage(QWidget *p)
    : QWidget(p)
{
    setupUi(this);
    renamePlaylistAction = ActionCollection::get()->createAction("renameplaylist", i18n("Rename"), "edit-rename");
    replacePlayQueue->setDefaultAction(StdActions::self()->replacePlayQueueAction);
    libraryUpdate->setDefaultAction(StdActions::self()->refreshAction);
    searchButton->setDefaultAction(StdActions::self()->searchAction);
    connect(genreCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(searchItems()));
    connect(PlaylistsModel::self(), SIGNAL(updateGenres(const QSet<QString> &)), genreCombo, SLOT(update(const QSet<QString> &)));

    view->allowGroupedView();
    view->addAction(StdActions::self()->addToPlayQueueAction);
    view->addAction(StdActions::self()->replacePlayQueueAction);
    view->addAction(StdActions::self()->addWithPriorityAction);
    #ifdef ENABLE_DEVICES_SUPPORT
    view->addAction(StdActions::self()->copyToDeviceAction);
    #endif
    view->addAction(renamePlaylistAction);
    view->addAction(StdActions::self()->removeAction);
    view->setUniformRowHeights(true);
    view->setAcceptDrops(true);
    view->setDragDropOverwriteMode(false);
    view->setDragDropMode(QAbstractItemView::DragDrop);

    proxy.setSourceModel(PlaylistsModel::self());
    view->setModel(&proxy);
    view->setDeleteAction(StdActions::self()->removeAction);

    connect(view, SIGNAL(doubleClicked(const QModelIndex &)), this, SLOT(itemDoubleClicked(const QModelIndex &)));
    connect(view, SIGNAL(itemsSelected(bool)), SLOT(controlActions()));
    connect(view, SIGNAL(searchItems()), this, SLOT(searchItems()));
    connect(view, SIGNAL(rootIndexSet(QModelIndex)), this, SLOT(updateGenres(QModelIndex)));
    //connect(this, SIGNAL(add(const QStringList &)), MPDConnection::self(), SLOT(add(const QStringList &)));
    connect(this, SIGNAL(loadPlaylist(const QString &, bool)), MPDConnection::self(), SLOT(loadPlaylist(const QString &, bool)));
    connect(this, SIGNAL(removePlaylist(const QString &)), MPDConnection::self(), SLOT(removePlaylist(const QString &)));
    connect(this, SIGNAL(savePlaylist(const QString &)), MPDConnection::self(), SLOT(savePlaylist(const QString &)));
    connect(this, SIGNAL(renamePlaylist(const QString &, const QString &)), MPDConnection::self(), SLOT(renamePlaylist(const QString &, const QString &)));
    connect(this, SIGNAL(removeFromPlaylist(const QString &, const QList<quint32> &)), MPDConnection::self(), SLOT(removeFromPlaylist(const QString &, const QList<quint32> &)));
    connect(StdActions::self()->savePlayQueueAction, SIGNAL(triggered(bool)), this, SLOT(savePlaylist()));
    connect(renamePlaylistAction, SIGNAL(triggered(bool)), this, SLOT(renamePlaylist()));
    connect(PlaylistsModel::self(), SIGNAL(updated(const QModelIndex &)), this, SLOT(updated(const QModelIndex &)));
    connect(PlaylistsModel::self(), SIGNAL(playlistRemoved(quint32)), view, SLOT(collectionRemoved(quint32)));
    QMenu *menu=new QMenu(this);
    menu->addAction(StdActions::self()->addToPlayQueueAction);
    menu->addAction(StdActions::self()->addWithPriorityAction);
    menu->addAction(StdActions::self()->removeAction);
    menu->addAction(renamePlaylistAction);
    menuButton->setMenu(menu);
}

PlaylistsPage::~PlaylistsPage()
{
}

void PlaylistsPage::setStartClosed(bool sc)
{
    view->setStartClosed(sc);
}

bool PlaylistsPage::isStartClosed()
{
    return view->isStartClosed();
}

void PlaylistsPage::updateRows()
{
    view->updateRows();
}

void PlaylistsPage::refresh()
{
    view->setLevel(0);
    PlaylistsModel::self()->getPlaylists();
}

void PlaylistsPage::clear()
{
    PlaylistsModel::self()->clear();
}

//QStringList PlaylistsPage::selectedFiles() const
//{
//    QModelIndexList indexes=view->selectedIndexes();
//    if (indexes.isEmpty()) {
//        return QStringList();
//    }
//
//    QModelIndexList mapped;
//    foreach (const QModelIndex &idx, indexes) {
//        mapped.append(proxy.mapToSource(idx));
//    }
//
//    return PlaylistsModel::self()->filenames(mapped, true);
//}

void PlaylistsPage::addSelectionToPlaylist(bool replace, quint8 priorty)
{
    addItemsToPlayQueue(view->selectedIndexes(), replace, priorty);
}

void PlaylistsPage::setView(int mode)
{
    bool diff=view->viewMode()!=mode;
    view->setMode((ItemView::Mode)mode);
    if (diff) {
        clear();
        refresh();
    }
}

void PlaylistsPage::removeItems()
{
    QSet<QString> remPlaylists;
    QMap<QString, QList<quint32> > remSongs;
    QModelIndexList selected = view->selectedIndexes();

    foreach(const QModelIndex &index, selected) {
        if(index.isValid()) {
            QModelIndex realIndex(proxy.mapToSource(index));

            if(realIndex.isValid()) {
                PlaylistsModel::Item *item=static_cast<PlaylistsModel::Item *>(realIndex.internalPointer());
                if(item->isPlaylist()) {
                    PlaylistsModel::PlaylistItem *pl=static_cast<PlaylistsModel::PlaylistItem *>(item);
                    remPlaylists.insert(pl->name);
                    if (remSongs.contains(pl->name)) {
                        remSongs.remove(pl->name);
                    }
                } else {
                    PlaylistsModel::SongItem *song=static_cast<PlaylistsModel::SongItem *>(item);
                    if (!remPlaylists.contains(song->parent->name)) {
                        remSongs[song->parent->name].append(song->parent->songs.indexOf(song));
                    }
                }
            }
        }
    }

    // Should not happen!
    if (remPlaylists.isEmpty() && remSongs.isEmpty()) {
        return;
    }

    if (remPlaylists.count() &&
        MessageBox::No==MessageBox::warningYesNo(this, i18n("Are you sure you wish to remove the selected playlists?\nThis cannot be undone."),
                                                 i18n("Remove Playlists"), StdGuiItem::remove(), StdGuiItem::cancel())) {
            return;
    }

    foreach (const QString &pl, remPlaylists) {
        emit removePlaylist(pl);
    }
    QMap<QString, QList<quint32> >::ConstIterator it=remSongs.constBegin();
    QMap<QString, QList<quint32> >::ConstIterator end=remSongs.constEnd();
    for (; it!=end; ++it) {
        emit removeFromPlaylist(it.key(), it.value());
    }
}

void PlaylistsPage::savePlaylist()
{
    QString name = InputDialog::getText(i18n("Playlist Name"), i18n("Enter a name for the playlist:"), QString(), 0, this);

    if (!name.isEmpty()) {
        if (PlaylistsModel::self()->exists(name)) {
            if (MessageBox::No==MessageBox::warningYesNo(this, i18n("A playlist named <b>%1</b> already exists!<br/>Overwrite?", name),
                                                         i18n("Overwrite Playlist"), StdGuiItem::overwrite(), StdGuiItem::cancel())) {
                return;
            }
            else {
                emit removePlaylist(name);
            }
        }
        emit savePlaylist(name);
    }
}

void PlaylistsPage::renamePlaylist()
{
    const QModelIndexList items = view->selectedIndexes(false); // Dont need sorted selection here...

    if (1==items.size()) {
        QModelIndex sourceIndex = proxy.mapToSource(items.first());
        QString name = PlaylistsModel::self()->data(sourceIndex, Qt::DisplayRole).toString();
        QString newName = InputDialog::getText(i18n("Rename Playlist"), i18n("Enter new name for playlist:"), name, 0, this);

        if (!newName.isEmpty() && name!=newName) {
            if (PlaylistsModel::self()->exists(newName)) {
                if (MessageBox::No==MessageBox::warningYesNo(this, i18n("A playlist named <b>%1</b> already exists!<br/>Overwrite?", newName),
                                                             i18n("Overwrite Playlist"), StdGuiItem::overwrite(), StdGuiItem::cancel())) {
                    return;
                }
                else {
                    emit removePlaylist(newName);
                }
            }
            emit renamePlaylist(name, newName);
        }
    }
}

void PlaylistsPage::itemDoubleClicked(const QModelIndex &index)
{
    if (
        #ifdef ENABLE_KDE_SUPPORT
        KGlobalSettings::singleClick()
        #else
        style()->styleHint(QStyle::SH_ItemView_ActivateItemOnSingleClick, 0, this)
        #endif
        || !static_cast<PlaylistsModel::Item *>(proxy.mapToSource(index).internalPointer())->isPlaylist()) {
        QModelIndexList indexes;
        indexes.append(index);
        addItemsToPlayQueue(indexes, false);
    }
}

void PlaylistsPage::addItemsToPlayQueue(const QModelIndexList &indexes, bool replace, quint8 priorty)
{
    if (indexes.isEmpty()) {
        return;
    }

    // If we only have 1 item selected, see if it is a playlist. If so, we might be able to
    // just ask MPD to load it...
    if (1==indexes.count() && 0==priorty) {
        QModelIndex idx=proxy.mapToSource(*(indexes.begin()));
        PlaylistsModel::Item *item=static_cast<PlaylistsModel::Item *>(idx.internalPointer());

        if (item->isPlaylist()) {
            emit loadPlaylist(static_cast<PlaylistsModel::PlaylistItem*>(item)->name, replace);
            return;
        }
    }

    QModelIndexList mapped;
    foreach (const QModelIndex &idx, indexes) {
        mapped.append(proxy.mapToSource(idx));
    }

    QStringList files=PlaylistsModel::self()->filenames(mapped);
    if (!files.isEmpty()) {
        emit add(files, replace, priorty);
        view->clearSelection();
    }
}

#ifdef ENABLE_DEVICES_SUPPORT
QList<Song> PlaylistsPage::selectedSongs() const
{
    QModelIndexList selected = view->selectedIndexes();

    if (selected.isEmpty()) {
        return QList<Song>();
    }

    QModelIndexList mapped;
    foreach (const QModelIndex &idx, selected) {
        mapped.append(proxy.mapToSource(idx));
    }

    return PlaylistsModel::self()->songs(mapped);
}

void PlaylistsPage::addSelectionToDevice(const QString &udi)
{
    QList<Song> songs=selectedSongs();
    if (!songs.isEmpty()) {
        emit addToDevice(QString(), udi, songs);
        view->clearSelection();
    }
}
#endif

void PlaylistsPage::controlActions()
{
    QModelIndexList selected=view->selectedIndexes(false); // Dont need sorted selection here...
    bool canRename=false;
    bool enableActions=selected.count()>0;

    if (1==selected.count()) {
        QModelIndex index = proxy.mapToSource(selected.first());
        PlaylistsModel::Item *item=static_cast<PlaylistsModel::Item *>(index.internalPointer());
        if (item && item->isPlaylist()) {
            canRename=true;
        }
    }

    renamePlaylistAction->setEnabled(canRename);
    StdActions::self()->removeAction->setEnabled(enableActions);
    StdActions::self()->replacePlayQueueAction->setEnabled(enableActions);
    StdActions::self()->addToPlayQueueAction->setEnabled(enableActions);
    StdActions::self()->addWithPriorityAction->setEnabled(enableActions);
    #ifdef ENABLE_DEVICES_SUPPORT
    StdActions::self()->copyToDeviceAction->setEnabled(enableActions);
    #endif
    menuButton->controlState();
}

void PlaylistsPage::searchItems()
{
    QString text=view->searchText().trimmed();
    bool updated=proxy.update(text, genreCombo->currentIndex()<=0 ? QString() : genreCombo->currentText());
    if (proxy.enabled() && !text.isEmpty()) {
        view->expandAll();
    }
    if(updated) {
        view->updateRows();
    }
}

void PlaylistsPage::updated(const QModelIndex &index)
{
    view->updateRows(proxy.mapFromSource(index));
}

void PlaylistsPage::updateGenres(const QModelIndex &idx)
{
    if (idx.isValid()) {
        QModelIndex m=proxy.mapToSource(idx);
        if (m.isValid() && static_cast<PlaylistsModel::Item *>(m.internalPointer())->isPlaylist()) {
            genreCombo->update(static_cast<PlaylistsModel::PlaylistItem *>(m.internalPointer())->genres);
            return;
        }
    }
    genreCombo->update(PlaylistsModel::self()->genres());
}
