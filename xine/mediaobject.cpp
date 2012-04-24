/*  This file is part of the KDE project
    Copyright (C) 2006 Tim Beaulen <tbscope@gmail.com>
    Copyright (C) 2006-2007 Matthias Kretz <kretz@kde.org>
    Copyright (C) 2008 Ian Monroe <imonroe@kde.org>

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

#include "mediaobject.h"

#include "bytestream.h"

#include <QEvent>
#include <QFile>
#include <QVector>
#include <QByteArray>
#include <QStringList>
#include <QMultiMap>
#include <QtDebug>
#include <QMetaType>
#include <QTextCodec>
#include <QUrl>

#include <cmath>
#include "xinethread.h"
#include "sinknode.h"
#include "videowidget.h"
#include "events.h"

static const char *const green  = "\033[1;40;32m";
static const char *const blue   = "\033[1;40;34m";
static const char *const normal = "\033[0m";

Q_DECLARE_METATYPE(QVariant)

namespace Phonon
{
namespace Xine
{
MediaObject::MediaObject(QObject *parent)
    : QObject(parent),
    SourceNode(XineThread::newStream()),
    m_state(Phonon::LoadingState),
    m_stream(static_cast<XineStream *>(SourceNode::threadSafeObject().data())),
    m_currentTitle(1),
    m_transitionTime(0),
    m_autoplayTitles(true),
    m_waitingForNextSource(false)
{
    m_stream->setMediaObject(this);
    m_stream->useGaplessPlayback(true);

    qRegisterMetaType<QMultiMap<QString,QString> >("QMultiMap<QString,QString>");
    connect(m_stream, SIGNAL(stateChanged(Phonon::State, Phonon::State)),
            SLOT(handleStateChange(Phonon::State, Phonon::State)));
    connect(m_stream, SIGNAL(metaDataChanged(const QMultiMap<QString, QString> &)),
            SIGNAL(metaDataChanged(const QMultiMap<QString, QString> &)));
    connect(m_stream, SIGNAL(seekableChanged(bool)), SIGNAL(seekableChanged(bool)));
    connect(m_stream, SIGNAL(hasVideoChanged(bool)), SIGNAL(hasVideoChanged(bool)));
    connect(m_stream, SIGNAL(hasVideoChanged(bool)), SLOT(handleHasVideoChanged(bool)));
    connect(m_stream, SIGNAL(bufferStatus(int)), SIGNAL(bufferStatus(int)));
    connect(m_stream, SIGNAL(tick(qint64)), SIGNAL(tick(qint64)));
    connect(m_stream, SIGNAL(availableSubtitlesChanged()), SIGNAL(availableSubtitlesChanged()));
    connect(m_stream, SIGNAL(availableAudioChannelsChanged()), SIGNAL(availableAudioChannelsChanged()));
    connect(m_stream, SIGNAL(availableChaptersChanged(int)), SIGNAL(availableChaptersChanged(int)));
    connect(m_stream, SIGNAL(chapterChanged(int)), SIGNAL(chapterChanged(int)));
    connect(m_stream, SIGNAL(availableAnglesChanged(int)), SIGNAL(availableAnglesChanged(int)));
    connect(m_stream, SIGNAL(angleChanged(int)), SIGNAL(angleChanged(int)));
    connect(m_stream, SIGNAL(finished()), SLOT(handleFinished()), Qt::QueuedConnection);
    connect(m_stream, SIGNAL(length(qint64)), SIGNAL(totalTimeChanged(qint64)), Qt::QueuedConnection);
    connect(m_stream, SIGNAL(prefinishMarkReached(qint32)), SIGNAL(prefinishMarkReached(qint32)), Qt::QueuedConnection);
    connect(m_stream, SIGNAL(availableTitlesChanged(int)), SLOT(handleAvailableTitlesChanged(int)));
    connect(m_stream, SIGNAL(needNextUrl()), SLOT(needNextUrl()));
    connect(m_stream, SIGNAL(downstreamEvent(Event *)), SLOT(downstreamEvent(Event *)));

    qRegisterMetaType<QVariant>();
    connect(m_stream, SIGNAL(hackSetProperty(const char *, const QVariant &)), SLOT(syncHackSetProperty(const char *, const QVariant &)), Qt::QueuedConnection);
}

void MediaObject::syncHackSetProperty(const char *name, const QVariant &val)
{
    if (parent()) {
        parent()->setProperty(name, val);
    }
}

MediaObject::~MediaObject()
{
    if (m_bytestream) {
        // ByteStream must be stopped before calling XineStream::closeBlocking to avoid deadlocks
        // when closeBlocking waits for data in the ByteStream that will never arrive since the main
        // thread is blocking
        m_bytestream->stop();
        // don't delete m_bytestream - the xine input plugin owns it
    }
    m_stream->closeBlocking();
}

State MediaObject::state() const
{
    return m_state;
}

bool MediaObject::hasVideo() const
{
    return m_stream->hasVideo();
}

MediaStreamTypes MediaObject::outputMediaStreamTypes() const
{
    return Phonon::Xine::Audio | Phonon::Xine::Video;
}

bool MediaObject::isSeekable() const
{
    return m_stream->isSeekable();
}

qint64 MediaObject::currentTime() const
{
    //debug() << Q_FUNC_INFO << kBacktrace();
    switch(m_stream->state()) {
    case Phonon::PausedState:
    case Phonon::BufferingState:
    case Phonon::PlayingState:
        return m_stream->currentTime();
    case Phonon::StoppedState:
    case Phonon::LoadingState:
        return 0;
    case Phonon::ErrorState:
        break;
    }
    return -1;
}

qint64 MediaObject::totalTime() const
{
    const qint64 ret = m_stream->totalTime();
    //debug() << Q_FUNC_INFO << "returning " << ret;
    return ret;
}

qint64 MediaObject::remainingTime() const
{
    switch(m_stream->state()) {
    case Phonon::PausedState:
    case Phonon::BufferingState:
    case Phonon::PlayingState:
        {
            const qint64 ret = m_stream->remainingTime();
            //debug() << Q_FUNC_INFO << "returning " << ret;
            return ret;
        }
        break;
    case Phonon::StoppedState:
    case Phonon::LoadingState:
        //debug() << Q_FUNC_INFO << "returning 0";
        return 0;
    case Phonon::ErrorState:
        break;
    }
    //debug() << Q_FUNC_INFO << "returning -1";
    return -1;
}

qint32 MediaObject::tickInterval() const
{
    return m_tickInterval;
}

void MediaObject::setTickInterval(qint32 newTickInterval)
{
    m_tickInterval = newTickInterval;
    m_stream->setTickInterval(m_tickInterval);
}

void MediaObject::play()
{
    debug() << Q_FUNC_INFO << green << "PLAY" << normal;
    m_stream->play();
}

void MediaObject::pause()
{
    m_stream->pause();
}

void MediaObject::stop()
{
    //if (m_state == Phonon::PlayingState || m_state == Phonon::PausedState || m_state == Phonon::BufferingState) {
        m_stream->stop();
    //}
}

void MediaObject::seek(qint64 time)
{
    //debug() << Q_FUNC_INFO << time;
    m_stream->seek(time);
}

QString MediaObject::errorString() const
{
    return m_stream->errorString();
}

Phonon::ErrorType MediaObject::errorType() const
{
    return m_stream->errorType();
}

void MediaObject::handleStateChange(Phonon::State newstate, Phonon::State oldstate)
{
    if (m_state == newstate && m_state == BufferingState) {
        debug() << Q_FUNC_INFO << blue << "end faking" << normal;
        // BufferingState -> BufferingState, nothing to do
        return;
    } else if (m_state != oldstate) {
        // m_state == oldstate always, except when faking buffering:

        // so we're faking BufferingState, then m_state must be in BufferingState
        Q_ASSERT(m_state == BufferingState);
        if (newstate == PlayingState || newstate == ErrorState) {
            debug() << Q_FUNC_INFO << blue << "end faking" << normal;
            oldstate = m_state;
        } else {
            // we're faking BufferingState and stay there until we either reach BufferingState,
            // PlayingState or ErrorState
            return;
        }
    }
    m_state = newstate;

    debug() << Q_FUNC_INFO << "reached " << newstate << " after " << oldstate;
    emit stateChanged(newstate, oldstate);
}
void MediaObject::handleFinished()
{
    debug() << Q_FUNC_INFO << "emit finished()";
    emit finished();
}

MediaSource MediaObject::source() const
{
    //debug() << Q_FUNC_INFO;
    return m_mediaSource;
}

qint32 MediaObject::prefinishMark() const
{
    //debug() << Q_FUNC_INFO;
    return m_prefinishMark;
}

qint32 MediaObject::transitionTime() const
{
    return m_transitionTime;
}

void MediaObject::setTransitionTime(qint32 newTransitionTime)
{
    if (m_transitionTime != newTransitionTime) {
        m_transitionTime = newTransitionTime;
        if (m_transitionTime == 0) {
            m_stream->useGaplessPlayback(true);
        } else if (m_transitionTime > 0) {
            m_stream->useGapOf((newTransitionTime + 50) / 100); // xine-lib provides a resolution of 1/10s
        } else {
            // TODO: a crossfade of milliseconds milliseconds
            m_stream->useGaplessPlayback(true);
        }
    }
}

static inline bool isEmptyOrInvalid(MediaSource::Type t)
{
    return t == MediaSource::Empty || t == MediaSource::Invalid;
}

void MediaObject::setNextSource(const MediaSource &source)
{
    m_waitingForNextSource = false;
    if (m_transitionTime < 0) {
        qWarning() <<  "crossfades are not supported with the xine backend";
    } else if (m_transitionTime > 0) {
        if (isEmptyOrInvalid(source.type())) {
            // tells gapless playback logic to stop waiting and emit finished()
            QMetaObject::invokeMethod(m_stream, "playbackFinished", Qt::QueuedConnection);
        }
        setSourceInternal(source, HardSwitch);
        if (!isEmptyOrInvalid(source.type())) {
            play();
        }
        return;
    }
    if (isEmptyOrInvalid(source.type())) {
        // tells gapless playback logic to stop waiting and emit finished()
        m_stream->gaplessSwitchTo(QByteArray());
    }
    setSourceInternal(source, GaplessSwitch);
}

void MediaObject::setSource(const MediaSource &source)
{
    setSourceInternal(source, HardSwitch);
}

static QByteArray mrlEncode(QByteArray mrl)
{
    bool localeUnicode = qgetenv("LANG").contains("UTF"); // test this

    unsigned char c;
    for (int i = 0; i < mrl.size(); ++i) {
        c = mrl.at(i);
        if ((localeUnicode && c=='#') || //TODO: remove this abomination in the far future when everyone has gotten sane locales :-D
            (!localeUnicode && (c & 0x80 || c == '\\' || c < 32 || c == '%' || c == '#'))) {
            char enc[4];
            qsnprintf(enc, 4, "%%%02X", c);
            mrl = mrl.left(i) + QByteArray(enc, 3) + mrl.mid(i + 1);
            i += 2;
        }
    }
    return mrl;
}

void MediaObject::setSourceInternal(const MediaSource &source, HowToSetTheUrl how)
{
    //debug() << Q_FUNC_INFO;
    m_titles.clear();
    m_mediaSource = source;

    switch (source.type()) {
    case MediaSource::Invalid:
        m_stream->stop();
        break;
    case MediaSource::Empty:
        m_stream->stop();
        m_stream->unload();
        break;
    case MediaSource::LocalFile:
    case MediaSource::Url:
        if (source.url().scheme() == QLatin1String("kbytestream")) {
            m_mediaSource = MediaSource();
            qWarning() <<  "do not ever use kbytestream:/ URLs with MediaObject!";
            m_stream->setMrl(QByteArray());
            m_stream->setError(Phonon::NormalError, tr("Cannot open media data at '<i>%1</i>'").arg(source.url().toString(QUrl::RemovePassword)));
            return;
        }
        {
            const QByteArray &mrl = (source.url().scheme() == QLatin1String("file") ?
                    "file:/" + mrlEncode (source.url().toLocalFile().toLocal8Bit()) :
                    source.url().toEncoded());
            switch (how) {
                case GaplessSwitch:
                    m_stream->gaplessSwitchTo(mrl);
                    break;
                case HardSwitch:
                    m_stream->setMrl(mrl);
                    break;
            }
        }
        break;
    case MediaSource::Disc:
        {
            m_mediaDevice = QFile::encodeName(source.deviceName());
            if (!m_mediaDevice.isEmpty() && !m_mediaDevice.startsWith('/')) {
                //qWarning() << "mediaDevice '%i' has to be an absolute path - starts with a /", m_mediaDevice);
                m_mediaDevice.clear();
            }
            m_mediaDevice += '/';

            QByteArray mrl;
            switch (source.discType()) {
            case Phonon::NoDisc:
                qWarning() << "I should never get to see a MediaSource that is a disc but doesn't specify which one";
                return;
            case Phonon::Cd:
                mrl = autoplayMrlsToTitles("CD", "cdda:/");
                break;
            case Phonon::Dvd:
                mrl = "dvd:" + m_mediaDevice;
                break;
            case Phonon::Vcd:
                mrl = autoplayMrlsToTitles("VCD", "vcd:/");
                break;
            default:
                qWarning() <<  "media " << source.discType() << " not implemented";
                return;
            }
            switch (how) {
            case GaplessSwitch:
                m_stream->gaplessSwitchTo(mrl);
                break;
            case HardSwitch:
                m_stream->setMrl(mrl);
                break;
            }
        }
        break;
    case MediaSource::Stream:
        {
            // m_bytestream may not be deleted, the xine input plugin takes ownership and will
            // delete it when xine frees the input plugin
            m_bytestream = new ByteStream(source, this);
            switch (how) {
            case GaplessSwitch:
                m_stream->gaplessSwitchTo(m_bytestream->mrl());
                break;
            case HardSwitch:
                m_stream->setMrl(m_bytestream->mrl());
                break;
            }
        }
        break;
    }
    emit currentSourceChanged(m_mediaSource);
//X     if (state() != Phonon::LoadingState) {
//X         stop();
//X     }
}

//X void MediaObject::openMedia(Phonon::MediaObject::Media m, const QString &mediaDevice)
//X {
//X     m_titles.clear();
//X
//X }

QByteArray MediaObject::autoplayMrlsToTitles(const char *plugin, const char *defaultMrl)
{
    const int lastSize = m_titles.size();
    m_titles.clear();
    int num = 0;
    const char*const*mrls = xine_get_autoplay_mrls(m_stream->xine(), plugin, &num);
    for (int i = 0; i < num; ++i) {
        if (mrls[i]) {
            debug() << Q_FUNC_INFO << mrls[i];
            m_titles << QByteArray(mrls[i]);
        }
    }
    if (lastSize != m_titles.size()) {
        emit availableTitlesChanged(m_titles.size());
    }
    if (m_titles.isEmpty()) {
        return defaultMrl;
    }
    m_currentTitle = 1;
    if (m_autoplayTitles) {
        m_stream->useGaplessPlayback(true);
    } else {
        m_stream->useGaplessPlayback(false);
    }
    return m_titles.first();
}

bool MediaObject::hasInterface(Interface interface) const
{
    switch (interface) {
    case AddonInterface::NavigationInterface:
        break;
    case AddonInterface::AngleInterface:
        break;
    case AddonInterface::TitleInterface:
        if (m_titles.size() > 1) {
            return true;
        }
        break;
    case AddonInterface::ChapterInterface:
        if (m_stream->availableChapters() > 1) {
            return true;
        }
        break;
    case AddonInterface::SubtitleInterface:
        if (m_stream->subtitlesSize() > 0) { //subtitles off by default, enable if any
            return true;
        }
        break;
    case AddonInterface::AudioChannelInterface:
        if (m_stream->audioChannelsSize() > 1) { //first audio channel on by default, enable if > 1
            return true;
        }
        break;
    }
    return false;
}

void MediaObject::handleAvailableTitlesChanged(int t)
{
    debug() << Q_FUNC_INFO << t;
    if (m_mediaSource.discType() == Phonon::Dvd) {
        QByteArray mrl = "dvd:" + m_mediaDevice;
        const int lastSize = m_titles.size();
        m_titles.clear();
        for (int i = 1; i <= t; ++i) {
            m_titles << mrl + QByteArray::number(i);
        }
        if (m_titles.size() != lastSize) {
            emit availableTitlesChanged(m_titles.size());
        }
    }
}

QVariant MediaObject::interfaceCall(Interface interface, int command, const QList<QVariant> &arguments)
{
    debug() << Q_FUNC_INFO << interface << ", " << command;

    switch (interface) {
    case AddonInterface::NavigationInterface:
        break;
    case AddonInterface::AngleInterface:
        break;
    case AddonInterface::ChapterInterface:
        switch (static_cast<AddonInterface::ChapterCommand>(command)) {
        case AddonInterface::availableChapters:
            return m_stream->availableChapters();
        case AddonInterface::chapter:
            return m_stream->currentChapter();
        case AddonInterface::setChapter:
            {
                if (arguments.isEmpty() || !arguments.first().canConvert(QVariant::Int)) {
                    debug() << Q_FUNC_INFO << "arguments invalid";
                    return false;
                }
                int c = arguments.first().toInt();
                int t = m_currentTitle - 1;
                if (t < 0) {
                    t = 0;
                }
                if (m_titles.size() > t) {
                    QByteArray mrl = m_titles[t] + '.' + QByteArray::number(c);
                    m_stream->setMrl(mrl, XineStream::KeepState);
                }
                return true;
            }
        }
        break;
    case AddonInterface::TitleInterface:
        switch (static_cast<AddonInterface::TitleCommand>(command)) {
        case AddonInterface::availableTitles:
            debug() << Q_FUNC_INFO << m_titles.size();
            return m_titles.size();
        case AddonInterface::title:
            debug() << Q_FUNC_INFO << m_currentTitle;
            return m_currentTitle;
        case AddonInterface::setTitle:
            {
                if (arguments.isEmpty() || !arguments.first().canConvert(QVariant::Int)) {
                    debug() << Q_FUNC_INFO << "arguments invalid";
                    return false;
                }
                int t = arguments.first().toInt();
                if (t > m_titles.size()) {
                    debug() << Q_FUNC_INFO << "invalid title";
                    return false;
                }
                if (m_currentTitle == t) {
                    debug() << Q_FUNC_INFO << "no title change";
                    return true;
                }
                debug() << Q_FUNC_INFO << "change title from " << m_currentTitle << " to " << t;
                m_currentTitle = t;
                m_stream->setMrl(m_titles[t - 1],
                        m_autoplayTitles ? XineStream::KeepState : XineStream::StoppedState);
                if (m_mediaSource.discType() == Phonon::Cd) {
                    emit titleChanged(m_currentTitle);
                }
                return true;
            }
        case AddonInterface::autoplayTitles:
            return m_autoplayTitles;
        case AddonInterface::setAutoplayTitles:
            {
                if (arguments.isEmpty() || !arguments.first().canConvert(QVariant::Bool)) {
                    debug() << Q_FUNC_INFO << "arguments invalid";
                    return false;
                }
                bool b = arguments.first().toBool();
                if (b == m_autoplayTitles) {
                    debug() << Q_FUNC_INFO << "setAutoplayTitles: no change";
                    return false;
                }
                m_autoplayTitles = b;
                if (b) {
                    debug() << Q_FUNC_INFO << "setAutoplayTitles: enable autoplay";
                    m_stream->useGaplessPlayback(true);
                } else {
                    debug() << Q_FUNC_INFO << "setAutoplayTitles: disable autoplay";
                    m_stream->useGaplessPlayback(false);
                }
                return true;
            }
        }
        break;
    case AddonInterface::SubtitleInterface:
        switch (static_cast<AddonInterface::SubtitleCommand>(command))
        {
            case AddonInterface::availableSubtitles:
                return QVariant::fromValue(m_stream->availableSubtitles());
            case AddonInterface::currentSubtitle:
                return QVariant::fromValue(m_stream->currentSubtitle());
            case AddonInterface::setCurrentSubtitle:
                if (arguments.isEmpty() || !arguments.first().canConvert<SubtitleDescription>()) {
                    debug() << Q_FUNC_INFO << "arguments invalid";
                    return false;
                }
                m_stream->setCurrentSubtitle(arguments.first().value<SubtitleDescription>());
                return true;
        }
        break;
    case AddonInterface::AudioChannelInterface:
        switch (static_cast<AddonInterface::AudioChannelCommand>(command))
        {
            case AddonInterface::availableAudioChannels:
                return QVariant::fromValue(m_stream->availableAudioChannels());
            case AddonInterface::currentAudioChannel:
                return QVariant::fromValue(m_stream->currentAudioChannel());
            case AddonInterface::setCurrentAudioChannel:
                if (arguments.isEmpty() || !arguments.first().canConvert<AudioChannelDescription>()) {
                    debug() << Q_FUNC_INFO << "arguments invalid";
                    return false;
                }
                m_stream->setCurrentAudioChannel(arguments.first().value<AudioChannelDescription>());
                return true;
         }
         break;
    }
    return QVariant();
}

void MediaObject::needNextUrl()
{
    if (m_mediaSource.type() == MediaSource::Disc && m_titles.size() > m_currentTitle) {
        m_stream->gaplessSwitchTo(m_titles[m_currentTitle]);
        ++m_currentTitle;
        emit titleChanged(m_currentTitle);
        return;
    }
    m_waitingForNextSource = true;
    emit aboutToFinish();
    if (m_waitingForNextSource) {
        if (m_transitionTime > 0) {
            QMetaObject::invokeMethod(m_stream, "playbackFinished", Qt::QueuedConnection);
        } else {
            m_stream->gaplessSwitchTo(QByteArray());
        }
    }
}

void MediaObject::setPrefinishMark(qint32 newPrefinishMark)
{
    m_prefinishMark = newPrefinishMark;
    m_stream->setPrefinishMark(newPrefinishMark);
}

void MediaObject::handleHasVideoChanged(bool hasVideo)
{
    downstreamEvent(new HasVideoEvent(hasVideo));
}

void MediaObject::upstreamEvent(Event *e)
{
    Q_ASSERT(e);
    switch (e->type()) {
        case Event::IsThereAXineEngineForMe:
        // yes there is
        downstreamEvent(new HeresYourXineStreamEvent(stream()));
        break;
    case Event::UpdateVolume:
        debug() << Q_FUNC_INFO << "UpdateVolumeEvent";
        // postEvent takes ownership of the event and will delete it when done
        QCoreApplication::postEvent(m_stream, copyEvent(static_cast<UpdateVolumeEvent *>(e)));
        break;
    case Event::RequestSnapshot:
        // postEvent takes ownership of the event and will delete it when done
        QCoreApplication::postEvent(m_stream, copyEvent(static_cast<RequestSnapshotEvent*>(e)));
        break;
    case Event::SetParam:
        // postEvent takes ownership of the event and will delete it when done
        QCoreApplication::postEvent(m_stream, copyEvent(static_cast<SetParamEvent *>(e)));
        break;
    case Event::EventSend:
        //debug() << Q_FUNC_INFO << "copying EventSendEvent and post it to XineStream" << m_stream;
        // postEvent takes ownership of the event and will delete it when done
        QCoreApplication::postEvent(m_stream, copyEvent(static_cast<EventSendEvent *>(e)));
        break;
    default:
        break;
    }
    SourceNode::upstreamEvent(e);
}

// the point of this reimplementation is to make downstreamEvent available as a slot
void MediaObject::downstreamEvent(Event *e)
{
    SourceNode::downstreamEvent(e);
}

}}

#include "mediaobject.moc"
// vim: sw=4 ts=4
