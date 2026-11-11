// Copyright The Phorepoc Authors
// SPDX-License-Identifier: BSD-2-Clause

/*
 * PlatformSymbian.cpp -- the small WebCore platform hooks of the Symbian port.
 *   SystemFontDatabase   system-font shorthands (`font: menu`, ...): the S60 UI's sans-serif at 16px (GLib asks GSettings; we have
 *                        no settings service yet -- the theme's font size comes later).
 *   MIMETypeRegistry     extension <-> type table (no system MIME database on Symbian; the apparc recognisers answer by content,
 *                        not by extension). The common web set; WebCore's own tables cover the rest of its checks.
 *   PlatformKeyboardEvent modifier state outside an event: none (the S60 FEP delivers modifiers with each key event).
 *   PublicSuffixStore    libnspsl (NetSurf's library, the public-suffix list compiled in; recipes/build_png_nspsl.sh).
 */
#include "config.h"

#include "MIMETypeRegistry.h"
#include "PlatformKeyboardEvent.h"
#include "PublicSuffixStore.h"
#include "SystemFontDatabase.h"
#include "WebKitFontFamilyNames.h"
#include <wtf/NeverDestroyed.h>
#include <wtf/text/CString.h>
#include <string>

extern "C" const char* nspsl_getpublicsuffix(const char* hostname);

namespace WebCore {

// ---- SystemFontDatabase
SystemFontDatabase& SystemFontDatabase::singleton()
{
    static NeverDestroyed<SystemFontDatabase> database = SystemFontDatabase();
    return database.get();
}

auto SystemFontDatabase::platformSystemFontShorthandInfo(FontShorthand) -> SystemFontShorthandInfo
{
    return { WebKitFontFamilyNames::standardFamily, 16, FontSelectionValue(400) };
}

void SystemFontDatabase::platformInvalidate()
{
}

// ---- MIMETypeRegistry
struct ExtensionMap {
    ASCIILiteral extension;
    ASCIILiteral mimeType;
};

static constexpr ExtensionMap extensionMap[] = {
    { "html"_s, "text/html"_s }, { "htm"_s, "text/html"_s }, { "xhtml"_s, "application/xhtml+xml"_s }, { "xht"_s, "application/xhtml+xml"_s },
    { "xml"_s, "text/xml"_s }, { "xsl"_s, "text/xsl"_s }, { "txt"_s, "text/plain"_s }, { "text"_s, "text/plain"_s },
    { "css"_s, "text/css"_s }, { "js"_s, "text/javascript"_s }, { "mjs"_s, "text/javascript"_s }, { "json"_s, "application/json"_s },
    { "svg"_s, "image/svg+xml"_s }, { "svgz"_s, "image/svg+xml"_s }, { "png"_s, "image/png"_s }, { "jpg"_s, "image/jpeg"_s },
    { "jpeg"_s, "image/jpeg"_s }, { "jpe"_s, "image/jpeg"_s }, { "gif"_s, "image/gif"_s }, { "webp"_s, "image/webp"_s },
    { "bmp"_s, "image/bmp"_s }, { "ico"_s, "image/vnd.microsoft.icon"_s }, { "woff"_s, "font/woff"_s }, { "woff2"_s, "font/woff2"_s },
    { "ttf"_s, "font/ttf"_s }, { "otf"_s, "font/otf"_s }, { "pdf"_s, "application/pdf"_s }, { "zip"_s, "application/zip"_s },
    { "wasm"_s, "application/wasm"_s }, { "mp3"_s, "audio/mpeg"_s }, { "ogg"_s, "audio/ogg"_s }, { "mp4"_s, "video/mp4"_s },
    { "webm"_s, "video/webm"_s }, { "sis"_s, "application/vnd.symbian.install"_s }, { "sisx"_s, "x-epoc/x-sisx-app"_s },
};

String MIMETypeRegistry::mimeTypeForExtension(StringView extension)
{
    for (auto& entry : extensionMap) {
        if (equalIgnoringASCIICase(extension, entry.extension))
            return entry.mimeType;
    }
    return { };
}

Vector<String> MIMETypeRegistry::extensionsForMIMEType(const String& type)
{
    Vector<String> extensions;
    for (auto& entry : extensionMap) {
        if (equalIgnoringASCIICase(type, entry.mimeType))
            extensions.append(entry.extension);
    }
    return extensions;
}

String MIMETypeRegistry::preferredExtensionForMIMEType(const String& type)
{
    for (auto& entry : extensionMap) {
        if (equalIgnoringASCIICase(type, entry.mimeType))
            return entry.extension;
    }
    return { };
}

bool MIMETypeRegistry::isApplicationPluginMIMEType(const String&)
{
    return false;
}

// ---- PlatformKeyboardEvent
OptionSet<PlatformEvent::Modifier> PlatformKeyboardEvent::currentStateOfModifierKeys()
{
    return { };
}

// (PlatformKeyboardEventLibWPE.cpp's, without the libwpe-only input-method flag)
void PlatformKeyboardEvent::disambiguateKeyDownEvent(Type type, bool backwardsCompatibility)
{
    ASSERT(m_type == PlatformEvent::Type::KeyDown);
    m_type = type;
    if (backwardsCompatibility)
        return;

    if (type == PlatformEvent::Type::RawKeyDown) {
        m_text = String();
        m_unmodifiedText = String();
    } else
        m_keyIdentifier = String();
}

// ---- PublicSuffixStore (libnspsl). Despite its name, nspsl_getpublicsuffix returns the REGISTRABLE domain -- the
// public suffix plus one label, a pointer into the host name -- or NULL when the host is itself a public suffix (or
// malformed): "a.b.example.co.uk" -> "example.co.uk", "co.uk" -> NULL. Reading it as "the suffix" made every
// registrable host a public suffix, and CookieJarDB::canAcceptCookie refused all their cookies (round 18: a
// herokuapp login lost its session cookie).
bool PublicSuffixStore::platformIsPublicSuffix(StringView domain) const
{
    // a host is a public suffix when one more label in front of it is exactly a registrable domain
    CString host = domain.convertToASCIILowercase().utf8();
    if (!host.length() || host.data()[0] == '.')
        return false;
    std::string probe = std::string("x.") + host.data();
    const char* registrable = nspsl_getpublicsuffix(probe.c_str());
    return registrable == probe.c_str();
}

String PublicSuffixStore::platformTopPrivatelyControlledDomain(StringView host) const
{
    CString name = host.convertToASCIILowercase().utf8();
    const char* start = name.data();
    while (*start == '.')                    // the cookie format may carry a leading dot
        ++start;
    const char* registrable = nspsl_getpublicsuffix(start);
    if (!registrable)
        return { };
    return String::fromUTF8(registrable);
}

} // namespace WebCore
