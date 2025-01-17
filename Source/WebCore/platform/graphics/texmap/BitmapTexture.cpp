/*
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies)
 * Copyright (C) 2014 Igalia S.L.
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
#include "BitmapTexture.h"

#include "GraphicsLayer.h"
#include "ImageBuffer.h"
#include "TextureMapper.h"

namespace WebCore {

BitmapTexture::~BitmapTexture()
{
}


void BitmapTexture::updateContents(TextureMapper* textureMapper, GraphicsLayer* sourceLayer, const IntRect& targetRect, const IntPoint& offset, UpdateContentsFlag updateContentsFlag)
{
    std::unique_ptr<ImageBuffer> imageBuffer = ImageBuffer::create(targetRect.size());

    if (!imageBuffer)
        return;

    GraphicsContext* context = imageBuffer->context();
    context->setImageInterpolationQuality(textureMapper->imageInterpolationQuality());
    context->setTextDrawingMode(textureMapper->textDrawingMode());

    IntRect sourceRect(targetRect);
    sourceRect.setLocation(offset);
    context->translate(-offset.x(), -offset.y());
    sourceLayer->paintGraphicsLayerContents(*context, sourceRect);

    RefPtr<Image> image = imageBuffer->copyImage(DontCopyBackingStore);
    if (!image)
        return;

    updateContents(image.get(), targetRect, IntPoint(), updateContentsFlag);
}

} // namespace
