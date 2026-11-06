> WebKit is the web browser engine used by Safari, Mail, App Store, and many other apps on macOS, iOS, and Linux.
>
> -- [WebKit](https://webkit.org/)

> WPE is the official port of the WebKit Web rendering engine for embedded platforms.
>
> -- [WPE WebKit](https://wpewebkit.org/)

The Phorepoc browser draws the page with a web engine that you install separately. This package is the WPE WebKit
release, built for Phorepoc as one plug-in library the browser finds and loads at run time. Install it, open Browser
and type an address.

No engine is part of any Phorepoc image: engines come only from Catalogue, so you can pick, update or remove one.
WebCore and JavaScriptCore are LGPL 2.1; the package installs every licence text and the source offer in
`\resource\phweb\licences\`.
