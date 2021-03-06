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
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPLv3 included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or later as published by the Free
** Software Foundation and appearing in the file LICENSE.GPL included in
** the packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 2.0 requirements will be
** met: http://www.gnu.org/licenses/gpl-2.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qwebengineurlrequestjob_p.h"

#include "qwebengineprofile.h"

#include "url_request_custom_job_delegate.h"

QT_BEGIN_NAMESPACE

QWebEngineUrlRequestJob::QWebEngineUrlRequestJob(QtWebEngineCore::URLRequestCustomJobDelegate * p)
    : QObject(p) // owned by the jobdelegate and deleted when the job is done
    , d_ptr(p)
{
}

QWebEngineUrlRequestJob::~QWebEngineUrlRequestJob()
{
}

QUrl QWebEngineUrlRequestJob::requestUrl() const
{
    return d_ptr->url();
}

void QWebEngineUrlRequestJob::setReply(const QByteArray &contentType, QIODevice *device)
{
    d_ptr->setReply(contentType, device);
}



QT_END_NAMESPACE
