// Copyright The Phorepoc Authors
// SPDX-License-Identifier: BSD-2-Clause

/*
 * GLStubsSymbian.cpp -- The Symbian port paints on the CPU (Skia raster) with
 * accelerated compositing off, so there is no GL display, context, fence, texture or composited GraphicsLayer. WebKit's Skia painting
 * code still references the GPU types; these definitions satisfy the link and report "no GPU" wherever a caller can take no for an
 * answer (sharedDisplayIfExists, isSupported, MakeEGL). The rest is unreachable without compositing and stops loudly if ever reached.
 * GPU work (a device GPU via our GLES stack) replaces this file, not the callers.
 */
#include "config.h"

#include "BitmapTexture.h"
#include "GLContext.h"
#include "GLDisplay.h"
#include "GLFence.h"
#include "GraphicsLayer.h"
#include "PlatformDisplay.h"
#include <skia/gpu/ganesh/gl/GrGLInterface.h>

namespace GrGLInterfaces {
sk_sp<const GrGLInterface> MakeEGL()
{
    return nullptr;
}
}

namespace WebCore {

PlatformDisplay* PlatformDisplay::sharedDisplayIfExists()
{
    return nullptr;
}

PlatformDisplay& PlatformDisplay::sharedDisplay()
{
    RELEASE_ASSERT_NOT_REACHED_WITH_MESSAGE("Phorepoc: no GL display (CPU rendering)");
}

bool GLDisplay::runIfNotTerminated(NOESCAPE const Function<void(EGLDisplay)>&)
{
    return false;
}

bool GLFence::isSupported(const GLDisplay&)
{
    return false;
}

std::unique_ptr<GLFence> GLFence::create(const GLDisplay&)
{
    return nullptr;
}

GLContextWrapper::~GLContextWrapper() = default;

GLContext::~GLContext() = default;

bool GLContext::makeCurrentImpl()
{
    return false;
}

bool GLContext::unmakeCurrentImpl()
{
    return false;
}

unsigned GLContext::glVersion() const
{
    return 0;
}

RefPtr<GLDisplay> GLContext::display() const
{
    return nullptr;
}

bool GLContext::makeContextCurrent()
{
    return false;
}

GLContext::ScopedGLContextCurrent::ScopedGLContextCurrent(GLContext&)
{
    RELEASE_ASSERT_NOT_REACHED_WITH_MESSAGE("Phorepoc: no GL context (CPU rendering)");
}

GLContext::ScopedGLContextCurrent::~ScopedGLContextCurrent() = default;

BitmapTexture::~BitmapTexture() = default;

void BitmapTexture::updateContents(const void*, const IntRect&, const IntPoint&, int, PixelFormat)
{
    RELEASE_ASSERT_NOT_REACHED_WITH_MESSAGE("Phorepoc: no GL textures (CPU rendering)");
}

GrBackendTexture BitmapTexture::createSkiaBackendTexture() const
{
    return { };
}

Ref<GraphicsLayer> GraphicsLayer::create(GraphicsLayerFactory*, GraphicsLayerClient&, Type)
{
    RELEASE_ASSERT_NOT_REACHED_WITH_MESSAGE("Phorepoc: no composited layers (acceleratedCompositingEnabled = false)");
}

} // namespace WebCore
