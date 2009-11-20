/*  This file is part of the KDE project
    Copyright (C) 2006-2008 Matthias Kretz <kretz@kde.org>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) version 3, or any
    later version accepted by the membership of KDE e.V. (or its
    successor approved by the membership of KDE e.V.), Nokia Corporation 
    (or its successors, if any) and the KDE Free Qt Foundation, which shall
    act as a proxy defined in Section 6 of version 3 of the license.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public 
    License along with this library.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "globalconfig_p.h"

#include "factory_p.h"
#include "objectdescription.h"
#include "phonondefs_p.h"
#include "platformplugin.h"
#include "backendinterface.h"
#include "qsettingsgroup_p.h"
#include "phononnamespace_p.h"
#include "pulsesupport_p.h"

#include <QtCore/QList>
#include <QtCore/QVariant>

QT_BEGIN_NAMESPACE

namespace Phonon
{

GlobalConfig::GlobalConfig() : m_config(QLatin1String("kde.org"), QLatin1String("libphonon"))
{
}

GlobalConfig::~GlobalConfig()
{
}

enum WhatToFilter {
    FilterAdvancedDevices = 1,
    FilterHardwareDevices = 2,
    FilterUnavailableDevices = 4
};

static void filter(ObjectDescriptionType type, BackendInterface *backendIface, QList<int> *list, int whatToFilter)
{
    QMutableListIterator<int> it(*list);
    while (it.hasNext()) {
        const QHash<QByteArray, QVariant> properties = backendIface->objectDescriptionProperties(type, it.next());
        QVariant var;
        if (whatToFilter & FilterAdvancedDevices) {
            var = properties.value("isAdvanced");
            if (var.isValid() && var.toBool()) {
                it.remove();
                continue;
            }
        }
        if (whatToFilter & FilterHardwareDevices) {
            var = properties.value("isHardwareDevice");
            if (var.isValid() && var.toBool()) {
                it.remove();
                continue;
            }
        }
        if (whatToFilter & FilterUnavailableDevices) {
            var = properties.value("available");
            if (var.isValid() && !var.toBool()) {
                it.remove();
                continue;
            }
        }
    }
}

QList<int> GlobalConfig::_sortDevicesByCategoryPriority(ObjectDescriptionType type, Phonon::Category category, QList<int> &defaultList) const
{
    Q_ASSERT(type == AudioOutputDeviceType || type == AudioCaptureDeviceType);

    if (defaultList.size() <= 1) {
        // nothing to sort
        return defaultList;
    } else {
        // make entries unique
        QSet<int> seen;
        QMutableListIterator<int> it(defaultList);
        while (it.hasNext()) {
            if (seen.contains(it.next())) {
                it.remove();
            } else {
                seen.insert(it.value());
            }
        }
    }

    QList<int> deviceList;
    PulseSupport *pulse = PulseSupport::getInstance();
    if (pulse->isActive()) {
        deviceList = pulse->objectIndexesByCategory(type, category);
    } else {
        //The devices need to be stored independently for every backend
        const QSettingsGroup backendConfig(&m_config, QLatin1String(type == AudioOutputDeviceType ? "AudioOutputDevice" : "AudioCaptureDevice")); // + Factory::identifier());
        QString categoryKey = QLatin1String("Category_") + QString::number(static_cast<int>(category));
        if (!backendConfig.hasKey(categoryKey)) {
            // no list in config for the given category
            categoryKey = QLatin1String("Category_") + QString::number(static_cast<int>(Phonon::NoCategory));
            if (!backendConfig.hasKey(categoryKey)) {
                // no list in config for NoCategory
                return defaultList;
            }
        }

        //Now the list from m_config
        deviceList = backendConfig.value(categoryKey, QList<int>());
    }

    //if there are devices in m_config that the backend doesn't report, remove them from the list
    QMutableListIterator<int> i(deviceList);
    while (i.hasNext()) {
        if (0 == defaultList.removeAll(i.next())) {
            i.remove();
        }
    }

    //if the backend reports more devices that are not in m_config append them to the list
    deviceList += defaultList;

    return deviceList;
}

bool GlobalConfig::getHideAdvancedDevices() const
{
    //The devices need to be stored independently for every backend
    const QSettingsGroup generalGroup(&m_config, QLatin1String("General"));
    return generalGroup.value(QLatin1String("HideAdvancedDevices"), true);
}

void GlobalConfig::hideAdvancedDevices(bool hide)
{
    QSettingsGroup generalGroup(&m_config, QLatin1String("General"));
    generalGroup.value(QLatin1String("HideAdvancedDevices"), hide);
}

bool GlobalConfig::isHiddenAudioOutputDevice(int i) const
{
    if (!getHideAdvancedDevices())
        return false;

    AudioOutputDevice ad = AudioOutputDevice::fromIndex(i);
    const QVariant var = ad.property("isAdvanced");
    return (var.isValid() && var.toBool());
}


QList<int> GlobalConfig::_reindexList(Phonon::Category category, QList<int>newOrder, bool output) const
{
#ifdef QT_NO_PHONON_AUDIOCAPTURE
    Q_ASSERT(output);
#endif

    /*QString sb;
    sb = QString("(Size %1)").arg(currentList.size());
    foreach (int i, currentList)
    sb += QString("%1, ").arg(i);
    fprintf(stderr, "=== Reindex Current: %s\n", sb.toUtf8().constData());
    sb = QString("(Size %1)").arg(newOrder.size());
    foreach (int i, newOrder)
    sb += QString("%1, ").arg(i);
    fprintf(stderr, "=== Reindex Before : %s\n", sb.toUtf8().constData());*/

    QList<int> currentList;
    if (output)
        currentList = audioOutputDeviceListFor(category, ShowUnavailableDevices|ShowAdvancedDevices);
