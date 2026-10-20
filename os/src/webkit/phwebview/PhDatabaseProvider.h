/*

 * PhDatabaseProvider.h -- adapted from WebKitLegacy's WebDatabaseProvider.h at tag wpewebkit-2.54.1
 * (in-process IndexedDB for the single-process WebCore embedding). Original notice follows.
 *
 * Copyright (C) 2015, 2016 Apple Inc. All rights reserved.
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
#pragma once

#include "PhIDBServer.h"
#include <WebCore/DatabaseProvider.h>
#include <wtf/HashMap.h>
#include <wtf/RefPtr.h>
#include <wtf/text/WTFString.h>

namespace Phorepoc {

// One PhIDBServer per PAL::SessionID, created on first use. Unlike WebKitLegacy's singleton (which derives the directory
// from the platform's application data path) this takes the directory at creation: empty = memory-backed IndexedDB,
// e.g. "c:/data/idb" = SQLite databases under it (every session shares that directory; WebKitLegacy gives ephemeral
// sessions memory and persistent ones the directory -- same here).
class PhDatabaseProvider final : public WebCore::DatabaseProvider {
public:
    static Ref<PhDatabaseProvider> create(const String& indexedDatabaseDirectoryPath = String()) { return adoptRef(*new PhDatabaseProvider(indexedDatabaseDirectoryPath)); }
    virtual ~PhDatabaseProvider();

    WebCore::IDBClient::IDBConnectionToServer& idbConnectionToServerForSession(PAL::SessionID) override;

    void deleteAllDatabases();

private:
    explicit PhDatabaseProvider(const String& indexedDatabaseDirectoryPath);

    String m_indexedDatabaseDirectoryPath;
    HashMap<PAL::SessionID, RefPtr<PhIDBServer>> m_idbServerMap;
};

} // namespace Phorepoc
