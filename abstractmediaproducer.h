/*  This file is part of the KDE project
    Copyright (C) 2006 Tim Beaulen <tbscope@gmail.com>

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
#ifndef Phonon_XINE_ABSTRACTMEDIAPRODUCER_H
#define Phonon_XINE_ABSTRACTMEDIAPRODUCER_H

#include <QObject>
#include <QTime>
#include <QList>
#include "audiopath.h"
#include "videopath.h"
#include <QHash>

#include <xine.h>
#include "xine_engine.h"
#include <phonon/mediaproducerinterface.h>
#include <QMultiMap>

class QTimer;

namespace Phonon
{

namespace Xine
{
	class AbstractMediaProducer : public QObject, public MediaProducerInterface
	{
		Q_OBJECT
		Q_INTERFACES( Phonon::MediaProducerInterface )
		public:
			AbstractMediaProducer( QObject* parent, XineEngine* xe );
			virtual ~AbstractMediaProducer();

			virtual bool addVideoPath( QObject* videoPath );
			virtual bool addAudioPath( QObject* audioPath );
			virtual void removeVideoPath( QObject* videoPath );
			virtual void removeAudioPath( QObject* audioPath );
			virtual State state() const;
			virtual bool hasVideo() const;
			virtual bool isSeekable() const;
			virtual qint64 currentTime() const;
			virtual qint32 tickInterval() const;

			virtual QStringList availableAudioStreams() const;
			virtual QStringList availableVideoStreams() const;
			virtual QStringList availableSubtitleStreams() const;

			virtual QString selectedAudioStream( const QObject* audioPath ) const;
			virtual QString selectedVideoStream( const QObject* videoPath ) const;
			virtual QString selectedSubtitleStream( const QObject* videoPath ) const;

			virtual void selectAudioStream( const QString& streamName, const QObject* audioPath );
			virtual void selectVideoStream( const QString& streamName, const QObject* videoPath );
			virtual void selectSubtitleStream( const QString& streamName, const QObject* videoPath );

			virtual void setTickInterval( qint32 newTickInterval );
			virtual void play();
			virtual void pause();
			virtual void stop();
			virtual void seek( qint64 time );

			xine_stream_t* stream() const { return m_stream; }
			void checkAudioOutput();
			void checkVideoOutput();

		Q_SIGNALS:
			void stateChanged( Phonon::State newstate, Phonon::State oldstate );
			void tick( qint64 time );
			void metaDataChanged( const QMultiMap<QString, QString>& );

		protected:
			void setState( State );
			virtual bool event( QEvent* ev );
			void updateMetaData();
			virtual void recreateStream();
			virtual void reachedPlayingState() {}
			virtual void leftPlayingState() {}

		protected Q_SLOTS:
			virtual void emitTick();

		private slots:
			void getStartTime();

		private:
			void createStream();

			XineEngine *m_xine_engine;
			xine_stream_t *m_stream;
			xine_event_queue_t *m_event_queue;
			State m_state;
			QTimer *m_tickTimer;
			qint32 m_tickInterval;
			int m_bufferSize;
			int m_startTime;
			AudioPath *m_audioPath;
			VideoPath *m_videoPath;

			QHash<const QObject*, QString> m_selectedAudioStream;
			QHash<const QObject*, QString> m_selectedVideoStream;
			QHash<const QObject*, QString> m_selectedSubtitleStream;
			QMultiMap<QString, QString> m_metaDataMap;

			mutable int m_currentTimeOverride;
	};
}} //namespace Phonon::Xine

// vim: sw=4 ts=4 tw=80 noet
#endif // Phonon_XINE_ABSTRACTMEDIAPRODUCER_H
