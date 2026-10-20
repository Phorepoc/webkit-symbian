/*

 * PhDatabaseProvider.cpp -- adapted from WebKitLegacy's WebDatabaseProvider.cpp at tag wpewebkit-2.54.1
 * (in-process IndexedDB for the single-process WebCore embedding). Original notice follows.
 *
 * Copyright (C) 2015 Apple Inc. All rights reserved.
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
#include "PhDatabaseProvider.h"

#include <pal/SessionID.h>
#include <wtf/WallTime.h>

namespace Phorepoc {

PhDatabaseProvider::PhDatabaseProvider(const String& indexedDatabaseDirectoryPath)
    : m_indexedDatabaseDirectoryPath(indexedDatabaseDirectoryPath)
{
}

PhDatabaseProvider::~PhDatabaseProvider() = default;

WebCore::IDBClient::IDBConnectionToServer& PhDatabaseProvider::idbConnectionToServerForSession(PAL::SessionID sessionID)
{
    return m_idbServerMap.ensure(sessionID, [&] {
        return PhIDBServer::create(sessionID, sessionID.isEphemeral() ? String() : m_indexedDatabaseDirectoryPath);
    }).iterator->value->connectionToServer();
}

void PhDatabaseProvider::deleteAllDatabases()
{
    for (auto& server : m_idbServerMap.values())
        server->closeAndDeleteDatabasesModifiedSince(-WallTime::infinity());
}

} // namespace Phorepoc
