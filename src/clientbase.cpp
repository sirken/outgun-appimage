/*
 *  clientbase.cpp
 *
 *  Copyright (C) 2002 - Fabio Reis Cecin
 *  Copyright (C) 2003, 2004, 2005, 2006, 2008, 2009, 2010, 2011 - Niko Ritari
 *  Copyright (C) 2003, 2004, 2005, 2006, 2008, 2009, 2010 - Jani Rivinoja
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

#include <sstream>

#include "binaryaccess.h"
#include "debugconfig.h" // for LOG_MESSAGE_TRAFFIC
#include "language.h"
#include "leetnet/client.h"
#include "network.h"
#include "protocol.h"
#include "timer.h"
#include "thread.h"

#include "clientbase.h"

using std::deque;
using std::max;
using std::min;
using std::ostringstream;
using std::string;
using std::stringstream;
using std::vector;

class TM_MapChange : public ThreadMessage {
    string name;
    uint16_t crc;

public:
    TM_MapChange(const string& name_, uint16_t crc_) throw () : name(name_), crc(crc_) { }
    ~TM_MapChange() throw () { }
    void execute(ClientBase* cl) const throw () { cl->server_map_command(name, crc); }
};

#ifndef DEDICATED_SERVER_ONLY
class TM_NameAuthenticationRequest : public ThreadMessage {
public:
    void execute(ClientBase* cl) const throw () {
        cl->processNameAuthenticationRequest();
    }
};
#endif

class TM_ConnectionUpdate : public ThreadMessage {
    int code;
    DataBlock data;

public:
    TM_ConnectionUpdate(int code_, ConstDataBlockRef data_) throw () : code(code_), data(data_) { }
    ~TM_ConnectionUpdate() throw () { }
    void execute(ClientBase* cl) const throw ();
};

void TM_ConnectionUpdate::execute(ClientBase* cl) const throw () {
    switch (code) {
    /*break;*/ case 0: cl->client_connected(data);
        break; case 1: cl->disconnected_base(data);
        break; case 2: cl->connect_failed_denied(data);
        break; case 3: cl->connect_failed_unreachable();
        break; case 5: cl->connect_failed_socket();
        break; case 4: cl->connect_failed_denied(_("The server is full."));
        break; default: nAssert(0);
    }
}

WorldCoords ClientBase::readPosition(BinaryReader& read) const throw (BinaryReader::ReadError) {
    WorldCoords pos;
    pos.room.x = read.U8(0, fx.map.w - 1);
    pos.room.y = read.U8(0, fx.map.h - 1);
    pos.x = read.S16();
    pos.y = read.S16();
    return pos;
}

ClientBase::ClientBase(const ClientExternalSettings& config, Log& clientLog, MemoryLog& externalErrorLog_) throw () :
    externalErrorLog(externalErrorLog_),
    errorLog(clientLog, externalErrorLog, "ERROR: "),
    //securityLog(clientLog, "SECURITY WARNING: ", wheregamedir + "log" + directory_separator + "client_securitylog.txt", false),
    log(&clientLog, &errorLog, 0),
    frameMutex("Client::frameMutex"),
    #ifndef DEDICATED_SERVER_ONLY
    botmode(false),
    #endif
    abortThreads(false),
    #ifndef DEDICATED_SERVER_ONLY
    replaying(false),
    spectating(false),
    #endif
    mapChanged(false),
    extConfig(config)
{
    //net client
    client = 0;

    setMaxPlayers(MAX_PLAYERS);
    for (int p = 0; p < MAX_PLAYERS; p++)
        fx.player[p].used = false;

    //wich player I am
    me = -1;

    //time of last packet received
    lastpackettime = 0;

    //game showing?
    gameshow = false;

    //connected? (that is, "connection accepted")
    connected = false;

    Thread::setCallerPriority(config.priority);
}

ClientBase::~ClientBase() throw () {
    log("Exiting client: destructor");

    abortThreads = true;
    if (client) {
        delete client;
        client = 0;
    }

    for (deque<ThreadMessage*>::const_iterator mi = messageQueue.begin(); mi != messageQueue.end(); ++mi)
        delete *mi;

    log("Exiting client: destructor exiting");
}

void ClientBase::startBase(ControlledPtr<client_c> networkProvider) throw () {
    clFrameSent = clFrameWorld = 0;
    fx.frame = -1;
    #ifndef DEDICATED_SERVER_ONLY
    fd.frame = -1;
    #endif
    frameReceiveTime = 0;

    frameOffsetDeltaTotal = 0;
    frameOffsetDeltaNum = 0;
    averageLag = 0;

    netsendAdjustment = 0;

    // default map
    map_ready = false;  // NO map change commands from server yet
    clientReadiesWaiting = 0;

    //not showing gameover plaque
    gameover_plaque = NEXTMAP_NONE;

    connected = false;

    client = networkProvider;
    client->setCallbackCustomPointer(this);
    client->setConnectionCallback(cfunc_connection_update);
    client->setServerDataCallback(cfunc_server_data);
}

FormattedText ClientBase::formatName(int pid) const throw () {
    return FormattedText::parse((fx.player[pid].team() == 0 ? "$R" : "$B") + FormattedText::escape(fx.player[pid].name) + "$>");
}

//send "client ready" message to server (when map load and/or download completes)
void ClientBase::send_client_ready() throw () {
    BinaryBuffer<256> msg;
    msg.U8(data_client_ready);
    client->send_message(msg);
}

// Server tells client of current map / map change.
// Client checks from the "cmaps" and "maps" directory.
// If the map file is not there, or the CRC's don't match, download the map from the server to "cmaps".
void ClientBase::server_map_command(const string& mapname, uint16_t server_crc) throw () {
    log("Received map change: '%s'", mapname.c_str());

    servermap = mapname;

    // Try to load the map from "cmaps", "maps" or even from "maps/generated" if necessary.
    if (load_map(CLIENT_MAPS_DIR, mapname, server_crc) || load_map(SERVER_MAPS_DIR, mapname, server_crc) ||
          load_map(string() + SERVER_MAPS_DIR + directory_separator + "generated", mapname, server_crc)) {
        log("Map '%s' loaded successfully.", mapname.c_str());
        remove_useless_flags();
        mapChanged = true;
        map_ready = true;
        ++clientReadiesWaiting;
        return;
    }

    // start download
    const string msg = _("Downloading map \"$1\"...", mapname);
    print_message(msg_info, msg);
    log("%s", msg.c_str());

    download_server_file("map", mapname);
}

bool ClientBase::load_map(const string& directory, const string& mapname, uint16_t server_crc) throw () {
    LogSet noLogSet(0, 0, 0);   // if there's an error with the map, don't log it

    const bool ok =
        #ifndef DEDICATED_SERVER_ONLY
        fd.load_map(noLogSet, directory, mapname) &&
        #endif
        fx.load_map(noLogSet, directory, mapname);

    if (!ok)
        log("Map '%s' not found in '%s'.", mapname.c_str(), directory.c_str());
    else if (fx.map.crc != server_crc)
        log("Map '%s' found in '%s' but its CRC %i differs from server map CRC %i.",
            mapname.c_str(), directory.c_str(), fx.map.crc, server_crc);
    else
        return true;
    return false;
}

void ClientBase::sendMinimapBandwidthAny(int players) throw () {
    if (protocolExtensions >= 0) {
        BinaryBuffer<256> msg;
        msg.U8(data_set_minimap_player_bandwidth);
        msg.U8(players);
        client->send_message(msg);
    }
}

void ClientBase::disconnect_command() throw () { // do not call from a network thread
    //disconnect the client here if was connected, else does nothing
    client->disconnect();
}

