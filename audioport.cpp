/*  This file is part of the KDE project
    Copyright (C) 2006 Matthias Kretz <kretz@kde.org>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License version 2 as published by the Free Software Foundation.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.

*/

#include "audioport.h"
#include "xineengine.h"
#include "backend.h"
#include <QByteArray>
#include <QStringList>
#include <xine.h>
#include <QSharedData>
#include <kdebug.h>

namespace Phonon
{
namespace Xine
{

AudioPortData::~AudioPortData()
{
    //kDebug(610) << k_funcinfo << this << " port = " << port << endl;
    if (port) {
        xine_close_audio_driver(XineEngine::xine(), port);
        port = 0;
        kDebug(610) << "----------------------------------------------- audio_port destroyed" << endl;
    }
}

AudioPort::AudioPort(int deviceIndex)
    : d(new AudioPortData)
{
    QByteArray outputPlugin = XineEngine::audioDriverFor(deviceIndex).toLatin1();
    //kDebug(610) << k_funcinfo << outputPlugin << alsaDevices << endl;

    if (outputPlugin == "alsa") {
        QStringList alsaDevices = XineEngine::alsaDevicesFor(deviceIndex);
        foreach (QString device, alsaDevices) {
            xine_cfg_entry_t alsaDeviceConfig;
            QByteArray deviceStr = device.toUtf8();
            if(!xine_config_lookup_entry(XineEngine::xine(), "audio.device.alsa_default_device",
                        &alsaDeviceConfig)) {
                // the config key is not registered yet - it is registered when the alsa output
                // plugin is opened. So we open the plugin and close it again, then we can set the
                // setting. :(
                xine_audio_port_t *port = xine_open_audio_driver(XineEngine::xine(), outputPlugin.constData(), 0);
                if (port) {
                    xine_close_audio_driver(XineEngine::xine(), port);
                    // port == 0 does not have to be fatal, since it might be only the default device
                    // that cannot be opened
                    //kError(610) << k_funcinfo << "creating the correct ALSA output failed!" << endl;
                    //return;
                }
                // now the config key should be registered
                if(!xine_config_lookup_entry(XineEngine::xine(), "audio.device.alsa_default_device",
                            &alsaDeviceConfig)) {
                    kError(610) << "cannot set the ALSA device on Xine's ALSA output plugin" << endl;
                    return;
                }
            }
            Q_ASSERT(alsaDeviceConfig.type == XINE_CONFIG_TYPE_STRING);
            alsaDeviceConfig.str_value = deviceStr.data();
            xine_config_update_entry(XineEngine::xine(), &alsaDeviceConfig);

            Q_ASSERT(xine_config_lookup_entry(XineEngine::xine(), "audio.device.alsa_front_device", &alsaDeviceConfig));
            Q_ASSERT(alsaDeviceConfig.type == XINE_CONFIG_TYPE_STRING);
            alsaDeviceConfig.str_value = deviceStr.data();
            xine_config_update_entry(XineEngine::xine(), &alsaDeviceConfig);

            d->port = xine_open_audio_driver(XineEngine::xine(), outputPlugin.constData(), 0);
            if (d->port) {
                kDebug(610) << k_funcinfo << "use ALSA device: " << device << endl;
                break;
            }
        }
    } else {
        kDebug(610) << k_funcinfo << "use output plugin: '" << outputPlugin << "'" << endl;
        d->port = xine_open_audio_driver(XineEngine::xine(), outputPlugin.constData(), 0);
    }
    kDebug(610) << "----------------------------------------------- audio_port created" << endl;
}

bool AudioPort::isValid() const
{
    return (d->port != 0);
}

bool AudioPort::operator==(const AudioPort& rhs) const
{
    return d->port == rhs.d->port;
}

bool AudioPort::operator!=(const AudioPort& rhs) const
{
    return d->port != rhs.d->port;
}

xine_audio_port_t *AudioPort::xinePort() const
{
    return d->port;
}

} // namespace Xine
} // namespace Phonon
// vim: sw=4 sts=4 et tw=100
