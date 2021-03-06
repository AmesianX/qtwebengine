/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the QtWebEngine module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPLv3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or later as published by the Free
** Software Foundation and appearing in the file LICENSE.GPL included in
** the packaging of this file. Please review the following information to
** ensure the GNU General Public License version 2.0 requirements will be
** met: http://www.gnu.org/licenses/gpl-2.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef QWEBENGINEPROFILE_H
#define QWEBENGINEPROFILE_H

#include "qtwebenginewidgetsglobal.h"

#include <QObject>
#include <QScopedPointer>
#include <QString>

QT_BEGIN_NAMESPACE

class QObject;
class QUrl;
class QWebEngineDownloadItem;
class QWebEnginePage;
class QWebEnginePagePrivate;
class QWebEngineProfilePrivate;
class QWebEngineSettings;
class QWebEngineScriptCollection;

class QWEBENGINEWIDGETS_EXPORT QWebEngineProfile : public QObject {
    Q_OBJECT
public:
    explicit QWebEngineProfile(QObject *parent = 0);
    explicit QWebEngineProfile(const QString &name, QObject *parent = 0);
    virtual ~QWebEngineProfile();

    enum HttpCacheType {
        MemoryHttpCache,
        DiskHttpCache
    };

    enum PersistentCookiesPolicy {
        NoPersistentCookies,
        AllowPersistentCookies,
        ForcePersistentCookies
    };

    QString storageName() const;
    bool isOffTheRecord() const;

    QString persistentStoragePath() const;
    void setPersistentStoragePath(const QString &path);

    QString cachePath() const;
    void setCachePath(const QString &path);

    QString httpUserAgent() const;
    void setHttpUserAgent(const QString &userAgent);

    HttpCacheType httpCacheType() const;
    void setHttpCacheType(QWebEngineProfile::HttpCacheType);

    PersistentCookiesPolicy persistentCookiesPolicy() const;
    void setPersistentCookiesPolicy(QWebEngineProfile::PersistentCookiesPolicy);

    int httpCacheMaximumSize() const;
    void setHttpCacheMaximumSize(int maxSize);

    void clearAllVisitedLinks();
    void clearVisitedLinks(const QList<QUrl> &urls);
    bool visitedLinksContainsUrl(const QUrl &url) const;

    QWebEngineSettings *settings() const;
    QWebEngineScriptCollection &scripts();

    static QWebEngineProfile *defaultProfile();

Q_SIGNALS:
    void downloadRequested(QWebEngineDownloadItem *download);

private:
    Q_DECLARE_PRIVATE(QWebEngineProfile)
    QWebEngineProfile(QWebEngineProfilePrivate *, QObject *parent = 0);

    friend class QWebEnginePagePrivate;
    friend class QWebEngineUrlSchemeHandler;
    QScopedPointer<QWebEngineProfilePrivate> d_ptr;
};

QT_END_NAMESPACE

#endif // QWEBENGINEPROFILE_H
