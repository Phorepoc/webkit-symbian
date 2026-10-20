// Copyright The Phorepoc Authors
// SPDX-License-Identifier: BSD-2-Clause

/*
 * PasteboardSymbian.cpp -- WebCore:Pasteboard for the Symbian port, the generic
 * shape of Pasteboard.h (context only, no platform members). A process-wide store of typed strings (text/plain, text/html,
 * text/uri-list): copy and paste work inside the browser. The system clipboard (CClipboard, the Symbian side) plugs in here later.
 */
#include "config.h"
#include "Pasteboard.h"

#include "Color.h"
#include "ColorSerialization.h"
#include "PasteboardStrategy.h"
#include "SharedBuffer.h"
#include <wtf/HashMap.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/text/StringHash.h>

namespace WebCore {

static HashMap<String, String>& store()
{
    static NeverDestroyed<HashMap<String, String>> types;
    return types;
}

static constexpr ASCIILiteral textPlain = "text/plain"_s;
static constexpr ASCIILiteral textHTML = "text/html"_s;
static constexpr ASCIILiteral uriList = "text/uri-list"_s;

static String normalizedType(const String& type)
{
    if (type == "text"_s || type.startsWith("text/plain"_s))
        return textPlain;
    if (type == "url"_s)
        return uriList;
    return type;
}

Pasteboard::Pasteboard(std::unique_ptr<PasteboardContext>&& context)
    : m_context(WTF::move(context))
{
}

std::unique_ptr<Pasteboard> Pasteboard::createForCopyAndPaste(std::unique_ptr<PasteboardContext>&& context)
{
    return makeUnique<Pasteboard>(WTF::move(context));
}

bool Pasteboard::hasData()
{
    return !store().isEmpty();
}

Vector<String> Pasteboard::typesSafeForBindings(const String&)
{
    return copyToVector(store().keys());
}

Vector<String> Pasteboard::typesForLegacyUnsafeBindings()
{
    return copyToVector(store().keys());
}

String Pasteboard::readOrigin()
{
    return { };
}

String Pasteboard::readString(const String& type)
{
    return store().get(normalizedType(type));
}

String Pasteboard::readStringInCustomData(const String& type)
{
    return store().get(normalizedType(type));
}

void Pasteboard::writeString(const String& type, const String& data)
{
    store().set(normalizedType(type), data);
}

void Pasteboard::clear()
{
    store().clear();
}

void Pasteboard::clear(const String& type)
{
    store().remove(normalizedType(type));
}

void Pasteboard::read(PasteboardPlainText& text, PlainTextURLReadingPolicy, std::optional<size_t>)
{
    text.text = store().get(textPlain);
}

void Pasteboard::read(PasteboardWebContentReader&, WebContentReadingPolicy, std::optional<size_t>)
{
    // PasteboardWebContentReader declares its read* methods only for Cocoa/GTK/WPE (Pasteboard.h): rich paste comes with that
    // interface; plain-text paste goes through read(PasteboardPlainText&)
}

void Pasteboard::read(PasteboardFileReader&, std::optional<size_t>)
{
}

void Pasteboard::write(const Color& color)
{
    store().set(textPlain, serializationForHTML(color));
}

void Pasteboard::write(const PasteboardURL& url)
{
    store().set(uriList, url.url.string());
    store().set(textPlain, url.url.string());
}

void Pasteboard::writeTrustworthyWebURLsPboardType(const PasteboardURL&)
{
}

void Pasteboard::write(const PasteboardImage&)
{
}

void Pasteboard::write(const PasteboardBuffer&)
{
}

void Pasteboard::write(const PasteboardWebContent&)
{
    // PasteboardWebContent carries no fields on this port (Pasteboard.h declares them per platform; a shared-header edit would
    // recompile much of WebCore). EditorSymbian.cpp writes the selection as plain text + markup instead.
}

void Pasteboard::writeCustomData(const Vector<PasteboardCustomData>&)
{
}

Pasteboard::FileContentState Pasteboard::fileContentState()
{
    return FileContentState::NoFileOrImageData;
}

bool Pasteboard::canSmartReplace()
{
    return false;
}

void Pasteboard::writeMarkup(const String& markup)
{
    store().set(textHTML, markup);
}

void Pasteboard::writePlainText(const String& text, SmartReplaceOption)
{
    clear();
    store().set(textPlain, text);
}

} // namespace WebCore