void ClientBase::client_connected(ConstDataBlockRef data) throw () {   // call with frameMutex locked
    log("Connection successful");

    connected = true;
    gameshow = true;

    try {
        BinaryDataBlockReader read(data);

        setMaxPlayers(read.U8(0, MAX_PLAYERS));

        netSetHostname(read.str());

        if (read.hasMore()) {
            const int protoExt = read.U8();
            log("Protocol extensions enabled. Server: %d (client: %d; using the smaller)", protoExt, PROTOCOL_EXTENSIONS_VERSION);
            protocolExtensions = min(protoExt, PROTOCOL_EXTENSIONS_VERSION);
        }
        else
            protocolExtensions = -1;

        while (read.hasMore()) {
            const uint32_t extensionId = read.U32();
            BinaryDataBlockReader extData(read.block(read.U8()));
            switch (extensionId) {
                /* To negotiate unofficial extension "example" at connection time, insert something like this: (search for "unofficial extension" for other relevant parts)
                 * break; case EXAMPLE_IDENTIFIER:
                 *    exampleLevel = min(extData.U8(), EXAMPLE_VERSION); // or whatever else you sent in servnet.cpp; also remember to flag the extension disabled before this "while (read.hasMore())"
                 */
            }
        }
    } catch (BinaryReader::ReadError) {
        log.error(_("Server sent invalid data."));
        disconnect_command();
        return;
    }

    if (botmode) {
        // Tell server that I am a bot.
        BinaryBuffer<256> msg;
        msg.U8(data_bot);
        client->send_message(msg);

        sendMinimapBandwidthAny(32);
    }

    //avoid "dropped" plaque
    lastpackettime = get_time() + 4.0;

    averageLag = 0;

    clFrameSent = clFrameWorld = 0;
    frameReceiveTime = 0;

    remove_flags = 0;

    issue_change_name_command();

    map_ready = false;
    clientReadiesWaiting = 0;
    servermap.clear();

    gameover_plaque = NEXTMAP_NONE;

    gunDir.from8way(0);
    gunDirRefreshedTime = get_time();
    next_respawn_time = 0;
    flag_return_delay = -1;

    me = -1;    // will be corrected from the first frame

    for (int i = 0; i < 2; i++) {
        fx.teams[i].clear_stats();
        fx.teams[i].remove_flags();
    }
    for (int i = 0; i < MAX_PLAYERS; i++)
        fx.player[i].clear(false, i, "", i / TSIZE);
    fx.reset();

    fx.frame = -1;
    fx.skipped = true;
    fx.physics = PhysicalSettings(); // to be filled later by a message

    carry_own_team_flag = false;
    capture_away_from_base = false;
    lock_team_flags_in_effect = false;
    lock_wild_flags_in_effect = false;
    capture_on_team_flags_in_effect = true;
    capture_on_wild_flags_in_effect = false;
    see_minimap_player_properties = 0;
}

void ClientBase::disconnected_base(ConstDataBlockRef data) throw () {
    if (!connected)
        return;

    connected = false;
    gameshow = false;

    client_disconnected(data);
}

void ClientBase::prepareForConnect() throw () {   // call with frameMutex locked
    const bool alreadyConnected = connected;

    client->disconnect();

    if (alreadyConnected)   // very basic and ugly hack to let the disconnection take place at least semi-reliably; this is needed because Leetnet sucks
        platSleep(500);

    handlePendingThreadMessages();  // this is needed so that the potential disconnection message doesn't screw up the new connection
}

void ClientBase::connect(const string& serverAddress, const string& serverPassword, const string& playerPassword) throw () {   // call with frameMutex locked
    //set connect-data (goes in every connect packet): outgun game name and protocol strings
    BinaryBuffer<256> msg;
    msg.str(GAME_STRING);
    msg.str(GAME_PROTOCOL);
    msg.str(playername);
    msg.str(serverPassword);    // empty or not, it's needed
    msg.str(playerPassword);    // empty or not, it's needed
    msg.U8(PROTOCOL_EXTENSIONS_VERSION);
    /* To negotiate unofficial extension "example" at connection time, insert something like this: (search for "unofficial extension" for other relevant parts)
     * msg.U32(EXAMPLE_IDENTIFIER);
     * msg.U8(1); // the number of bytes of what is added to msg by this extension after this
     * msg.U8(EXAMPLE_VERSION); // or whatever else you want to send (here's a possible idea: send the value of data_example_first this client wants to use with the extension, to make it changeable if necessary for mixing extensions)
     */
    client->connect(serverAddress.c_str(), msg, extConfig.minLocalPort, extConfig.maxLocalPort);
}

void ClientBase::issue_change_name_command() throw () {
    if (!connected)
        return;
    //regular change name
    BinaryBuffer<256> msg;
    msg.U8(data_name_update);
    nAssert(check_name(playername));
    msg.str(playername);
    msg.str(getPlayerPassword());    // empty or not, it's needed
    client->send_message(msg);
}

void ClientBase::readMinimapPlayerPosition(BinaryReader& reader, int pid) throw (BinaryReader::ReadError) {
    ClientPlayer& pl = fx.player[pid];
    const uint8_t whox = reader.U8(), whoy = reader.U8();
    if (pid == me || pl.onscreen)
        return;
    const double oldx = pl.pos.xTotal(), oldy = pl.pos.yTotal();
    const int xmul = 255 / fx.map.w;
    const int ymul = 255 / fx.map.h;
    const double xStep = double(plw) / xmul;
    const double yStep = double(plh) / ymul;
    pl.setPosition(WorldCoords(whox / xmul,
                               whoy / ymul,
                               (whox % xmul + .5) * xStep,
                               (whoy % ymul + .5) * yStep),
                   fx.frame, true);
    double newx = pl.pos.xTotal(), newy = pl.pos.yTotal();
    if (fabs(newx - oldx) > fx.map.w * plw / 2)
        newx += fx.map.w * plw * (newx < oldx ? +1 : -1);
    if (fabs(newy - oldy) > fx.map.h * plh / 2)
        newy += fx.map.h * plh * (newy < oldy ? +1 : -1);
    const double dx = newx - oldx, dy = newy - oldy;
    if (fabs(dx) >= xStep || fabs(dy) >= yStep)
        pl.gundir.fromRad(atan2(dy, dx));
    pl.vel = Vec(0, 0); // keep bots from trying to predict minimap players' movement from old velocities
}

