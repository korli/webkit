/*
 * Copyright (C) 2014 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "WebVisitedLinkStore.h"

#include "NotImplemented.h"
#include "WebView.h"
#include <WebCore/history/PageCache.h>
#include <wtf/NeverDestroyed.h>

using namespace WebCore;

static bool s_shouldTrackVisitedLinks;

WebVisitedLinkStore& WebVisitedLinkStore::shared()
{
    static NeverDestroyed<WebVisitedLinkStore> visitedLinkStore;
    
    return visitedLinkStore;
}

WebVisitedLinkStore::WebVisitedLinkStore()
    : m_visitedLinksPopulated(false)
{
}

WebVisitedLinkStore::~WebVisitedLinkStore()
{
}

void WebVisitedLinkStore::setShouldTrackVisitedLinks(bool shouldTrackVisitedLinks)
{
    if (s_shouldTrackVisitedLinks == shouldTrackVisitedLinks)
        return;

    s_shouldTrackVisitedLinks = shouldTrackVisitedLinks;
    if (!s_shouldTrackVisitedLinks)
        removeAllVisitedLinks();
}

void WebVisitedLinkStore::removeAllVisitedLinks()
{
    shared().removeVisitedLinkHashes();
    PageCache::singleton().markPagesForVisitedLinkStyleRecalc();
}

void WebVisitedLinkStore::addVisitedLink(const String& urlString)
{
    addVisitedLinkHash(visitedLinkHash(urlString));
}

bool WebVisitedLinkStore::isLinkVisited(Page& page, LinkHash linkHash, const URL& baseURL, const AtomicString& attributeURL)
{
    populateVisitedLinksIfNeeded(page);

    return m_visitedLinkHashes.contains(linkHash);
}

void WebVisitedLinkStore::addVisitedLink(Page&, LinkHash linkHash)
{
    if (!s_shouldTrackVisitedLinks)
        return;

    addVisitedLinkHash(linkHash);
}

void WebVisitedLinkStore::populateVisitedLinksIfNeeded(Page& sourcePage)
{
    if (m_visitedLinksPopulated)
        return;

    m_visitedLinksPopulated = true;

    notImplemented();
#if 0
    WebView* webView = kit(&sourcePage);
    if (!webView)
        return;

    COMPtr<IWebHistoryDelegate> historyDelegate;
    webView->historyDelegate(&historyDelegate);
    if (historyDelegate) {
        historyDelegate->populateVisitedLinksForWebView(webView);
        return;
    }

    WebHistory* history = WebHistory::sharedHistory();
    if (!history)
        return;
    history->addVisitedLinksToVisitedLinkStore(*this);
#endif
}

void WebVisitedLinkStore::addVisitedLinkHash(LinkHash linkHash)
{
    ASSERT(s_shouldTrackVisitedLinks);
    m_visitedLinkHashes.add(linkHash);

    invalidateStylesForLink(linkHash);
    PageCache::singleton().markPagesForVisitedLinkStyleRecalc();
}

void WebVisitedLinkStore::removeVisitedLinkHashes()
{
    m_visitedLinksPopulated = false;
    if (m_visitedLinkHashes.isEmpty())
        return;
    m_visitedLinkHashes.clear();

    invalidateStylesForAllLinks();
}
