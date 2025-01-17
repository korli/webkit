/*
 * Copyright (C) 2014 Haiku, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * aint with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef MediaPlayerPrivateHaiku_h
#define MediaPlayerPrivateHaiku_h
#if ENABLE(VIDEO)

#include "MediaPlayerPrivate.h"

#include <ObjectList.h>
#include <UrlProtocolAsynchronousListener.h>

class BBitmap;
class BDataIO;
class BMediaFile;
class BMediaTrack;
class BSoundPlayer;
class BUrlRequest;
struct media_raw_audio_format;

namespace WebCore {

class MediaPlayerPrivate : public MediaPlayerPrivateInterface, BUrlProtocolAsynchronousListener {
    public:
        static void registerMediaEngine(MediaEngineRegistrar);

        MediaPlayerPrivate(MediaPlayer*);

        ~MediaPlayerPrivate();

        void load(const String& url) override;
        void cancelLoad() override;

        void prepareToPlay() override;

        void play() override;
        void pause() override;

        FloatSize naturalSize() const override;
        bool hasAudio() const override;
        bool hasVideo() const override;

        void setVisible(bool) override;

        float duration() const override;
        float currentTime() const override;

        void seek(float) override;
        bool seeking() const override;
        bool paused() const override;

        void setVolume(float) override;

        MediaPlayer::NetworkState networkState() const override;
        MediaPlayer::ReadyState readyState() const override;

        float maxTimeSeekable() const override { return currentTime(); }

        std::unique_ptr<PlatformTimeRanges> buffered() const override;
        bool didLoadingProgress() const override;

        void setSize(const IntSize&) override;

        void paint(GraphicsContext*, const FloatRect&) override;

        // BUrlProtocolListener API
	    void DataReceived(BUrlRequest* caller, const char* data, off_t position, ssize_t size) override;
        void DownloadProgress(BUrlRequest*, ssize_t, ssize_t) override;
        void RequestCompleted(BUrlRequest*, bool success) override;

        // BHandler API
	    void MessageReceived(BMessage* message) override;
    private:
        
        void IdentifyTracks();

        static void playCallback(void*, void*, size_t,
            const media_raw_audio_format&);

        // engine support
        static void getSupportedTypes(HashSet<String>& types);
        static MediaPlayer::SupportsType supportsType(const MediaEngineSupportParameters&);

        mutable bool m_didReceiveData;
        BObjectList<BUrlRequest> m_requests;
        BPositionIO* m_cache;
        BMediaFile* m_mediaFile;
        BMediaTrack* m_audioTrack;
        BMediaTrack* m_videoTrack;
        BSoundPlayer* m_soundPlayer;
        BBitmap* m_frameBuffer;

        MediaPlayer* m_player;
        MediaPlayer::NetworkState m_networkState;
        MediaPlayer::ReadyState m_readyState;

        float m_volume;
        float m_currentTime;
        bool m_paused;
};

}

#endif
#endif