void ClientBase::process_live_frame_data(ConstDataBlockRef data) throw (ServerDataError) {
 try {
    BinaryDataBlockReader read(data);

    const uint32_t svframe = read.U32();    //server's frame

    #ifndef DEDICATED_SERVER_ONLY
    if (WATCH_CONNECTION && svframe != fx.frame + 1) {
        ostringstream dstr;
        if (svframe == fx.frame)
            dstr << "S>C packet duplicated: " << svframe;
        else if (svframe < fx.frame)
            dstr << "S>C packet order: prev " << fx.frame << " this " << svframe;
        else
            dstr << "S>C packet lost : prev " << fx.frame << " this " << svframe;
        addThreadMessage(new TM_Text(msg_warning, dstr.str().c_str()));
    }
    #endif
    if (svframe < fx.frame)
        return;

    ClientPhysicsCallbacks cb(*this);
    fx.rocketFrameAdvance(static_cast<int>(svframe - fx.frame), cb);
    fx.frame = svframe;

    frameReceiveTime = get_time();

    clFrameWorld = read.U8();
    /* svframe - .5 is roughly when clFrameWorld was received in server (using '- 1. + offsetDelta' instead of -.5 here would be possible but not necessarily really better)
     * svFrameHistory[clFrameWorld] is the apparent server frame when clFrameSent was sent
     */
    const double currentLag = bound(svframe - .5 - svFrameHistory[clFrameWorld], 0., 50.);    // bound because svFrameHistory has invalid frame# at connect to server
    averageLag = averageLag * .99 + currentLag * .01;

    const uint8_t frameOffset = read.U8();
    const double offsetDelta = (frameOffset / 256.) - .5;    // the deviation from aim, in frames
    frameOffsetDeltaTotal += offsetDelta;
    if (++frameOffsetDeltaNum == 10) {  // try to fix deviations every 10 frames
        frameOffsetDeltaTotal /= 10.;
        netsendAdjustment = -(frameOffsetDeltaTotal * .1); // convert to seconds
        frameOffsetDeltaTotal = 0;
        frameOffsetDeltaNum = 0;
    }

    const uint8_t xtra = read.U8();

    const int extraHealth = (xtra & 1) ? 256 : 0;
    const int extraEnergy = (xtra & 2) ? 256 : 0;
    const bool empty_frame_cause_not_ready_yet = (xtra & 4) != 0;

    if (me == -1)   // only read this when just connected to the server; otherwise, changes in "me" should be taken in only with the change teams message
        me = xtra >> 3;

    if (empty_frame_cause_not_ready_yet) {
        fx.skipped = true;
        return;
    }

    if (!map_ready) {
        log("Server sent frame data when loading map");
        throw ServerDataError();
    }
    fx.skipped = false;

    fx.player[me].pos.room.x = read.U8(0, fx.map.w - 1);
    fx.player[me].pos.room.y = read.U8(0, fx.map.h - 1);

    if (fx.player[me].room() != fx.player[me].oldRoom) {
        for (int j = 0; j < MAX_POWERUPS; j++)
            fx.item[j].kind = Powerup::pup_unused;  // the server will send messages for all seen, others should be forgotten

        fx.player[me].oldRoom = fx.player[me].room();
    }

    const uint32_t players_onscreen = read.U32();

    //decode players_onscreen and update player data
    for (int i = 0; i < maxplayers; i++) {
        //decode players_onscreen: sets if "player" record is there to be read
        if (players_onscreen & (1 << i))
            fx.player[i].onscreen = true;
        else {
            fx.player[i].onscreen = false;
            continue;
        }

        ClientPlayer& h = fx.player[i];

        double lx, ly;
        {
            const uint8_t xLowBits = read.U8(), yLowBits = read.U8(), highBits = read.U8();
            lx = ((highBits & 0x0F) << 8 | xLowBits) * (plw / double(0xFFF));
            ly = ((highBits & 0xF0) << 4 | yLowBits) * (plh / double(0xFFF));
        }

        if (protocolExtensions < 0) {
            typedef SignedByteFloat<3, -2> SpeedType;   // exponent from -2 to +6, with 4 significant bits -> epsilon = .25, max representable 32 * 31 = enough :)
            h.vel.x = SpeedType::toDouble(read.U8());
            h.vel.y = SpeedType::toDouble(read.U8());
        }

        const uint8_t extra = read.U8();
        h.dead = (extra & 1) != 0;
        h.item_deathbringer = (extra & 2) != 0;
        h.deathbringer_affected = (extra & 4) != 0;
        h.item_shield = (extra & 8) != 0;
        h.item_turbo = (extra & 16) != 0;
        h.item_power = (extra & 32) != 0;
        const bool preciseGundir = (extra & 64) != 0;

        if (protocolExtensions >= 0) {
            if (h.dead)
                h.vel = Vec(0, 0);
            else {
                typedef SignedByteFloat<3, -2> SpeedType;   // exponent from -2 to +6, with 4 significant bits -> epsilon = .25, max representable 32 * 31 = enough :)
                h.vel.x = SpeedType::toDouble(read.U8());
                h.vel.y = SpeedType::toDouble(read.U8());
            }
        }

        const uint8_t ccb = read.U8();
        h.controls.fromNetwork(ccb, true);

        if (preciseGundir)
            h.gundir.fromNetworkLongForm(((ccb >> 5) << 8) | read.U8());
        else
            h.gundir.fromNetworkShortForm(ccb >> 5);

        if (protocolExtensions < 0 || !h.dead)
            h.visibility = read.U8();
        else
            h.visibility = 255;

        if (i == me) {
            if (!h.item_turbo)
                fx.player[me].item_turbo_time = 0;
            if (!h.item_power)
                fx.player[me].item_power_time = 0;
            if (h.visibility == 255)
                fx.player[me].item_shadow_time = 0;
        }

        const bool clearlyVisible = i / TSIZE == me / TSIZE || h.visibility >= 10 || h.stats().has_flag();
        h.setPosition(WorldCoords(fx.player[me].room(), lx, ly), svframe, false, clearlyVisible);
    }

    // see servnet.cpp for a short documentation of the minimap player position protocol
    if (protocolExtensions < 0) {
        uint8_t pid = read.U8();
        if (pid < MAX_PLAYERS)
            readMinimapPlayerPosition(read, pid);
        pid = read.U8();
        if (pid < MAX_PLAYERS)
            readMinimapPlayerPosition(read, pid);
    }
    else {
        const uint8_t mmByte = read.U8();
        const int pos = 4 * (mmByte >> 5);
        const int extraBytes = ((mmByte & 0x18) >> 3) + 1;
        uint32_t rotP = mmByte & 0x07;
        for (int i = 0, rotPpos = 3; i < extraBytes; ++i, rotPpos += 8)
            rotP |= read.U8() << rotPpos;
        for (int pid = pos; rotP; pid = (pid + 1) % 32, rotP >>= 1)
            if (rotP & 1)
                readMinimapPlayerPosition(read, pid);
    }

    fx.player[me].health = read.U8() + extraHealth;
    fx.player[me].energy = read.U8() + extraEnergy;

    if (read.hasMore())
        fx.player[svframe % maxplayers].ping = max<int16_t>(read.S16(), 0); // Server versions up to 1.0.3 using a multicore processor can send negative pings.
 } catch (BinaryReader::ReadError) {
     throw ServerDataError();
 }
}

