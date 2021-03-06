/*  This file is part of the KDE project
    Copyright (C) 2006 Tim Beaulen <tbscope@gmail.com>
    Copyright (C) 2006-2007 Matthias Kretz <kretz@kde.org>

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.

*/

#include "xineengine.h"
#include "backend.h"
#include "macros.h"

#include <QtCore/QSettings>
#include <QtCore/QByteArray>
#include <QtCore/QFile>
#include <QtCore/QDebug>

#include <cstdlib>

extern "C" {
#include <xine/xine_plugin.h>
extern plugin_info_t phonon_xine_plugin_info[];
#ifdef USE_CUSTOM_WAV_DEMUXER
extern plugin_info_t phonon_xine_plugin_info_2[];
#endif
}

namespace Phonon
{
namespace Xine
{

XineEngineData::XineEngineData()
    : m_xine(xine_new())
{
    const QByteArray phonon_xine_verbosity(getenv("PHONON_XINE_VERBOSITY"));
    debug() << Q_FUNC_INFO << "setting xine verbosity to" << phonon_xine_verbosity.toInt();
    xine_engine_set_param(m_xine, XINE_ENGINE_PARAM_VERBOSITY, phonon_xine_verbosity.toInt());

    // Phonon-Xine is for "real QSettings", Xine.xine for the libxine handled settings
    const QSettings cg("kde.org", "Phonon-Xine.xine");
    const QString &configfileString = cg.fileName();
    const QByteArray &configfile = QFile::encodeName(configfileString);
    xine_config_load(m_xine, configfile.constData());
    xine_init(m_xine);
    xine_register_plugins(m_xine, phonon_xine_plugin_info);
#ifdef USE_CUSTOM_WAV_DEMUXER
    int xine_major, xine_minor, xine_sub;
    xine_get_version(&xine_major, &xine_minor, &xine_sub);
    if (xine_major == 1 && xine_minor == 1 && xine_sub < 16) {
        xine_register_plugins(m_xine, phonon_xine_plugin_info_2);
    }
#endif
    if (!QFile::exists(configfileString)) {
        debug() << "save xine config to" << configfile.constData();
        xine_config_save(m_xine, configfile.constData());
    }
}

XineEngineData::~XineEngineData()
{
    if (m_xine) {
        xine_exit(m_xine);
    }
}

void XineEngine::create()
{
    if (!d.data()) {
        d = new XineEngineData;
    }
}

} // namespace Xine
} // namespace Phonon
