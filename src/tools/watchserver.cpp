/*
 *  watchserver.cpp
 *
 *  Copyright (C) 2010, 2011 - Niko Ritari
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

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <cstdlib>
#include <ctime>

#include "../binaryaccess.h"
#include "../commont.h"
#include "../function_utility.h"
#include "../network.h"
#include "../platform.h"
#include "../protocol.h"
#include "../timer.h"

using std::cerr;
using std::cout;
using std::flush;
using std::istringstream;
using std::sort;
using std::string;
using std::vector;

class SyntaxError { }; // exception

int argAtoi(const string& s) throw (SyntaxError) {
    istringstream is(s);
    int value;
    is >> value;
    if (!is || !is.eof())
        throw SyntaxError();
    return value;
}

string toLocal(const string& str) throw () {
    return utf8_mode ? latin1_to_utf8(str) : str;
}

string fromLocal(const string& str) throw () {
    return utf8_mode ? utf8_to_latin1(str) : str;
}

struct Human {
    string name;
    int32_t ping;

    Human() throw () : ping(-1) { }
    bool operator<(const Human& o) const throw () { return name < o.name || name == o.name && ping < o.ping; }
};

struct Waiter {
    string name;
    int minPlayers;

    Waiter() throw () : minPlayers(-1) { }
    bool operator<(const Waiter& o) const throw () { return minPlayers < o.minPlayers || minPlayers == o.minPlayers && name < o.name; }
};

struct ProbeResult {
    vector<Human> players;
    vector<Waiter> waiters;
    bool knownWaiters;

    ProbeResult(const vector<Human>& p) throw () : players(p), knownWaiters(false) { }
    ProbeResult(const vector<Human>& p, const vector<Waiter>& w) throw () : players(p), waiters(w), knownWaiters(true) { }
};

class BadData { }; // exception

ProbeResult parseOldFormatReply(BinaryReader& msg, uint8_t sentByte1, uint8_t sentByte2) throw (BadData, BinaryReader::ReadError) {
    const uint32_t dw2 = msg.U32();
    const uint8_t b1 = msg.U8();
    const uint8_t b2 = msg.U8();
    if (dw2 != 200 || b1 != sentByte1 || b2 != sentByte2)
        throw BadData();
    const string text = msg.str();

    const string::size_type slashPos = text.find_first_of('/');
    if (slashPos == string::npos)
        throw BadData();
    const string::size_type lastBefore = text.find_last_not_of("0123456789", slashPos - 1);
    const string::size_type numStart = lastBefore == string::npos ? 0 : lastBefore + 1;
    istringstream numStr(text.substr(numStart, slashPos - numStart));
    int nPlayers;
    numStr >> nPlayers;
    if (!numStr)
        throw BadData();
    nAssert(numStr.eof());
    return ProbeResult(vector<Human>(nPlayers));
}

ProbeResult parseNewFormatReply(BinaryReader& msg, uint8_t sentByte1, uint8_t sentByte2) throw (BadData, BinaryReader::ReadError) {
    const uint8_t b1 = msg.U8();
    const uint8_t b2 = msg.U8();
    if (b1 != sentByte1 || b2 != sentByte2)
        throw BadData();

    const uint32_t version = msg.U32dyn8();
    if (version > EXTENDED_QUERY_PROTOCOL_VERSION)
        throw BadData();
    const uint32_t contents = msg.U32dyn8();
    const uint32_t queries = contents & EQC_SimpleQueries ? msg.U32dyn8() : 0;

    string gameString, protocolString;
    string serverVersion, serverName;
    int maxPlayers;
    bool password, ded, spect, ft, ff, pups, carryOwn, unofficialExt;
    int allAdvantagesExtensionsLevel;
    uint32_t uptime;
    vector<Human> humans;
    vector<int> botPings;
    vector<Waiter> waiters;
    string currentMap;
    int score[2], captureLimit;
    int mapTime, timeLimit;

    if (queries & EQSQ_GameProtocol) {
        gameString = msg.str();
        protocolString = msg.str();
    }
    if (queries & EQSQ_Version)
        serverVersion = toLocal(msg.str());
    if (queries & EQSQ_ServerName)
        serverName = toLocal(msg.str());
    if (queries & EQSQ_BasicSettings) {
        const uint32_t flags = msg.U32dyn16();
        maxPlayers = ((flags & 0x0F) + 1) * 2;
        password = flags & 0x10;
        ded = flags & 0x20;
        spect = flags & 0x40;
        ft = flags & 0x80;
        ff = flags & 0x100;
        pups = flags & 0x200;
        carryOwn = flags & 0x400;
        unofficialExt = flags & 0x800;
        allAdvantagesExtensionsLevel = (msg.U32dyn8() & 0xFFFF) - 1;
    }
    if (queries & EQSQ_Uptime)
        uptime = msg.U32dyn8();
    if (queries & EQSQ_Players) {
        humans.resize(msg.U32dyn8() & 0xFFF);
        botPings.resize(msg.U32dyn8() & 0xFFF, -1);
        waiters.resize(msg.U32dyn8() & 0xFFFF);
        if (queries & EQSQ_PlayerNames) {
            for (vector<Human>::iterator hi = humans.begin(); hi != humans.end(); ++hi)
                hi->name = toLocal(msg.str());
            for (vector<Waiter>::iterator wi = waiters.begin(); wi != waiters.end(); ++wi) {
                wi->name = toLocal(msg.str());
                wi->minPlayers = msg.U32dyn8() & 0x1F;
            }
        }
        if (queries & EQSQ_PlayerPings) {
            for (vector<Human>::iterator hi = humans.begin(); hi != humans.end(); ++hi)
                hi->ping = msg.U32dyn8();
            for (vector<int>::iterator bi = botPings.begin(); bi != botPings.end(); ++bi)
                *bi = msg.U32dyn8();
        }
        sort(humans.begin(), humans.end());
        sort(botPings.begin(), botPings.end());
        sort(waiters.begin(), waiters.end());
    }
    if (queries & EQSQ_CurrentMap)
        currentMap = toLocal(msg.str());
    if (queries & EQSQ_CurrentGame) {
        score[0] = msg.U32dyn8();
        score[1] = msg.U32dyn8();
        captureLimit = msg.U32dyn8();
        mapTime = msg.U32dyn8();
        timeLimit = msg.U32dyn8();
    }
    if (queries == EQSQ_ALL) {
        cout << "Watching " << serverName << ", version " << serverVersion << ", up " << formatDuration(uptime) << '\n';

        if (gameString != GAME_STRING || protocolString != GAME_PROTOCOL) {
            cout << "Incompatible server. ";
            if (gameString != GAME_STRING)
                cout << "Different game: \"" << toLocal(gameString) << "\" not \"" << toLocal(GAME_STRING) << "\".\n";
            else
                cout << "Different protocol: \"" << toLocal(protocolString) << "\" not \"" << toLocal(GAME_PROTOCOL) << "\".\n";
        }

        const bool officialExt = allAdvantagesExtensionsLevel > PROTOCOL_EXTENSIONS_VERSION;
        vector<string> properties, features;
        if (password)      properties.push_back("password protected");
        if (ded)           properties.push_back("dedicated server");
        if (spect)         properties.push_back("spectating enabled");
        if (unofficialExt) properties.push_back("important unknown unofficial extensions");
        if (officialExt)   properties.push_back("important unknown (newer) official extensions");
        if (ft)              features.push_back("free turning");
        if (ff)              features.push_back("friendly fire");
        if (pups)            features.push_back("powerups");
        if (carryOwn)        features.push_back("own flag carrying");
        if (!properties.empty()) {
            cout << "Properties: ";
            for (vector<string>::const_iterator pi = properties.begin(); pi != properties.end(); ++pi) {
                if (pi != properties.begin())
                    cout << ", ";
                cout << *pi;
            }
            cout << '\n';
        }
        if (!features.empty()) {
            cout << "Enabled game features: ";
            for (vector<string>::const_iterator fi = features.begin(); fi != features.end(); ++fi) {
                if (fi != features.begin())
                    cout << ", ";
                cout << *fi;
            }
            cout << '\n';
        }

        cout << "Now playing: " << currentMap << ", " << score[0] << '-' << score[1] << " running for " << formatDuration(mapTime);
        if (captureLimit && timeLimit)
            cout << " - playing for " << captureLimit << " captures or " << formatDuration(timeLimit);
        else if (captureLimit)
            cout << " - playing for " << captureLimit << " captures";
        else if (timeLimit)
            cout << " - playing for " << formatDuration(timeLimit);
        cout << '\n';

        cout << humans.size() << '+' << botPings.size() << '/' << maxPlayers << " players, " << waiters.size() << " waiters";
        if (!humans.empty()) {
            cout << " - humans: ";
            for (vector<Human>::const_iterator hi = humans.begin(); hi != humans.end(); ++hi) {
                if (hi != humans.begin())
                    cout << ", ";
                cout << hi->name << " (" << hi->ping << ")";
            }
        }
        if (!botPings.empty()) {
            cout << " - bot pings: ";
            sort(botPings.begin(), botPings.end());
            if (botPings.front() == botPings.back())
                cout << botPings.front();
            else
                for (vector<int>::const_iterator bi = botPings.begin(); bi != botPings.end(); ++bi) {
                    if (bi != botPings.begin())
                        cout << ", ";
                    cout << *bi;
                }
        }
        if (!waiters.empty()) {
            cout << " - waiters: ";
            for (vector<Waiter>::const_iterator wi = waiters.begin(); wi != waiters.end(); ++wi) {
                if (wi != waiters.begin())
                    cout << ", ";
                cout << wi->name << " (" << wi->minPlayers << ")";
            }
        }
        cout << "\n\n";
    }
    if (contents & EQC_RegisterWaiter) {
        const uint32_t status = msg.U32dyn8();
        if (!(status & 1))
            cout << "Error registering as waiting. Name not registered on the server or wrong password?\n";
    }
    if (contents & EQC_ExtraInfoRequest)
        cout << "Human-readable message directly from server: " << toLocal(msg.str()) << '\n';
    if (!(queries & EQSQ_Players))
        cout << "The server responded but player counts are unavailable.\n";
    return ProbeResult(humans, waiters);
}

class NoReply { }; // exception

ProbeResult probe(Network::UDPSocket& sock, const Network::Address& address, bool firstProbe, const string& waiterName, const string& password, int minPlayers) throw (NoReply, Network::Error) {
    const uint8_t sentByte1 = rand() & 0xFF;
    const uint8_t sentByte2 = rand() & 0xFF;

    BinaryBuffer<512> msg;
    msg.U32(0);
    msg.U32(200);
    msg.U8(sentByte1);
    msg.U8(sentByte2);
    msg.U32dyn8(EXTENDED_QUERY_PROTOCOL_VERSION);
    const uint32_t contents = EQC_SimpleQueries | (!waiterName.empty() ? EQC_RegisterWaiter : 0) | (firstProbe ? EQC_ExtraInfoRequest : 0);
    const uint32_t queries = firstProbe ? EQSQ_ALL : (EQSQ_Players | EQSQ_PlayerNames);
    msg.U32dyn8(contents);
    if (contents & EQC_SimpleQueries) {
        msg.U32dyn8(queries);
        if (queries & EQSQ_Version) {
            msg.U32dyn8(3);
            msg.U32dyn8(0);
            msg.U32dyn8(0);
        }
    }
    if (contents & EQC_RegisterWaiter) {
        msg.str(fromLocal(waiterName));
        msg.str(fromLocal(password));
        nAssert(minPlayers >= 1 && minPlayers < 32);
        msg.U32dyn8(minPlayers);
    }
    sock.write(address, msg);

    platSleep(5000);

    const int bufSize = 4096;
    char buffer[bufSize];
    Network::UDPSocket::ReadResult result;
    int nInvalid = 0;
    for (;;) {
        result = sock.read(buffer, bufSize);
        if (result.length == 0) {
            cerr << "No reply.";
            if (nInvalid)
                cout << " (" << nInvalid << " invalid packets)";
            cout << '\n';
            throw NoReply();
        }
        if (result.source != address)
            continue;
        try {
            BinaryDataBlockReader msg(buffer, result.length);

            const uint32_t dw1 = msg.U32();
            if (dw1 == 0)
                return parseOldFormatReply(msg, sentByte1, sentByte2);
            else if (dw1 == 0xD58E065D)
                return parseNewFormatReply(msg, sentByte1, sentByte2);
            else
                throw BadData();
        } catch (BinaryReader::ReadOutside) {
            ++nInvalid;
        } catch (BadData) {
            ++nInvalid;
        }
    }
}

void notify() throw () {
    int status = system("./watchserver-notifier");
    if (status == -1) {
        cerr << "Executing ./watchserver-notifier failed\n";
        abort();
    }
    #ifdef WIFEXITED
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        cerr << "./watchserver-notifier failed (returned " << WEXITSTATUS(status) << ")\n";
    #endif
}

int main(int argc, const char* argv[]) {
    platInit();
    check_utf8_mode();
    AtScopeExit autoPlatUninit(newFun0(platUninit));

    int minPlayers = 1, minutesBeforeNotify = 1;
    string server;
    string waiterName, password;

    try {
        for (int iArg = 1; iArg < argc; ++iArg) {
            const string arg = argv[iArg];
            if (arg == "-p" && iArg + 1 < argc) {
                minPlayers = argAtoi(argv[++iArg]);
                if (minPlayers < 1 || minPlayers >= MAX_PLAYERS)
                    throw SyntaxError();
            }
            else if (arg == "-t" && iArg + 1 < argc) {
                minutesBeforeNotify = argAtoi(argv[++iArg]);
                if (minutesBeforeNotify < 0)
                    throw SyntaxError();
            }
            else if (arg == "-w" && iArg + 2 < argc) {
                waiterName = argv[++iArg];
                password = argv[++iArg];
                if (waiterName.empty() || password.empty())
                    throw SyntaxError();
            }
            else if (server.empty() && !arg.empty() && arg[0] != '-')
                server = arg;
            else
                throw SyntaxError();
        }
        if (server.empty())
            throw SyntaxError();
    } catch (SyntaxError) {
        cerr << "syntax: " << argv[0] << " [-p players] [-t minutes] [-w name password] server-address\n"
             << "Executes ./watchserver-notifier after the server has reported at least the given number of players plus matching waiters (1-31, default 1) for the given time (default 1 minute, min 0).\n"
             << "If -w is given, the player name is added to the waiter list on the server for as long as the program is running. The name must be registered on the server (in auth.txt) using the given password. You should be ready to play soon after notified.\n";
        return 2;
    }

    try {
        Network::init();
    } catch (const Network::Error& e) {
        cerr << e.str() << '\n';
        return 1;
    }
    AtScopeExit autoShutdownNetwork(newFun0(Network::shutdown));

    Network::Address address;
    if (!address.tryResolve(server)) {
        cerr << "Can't resolve IP address for " << server << '\n';
        return 1;
    }
    if (address.getPort() == 0)
        address.setPort(DEFAULT_UDP_PORT);

    Network::UDPSocket sock(true);
    try {
        sock.open(Network::NonBlocking, 0);
    } catch (const Network::Error& e) {
        cerr << "Can't open socket. " << e.str() << '\n';
        return 1;
    }

    srand(time(0));

    const int queryPeriod = 60; // seconds
    bool firstProbe = true;
    int activeInRow = 0;
    time_t lastRoundTime = 0;
    for (;;) {
        for (;;) {
            const time_t now = time(0);
            if (now >= lastRoundTime + queryPeriod) {
                lastRoundTime = now;
                break;
            }
            platSleep(1000 * (lastRoundTime + queryPeriod - now));
        }

        try {
            const ProbeResult res = probe(sock, address, firstProbe, waiterName, password, minPlayers);
            firstProbe = false;
            cout << date_and_time() << ' ' << res.players.size() << " players";
            if (res.knownWaiters)
                cout << ", " << res.waiters.size() << " waiters";
            const bool playerNames = !res.players.empty() && !res.players.front().name.empty();
            const bool waiterNames = !res.waiters.empty() && !res.waiters.front().name.empty();
            if (playerNames || waiterNames) {
                cout << ": ";
                if (playerNames)
                    for (vector<Human>::const_iterator pi = res.players.begin(); pi != res.players.end(); ++pi) {
                        if (pi != res.players.begin())
                            cout << ", ";
                        cout << pi->name;
                    }
                if (playerNames && waiterNames)
                    cout << " + ";
                if (waiterNames)
                    for (vector<Waiter>::const_iterator wi = res.waiters.begin(); wi != res.waiters.end(); ++wi) {
                        if (wi != res.waiters.begin())
                            cout << ", ";
                        cout << wi->name;
                        if (wi->minPlayers != -1)
                            cout << ':' << wi->minPlayers;
                    }
            }
            cout << "\n" << std::flush;
            int nGoodWaiters = 0;
            if (waiterNames)
                for (vector<Waiter>::const_iterator wi = res.waiters.begin(); wi != res.waiters.end(); ++wi)
                    if (wi->minPlayers <= minPlayers && !(wi->name == waiterName && !waiterName.empty()))
                        ++nGoodWaiters;
            if ((int)res.players.size() + nGoodWaiters < minPlayers)
                activeInRow = 0;
            else if (activeInRow++ >= minutesBeforeNotify)
                notify();
        } catch (Network::Error& e) {
            cerr << "Network error. " << e.str() << '\n';
            activeInRow = 0;
        } catch (NoReply) {
            activeInRow = 0;
        }
    }
}
