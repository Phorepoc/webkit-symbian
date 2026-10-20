/*

 * PhIDBServer.cpp -- adapted from WebKitLegacy's InProcessIDBServer.cpp at tag wpewebkit-2.54.1
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
#include "PhIDBServer.h"

#include <WebCore/ClientOrigin.h>
#include <WebCore/IDBConnectionToClient.h>
#include <WebCore/IDBConnectionToServer.h>
#include <WebCore/IDBCursorInfo.h>
#include <WebCore/IDBGetRecordData.h>
#include <WebCore/IDBIterateCursorData.h>
#include <WebCore/IDBKeyRangeData.h>
#include <WebCore/IDBOpenDBRequest.h>
#include <WebCore/IDBRequestData.h>
#include <WebCore/IDBResultData.h>
#include <WebCore/IDBTransactionInfo.h>
#include <WebCore/IDBValue.h>
#include <WebCore/ProcessIdentifier.h>
#include <pal/SessionID.h>
#include <wtf/WorkQueue.h>
#include <wtf/threads/BinarySemaphore.h>

namespace Phorepoc {
using namespace WebCore;

Ref<PhIDBServer> PhIDBServer::create(PAL::SessionID sessionID, const String& databaseDirectoryPath)
{
    return adoptRef(*new PhIDBServer(sessionID, databaseDirectoryPath));
}

PhIDBServer::~PhIDBServer()
{
    BinarySemaphore semaphore;
    dispatchTask([this, &semaphore] {
        assertIsCurrent(m_queue.get());
        m_server = nullptr;
        m_connectionToClient = nullptr;
        semaphore.signal();
    });
    semaphore.wait();
}

PhIDBServer::PhIDBServer(PAL::SessionID sessionID, const String& databaseDirectoryPath)
    : m_queue(WorkQueue::create("com.apple.WebKit.IndexedDBServer"_s))
{
    ASSERT(isMainThread());
    m_connectionToServer = IDBClient::IDBConnectionToServer::create(*this, sessionID);
    dispatchTask([this, protectedThis = Ref { *this }, directory = databaseDirectoryPath.isolatedCopy()] () mutable {
        assertIsCurrent(m_queue.get());
        Ref connectionToClient = IDBServer::IDBConnectionToClient::create(*this);
        m_connectionToClient = connectionToClient.copyRef();

        m_server = makeUnique<IDBServer::IDBServer>(directory, [](const ClientOrigin&, uint64_t) {
            return true;
        });
        m_server->registerConnection(connectionToClient);
    });
}

std::optional<IDBConnectionIdentifier> PhIDBServer::identifier() const
{
    // An instance of PhIDBServer always has a 1:1 relationship with its instance of IDBServer.
    // Therefore the connection identifier between the two can always be "1".
    return Process::identifier();
}

IDBClient::IDBConnectionToServer& PhIDBServer::connectionToServer() const
{
    return *m_connectionToServer;
}

IDBServer::IDBConnectionToClient& PhIDBServer::connectionToClient() const
{
    return *m_connectionToClient;
}

void PhIDBServer::deleteDatabase(const WebCore::IDBOpenRequestData& requestData)
{
    dispatchTask([this, protectedThis = Ref { *this }, requestData = requestData.isolatedCopy()] {
        assertIsCurrent(m_queue.get());
        m_server->deleteDatabase(requestData);
    });
}

void PhIDBServer::didDeleteDatabase(const IDBResultData& resultData)
{
    dispatchTaskReply([this, protectedThis = Ref { *this }, resultData = resultData.isolatedCopy()] {
        m_connectionToServer->didDeleteDatabase(resultData);
    });
}

void PhIDBServer::openDatabase(const WebCore::IDBOpenRequestData& requestData)
{
    dispatchTask([this, protectedThis = Ref { *this }, requestData = requestData.isolatedCopy()] {
        assertIsCurrent(m_queue.get());
        m_server->openDatabase(requestData);
    });
}

void PhIDBServer::didOpenDatabase(const IDBResultData& resultData)
{
    dispatchTaskReply([this, protectedThis = Ref { *this }, resultData = resultData.isolatedCopy()] {
        m_connectionToServer->didOpenDatabase(resultData);
    });
}

void PhIDBServer::didAbortTransaction(const WebCore::IDBResourceIdentifier& transactionIdentifier, const IDBError& error)
{
    dispatchTaskReply([this, protectedThis = Ref { *this }, transactionIdentifier = transactionIdentifier.isolatedCopy(), error = error.isolatedCopy()] {
        m_connectionToServer->didAbortTransaction(transactionIdentifier, error);
    });
}

void PhIDBServer::didCommitTransaction(const WebCore::IDBResourceIdentifier& transactionIdentifier, const IDBError& error)
{
    dispatchTaskReply([this, protectedThis = Ref { *this }, transactionIdentifier = transactionIdentifier.isolatedCopy(), error = error.isolatedCopy()] {
        m_connectionToServer->didCommitTransaction(transactionIdentifier, error);
    });
}

void PhIDBServer::didCreateObjectStore(const IDBResultData& resultData)
{
    dispatchTaskReply([this, protectedThis = Ref { *this }, resultData = resultData.isolatedCopy()] {
        m_connectionToServer->didCreateObjectStore(resultData);
    });
}

void PhIDBServer::didDeleteObjectStore(const IDBResultData& resultData)
{
    dispatchTaskReply([this, protectedThis = Ref { *this }, resultData = resultData.isolatedCopy()] {
        m_connectionToServer->didDeleteObjectStore(resultData);
    });
}

void PhIDBServer::didRenameObjectStore(const IDBResultData& resultData)
{
    dispatchTaskReply([this, protectedThis = Ref { *this }, resultData = resultData.isolatedCopy()] {
        m_connectionToServer->didRenameObjectStore(resultData);
    });
}

void PhIDBServer::didClearObjectStore(const IDBResultData& resultData)
{
    dispatchTaskReply([this, protectedThis = Ref { *this }, resultData = resultData.isolatedCopy()] {
        m_connectionToServer->didClearObjectStore(resultData);
    });
}

void PhIDBServer::didCreateIndex(const IDBResultData& resultData)
{
    dispatchTaskReply([this, protectedThis = Ref { *this }, resultData = resultData.isolatedCopy()] {
        m_connectionToServer->didCreateIndex(resultData);
    });
}

void PhIDBServer::didDeleteIndex(const IDBResultData& resultData)
{
    dispatchTaskReply([this, protectedThis = Ref { *this }, resultData = resultData.isolatedCopy()] {
        m_connectionToServer->didDeleteIndex(resultData);
    });
}

void PhIDBServer::didRenameIndex(const IDBResultData& resultData)
{
    dispatchTaskReply([this, protectedThis = Ref { *this }, resultData = resultData.isolatedCopy()] {
        m_connectionToServer->didRenameIndex(resultData);
    });
}

void PhIDBServer::didPutOrAdd(const IDBResultData& resultData)
{
    dispatchTaskReply([this, protectedThis = Ref { *this }, resultData = resultData.isolatedCopy()] {
        m_connectionToServer->didPutOrAdd(resultData);
    });
}

void PhIDBServer::didGetRecord(const IDBResultData& resultData)
{
    dispatchTaskReply([this, protectedThis = Ref { *this }, resultData = resultData.isolatedCopy()] {
        m_connectionToServer->didGetRecord(resultData);
    });
}

void PhIDBServer::didGetAllRecords(const IDBResultData& resultData)
{
    dispatchTaskReply([this, protectedThis = Ref { *this }, resultData = resultData.isolatedCopy()] {
        m_connectionToServer->didGetAllRecords(resultData);
    });
}

void PhIDBServer::didGetCount(const IDBResultData& resultData)
{
    dispatchTaskReply([this, protectedThis = Ref { *this }, resultData = resultData.isolatedCopy()] {
        m_connectionToServer->didGetCount(resultData);
    });
}

void PhIDBServer::didDeleteRecord(const IDBResultData& resultData)
{
    dispatchTaskReply([this, protectedThis = Ref { *this }, resultData = resultData.isolatedCopy()] {
        m_connectionToServer->didDeleteRecord(resultData);
    });
}

void PhIDBServer::didOpenCursor(const IDBResultData& resultData)
{
    dispatchTaskReply([this, protectedThis = Ref { *this }, resultData = resultData.isolatedCopy()] {
        m_connectionToServer->didOpenCursor(resultData);
    });
}

void PhIDBServer::didIterateCursor(const IDBResultData& resultData)
{
    dispatchTaskReply([this, protectedThis = Ref { *this }, resultData = resultData.isolatedCopy()] {
        m_connectionToServer->didIterateCursor(resultData);
    });
}

void PhIDBServer::abortTransaction(const WebCore::IDBResourceIdentifier& resourceIdentifier)
{
    dispatchTask([this, protectedThis = Ref { *this }, resourceIdentifier = resourceIdentifier.isolatedCopy()] {
        assertIsCurrent(m_queue.get());
        m_server->abortTransaction(resourceIdentifier);
    });
}

void PhIDBServer::commitTransaction(const WebCore::IDBResourceIdentifier& resourceIdentifier, uint64_t pendingCountRequest)
{
    dispatchTask([this, protectedThis = Ref { *this }, resourceIdentifier = resourceIdentifier.isolatedCopy(), pendingCountRequest] {
        assertIsCurrent(m_queue.get());
        m_server->commitTransaction(resourceIdentifier, pendingCountRequest);
    });
}

void PhIDBServer::didFinishHandlingVersionChangeTransaction(IDBDatabaseConnectionIdentifier databaseConnectionIdentifier, const WebCore::IDBResourceIdentifier& transactionIdentifier)
{
    dispatchTask([this, protectedThis = Ref { *this }, databaseConnectionIdentifier, transactionIdentifier = transactionIdentifier.isolatedCopy()] {
        assertIsCurrent(m_queue.get());
        m_server->didFinishHandlingVersionChangeTransaction(databaseConnectionIdentifier, transactionIdentifier);
    });
}

void PhIDBServer::createObjectStore(const WebCore::IDBRequestData& resultData, const IDBObjectStoreInfo& info)
{
    dispatchTask([this, protectedThis = Ref { *this }, resultData = resultData.isolatedCopy(), info = info.isolatedCopy()] {
        assertIsCurrent(m_queue.get());
        m_server->createObjectStore(resultData, info);
    });
}

void PhIDBServer::deleteObjectStore(const WebCore::IDBRequestData& requestData, const String& objectStoreName)
{
    dispatchTask([this, protectedThis = Ref { *this }, requestData = requestData.isolatedCopy(), objectStoreName = objectStoreName.isolatedCopy()] () mutable {
        assertIsCurrent(m_queue.get());
        m_server->deleteObjectStore(requestData, objectStoreName);
    });
}

void PhIDBServer::renameObjectStore(const WebCore::IDBRequestData& requestData, WebCore::IDBObjectStoreIdentifier objectStoreIdentifier, const String& newName)
{
    dispatchTask([this, protectedThis = Ref { *this }, requestData = requestData.isolatedCopy(), objectStoreIdentifier, newName = newName.isolatedCopy()] () mutable {
        assertIsCurrent(m_queue.get());
        m_server->renameObjectStore(requestData, objectStoreIdentifier, newName);
    });
}

void PhIDBServer::clearObjectStore(const WebCore::IDBRequestData& requestData, WebCore::IDBObjectStoreIdentifier objectStoreIdentifier)
{
    dispatchTask([this, protectedThis = Ref { *this }, requestData = requestData.isolatedCopy(), objectStoreIdentifier] {
        assertIsCurrent(m_queue.get());
        m_server->clearObjectStore(requestData, objectStoreIdentifier);
    });
}

void PhIDBServer::createIndex(const WebCore::IDBRequestData& requestData, const IDBIndexInfo& info)
{
    dispatchTask([this, protectedThis = Ref { *this }, requestData = requestData.isolatedCopy(), info = info.isolatedCopy()] {
        assertIsCurrent(m_queue.get());
        m_server->createIndex(requestData, info);
    });
}

void PhIDBServer::deleteIndex(const WebCore::IDBRequestData& requestData, WebCore::IDBObjectStoreIdentifier objectStoreIdentifier, const String& indexName)
{
    dispatchTask([this, protectedThis = Ref { *this }, requestData = requestData.isolatedCopy(), objectStoreIdentifier, indexName = indexName.isolatedCopy()] {
        assertIsCurrent(m_queue.get());
        m_server->deleteIndex(requestData, objectStoreIdentifier, indexName);
    });
}

void PhIDBServer::renameIndex(const WebCore::IDBRequestData& requestData, WebCore::IDBObjectStoreIdentifier objectStoreIdentifier, WebCore::IDBIndexIdentifier indexIdentifier, const String& newName)
{
    dispatchTask([this, protectedThis = Ref { *this }, requestData = requestData.isolatedCopy(), objectStoreIdentifier, indexIdentifier, newName = newName.isolatedCopy()] {
        assertIsCurrent(m_queue.get());
        m_server->renameIndex(requestData, objectStoreIdentifier, indexIdentifier, newName);
    });
}

void PhIDBServer::putOrAdd(const WebCore::IDBRequestData& requestData, const IDBKeyData& keyData, const IDBValue& value, const IndexIDToIndexKeyMap& indexKeys, const IndexedDB::ObjectStoreOverwriteMode overwriteMode)
{
    dispatchTask([this, protectedThis = Ref { *this }, requestData = requestData.isolatedCopy(), keyData = keyData.isolatedCopy(), value = value.isolatedCopy(), indexKeys = crossThreadCopy(indexKeys), overwriteMode] {
        assertIsCurrent(m_queue.get());
        m_server->putOrAdd(requestData, keyData, value, indexKeys, overwriteMode);
    });
}

void PhIDBServer::getRecord(const WebCore::IDBRequestData& requestData, const IDBGetRecordData& getRecordData)
{
    dispatchTask([this, protectedThis = Ref { *this }, requestData = requestData.isolatedCopy(), getRecordData = getRecordData.isolatedCopy()] {
        assertIsCurrent(m_queue.get());
        m_server->getRecord(requestData, getRecordData);
    });
}

void PhIDBServer::getAllRecords(const WebCore::IDBRequestData& requestData, const IDBGetAllRecordsData& getAllRecordsData)
{
    dispatchTask([this, protectedThis = Ref { *this }, requestData = requestData.isolatedCopy(), getAllRecordsData = getAllRecordsData.isolatedCopy()] {
        assertIsCurrent(m_queue.get());
        m_server->getAllRecords(requestData, getAllRecordsData);
    });
}

void PhIDBServer::getCount(const WebCore::IDBRequestData& requestData, const IDBKeyRangeData& keyRangeData)
{
    dispatchTask([this, protectedThis = Ref { *this }, requestData = requestData.isolatedCopy(), keyRangeData = keyRangeData.isolatedCopy()] {
        assertIsCurrent(m_queue.get());
        m_server->getCount(requestData, keyRangeData);
    });
}

void PhIDBServer::deleteRecord(const WebCore::IDBRequestData& requestData, const IDBKeyRangeData& keyRangeData)
{
    dispatchTask([this, protectedThis = Ref { *this }, requestData = requestData.isolatedCopy(), keyRangeData = keyRangeData.isolatedCopy()] {
        assertIsCurrent(m_queue.get());
        m_server->deleteRecord(requestData, keyRangeData);
    });
}

void PhIDBServer::openCursor(const WebCore::IDBRequestData& requestData, const IDBCursorInfo& info)
{
    dispatchTask([this, protectedThis = Ref { *this }, requestData = requestData.isolatedCopy(), info = info.isolatedCopy()] () mutable {
        assertIsCurrent(m_queue.get());
        m_server->openCursor(requestData, info);
    });
}

void PhIDBServer::iterateCursor(const WebCore::IDBRequestData& requestData, const IDBIterateCursorData& data)
{
    dispatchTask([this, protectedThis = Ref { *this }, requestData = requestData.isolatedCopy(), data = data.isolatedCopy()] {
        assertIsCurrent(m_queue.get());
        m_server->iterateCursor(requestData, data);
    });
}

void PhIDBServer::establishTransaction(IDBDatabaseConnectionIdentifier databaseConnectionIdentifier, const IDBTransactionInfo& info)
{
    dispatchTask([this, protectedThis = Ref { *this }, databaseConnectionIdentifier, info = info.isolatedCopy()] {
        assertIsCurrent(m_queue.get());
        m_server->establishTransaction(databaseConnectionIdentifier, info);
    });
}

void PhIDBServer::fireVersionChangeEvent(IDBServer::UniqueIDBDatabaseConnection& connection, const WebCore::IDBResourceIdentifier& requestIdentifier, uint64_t requestedVersion)
{
    dispatchTaskReply([this, protectedThis = Ref { *this }, databaseConnectionIdentifier = connection.identifier(), requestIdentifier = requestIdentifier.isolatedCopy(), requestedVersion] {
        m_connectionToServer->fireVersionChangeEvent(databaseConnectionIdentifier, requestIdentifier, requestedVersion);
    });
}

void PhIDBServer::generateIndexKeyForRecord(const WebCore::IDBResourceIdentifier& requestIdentifier, const WebCore::IDBIndexInfo& indexInfo, const std::optional<WebCore::IDBKeyPath>& keyPath, const WebCore::IDBKeyData& key, const WebCore::IDBValue& value, std::optional<int64_t> recordID)
{
    dispatchTaskReply([this, protectedThis = Ref { *this }, requestIdentifier = crossThreadCopy(requestIdentifier), indexInfo = crossThreadCopy(indexInfo), keyPath = crossThreadCopy(keyPath), key = crossThreadCopy(key), value = crossThreadCopy(value), recordID] {
        m_connectionToServer->generateIndexKeyForRecord(requestIdentifier, indexInfo, keyPath, key, value, recordID);
    });
}

void PhIDBServer::didStartTransaction(const WebCore::IDBResourceIdentifier& transactionIdentifier, const IDBError& error)
{
    dispatchTaskReply([this, protectedThis = Ref { *this }, transactionIdentifier = transactionIdentifier.isolatedCopy(), error = error.isolatedCopy()] {
        m_connectionToServer->didStartTransaction(transactionIdentifier, error);
    });
}

void PhIDBServer::didCloseFromServer(IDBServer::UniqueIDBDatabaseConnection& connection, const IDBError& error)
{
    dispatchTaskReply([this, protectedThis = Ref { *this }, databaseConnectionIdentifier = connection.identifier(), error = error.isolatedCopy()] {
        m_connectionToServer->didCloseFromServer(databaseConnectionIdentifier, error);
    });
}

void PhIDBServer::notifyOpenDBRequestBlocked(const WebCore::IDBResourceIdentifier& requestIdentifier, uint64_t oldVersion, uint64_t newVersion)
{
    dispatchTaskReply([this, protectedThis = Ref { *this }, requestIdentifier = requestIdentifier.isolatedCopy(), oldVersion, newVersion] {
        m_connectionToServer->notifyOpenDBRequestBlocked(requestIdentifier, oldVersion, newVersion);
    });
}

void PhIDBServer::databaseConnectionPendingClose(IDBDatabaseConnectionIdentifier databaseConnectionIdentifier)
{
    dispatchTask([this, protectedThis = Ref { *this }, databaseConnectionIdentifier] {
        assertIsCurrent(m_queue.get());
        m_server->databaseConnectionPendingClose(databaseConnectionIdentifier);
    });
}

void PhIDBServer::databaseConnectionClosed(IDBDatabaseConnectionIdentifier databaseConnectionIdentifier)
{
    dispatchTask([this, protectedThis = Ref { *this }, databaseConnectionIdentifier] {
        assertIsCurrent(m_queue.get());
        m_server->databaseConnectionClosed(databaseConnectionIdentifier);
    });
}

void PhIDBServer::abortOpenAndUpgradeNeeded(IDBDatabaseConnectionIdentifier databaseConnectionIdentifier, const std::optional<WebCore::IDBResourceIdentifier>& transactionIdentifier)
{
    std::optional<WebCore::IDBResourceIdentifier> transactionIdentifierCopy;
    if (transactionIdentifier)
        transactionIdentifierCopy = transactionIdentifier->isolatedCopy();
    dispatchTask([this, protectedThis = Ref { *this }, databaseConnectionIdentifier, transactionIdentifier = WTF::move(transactionIdentifierCopy)] {
        assertIsCurrent(m_queue.get());
        m_server->abortOpenAndUpgradeNeeded(databaseConnectionIdentifier, transactionIdentifier);
    });
}

void PhIDBServer::didFireVersionChangeEvent(IDBDatabaseConnectionIdentifier databaseConnectionIdentifier, const WebCore::IDBResourceIdentifier& requestIdentifier, const IndexedDB::ConnectionClosedOnBehalfOfServer connectionClosed)
{
    dispatchTask([this, protectedThis = Ref { *this }, databaseConnectionIdentifier, requestIdentifier = crossThreadCopy(requestIdentifier), connectionClosed] {
        assertIsCurrent(m_queue.get());
        m_server->didFireVersionChangeEvent(databaseConnectionIdentifier, requestIdentifier, connectionClosed);
    });
}

void PhIDBServer::didGenerateIndexKeyForRecord(const IDBResourceIdentifier& transactionIdentifier, const IDBResourceIdentifier& requestIdentifier, const IDBIndexInfo& indexInfo, const IDBKeyData& key, const IndexKey& indexKey, std::optional<int64_t> recordID)
{
    dispatchTask([this, protectedThis = Ref { *this }, transactionIdentifier = crossThreadCopy(transactionIdentifier), requestIdentifier = crossThreadCopy(requestIdentifier), indexInfo = crossThreadCopy(indexInfo), key = crossThreadCopy(key), indexKey = crossThreadCopy(indexKey), recordID] {
        assertIsCurrent(m_queue.get());
        m_server->didGenerateIndexKeyForRecord(transactionIdentifier, requestIdentifier, indexInfo, key, indexKey, recordID);
    });
}

void PhIDBServer::openDBRequestCancelled(const WebCore::IDBOpenRequestData& requestData)
{
    dispatchTask([this, protectedThis = Ref { *this }, requestData = requestData.isolatedCopy()] {
        assertIsCurrent(m_queue.get());
        m_server->openDBRequestCancelled(requestData);
    });
}

void PhIDBServer::getAllDatabaseNamesAndVersions(const WebCore::IDBResourceIdentifier& requestIdentifier, const ClientOrigin& origin)
{
    dispatchTask([this, protectedThis = Ref { *this }, identifier = m_connectionToServer->identifier(), requestIdentifier, origin = origin.isolatedCopy()] {
        assertIsCurrent(m_queue.get());
        m_server->getAllDatabaseNamesAndVersions(identifier, requestIdentifier, origin);
    });
}

void PhIDBServer::didGetAllDatabaseNamesAndVersions(const WebCore::IDBResourceIdentifier& requestIdentifier, Vector<WebCore::IDBDatabaseNameAndVersion>&& databases)
{
    dispatchTaskReply([this, protectedThis = Ref { *this }, requestIdentifier, databases = crossThreadCopy(WTF::move(databases))]() mutable {
        m_connectionToServer->didGetAllDatabaseNamesAndVersions(requestIdentifier, WTF::move(databases));
    });
}

void PhIDBServer::closeAndDeleteDatabasesModifiedSince(WallTime modificationTime)
{
    dispatchTask([this, protectedThis = Ref { *this }, modificationTime] {
        assertIsCurrent(m_queue.get());
        m_server->closeAndDeleteDatabasesModifiedSince(modificationTime);
    });
}

void PhIDBServer::dispatchTask(Function<void()>&& function)
{
    ASSERT(isMainThread());
    m_queue->dispatch(WTF::move(function));
}

void PhIDBServer::dispatchTaskReply(Function<void()>&& function)
{
    ASSERT(!isMainThread());
    callOnMainThread(WTF::move(function));
}

} // namespace Phorepoc
