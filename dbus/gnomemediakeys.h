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

#ifndef GNOME_MEDIA_KEYS_H
#define GNOME_MEDIA_KEYS_H

#include <QObject>

class OrgGnomeSettingsDaemonInterface;
class OrgGnomeSettingsDaemonMediaKeysInterface;
class QDBusPendingCallWatcher;
class MainWindow;
class QDBusServiceWatcher;

class GnomeMediaKeys : public QObject
{
    Q_OBJECT

public:
    GnomeMediaKeys(MainWindow *parent);

    void setEnabled(bool en);

private:
    bool daemonIsRunning();
    void releaseKeys();
    void grabKeys();
    void disconnectDaemon();

private Q_SLOTS:
    void serviceOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);
    void registerFinished(QDBusPendingCallWatcher *watcher);
    void keyPressed(const QString &app, const QString &key);
    void pluginActivated(const QString &name);

private:
    MainWindow *mw;
    OrgGnomeSettingsDaemonInterface *daemon;
    OrgGnomeSettingsDaemonMediaKeysInterface *mk;
    QDBusServiceWatcher *watcher;
};

#endif