#ifndef QT_NO_PHONON_AUDIOCAPTURE
    else
        currentList = audioCaptureDeviceListFor(category, ShowUnavailableDevices|ShowAdvancedDevices);
#endif

    QList<int> newList;

    foreach (int i, newOrder) {
        int found = currentList.indexOf(i);
        if (found < 0) {
            // It's not in the list, so something is odd (e.g. client error). Ignore it.
            continue;
        }

        // Iterate through the list from this point onward. If there are hidden devices
        // immediately following, take them too.
        newList.append(currentList.takeAt(found));
        while (found < currentList.size()) {
            bool hidden = true;
            if (output)
                hidden = isHiddenAudioOutputDevice(currentList.at(found));
#ifndef QT_NO_PHONON_AUDIOCAPTURE
            else
                hidden = isHiddenAudioCaptureDevice(currentList.at(found));
#endif

            if (!hidden)
                break;
            newList.append(currentList.takeAt(found));
        }
    }

    // If there are any devices left in.. just tack them on the end.
    if (currentList.size() > 0)
        newList += currentList;

    /*sb = QString("(Size %1)").arg(newList.size());
    foreach (int i, newList)
    sb += QString("%1, ").arg(i);
    fprintf(stderr, "=== Reindex After  : %s\n", sb.toUtf8().constData());*/
    return newList;
}


void GlobalConfig::setAudioOutputDeviceListFor(Phonon::Category category, QList<int> order)
{
    if (PulseSupport::getInstance()->isActive()) {
        /// @todo Write support to push the priority list to pulseaudio
        return;
    }

    QSettingsGroup backendConfig(&m_config, QLatin1String("AudioOutputDevice")); // + Factory::identifier());

    order = _reindexList(category, order, true);

    const QList<int> noCategoryOrder = audioOutputDeviceListFor(Phonon::NoCategory, ShowUnavailableDevices|ShowAdvancedDevices);
    if (category != Phonon::NoCategory && order == noCategoryOrder) {
        backendConfig.removeEntry(QLatin1String("Category_") + QString::number(category));
    } else {
        backendConfig.setValue(QLatin1String("Category_") + QString::number(category), order);
    }
}

QList<int> GlobalConfig::audioOutputDeviceListFor(Phonon::Category category, int override) const
{
    const bool hideAdvancedDevices = ((override & AdvancedDevicesFromSettings)
            ? getHideAdvancedDevices()
            : static_cast<bool>(override & HideAdvancedDevices));

    QList<int> defaultList;
    BackendInterface *backendIface = qobject_cast<BackendInterface *>(Factory::backend());

#ifndef QT_NO_PHONON_PLATFORMPLUGIN
    if (!backendIface || !PulseSupport::getInstance()->isActive()) {
        if (PlatformPlugin *platformPlugin = Factory::platformPlugin()) {
            // the platform plugin lists the audio devices for the platform
            // this list already is in default order (as defined by the platform plugin)
            defaultList = platformPlugin->objectDescriptionIndexes(Phonon::AudioOutputDeviceType);
            if (hideAdvancedDevices) {
                QMutableListIterator<int> it(defaultList);
                while (it.hasNext()) {
                    AudioOutputDevice objDesc = AudioOutputDevice::fromIndex(it.next());
                    const QVariant var = objDesc.property("isAdvanced");
                    if (var.isValid() && var.toBool()) {
                        it.remove();
                    }
                }
            }
        }
    }
#endif //QT_NO_PHONON_PLATFORMPLUGIN

    // lookup the available devices directly from the backend
    if (backendIface) {
        // this list already is in default order (as defined by the backend)
        QList<int> list = backendIface->objectDescriptionIndexes(Phonon::AudioOutputDeviceType);
        if (hideAdvancedDevices || !defaultList.isEmpty() || (override & HideUnavailableDevices)) {
            filter(AudioOutputDeviceType, backendIface, &list,
                    (hideAdvancedDevices ? FilterAdvancedDevices : 0)
                    // the platform plugin already provided the hardware devices
                    | (defaultList.isEmpty() ? 0 : FilterHardwareDevices)
                    | ((override & HideUnavailableDevices) ? FilterUnavailableDevices : 0)
                    );
        }
        defaultList += list;
    }

    return _sortDevicesByCategoryPriority(AudioOutputDeviceType, category, defaultList);
}

