/*
 *  localconnection.h
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

#ifndef LOCALCONNECTION_H_INC
#define LOCALCONNECTION_H_INC

#include <queue>
#include <vector>

#include "leetnet/client.h"
#include "mutex.h"
#include "utility.h"

class ServerNetworking;

class ClientServerLocalConnection : private NoCopying { // passes frames immediately to ServerNetworking via callbacks
public:
    ClientServerLocalConnection(ServerNetworking& sn, int cid_) throw () : messageMutex("ClientServerLocalConnection::messageMutex"), server(sn), cid(cid_), connected(false) { }
    ~ClientServerLocalConnection() throw ();

    void reportPing(int ping) throw ();

    // interface to client
    void sendMessage(ConstDataBlockRef data) throw ();
    void sendFrame(ConstDataBlockRef data) throw ();
    DataBlock connect(ConstDataBlockRef data) throw ();

    void disconnect(bool calledFromServer = false) throw ();

    // interface to server
    ConstDataBlockRef openMessage() throw (); // messages remain locked until call to closeMessage, if anything was returned
    void closeMessage() throw ();

private:
    mutable Mutex messageMutex;

    ServerNetworking& server;
    int cid;
    bool connected;
    std::queue<DataBlock> messages;
};

class ServerClientLocalConnection : private NoCopying { // stores frames to implement ping delay; polled by client
public:
    struct Frame {
        DataBlock frameData;
        std::vector<DataBlock> messages;

        Frame(ConstDataBlockRef data) throw () : frameData(data) { }
    };

    ServerClientLocalConnection() throw () : mutex("ServerClientLocalConnection::mutex"), disconnected(false), delay(0) { }
    ~ServerClientLocalConnection() throw ();

    int getPing() const throw () { return int(delay * 1000); }
    void setPing(int ms) throw () { delay = ms * .001; }

    // interface to server
    void sendMessage(ConstDataBlockRef data) throw ();
    void sendFrame(ConstDataBlockRef data) throw ();
    void disconnect() throw () { disconnected = true; }

    // interface to client
    ControlledPtr<Frame> receiveFrame() throw ();
    bool connected() const throw () { return !disconnected; }

private:
    Mutex mutex;

    volatile bool disconnected;
    double delay;
    std::vector<DataBlock> messages;
    std::queue< std::pair<double, Frame*> > frames;
};

struct LocalConnection {
    ClientServerLocalConnection cs;
    ServerClientLocalConnection sc;

    LocalConnection(ServerNetworking& sn, int cid) throw () : cs(sn, cid) { }
};

class ClientLocalConnection : public client_c, private NoCopying {
    LocalConnection& conn;

    DataBlock helloReply;
    ServerClientLocalConnection::Frame* openFrame;
    std::vector<DataBlock>::const_iterator messageIterator;
    bool opened;

public:
    ClientLocalConnection(LocalConnection& conn_) throw () : conn(conn_), openFrame(0), opened(false) { }
    ~ClientLocalConnection() throw () { delete openFrame; }

    void setConnectionCallback(connectionCallbackT*) throw () { }
    void setServerDataCallback(serverDataCallbackT*) throw () { }

    void setCallbackCustomPointer(void*) throw () { }

    void connect(const char*, ConstDataBlockRef data, int, int) throw ();
    void disconnect() throw ();

    void send_message(ConstDataBlockRef data) throw ();
    void send_frame(ConstDataBlockRef data) throw ();

    ConstDataBlockRef readHelloReply() const throw () { return helloReply; }
    void clearHelloReply() throw () { helloReply = DataBlock(); }
    ConstDataBlockRef receive_frame() throw ();
    ConstDataBlockRef receive_message() throw ();

    int get_socket_stat(Network::Socket::StatisticType) throw () { nAssert(0); return 0; }

    double increasePacketDelay(double) throw () { nAssert(0); }
    double decreasePacketDelay(double) throw () { nAssert(0); }
};

#endif
