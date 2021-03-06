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

#ifndef _HTTP_SERVER_H
#define _HTTP_SERVER_H

#include <qglobal.h>
#include <QByteArray>
#include "song.h"

class HttpSocket;
class Thread;
class QUrl;

class HttpServer
{
public:
    static HttpServer * self();

    HttpServer() : thread(0), socket(0) { }
    virtual ~HttpServer() { }

    void stop();
    bool isAlive() const;
    bool readConfig();
    QString address() const;
    bool isOurs(const QString &url) const;
    QByteArray encodeUrl(const Song &s) const;
    QByteArray encodeUrl(const QString &file) const;
    Song decodeUrl(const QUrl &url) const;
    Song decodeUrl(const QString &file) const;

private:
    Thread *thread;
    HttpSocket *socket;
};

#endif