int GlobalConfig::audioOutputDeviceFor(Phonon::Category category, int override) const
{
    QList<int> ret = audioOutputDeviceListFor(category, override);
    if (ret.isEmpty())
        return -1;
    return ret.first();
}

#ifndef QT_NO_PHONON_AUDIOCAPTURE
bool GlobalConfig::isHiddenAudioCaptureDevice(int i) const
{
    if (!getHideAdvancedDevices())
        return false;

    AudioCaptureDevice ad = AudioCaptureDevice::fromIndex(i);
    const QVariant var = ad.property("isAdvanced");
    return (var.isValid() && var.toBool());
}

void GlobalConfig::setAudioCaptureDeviceListFor(Phonon::Category category, QList<int> order)
{
    if (PulseSupport::getInstance()->isActive()) {
        /// @todo Write support to push the priority list to pulseaudio
        return;
    }

    QSettingsGroup backendConfig(&m_config, QLatin1String("AudioCaptureDevice")); // + Factory::identifier());

    order = _reindexList(category, order, false);

    const QList<int> noCategoryOrder = audioCaptureDeviceListFor(Phonon::NoCategory, ShowUnavailableDevices|ShowAdvancedDevices);
    if (category != Phonon::NoCategory && order == noCategoryOrder) {
        backendConfig.removeEntry(QLatin1String("Category_") + QString::number(category));
    } else {
        backendConfig.setValue(QLatin1String("Category_") + QString::number(category), order);
    }
}

QList<int> GlobalConfig::audioCaptureDeviceListFor(Phonon::Category category, int override) const
{
    const bool hideAdvancedDevices = ((override & AdvancedDevicesFromSettings)
            ? getHideAdvancedDevices()
            : static_cast<bool>(override & HideAdvancedDevices));

    QList<int> defaultList;
    BackendInterface *backendIface = qobject_cast<BackendInterface *>(Factory::backend());

#ifndef QT_NO_PHONON_PLATFORMPLUGIN
    if (!backendIface || !PulseSupport::getInstance()->isActive()) {
        if (PlatformPlugin *platformPlugin = Factory::platformPlugin()) {
            // the platform plugin lists the audio devices for the platform
            // this list already is in default order (as defined by the platform plugin)
            defaultList = platformPlugin->objectDescriptionIndexes(Phonon::AudioCaptureDeviceType);
            if (hideAdvancedDevices) {
                QMutableListIterator<int> it(defaultList);
                while (it.hasNext()) {
                    AudioCaptureDevice objDesc = AudioCaptureDevice::fromIndex(it.next());
                    const QVariant var = objDesc.property("isAdvanced");
                    if (var.isValid() && var.toBool()) {
                        it.remove();
                    }
                }
            }
        }
    }
#endif //QT_NO_PHONON_PLATFORMPLUGIN

    // lookup the available devices directly from the backend
    if (backendIface) {
        // this list already is in default order (as defined by the backend)
        QList<int> list = backendIface->objectDescriptionIndexes(Phonon::AudioCaptureDeviceType);
        if (hideAdvancedDevices || !defaultList.isEmpty() || (override & HideUnavailableDevices)) {
            filter(AudioCaptureDeviceType, backendIface, &list,
                    (hideAdvancedDevices ? FilterAdvancedDevices : 0)
                    // the platform plugin already provided the hardware devices
                    | (defaultList.isEmpty() ? 0 : FilterHardwareDevices)
                    | ((override & HideUnavailableDevices) ? FilterUnavailableDevices : 0)
                  );
        }
        defaultList += list;
    }

    return _sortDevicesByCategoryPriority(AudioCaptureDeviceType, category, defaultList);
}

int GlobalConfig::audioCaptureDeviceFor(Phonon::Category category, int override) const
{
    QList<int> ret = audioCaptureDeviceListFor(category, override);
    if (ret.isEmpty())
        return -1;
    return ret.first();
}
#endif //QT_NO_PHONON_AUDIOCAPTURE


} // namespace Phonon

QT_END_NAMESPACE
