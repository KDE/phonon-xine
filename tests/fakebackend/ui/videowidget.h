/*  This file is part of the KDE project
    Copyright (C) 2005-2006 Matthias Kretz <kretz@kde.org>

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
#ifndef Phonon_UI_FAKE_VIDEOWIDGET_H
#define Phonon_UI_FAKE_VIDEOWIDGET_H

#include <QWidget>
#include <phonon/videoframe.h>
#include "../abstractvideooutput.h"
#include <QPixmap>

class QString;

namespace Phonon
{
namespace Fake
{
	class VideoWidget : public QWidget, public Phonon::Fake::AbstractVideoOutput
	{
		Q_OBJECT
		public:
			VideoWidget( QWidget* parent = 0 );

			// Fake specific:
			virtual void* internal1( void* = 0 ) { return static_cast<Phonon::Fake::AbstractVideoOutput*>( this ); }
			virtual void processFrame( Phonon::VideoFrame& frame );

		protected:
			virtual void paintEvent( QPaintEvent* ev );

		private:
			bool m_fullscreen;
			QPixmap m_pixmap;
	};
}} //namespace Phonon::Fake

// vim: sw=4 ts=4 tw=80 noet
#endif // Phonon_UI_FAKE_VIDEOWIDGET_H