// process a message (update fx, and add the non frame-related messages to messageQueue)
void ClientBase::process_message(ConstDataBlockRef data) throw (ServerDataError) {
 try {
    const double time = fx.frame / 10;
    BinaryDataBlockReader read(data);

    const uint8_t code = read.U8();

    if (LOG_MESSAGE_TRAFFIC)
        log("Message from server, code = %i, length = %i bytes", code, data.size());

    switch (static_cast<Network_data_code>(code)) {
    /*break;*/ case data_name_update: {
        const uint8_t pid = read.U8();
        const string name = read.str();
        if (check_name(name)) {
            if (fx.player[pid].name.empty()) {
                fx.player[pid].name = name;
                addThreadMessage(new TM_Text(msg_info, tf("$1 entered the game.", formatName(pid))));
                addThreadMessage(new TM_Sound(SAMPLE_ENTERGAME));
            }
            else if (fx.player[pid].name != " " && fx.player[pid].name != name)    // " " is the case with players already in game when connecting
                addThreadMessage(new TM_Text(msg_info, tf("$1 changed name to $2.", formatName(pid), name)));
            fx.player[pid].name = name;
        }
        else
            log.error("Invalid name for player " + itoa(pid) + '.');
    }

    break; case data_text_message: {
        const Message_type type = static_cast<Message_type>(read.U8(0, Message_types - 1));
        const string text = read.str();
        if (find_nonprintable_char(text)) {
            log.error("Server sent non-printable characters in a message.");
            addThreadMessage(new TM_DoDisconnect());
            break;
        }
        int8_t sender_team = -1;
        if (protocolExtensions >= 0 || replaying) {
            if (type == msg_team || type == msg_normal)
                sender_team = read.S8();
        }
        else if (type == msg_team)
            sender_team = fx.player[me].team();
        net_text_message(type, sender_team, text);
    }

    break; case data_first_packet: {
        me = read.U8();

        fx.player[me].set_color(read.U8(0, PlayerBase::invalid_color - 1));
        netSetCurrentMap(read.U8());

        const bool e = protocolExtensions >= 0;

        uint8_t score = read.U32dyn8orU8(e);
        fx.teams[0].set_score(score);
        if (fx.teams[0].captures().size() == 0) // only if just joined the server
            fx.teams[0].set_base_score(score);
        score = read.U32dyn8orU8(e);
        fx.teams[1].set_score(score);
        if (fx.teams[1].captures().size() == 0) // only if just joined the server
            fx.teams[1].set_base_score(score);

        // room is probably changed
        fx.player[me].oldRoom = RoomCoords(-1, -1);
    }

    break; case data_frags_update: {
        #ifndef DEDICATED_SERVER_ONLY
        const uint8_t pid = read.U8();
        fx.player[pid].stats().set_frags(read.U32());
        stable_sort(players_sb.begin(), players_sb.end(), compare_players);
        #endif
    }

    break; case data_flag_update: {
        const uint8_t team = read.U8();
        const int8_t flags = read.S8();
        bool new_flag = false;
        for (int i = 0; i < flags; i++) {
            if (team == 2) {
                if (i >= static_cast<int>(fx.wild_flags.size())) {
                    fx.wild_flags.push_back(Flag(WorldCoords()));
                    new_flag = true;
                }
            }
            else if (i >= static_cast<int>(fx.teams[team].flags().size())) {
                fx.teams[team].add_flag(WorldCoords());
                new_flag = true;
            }
            const uint8_t carried = read.U8();
            if (carried == 0) {
                //not carried: update position
                const uint8_t px = read.U8(), py = read.U8();
                const int16_t x = read.S16(), y = read.S16();
                const WorldCoords pos(px, py, x, y);
                bool was_carried;
                if (team == 2) {
                    was_carried = fx.wild_flags[i].carried();
                    fx.wild_flags[i].move(pos);
                    fx.wild_flags[i].drop();
                }
                else {
                    was_carried = fx.teams[team].flag(i).carried();
                    fx.teams[team].drop_flag(i, pos);
                }
                if (!new_flag && was_carried)
                    if (team == 2)
                        fx.wild_flags[i].set_return_time(time);
                    else {
                        fx.teams[team].set_flag_drop_time(i, time);
                        fx.teams[team].set_flag_return_time(i, time);
                    }
            }
            else {
                //carried: get carrier
                const uint8_t carrier = read.U8();
                if (!new_flag) {
                    if (!fx.player[carrier].onscreen && !replaying) {
                        const WorldCoords& flagPos = (team == 2 ? fx.wild_flags[i] : fx.teams[team].flag(i)).position();
                        if (!flagPos.unknown())
                            fx.player[carrier].setPosition(flagPos, fx.frame);
                    }
                    addThreadMessage(new TM_Sound(SAMPLE_CTF_GOT));
                }
                if (team == 2)
                    fx.wild_flags[i].take(carrier);
                else
                    fx.teams[team].steal_flag(i, carrier);
            }
        }
    }

    break; case data_rocket_fire: {
        if (!map_ready)
            break;

        const uint8_t rpow = read.U8(1, 9), rdir = read.U8();

        GunDirection dir;
        if (rdir & 0x80) {
            if (protocolExtensions < 0)
                throw ServerDataError();
            dir.fromNetworkLongForm(((rdir & 0x7F) << 8) | read.U8());
        }
        else
            dir.fromNetworkShortForm(rdir);

        uint8_t rids[16];
        for (int i = 0; i < rpow; i++)
            rids[i] = read.U8();

        const uint32_t frameno = read.U32();
        const uint8_t rteampower = read.U8(); // power (bit 0) and shooter pid/team
        const bool power = rteampower & 1;

        WorldCoords pos = readPosition(read);

        int team;
        if (protocolExtensions >= 0) {
            if (rteampower & 2) { // with player id
                const int pid = (rteampower & 0x7C) >> 2;
                if (pid >= maxplayers || !fx.player[pid].used) {
                    log("Bad pid in data_rocket_fire: %d.", pid);
                    throw ServerDataError();
                }
                team = pid / TSIZE;
                if (fx.player[pid].posUpdated < frameno) {
                    fx.player[pid].setPosition(pos, frameno);
                    fx.player[pid].gundir = dir;
                }
            }
            else
                team = (rteampower & 4) >> 2;
        }
        else
            team = (rteampower & 2) >> 1;

        ClientPhysicsCallbacks cb(*this);
        fx.shootRockets(cb, 0, rpow, dir, rids, static_cast<int>(fx.frame - frameno), team, power, pos);

        netRocketFired(pos, power);
    }

    break; case data_old_rocket_visible: {
        if (!map_ready)
            break;

        const uint8_t rockid = read.U8(), rdir = read.U8();

        GunDirection dir;
        if (rdir & 0x80) {
            if (protocolExtensions < 0)
                throw ServerDataError();
            dir.fromNetworkLongForm(((rdir & 0x7F) << 8) | read.U8());
        }
        else
            dir.fromNetworkShortForm(rdir);

        const uint32_t frameno = read.U32();
        const uint8_t rteampower = read.U8();

        const bool power = rteampower & 1;
        const int team = (rteampower & 2) >> 1;

        WorldCoords pos = readPosition(read);

        ClientPhysicsCallbacks cb(*this);
        fx.shootRockets(cb, 0, 1, dir, &rockid, static_cast<int>(fx.frame - frameno), team, power, pos);
        // no sound
    }

    break; case data_rocket_delete: {
        if (!map_ready)
            break;

        const uint8_t rockid = read.U8();
        fx.rock[rockid].owner = -1;
        const uint8_t target = read.U8();
        if (target != 255) {    // hit player
            if (target != 252) {  // not shield hit -> blink player
                if (target >= maxplayers)
                    throw ServerDataError();
                fx.player[target].hitfx = time + .3;
            }
            //hit position
            const int16_t rokx = read.S16(), roky = read.S16();
            netRocketHitPlayer(rockid, rokx, roky, time);
        }
    }

    break; case data_power_collision: {
        const uint8_t target = read.U8(0, maxplayers - 1);
        netPowerCollision(target, time);
    }

    break; case data_score_update: {
        const uint8_t team = read.U8(0, 1);
        fx.teams[team].set_score(read.U32dyn8orU8(protocolExtensions >= 0));
    }

    break; case data_sound:
        net_data_sound(read);

    break; case data_pup_visible: {
        const uint8_t iid = read.U8();
        fx.item[iid].kind = static_cast<Powerup::Pup_type>(read.U8(0, Powerup::pup_last_real));
        fx.item[iid].pos.room.x = read.U8(0, fx.map.w - 1);
        fx.item[iid].pos.room.y = read.U8(0, fx.map.h - 1);
        fx.item[iid].pos.x = read.U16();
        fx.item[iid].pos.y = read.U16();
    }

    break; case data_pup_picked:
        fx.item[read.U8()].kind = Powerup::pup_unused;

    break; case data_pup_timer: {
        const uint8_t type = read.U8();
        const double pup_time = time + read.U16();
        if (type == Powerup::pup_turbo)
            fx.player[me].item_turbo_time = pup_time;
        else if (type == Powerup::pup_shadow)
            fx.player[me].item_shadow_time = pup_time;
        else if (type == Powerup::pup_power)
            fx.player[me].item_power_time = pup_time;
    }

    break; case data_weapon_change:
        fx.player[me].weapon = read.U8(1, 9);

    break; case data_map_change: {
        map_ready = false;

        fx.teams[0].remove_flags();
        fx.teams[1].remove_flags();
        fx.wild_flags.clear();
        for (int i = 0; i < MAX_ROCKETS; ++i)
            fx.rock[i].owner = -1;
        const uint16_t crc = read.U16();
        const string mapname = read.str();
        const string maptitle = read.str();
        const uint8_t map_number = read.U8();
        const uint8_t total_maps = read.U8();
        if (me != -1)
            fx.player[me].oldRoom = RoomCoords(-1, -1);
        if (read.hasMore())
            remove_flags = read.S8();
        else
            remove_flags = 0;
        if (!replaying)
            addThreadMessage(new TM_MapChange(mapname, crc));
        #ifndef DEDICATED_SERVER_ONLY
        else { // The map is saved with the message
            stringstream mapStream;
            const ConstDataBlockRef mapData = read.block(read.U32());
            mapStream.write(static_cast<const char*>(mapData.data()), mapData.size());
            if (!fx.map.parse_file(log, mapStream)) {
                log("Problem with map data.");
                throw ServerDataError();
            }
            fd.map = fx.map;
            log("Map loaded from the replay: %s", fx.map.title.c_str());
            remove_useless_flags();
            mapChanged = true;
            map_ready = true;
        }
        #endif
        netMapChange(maptitle, map_number, total_maps);
    }

    break; case data_world_reset:
        #ifndef DEDICATED_SERVER_ONLY
        if (replaying && !spectating)
            break;
        #endif
        for (vector<ClientPlayer>::iterator pi = fx.player.begin(); pi != fx.player.end(); ++pi)
            pi->stats().finish_stats(time);
        fx.reset();
        #ifndef DEDICATED_SERVER_ONLY
        fd.reset();
        #endif

    break; case data_gameover_show: {
        #ifndef DEDICATED_SERVER_ONLY
        extra_time_running = false;
        if (replaying)
            map_ready = false;
        #endif
        const uint8_t plaque = read.U8();
        if (plaque == NEXTMAP_CAPTURE_LIMIT || plaque == NEXTMAP_VOTE_EXIT) {
            gameover_plaque = plaque;
            const bool e = protocolExtensions >= 0;
            const uint32_t redScore = read.U32dyn8orU8(e);
            const uint32_t blueScore = read.U32dyn8orU8(e);
            const uint8_t caplimit = read.U8();
            const uint8_t timelimit = read.U8();

            for (vector<ClientPlayer>::iterator pi = fx.player.begin(); pi != fx.player.end(); ++pi)
                pi->stats().finish_stats(time);

            netGameoverPeriodStart(redScore, blueScore, caplimit, timelimit);
        }
        else {
            gameover_plaque = NEXTMAP_NONE;
            netGameoverPeriodEnd();
        }
    }

    break; case data_start_game:
        fx.teams[0].clear_stats();
        fx.teams[1].clear_stats();
        for (vector<ClientPlayer>::iterator pi = fx.player.begin(); pi != fx.player.end(); ++pi)
            pi->stats().clear(true);
        gameover_plaque = NEXTMAP_NONE;
        netGameStarted();

    break; case data_deathbringer: {
        const uint8_t team = read.U8();
        const uint32_t frameno = read.U32();
        const uint8_t roomx = read.U8(0, fx.map.w - 1), roomy = read.U8(0, fx.map.h - 1);
        const uint16_t lx = read.U16(), ly = read.U16();
        fx.addDeathbringerExplosion(DeathbringerExplosion(frameno, WorldCoords(roomx, roomy, lx, ly), team));
        #ifndef DEDICATED_SERVER_ONLY
        addThreadMessage(new TM_Sound(SAMPLE_USEDEATHBRINGER));
        #endif
    }

    break; case data_file_download: {
        const uint16_t chunkSize = read.U16();
        const uint8_t last = read.U8();
        process_udp_download_chunk(read.block(chunkSize), (last != 0));
    }

    break; case data_registration_response:
        net_data_registration_response(read);

    break; case data_crap_update:
        net_data_crap_update(read);

    break; case data_map_time: {
        #ifndef DEDICATED_SERVER_ONLY
        const uint32_t current_time = read.U32(), time_left = read.U32();
        map_start_time = static_cast<int>(time) - current_time;
        if (time_left > 0) {
            map_end_time = static_cast<int>(time) + time_left;
            map_time_limit = true;
        }
        else
            map_time_limit = false;
        if (LOG_MESSAGE_TRAFFIC)
            log("Map time received. Time left %d seconds.", time_left);
        #endif
    }

    break; case data_reset_map_list:
        net_data_reset_map_list(read);

    break; case data_current_map:
        netSetCurrentMap(read.U8());

    break; case data_quick_map_list:
        net_data_quick_map_list(read);

    break; case data_map_list:
        net_data_map_list(read);

    break; case data_map_vote:
        net_data_map_vote(read);

    break; case data_map_votes_update:
        net_data_map_votes_update(read);

    break; case data_stats_ready: {
        for (vector<ClientPlayer>::iterator pi = fx.player.begin(); pi != fx.player.end(); ++pi)
            pi->stats().finish_stats(time);
        netStatsReady();
    }

    break; case data_capture: {
        uint8_t pid = read.U8();
        const int8_t ass_pid = read.hasMore() ? read.S8() : -1;
        #ifndef DEDICATED_SERVER_ONLY
        const bool wild_flag = pid & 0x80;
        #endif
        pid &= ~0x80;
        if (pid >= maxplayers || ass_pid >= maxplayers || ass_pid < -1)
            throw ServerDataError();
        fx.player[pid].stats().add_capture(time);
        #ifndef DEDICATED_SERVER_ONLY
        string capturers = fx.player[pid].name;
        if (ass_pid != -1) {
            fx.player[ass_pid].stats().add_assist();
            capturers += " (" + fx.player[ass_pid].name + ")";
        }
        const int team = pid / TSIZE;
        fx.teams[team].add_score(time - map_start_time, capturers);
        FormattedText msg;
        if (wild_flag)
            msg = tf("$1 CAPTURED THE $GWILD FLAG$>!", formatName(pid));
        else if (1 - team == 0)
            msg = tf("$1 CAPTURED THE $RRED FLAG$>!", formatName(pid));
        else
            msg = tf("$1 CAPTURED THE $BBLUE FLAG$>!", formatName(pid));
        addThreadMessage(new TM_Text(msg_info, msg));
        addThreadMessage(new TM_Sound(SAMPLE_CTF_CAPTURE));
        #endif
    }

    break; case data_kill: {
        uint8_t attacker = read.U8(), target = read.U8();
        const DamageType cause = ((attacker & 0x80) ? DT_deathbringer : (target & 0x20) ? DT_collision : DT_rocket);
        const bool carrier_defended = attacker & 0x40;
        const bool flag_defended = attacker & 0x20;
        const bool flag = target & 0x80;
        const bool wild_flag = target & 0x40;
        attacker &= 0x1F;
        target &= 0x1F;
        Statistics::FlagType carriedFlag;
        if (flag)
            if (wild_flag)
                carriedFlag = Statistics::flagWild;
            else
                carriedFlag = Statistics::flagEnemy;
        else if (wild_flag)
            carriedFlag = Statistics::flagOwn;
        else
            carriedFlag = Statistics::flagNone;
        if (attacker >= maxplayers && attacker != MAX_PLAYERS - 1 || target >= maxplayers) // attacker = MAX_PLAYERS - 1 if attacker already left the server
            throw ServerDataError();

        const bool attacker_team = attacker / TSIZE;
        const bool target_team = target / TSIZE;
        const bool same_team = (attacker_team == target_team);
        const bool known_attacker = fx.player[attacker].used;
        const bool spree_ended = fx.player[target].stats().current_cons_kills() >= 10;

        if (!same_team) {
            if (known_attacker)
                fx.player[attacker].stats().add_kill(cause == DT_deathbringer);
            fx.teams[attacker_team].add_kill();
        }
        fx.player[target].stats().add_death(cause == DT_deathbringer, static_cast<int>(time));
        fx.player[target].dead = true;
        fx.teams[target_team].add_death();
        if (carriedFlag != Statistics::flagNone) {
            if (!same_team && known_attacker)
                fx.player[attacker].stats().add_carrier_kill();
            fx.player[target].stats().add_flag_drop(time);
            fx.teams[target_team].add_flag_drop();
        }

        const bool spree_started = !same_team && known_attacker && fx.player[attacker].stats().current_cons_kills() % 10 == 0;

        netKill(attacker, target, cause, carrier_defended, flag_defended, carriedFlag, spree_ended, spree_started);
    }

    break; case data_flag_take: {
        uint8_t pid = read.U8();
        const bool wild_flag = pid & 0x80;
        const bool own_flag = pid & 0x40;
        pid &= 0x1F;
        const Statistics::FlagType carriedFlag = wild_flag ? Statistics::flagWild : own_flag ? Statistics::flagOwn : Statistics::flagEnemy;
        if (pid >= maxplayers)
            throw ServerDataError();

        fx.player[pid].stats().add_flag_take(time, carriedFlag);
        const int team = pid / TSIZE;
        fx.teams[team].add_flag_take();
        netFlagTake(pid, carriedFlag);
    }

    break; case data_flag_return: {
        const uint8_t pid = read.U8(0, maxplayers - 1);
        fx.player[pid].stats().add_flag_return();
        fx.teams[pid / TSIZE].add_flag_return();
        netFlagReturn(pid);
    }

    break; case data_flag_drop: {
        uint8_t pid = read.U8();
        const bool wild_flag = pid & 0x80;
        const bool own_flag = pid & 0x40;
        const bool fakeDrop = pid & 0x20;
        pid &= 0x1F;
        if (pid >= maxplayers)
            throw ServerDataError();
        fx.player[pid].stats().add_flag_drop(time, !fakeDrop);
        if (!fakeDrop) {
            const int team = pid / TSIZE;
            fx.teams[team].add_flag_drop();
            const Statistics::FlagType carriedFlag = wild_flag ? Statistics::flagWild : own_flag ? Statistics::flagOwn : Statistics::flagEnemy;
            netFlagDrop(pid, carriedFlag);
        }
    }

    break; case data_suicide: {
        uint8_t pid = read.U8();
        const bool flag = pid & 0x80;
        const bool wild_flag = pid & 0x40;
        pid &= 0x1F;
        Statistics::FlagType carriedFlag;
        if (flag)
            if (wild_flag)
                carriedFlag = Statistics::flagWild;
            else
                carriedFlag = Statistics::flagEnemy;
        else if (wild_flag)
            carriedFlag = Statistics::flagOwn;
        else
            carriedFlag = Statistics::flagNone;
        const bool spree_ended = fx.player[pid].stats().current_cons_kills() >= 10;
        if (pid >= maxplayers)
            throw ServerDataError();
        const int team = pid / TSIZE;
        fx.player[pid].stats().add_suicide(static_cast<int>(time));
        fx.player[pid].dead = true;
        fx.teams[team].add_suicide();
        if (carriedFlag != Statistics::flagNone) {
            fx.player[pid].stats().add_flag_drop(time);
            fx.teams[team].add_flag_drop();
        }
        netSuicide(pid, carriedFlag, spree_ended);
    }

    break; case data_players_present: {       // this is only sent immediately after connecting to the server
        #ifndef DEDICATED_SERVER_ONLY
        if (replaying && replay_version >= 1)
            throw ServerDataError();          // Invalid replay file. This message should be only in version 0.
        #endif
        const uint32_t pp = read.U32();
        for (int i = 0; i < maxplayers; ++i) {
            if (fx.player[i].used)  // this shouldn't happen except for i == me; either way, the player is already initialized
                continue;
            if (pp & (1 << i)) {
                fx.player[i].clear(true, i, " ", i / TSIZE);  // hack... use " " for name to suppress announcement when the name is received
                #ifndef DEDICATED_SERVER_ONLY
                players_sb.push_back(&fx.player[i]);
                #endif
            }
        }
    }

    break; case data_new_player: {
        const uint8_t pid = read.U8(0, maxplayers - 1);
        nAssert(!fx.player[pid].used || replaying);
        #ifndef DEDICATED_SERVER_ONLY
        if (replaying && replay_version >= 1) { // Reset players when parsing replay frame data, not here.
            fx.player[pid].name = "";           // Clear to show the join message.
            break;
        }
        #endif
        fx.player[pid].clear(true, pid, "", pid / TSIZE);
        fx.player[pid].stats().set_start_time(time);
        #ifndef DEDICATED_SERVER_ONLY
        players_sb.push_back(&fx.player[pid]);
        #endif
    }

    break; case data_player_left: {
        const uint8_t pid = read.U8(0, maxplayers - 1);
        #ifndef DEDICATED_SERVER_ONLY
        const FormattedText msg = tf("$1 left the game with $2 frags.", formatName(pid), itoa(fx.player[pid].stats().frags()));
        addThreadMessage(new TM_Text(msg_info, msg));
        addThreadMessage(new TM_Sound(SAMPLE_LEFTGAME));
        const vector<ClientPlayer*>::iterator rm = find(players_sb.begin(), players_sb.end(), &fx.player[pid]);
        if (rm == players_sb.end())
            throw ServerDataError();
        players_sb.erase(rm);
        #endif
        nAssert(fx.player[pid].used);
        fx.player[pid].used = false;
    }

    break; case data_team_change: {
        const uint8_t from = read.U8(0, maxplayers - 1), to = read.U8(0, maxplayers - 1), col1 = read.U8(), col2 = read.U8();
        const bool swap = (col2 != 255);
        nAssert(fx.player[from].used && swap == fx.player[to].used);

        netTeamChange(from, swap ? to : -1);

        if (swap) {
            std::swap(fx.player[from], fx.player[to]);
            fx.player[from].pid = from;
            fx.player[to  ].pid =   to;
            fx.player[from].set_team(from / TSIZE);
            fx.player[to  ].set_team(  to / TSIZE);
            // both players already exist in players_sb -> no changes except resorting
            #ifndef DEDICATED_SERVER_ONLY
            stable_sort(players_sb.begin(), players_sb.end(), compare_players);
            #endif
        }
        else {
            fx.player[to] = fx.player[from];
            fx.player[from].used = false;
            fx.player[to].pid = to;
            fx.player[to].set_team(to / TSIZE);
            #ifndef DEDICATED_SERVER_ONLY
            const vector<ClientPlayer*>::iterator rm = find(players_sb.begin(), players_sb.end(), &fx.player[from]);
            if (rm == players_sb.end())
                throw ServerDataError();
            players_sb.erase(rm);
            players_sb.push_back(&fx.player[to]);
            #endif
        }

        if (from == me || to == me)
            me = (me == from) ? to : from;

        #ifndef DEDICATED_SERVER_ONLY
        if (col1 >= PlayerBase::invalid_color)
            throw ServerDataError();
        fx.player[to].set_color(col1);
        #else
        (void)col1;
        #endif
        fx.player[to].stats().kill(static_cast<int>(time), true);
        fx.player[to].dead = true;  // this was already read from the frame data but overwritten by the team change
        if (swap) {
            #ifndef DEDICATED_SERVER_ONLY
            if (col2 >= PlayerBase::invalid_color)
                throw ServerDataError();
            fx.player[from].set_color(col2);
            #endif
            fx.player[from].stats().kill(static_cast<int>(time), true);
            fx.player[from].dead = true;    // this was already read from the frame data but overwritten by the team change
        }
    }

    break; case data_spawn: {
        const uint8_t pid = read.U8(0, maxplayers - 1);
        fx.player[pid].stats().spawn(time);
        if (fx.player[pid].posUpdated != fx.frame)   // this information is after the spawn
            fx.player[pid].posUpdated = fx.player[pid].prevMapPosUpdateFrame = -1e10;  // (probably) not seen in this life; if seen before spawning, not valid anymore
        fx.player[pid].dead = false;
        fx.player[pid].setProperties(PlayerBase::RemotelyVisiblePropertiesData());
    }

    break; case data_minimap_player_properties:
        while (read.hasMore()) {
            const uint8_t pid = read.U8(0, maxplayers - 1);
            fx.player[pid].setProperties(PlayerBase::RemotelyVisiblePropertiesData().read(read));
        }

    break; case data_team_movements_shots: {
        const bool e = protocolExtensions >= 0;
        for (int i = 0; i < 2; i++) {
            fx.teams[i].set_movement(read.U32());
            fx.teams[i].set_shots(read.U32dyn16orU16(e));
            fx.teams[i].set_hits(read.U32dyn16orU16(e));
            fx.teams[i].set_shots_taken(read.U32dyn16orU16(e));
        }
    }

    break; case data_team_stats: {
        const bool e = protocolExtensions >= 0;
        for (int i = 0; i < 2; i++) {
            fx.teams[i].set_kills(read.U32dyn8orU8(e));
            fx.teams[i].set_deaths(read.U32dyn8orU8(e));
            fx.teams[i].set_suicides(read.U32dyn8orU8(e));
            fx.teams[i].set_flags_taken(read.U32dyn8orU8(e));
            fx.teams[i].set_flags_dropped(read.U32dyn8orU8(e));
            fx.teams[i].set_flags_returned(read.U32dyn8orU8(e));
        }
    }

    break; case data_movements_shots: {
        const bool e = protocolExtensions >= 0;
        const uint8_t pid = read.U8(0, maxplayers - 1);
        fx.player[pid].stats().set_movement(read.U32());
        fx.player[pid].stats().save_speed(time);
        fx.player[pid].stats().set_shots(read.U32dyn16orU16(e));
        fx.player[pid].stats().set_hits(read.U32dyn16orU16(e));
        fx.player[pid].stats().set_shots_taken(read.U32dyn16orU16(e));
    }

    break; case data_stats: {
        uint8_t pid = read.U8();
        const bool flag = (pid & 0x80);
        const bool wild_flag = (pid & 0x40);
        const bool dead = (pid & 0x20);
        pid &= 0x1F;
        if (pid >= maxplayers)
            throw ServerDataError();
        Statistics& stats = fx.player[pid].stats();
        Statistics::FlagType carriedFlag;
        if (flag)
            if (wild_flag)
                carriedFlag = Statistics::flagWild;
            else
                carriedFlag = Statistics::flagEnemy;
        else if (wild_flag)
            carriedFlag = Statistics::flagOwn;
        else
            carriedFlag = Statistics::flagNone;
        stats.set_flag(carriedFlag);
        fx.player[pid].dead = dead;
        stats.set_dead               (dead);
        const bool e = protocolExtensions >= 0;
        stats.set_kills              (read.U32dyn8orU8(e));
        stats.set_deaths             (read.U32dyn8orU8(e));
        stats.set_cons_kills         (read.U32dyn8orU8(e));
        stats.set_current_cons_kills (read.U32dyn8orU8(e));
        stats.set_cons_deaths        (read.U32dyn8orU8(e));
        stats.set_current_cons_deaths(read.U32dyn8orU8(e));
        stats.set_suicides           (read.U32dyn8orU8(e));
        stats.set_captures           (read.U32dyn8orU8(e));
        stats.set_flags_taken        (read.U32dyn8orU8(e));
        stats.set_flags_dropped      (read.U32dyn8orU8(e));
        stats.set_flags_returned     (read.U32dyn8orU8(e));
        stats.set_carriers_killed    (read.U32dyn8orU8(e));
        stats.set_start_time         (time - read.U32());
        stats.set_lifetime           (read.U32());
        stats.set_spawn_time         (time);
        stats.set_flag_carrying_time (read.U32());
        stats.set_flag_take_time     (time);
    }

    break; case data_name_authentication_request:
        #ifndef DEDICATED_SERVER_ONLY
        addThreadMessage(new TM_NameAuthenticationRequest());
        #endif

    break; case data_server_settings: {
        const uint8_t caplimit = read.U8(), timelimit = read.U8(), extratime = read.U8();
        const uint16_t misc1 = read.U16(), pupMin = read.U16(), pupMax = read.U16(), pupAddTime = read.U16(), pupMaxTime = read.U16();
        fx.physics.read(read);
        if (read.hasMore())
            flag_return_delay = read.U16();
        else
            flag_return_delay = -1;
        uint8_t et_periods;
        if (read.hasMore())
            et_periods = read.U8();
        else
            et_periods = 1;
        uint8_t win_score_diff;
        if (read.hasMore())
            win_score_diff = read.U8();
        else
            win_score_diff = 1;
        #ifndef DEDICATED_SERVER_ONLY
        fd.physics = fx.physics;

        netPhysicsChanged();

        addThreadMessage(new TM_ServerSettings(caplimit, timelimit, extratime, et_periods, misc1, pupMin, pupMax, pupAddTime, pupMaxTime, flag_return_delay));
        gameSettings.capture_limit = caplimit;
        gameSettings.time_limit = timelimit * 600; // convert to frames
        gameSettings.extra_time = extratime * 600; // convert to frames
        gameSettings.extra_time_periods = et_periods;
        gameSettings.win_score_difference = win_score_diff;
        gameSettings.sudden_death = misc1 & (1 << 10);
        #else
        (void)(caplimit && timelimit && extratime && misc1 && pupMin && pupMax && pupAddTime && pupMaxTime && et_periods && win_score_diff);
        #endif
    }

    break; case data_5_min_left:
        #ifndef DEDICATED_SERVER_ONLY
        addThreadMessage(new TM_Text(msg_info, _("*** Five minutes remaining")));
        addThreadMessage(new TM_Sound(SAMPLE_5_MIN_LEFT));
        #endif

    break; case data_1_min_left:
        #ifndef DEDICATED_SERVER_ONLY
        addThreadMessage(new TM_Text(msg_info, _("*** One minute remaining")));
        addThreadMessage(new TM_Sound(SAMPLE_1_MIN_LEFT));
        #endif

    break; case data_30_s_left:
        #ifndef DEDICATED_SERVER_ONLY
        addThreadMessage(new TM_Text(msg_info, _("*** 30 seconds remaining")));
        #endif

    break; case data_time_out:
        #ifndef DEDICATED_SERVER_ONLY
        addThreadMessage(new TM_Text(msg_info, _("*** Time out - CTF game over")));
        #endif

    break; case data_extra_time_out:
        #ifndef DEDICATED_SERVER_ONLY
        addThreadMessage(new TM_Text(msg_info, _("*** Extra-time out - CTF game over")));
        #endif

    break; case data_normal_time_out: {
        #ifndef DEDICATED_SERVER_ONLY
        extra_time_running = true;
        string msg = _("*** Normal time out - extra-time started");
        if (read.U8() & 0x01)
            msg += " " + _("(sudden death)");
        addThreadMessage(new TM_Text(msg_info, msg));
        #endif
    }

    break; case data_map_change_info: {
        #ifndef DEDICATED_SERVER_ONLY
        const uint8_t votes = read.U8(), needed = read.U8();
        const uint16_t vote_block_time = read.U16();
        string msg = _("*** $1/$2 votes for mapchange.", itoa(votes), itoa(needed));
        if (vote_block_time > 0)
            msg += ' ' + _("(All players needed for $1 more seconds.)", itoa(vote_block_time));
        addThreadMessage(new TM_Text(msg_info, msg));
        #endif
    }

    break; case data_too_much_talk:
        #ifndef DEDICATED_SERVER_ONLY
        addThreadMessage(new TM_Text(msg_warning, _("Too much talk. Chill...")));
        #endif

    break; case data_mute_notification:
        #ifndef DEDICATED_SERVER_ONLY
        addThreadMessage(new TM_Text(msg_warning, _("You are muted. You can't send messages.")));
        #endif

    break; case data_ranking_update_failed:
        #ifndef DEDICATED_SERVER_ONLY
        addThreadMessage(new TM_Text(msg_warning, _("Updating your ranking score failed!")));
        #endif

    break; case data_player_mute: {
        #ifndef DEDICATED_SERVER_ONLY
        const uint8_t pid = read.U8(0, maxplayers - 1), mode = read.U8();
        string admin = read.str();
        if (admin.empty())
            admin = _("The admin");
        if (pid == me) {
            string msg;
            if (mode == 0)
                msg = _("You have been unmuted (you can send messages again).");
            else if (mode == 1)
                msg = _("You have been muted by $1 (you can't send messages).", admin);
            else
                nAssert(0);     // The silent mute should not be known by the muted player.
            addThreadMessage(new TM_Text(msg_warning, msg));
        }
        else {
            FormattedText msg;
            if (mode == 0)
                msg = tf("$1 has unmuted $2.", admin, formatName(pid));
            else
                msg = tf("$1 has muted $2.", admin, formatName(pid));
            addThreadMessage(new TM_Text(msg_info, msg));
        }
        #endif
    }

    break; case data_player_kick: {
        #ifndef DEDICATED_SERVER_ONLY
        const uint8_t pid = read.U8(0, maxplayers - 1);
        const uint32_t minutes = read.U32();
        string admin = read.str();
        if (admin.empty())
            admin = _("The admin");
        if (pid == me) {
            string msg;
            if (minutes == 0)
                msg = _("You are being kicked from this server by $1!", admin);
            else
                msg = _("$1 has BANNED you from this server for $2!", admin, approxTime(minutes * 60));
            addThreadMessage(new TM_Text(msg_warning, msg));
        }
        else {
            FormattedText msg;
            if (minutes == 0)
                msg = tf("$1 has kicked $2 (disconnect in 10 seconds).", admin, formatName(pid));
            else
                msg = tf("$1 has banned $2 (disconnect in 10 seconds).", admin, formatName(pid));
            addThreadMessage(new TM_Text(msg_info, msg));
        }
        #endif
    }

    break; case data_disconnecting:
        #ifndef DEDICATED_SERVER_ONLY
        addThreadMessage(new TM_Text(msg_warning, _("Disconnecting in $1...", itoa(read.U8()))));
        #endif

    break; case data_idlekick_warning:
        #ifndef DEDICATED_SERVER_ONLY
        addThreadMessage(new TM_Text(msg_warning, _("*** Idle kick: move or be kicked in $1 seconds.", itoa(read.U8()))));
        #endif

    break; case data_broken_map:
        #ifndef DEDICATED_SERVER_ONLY
        addThreadMessage(new TM_Text(msg_warning, _("This map is broken. There is an instantly capturable flag. Avoid it.")));
        #endif

    break; case data_acceleration_modes: {
        const uint32_t mask = read.U32();
        for (int i = 0; i < maxplayers; ++i)
            fx.player[i].accelerationMode = (mask & (1 << i)) ? AM_Gun : AM_World;
    }

    break; case data_flag_modes: {
        const uint8_t mask = read.U8();
        see_minimap_player_properties = (mask & 0xC0) >> 6;
        carry_own_team_flag = mask & 0x20;
        capture_away_from_base = mask & 0x10;
        lock_team_flags_in_effect = mask & 8;
        lock_wild_flags_in_effect = mask & 4;
        capture_on_team_flags_in_effect = mask & 2;
        capture_on_wild_flags_in_effect = mask & 1;
    }

    break; case data_waiting_time: {
        const uint32_t waiting_time = read.U32dyn8();
        if (waiting_time >= 10)
            next_respawn_time = get_time() + waiting_time / 10.;
    }

    break; case data_extension_advantage:
        #ifndef DEDICATED_SERVER_ONLY
        addThreadMessage(new TM_Text(msg_warning, _("Warning: This server has extensions enabled that give an advantage over you to players with a supporting Outgun client.")));
        #endif

    break; default:
        if (code < data_reserved_range_first || code > data_reserved_range_last) {
            log("Unknown message code: %d, length %d", code, data.size());
            throw ServerDataError();
        }
        // just ignore commands in reserved range: they're probably some extension we don't have to care about
    }
 } catch (BinaryReader::ReadError) {
     throw ServerDataError();
 }
}

