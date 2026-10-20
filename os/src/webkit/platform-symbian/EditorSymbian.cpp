// Copyright The Phorepoc Authors
// SPDX-License-Identifier: BSD-2-Clause

/*
 * EditorSymbian.cpp -- the platform half of WebCore:Editor (copy/paste), after
 * editing/glib/EditorGLib.cpp without the GLib SelectionData path: the selection goes to the Pasteboard as plain text + markup
 * (PasteboardSymbian.cpp); paste goes through WebCore's webContentFromPasteboard (markup first, then plain text).
 */
#include "config.h"
#include "Editor.h"

#include "DocumentFragment.h"
#include "Element.h"
#include "FrameSelection.h"
#include "LocalFrame.h"
#include "Pasteboard.h"
#include "Range.h"
#include "WebContentReader.h"
#include "markup.h"

namespace WebCore {

void Editor::pasteWithPasteboard(Pasteboard* pasteboard, OptionSet<PasteOption> options)
{
    auto range = selectedRange();
    if (!range)
        return;

    bool chosePlainText;
    auto fragment = webContentFromPasteboard(*pasteboard, *range, options.contains(PasteOption::AllowPlainText), chosePlainText);
    if (fragment && options.contains(PasteOption::AsQuotation))
        quoteFragmentForPasting(*fragment);
    if (fragment && shouldInsertFragment(*fragment, *range, EditorInsertAction::Pasted))
        pasteAsFragment(fragment.releaseNonNull(), canSmartReplaceWithPasteboard(*pasteboard), chosePlainText, options.contains(PasteOption::IgnoreMailBlockquote) ? MailBlockquoteHandling::IgnoreBlockquote : MailBlockquoteHandling::RespectBlockquote);
}

void Editor::platformCopyFont()
{
}

void Editor::platformPasteFont()
{
}

void Editor::writeImageToPasteboard(Pasteboard&, Element&, const URL&, const String&)
{
    // images are not copied yet (no image pasteboard type on this port)
}

void Editor::writeSelectionToPasteboard(Pasteboard& pasteboard)
{
    pasteboard.writePlainText(selectedTextForDataTransfer(), canSmartCopyOrDelete() ? Pasteboard::CanSmartReplace : Pasteboard::CannotSmartReplace);
    pasteboard.writeMarkup(serializePreservingVisualAppearance(document().selection().selection(), ResolveURLs::YesExcludingURLsForPrivacy, SerializeComposedTree::Yes, IgnoreUserSelectNone::Yes));
}

RefPtr<DocumentFragment> Editor::webContentFromPasteboard(Pasteboard& pasteboard, const SimpleRange& context, bool allowPlainText, bool& chosePlainText)
{
    // the plain-text path (PasteboardWebContentReader has no read* methods on this port, PasteboardSymbian.cpp)
    chosePlainText = false;
    if (!allowPlainText)
        return nullptr;
    PasteboardPlainText text;
    pasteboard.read(text);
    if (text.text.isEmpty())
        return nullptr;
    chosePlainText = true;
    return createFragmentFromText(context, text.text);
}

} // namespace WebCore
