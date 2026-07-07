/*
 *  localconnection.cpp
 *
 *  Copyright (C) 2010 - Niko Ritari
 *
 *  This file is part of Outgun.
 *
 *  Outgun is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Outgun is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Outgun; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "servnet.h"
#include "timer.h"

#include "localconnection.h"

using std::make_pair;

ClientServerLocalConnection::~ClientServerLocalConnection() throw () {
    disconnect();
    server.leetServer->returnClientId(cid);
}

void ClientServerLocalConnection::reportPing(int ping) throw () {
    server.ping_result(cid, ping); // pass threadLock because this is called from server
}

void ClientServerLocalConnection::sendMessage(ConstDataBlockRef data) throw () {
    Lock ml(messageMutex);
    messages.push(data);
}

void ClientServerLocalConnection::sendFrame(ConstDataBlockRef data) throw () {
    ServerNetworking::sfunc_client_data(&server, cid, data);
}

DataBlock ClientServerLocalConnection::connect(ConstDataBlockRef data) throw () {
    nAssert(!connected);
    ServerHelloResult res;
    Network::Address localAddr;
    localAddr.fromValidIP("127.0.0.1");
    ServerNetworking::sfunc_client_hello(&server, localAddr, data, &res, true);
    nAssert(res.accepted);
    ServerNetworking::sfunc_local_client_connected(&server, cid, res.customStoredData);
    connected = true;
    return DataBlock(ConstDataBlockRef(res.customData, res.customDataLength));
}

void ClientServerLocalConnection::disconnect(bool calledFromServer) throw () {
    if (!connected)
        return;
    ServerNetworking::sfunc_client_disconnected(&server, cid, calledFromServer);
    connected = false;
}

ConstDataBlockRef ClientServerLocalConnection::openMessage() throw () { // messages remain locked until call to closeMessage, if anything was returned
    messageMutex.lock();
    if (messages.empty()) {
        messageMutex.unlock();
        return ConstDataBlockRef(0, 0);
    }
    return messages.front();
}

void ClientServerLocalConnection::closeMessage() throw () {
    nAssert(!messages.empty());
    messages.pop();
    messageMutex.unlock();
}


ServerClientLocalConnection::~ServerClientLocalConnection() throw () {
    while (!frames.empty()) {
        delete frames.front().second;
        frames.pop();
    }
}

void ServerClientLocalConnection::sendMessage(ConstDataBlockRef data) throw () {
    Lock ml(mutex);
    messages.push_back(data);
}

void ServerClientLocalConnection::sendFrame(ConstDataBlockRef data) throw () {
    Lock ml(mutex);
    Frame* const f = new Frame(data);
    f->messages.swap(messages);
    frames.push(make_pair(get_time() + delay, f));
}

ControlledPtr<ServerClientLocalConnection::Frame> ServerClientLocalConnection::receiveFrame() throw () {
    Lock ml(mutex);
    if (frames.empty() || frames.front().first > get_time())
        return ControlledPtr<ServerClientLocalConnection::Frame>(0);
    else {
        Frame* const f = frames.front().second;
        frames.pop();
        return give_control(f);
    }
}

void ClientLocalConnection::connect(const char*, ConstDataBlockRef data, int, int) throw () {
    nAssert(!opened);
    helloReply = conn.cs.connect(data);
    opened = true;
}

void ClientLocalConnection::disconnect() throw () {
    if (opened)
        conn.cs.disconnect();
}

void ClientLocalConnection::send_message(ConstDataBlockRef data) throw () {
    conn.cs.sendMessage(data);
}

void ClientLocalConnection::send_frame(ConstDataBlockRef data) throw () {
    conn.cs.sendFrame(data);
}

ConstDataBlockRef ClientLocalConnection::receive_frame() throw () {
    nAssert(!openFrame || messageIterator == openFrame->messages.end());
    delete openFrame;
    openFrame = conn.sc.receiveFrame();
    if (!openFrame)
        return ConstDataBlockRef(0, 0);
    messageIterator = openFrame->messages.begin();
    return openFrame->frameData;
}

ConstDataBlockRef ClientLocalConnection::receive_message() throw () {
    nAssert(openFrame);
    if (messageIterator == openFrame->messages.end())
        return ConstDataBlockRef(0, 0);
    return *messageIterator++;
}