void ClientBase::netKill(int attacker, int target, DamageType cause, bool carrier_defended, bool flag_defended, Statistics::FlagType, bool spree_ended, bool spree_started) throw () {
    (void)(attacker && target && cause && carrier_defended && flag_defended && spree_ended && spree_started);
}

void ClientBase::process_incoming_data(ConstDataBlockRef data) throw (ServerDataError) {
    Lock ml(frameMutex);

    if (!connected && !replaying) // means that the connection notification is still in the thread message queue
        return;

    lastpackettime = get_time();

    if (replaying)
        process_replay_packet(data);
    else {
        process_live_frame_data(data);
        for (;;) {
            const ConstDataBlockRef message = client->receive_message();
            if (!message.data())
                break;
            process_message(message);
        }
    }
}

void ClientBase::process_server_data(ConstDataBlockRef data) throw () {
    try {
        process_incoming_data(data);
    } catch (ServerDataError) {
        nAssert(!botmode);
        log.error(_("Format error in data received from the server."));
        addThreadMessage(new TM_DoDisconnect());
        return;
    }
}

void ClientBase::handlePendingThreadMessages() throw () {    // should only be called by the main thread
    while (!messageQueue.empty()) {
        ThreadMessage* msg = messageQueue.front();
        messageQueue.pop_front();
        msg->execute(this);
        delete msg;
    }
}

