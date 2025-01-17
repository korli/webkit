/*
 * Copyright (C) 2006 Dirk Mueller <mueller@kde.org>
 * Copyright (C) 2007 Ryan Leavengood <leavengood@gmail.com>
 * Copyright (C) 2010 Stephan Aßmus <superstippi@gmx.de>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "FontCache.h"

#include "Font.h"
#include "FontPlatformData.h"
#include "NotImplemented.h"
#include "Font.h"
#include <Font.h>
#include <String.h>
#include <interface/Font.h>

namespace WebCore {

void FontCache::platformInit()
{
}

RefPtr<Font> FontCache::systemFallbackForCharacters(const FontDescription& description, const Font* /*originalFontData*/, bool, const UChar* /*characters*/, unsigned /*length*/)
{
    FontPlatformData data(description, description.firstFamily());
        // TODO be smarter, try the other families and actually check that the
        // characters are available
    return fontForPlatformData(data);
}

Vector<String> FontCache::systemFontFamilies()
{
    Vector<String> fontFamilies;
    font_family family;
    font_style style;

    be_plain_font->GetFamilyAndStyle(&family, &style);
    fontFamilies.append(family);
    be_bold_font->GetFamilyAndStyle(&family, &style);
    fontFamilies.append(family);
    be_fixed_font->GetFamilyAndStyle(&family, &style);
    fontFamilies.append(family);

    return fontFamilies;
}

Ref<Font> FontCache::lastResortFallbackFont(const FontDescription& fontDescription)
{
    font_family family;
    font_style style;
    be_plain_font->GetFamilyAndStyle(&family, &style);
    AtomicString plainFontFamily(family);
    return *fontForFamily(fontDescription, plainFontFamily);
}

std::unique_ptr<FontPlatformData> FontCache::createFontPlatformData(const FontDescription& fontDescription, const AtomicString& family)
{
    return std::make_unique<FontPlatformData>(fontDescription, family);
}

void FontCache::getTraitsInFamily(const AtomicString& familyName, Vector<unsigned>& traitsMasks)
{
    // notImplemented(); also by fretype, must not be that important
}

} // namespace WebCore

