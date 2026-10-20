// Copyright The Phorepoc Authors
// SPDX-License-Identifier: BSD-2-Clause

/*
 * AXObjectCacheSymbian.cpp -- the platform half of WebCore's accessibility cache
 * without an assistive-technology bridge (Symbian has no AT-SPI; a screen-reader bridge would post these to the S60 accessibility
 * framework later). Wrappers are attached as on the PlayStation port (AccessibilityObjectPlayStation.cpp, used as is); every platform
 * notification is dropped. The PlayStation AXObjectCache file forwards to PlayStation-only ChromeClient hooks, hence this copy.
 */
#include "config.h"
#include "AXObjectCache.h"

#include "AXNotifications.h"
#include "AccessibilityObject.h"

namespace WebCore {

void AXObjectCache::attachWrapper(AccessibilityObject& object)
{
    auto wrapper = adoptRef(*new AccessibilityObjectWrapper());
    object.setWrapper(wrapper.ptr());
}

void AXObjectCache::postPlatformNotification(AccessibilityObject&, AXNotification)
{
}

void AXObjectCache::nodeTextChangePlatformNotification(AccessibilityObject*, AXTextChange, unsigned, const String&)
{
}

void AXObjectCache::frameLoadingEventPlatformNotification(RenderView*, AXLoadingEvent)
{
}

void AXObjectCache::platformHandleFocusedUIElementChanged(AccessibilityObject*, AccessibilityObject*)
{
}

void AXObjectCache::handleScrolledToAnchor(const Node&)
{
}

void AXObjectCache::platformPerformDeferredCacheUpdate()
{
}

} // namespace WebCore