void ClientBase::stop() throw () {
    abortThreads = true;

    //at least disconnect
    disconnect_command();
}

void ClientBase::rocketHitWallCallback(int rid, bool power, const WorldCoords& pos) throw () {
    (void)power; (void)pos;
    fx.rock[rid].owner = -1;   // erase from clientside simulation
    #ifndef DEDICATED_SERVER_ONLY
    fd.rock[rid].owner = -1;
    #endif
}

void ClientBase::rocketOutOfBoundsCallback(int rid) throw () {
    fx.rock[rid].owner = -1;   // erase from clientside simulation
    #ifndef DEDICATED_SERVER_ONLY
    fd.rock[rid].owner = -1;
    #endif
}

bool ClientBase::shouldApplyPhysicsToPlayerCallback(int pid) throw () {
    return (fx.player[pid].onscreen || replaying) && !fx.player[pid].dead;
}

void ClientBase::remove_useless_flags() throw () {
    for (int i = 0; i < 3; i++)
        if (remove_flags & (0x01 << i)) {
            fx.remove_team_flags(i);
            #ifndef DEDICATED_SERVER_ONLY
            fd.remove_team_flags(i);
            #endif
        }
}

void ClientBase::cfunc_connection_update(void* customp, int connect_result, ConstDataBlockRef data) throw () {
    ClientBase* cl = static_cast<ClientBase*>(customp);
    cl->connection_update(connect_result, data);
}

void ClientBase::connection_update(int connect_result, ConstDataBlockRef data) throw () {
    addThreadMessage(new TM_ConnectionUpdate(connect_result, data));
}

void ClientBase::cfunc_server_data(void* customp, ConstDataBlockRef data) throw () {
    ClientBase* cl = static_cast<ClientBase*>(customp);
    cl->process_server_data(data);
}
