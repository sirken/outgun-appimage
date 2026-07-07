/*
 *  servnet.cpp
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

#include "incalleg.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <cctype>

#include "leetnet/server.h"
#include "admshell.h"
#include "binaryaccess.h"
#include "debug.h"
#include "debugconfig.h"    // for LOG_MESSAGE_TRAFFIC
#include "function_utility.h"
#include "language.h"
#include "localconnection.h"
#include "nassert.h"
#include "platform.h"
#include "protocol.h"
#include "server.h"
#include "timer.h"
#include "version.h"

// Delay for the server contacting the master server, in seconds.
// It is good if this delay is set to a minute or so, since this will
// filter out people opening and closing servers frequently.
const double delay_to_report_server = 30.0;

using std::ifstream;
using std::make_pair;
using std::map;
using std::max;
using std::min;
using std::ofstream;
using std::ostream;
using std::ostringstream;
using std::pair;
using std::queue;
using std::setfill;
using std::setw;
using std::string;
using std::stringstream;
using std::vector;

// ranking thread job struct
class MasterQuery {
public:
    string request;
    enum JobType { JT_score, JT_login, JT_nameCheck };
    JobType code;
    int cid;
};

ServerNetworking::ServerNetworking(Server* hostp, const Settings& settings_, ServerWorld& w, LogSet logs, bool threadLock_, Mutex& threadLockMutex_) throw () :
    threadLock(threadLock_),
    threadLockMutex(threadLockMutex_),
    host(hostp),
    settings(settings_),
    world(w),
    log(logs),
    mjob_mutex("ServerNetworking::mjob_mutex"),
    player_count(0),
    localPlayers(0),
    addPlayerMutex("ServerNetworking::addPlayerMutex"),
    newUniqueId(0),
    accelerationModeMask(0),
    flagModeMask(0),
    rankingLoginSetPreviously(false),
    maplist_revision(0),
    relayThread(logs, file_threads_quit),
    playerSlotReservationTime(get_time()),
    reservedPlayerSlots(0)
{
    leetServer = 0;
    for (int i = 0; i < MAX_CLIENTS; ++i)
        clientConnection[i] = 0;
    frameSentTime = 0;  // no meaning
}

ServerNetworking::~ServerNetworking() throw () {
    delete leetServer;
    leetServer = 0;
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        nAssert(clientConnection[i] == 0);
        delete clientConnection[i];
    }
}

void ServerNetworking::writePos(BinaryWriter& msg, const WorldCoords& pos) const throw () {
    msg.U8(pos.room.x);
    msg.U8(pos.room.y);
    msg.S16(static_cast<int>(pos.x));
    msg.S16(static_cast<int>(pos.y));
}

bool ServerNetworking::writeToAdminShell(ConstDataBlockRef data) const throw () {
    try {
        int written;
        shellssock.write(data, &written);
        if (static_cast<unsigned>(written) == data.size())
            return true;
        log.error(_("Admin shell connection: Not all written."));
    } catch (const Network::Error& e) {
        log.error(_("Admin shell connection: $1", e.str()));
    }
    shellssock.close();
    return false;
}

void ServerNetworking::upload_next_file_chunk(int cid) throw () {
    const int max_chunksize = 128;      // the max chunk size in bytes

    //actual size sent
    int chunksize = fileTransfer[cid].data.size() - fileTransfer[cid].dp;     //attempt to send remaining...
    if (chunksize > max_chunksize)                          //...but there is the maximum
        chunksize = max_chunksize;

    const uint8_t islast = fileTransfer[cid].dp + chunksize == fileTransfer[cid].data.size();

    BinaryBuffer<256> msg;
    msg.U8(data_file_download);
    msg.U16(chunksize);
    msg.U8(islast);
    msg.block(ConstDataBlockRef(fileTransfer[cid].data.data() + fileTransfer[cid].dp, chunksize));
    send_message(cid, msg);

    //save old dp for the ack
    fileTransfer[cid].old_dp = fileTransfer[cid].dp;

    //inc dp
    fileTransfer[cid].dp += chunksize;
}

string ServerNetworking::get_download_file(const string& ftype, const string& fname) throw () {
    if (ftype == "map") {
        if (fname.find_first_of("./:\\") != string::npos) {
            log("Illegal file download attempt: map \"%s\"", fname.c_str());
            return string();
        }

        string fileName = wheregamedir + SERVER_MAPS_DIR + directory_separator + fname + ".txt";

        FILE* fmap = fopen(fileName.c_str(), "rb");
        if (!fmap) { // check from generated maps (writable per-user data; see ServerWorld::generate_map())
            fileName = whereuserdir + SERVER_MAPS_DIR + directory_separator + GENERATED_MAPS_SUBDIR_NAME + directory_separator + fname + ".txt";
            fmap = fopen(fileName.c_str(), "rb");
            if (!fmap) {
                log("Nonexisting map download attempt: map \"%s\"", fname.c_str());
                return string();
            }
        }
        string data;
        while (!feof(fmap)) {
            static const unsigned bufSize = 16000;
            char buf[bufSize];
            const int amount = fread(buf, 1, bufSize, fmap);
            data.append(buf, amount);
        }
        fclose(fmap);
        log("Uploading map \"%s\"", fname.c_str());
        return data;
    }
    else {
        log("Unknown download type \"%s\"", ftype.c_str());
        return string();
    }
}

void ServerNetworking::record_message(ConstDataBlockRef data) const throw () {
    if (host->recording_active()) {
        BinaryWriter& writer = host->recordMessageWriter();
        writer.U32dyn8(data.size());
        writer.block(data);
    }
}

void ServerNetworking::broadcast_message(ConstDataBlockRef data) const throw () {
    for (int i = 0; i < maxplayers; ++i)
        if (world.player[i].used)
            send_message(world.player[i].cid, data);
}

void ServerNetworking::send_simple_message(Network_data_code code, int pid) const throw () {
    BinaryBuffer<1> msg;
    msg.U8(code);
    send_message(world.player[pid].cid, msg);
}

void ServerNetworking::broadcast_simple_message(Network_data_code code) const throw () {
    BinaryBuffer<1> msg;
    msg.U8(code);
    broadcast_message(msg);
    record_message(msg);
}

void ServerNetworking::send_me_packet(int pid) const throw () {
    BinaryBuffer<256> msg;
    msg.U8(data_first_packet);
    msg.U8(pid);                    // who am I
    msg.U8(world.player[pid].color());
    msg.U8(host->current_map_nr()); // current map
    const bool e = world.player[pid].protocolExtensionsLevel >= 0;
    msg.U32dyn8orU8(world.teams[0].score(), e); // team 0 current score
    msg.U32dyn8orU8(world.teams[1].score(), e); // team 1 current score
    send_message(world.player[pid].cid, msg);
}

// send a player name update to a client (cid = pid_all: to all clients; pid_record: record only)
void ServerNetworking::send_player_name_update(int cid, int pid) const throw () {
    if (world.player[pid].name.empty())
        return;

    BinaryBuffer<256> msg;
    msg.U8(data_name_update);
    msg.U8(pid);       // what player id
    msg.str(world.player[pid].name);

    if (cid == pid_record)
        record_message(msg);
    else if (cid == pid_all) {
        broadcast_message(msg);
        record_message(msg);

        if (shellssock.isOpen()) {
            BinaryBuffer<256> msg;
            msg.U32(STA_PLAYER_NAME_UPDATE);
            msg.U32(world.player[pid].cid);
            msg.str(world.player[pid].name);
            writeToAdminShell(msg);
        }
    }
    else
        send_message(cid, msg);
}

void ServerNetworking::broadcast_player_name(int pid) const throw () {
    send_player_name_update(pid_all, pid);
}

// cid = to who, pid_all = everybody, pid_record = record only; pid = whose crap
void ServerNetworking::send_player_crap_update(int cid, int pid) throw () {
    const ClientData& clid = host->getClientData(world.player[pid].cid);

    BinaryBuffer<256> msg;
    msg.U8(data_crap_update);

    // --- RECALC CRAP ---
    ClientLoginStatus st;
    st.setToken(clid.token_have);
    st.setMasterAuth(clid.token_have && clid.token_valid);
    st.setRanking(host->rankingLoginSet() && clid.token_have && clid.current_participation);
    st.setLocalAuth(host->isLocallyAuthenticated(pid));
    st.setAdmin(host->isAdmin(pid));
    world.player[pid].reg_status = st;

    msg.U8(pid);
    msg.U8(world.player[pid].color());
    msg.U8(world.player[pid].reg_status.toNetwork());
    msg.U32(clid.rank);
    msg.U32(clid.score);
    msg.U32(clid.neg_score);
    msg.U32(max_world_rank);

    if (cid == pid_record)
        record_message(msg);
    else if (cid == pid_all) {
        record_message(msg);
        broadcast_message(msg);
    }
    else
        send_message(cid, msg);
}

void ServerNetworking::broadcast_player_crap(int pid) throw () {
    send_player_crap_update(pid_all, pid);
}

void ServerNetworking::send_acceleration_modes(int pid) const throw () {
    BinaryBuffer<10> msg;
    msg.U8(data_acceleration_modes);
    msg.U32(accelerationModeMask);
    if (pid != pid_all)
        send_message(world.player[pid].cid, msg);
    else {
        for (int i = 0; i < maxplayers; ++i)
            if (world.player[i].used && world.player[i].protocolExtensionsLevel >= 0)
                send_message(world.player[i].cid, msg);
        record_message(msg);
    }
}

void ServerNetworking::send_flag_modes(int pid) const throw () {
    BinaryBuffer<10> msg;
    msg.U8(data_flag_modes);
    msg.U8(flagModeMask);
    if (pid != pid_all)
        send_message(world.player[pid].cid, msg);
    else {
        for (int i = 0; i < maxplayers; ++i)
            if (world.player[i].used && world.player[i].protocolExtensionsLevel >= 0)
                send_message(world.player[i].cid, msg);
        record_message(msg);
    }
}

void ServerNetworking::warnAboutExtensionAdvantage(int pid) const throw () {
    if (world.player[pid].protocolExtensionsLevel < 0)
        player_message(pid, msg_warning, "Warning: This server has extensions enabled that give an advantage over you to players with a supporting Outgun client.");
    else
        send_simple_message(data_extension_advantage, pid);
}

void ServerNetworking::move_update_player(int a) throw () {
    ctop[world.player[a].cid] = a;
}

void ServerNetworking::broadcast_team_change(int from, int to, bool swap) const throw () {
    BinaryBuffer<64> msg;
    msg.U8(data_team_change);
    msg.U8(from);
    msg.U8(to);
    msg.U8(world.player[to].color());
    if (swap)
        msg.U8(world.player[from].color());
    else
        msg.U8(255);
    broadcast_message(msg);
    record_message(msg);
}

void ServerNetworking::broadcast_sample(int code) const throw () {
    BinaryBuffer<64> msg;
    msg.U8(data_sound);
    msg.U8(code);
    broadcast_message(msg);
    record_message(msg);
}

void ServerNetworking::broadcast_screen_sample(int p, int code) const throw () {
    BinaryBuffer<64> msg;
    msg.U8(data_sound);
    msg.U8(code);
    broadcast_screen_message(world.player[p].room().x, world.player[p].room().y, msg);
}

void ServerNetworking::broadcast_screen_power_collision(int p) const throw () {
    BinaryBuffer<64> msg;
    msg.U8(data_power_collision);
    msg.U8(p);
    broadcast_screen_message(world.player[p].room().x, world.player[p].room().y, msg);
}

//send current flag status (cid == pid_all : broadcast)
void ServerNetworking::ctf_net_flag_status(int cid, int team) const throw () {
    //just resetting server state -- no update needed
    if (!leetServer)
        return;

    BinaryBuffer<256> msg;
    msg.U8(data_flag_update);

    msg.U8(team);

    // how many flags
    if (team == 2)
        msg.U8(world.wild_flags.size());
    else
        msg.U8(world.teams[team].flags().size());

    const vector<Flag>& flags = team == 2 ? world.wild_flags : world.teams[team].flags();
    for (vector<Flag>::const_iterator fi = flags.begin(); fi != flags.end(); ++fi)
        if (fi->carried())  {
            msg.U8(1); // carried
            //new flag carrier
            msg.U8(fi->carrier());   //player who took it
        }
        else {
            msg.U8(0); // not carried
            //new flag position
            msg.U8(fi->position().room.x);
            msg.U8(fi->position().room.y);
            msg.S16(static_cast<int>(fi->position().x));
            msg.S16(static_cast<int>(fi->position().y));
        }

    if (cid == pid_all || cid == pid_record) {
        if (cid == pid_all)
            broadcast_message(msg);
        record_message(msg);
    }
    else
        send_message(cid, msg);
}

//update team scores
void ServerNetworking::ctf_update_teamscore(int t) const throw () {
    BinaryBuffer<64> ext_msg;
    ext_msg.U8(data_score_update);
    ext_msg.U8(t);
    BinaryBuffer<64> old_msg = ext_msg;
    old_msg.U8(min(world.teams[t].score(), 255));
    ext_msg.U32dyn8(world.teams[t].score());
    for (int i = 0; i < maxplayers; i++) {
        const ServerPlayer& player = world.player[i];
        if (player.used)
            send_message(player.cid, player.protocolExtensionsLevel >= 0 ? ext_msg : old_msg);
    }
    record_message(ext_msg);
}

void ServerNetworking::broadcast_reset_map_list() throw () {
    ++maplist_revision;
    broadcast_simple_message(data_reset_map_list);
}

void ServerNetworking::broadcast_current_map(int mapNr) const throw () {
    BinaryBuffer<64> msg;
    msg.U8(data_current_map);
    msg.U8(mapNr);
    broadcast_message(msg);
}

// Tell that stats are ready for saving.
void ServerNetworking::broadcast_stats_ready() const throw () {
    broadcast_simple_message(data_stats_ready);
}

void ServerNetworking::broadcast_5_min_left() const throw () {
    broadcast_simple_message(data_5_min_left);
}

void ServerNetworking::broadcast_1_min_left() const throw () {
    broadcast_simple_message(data_1_min_left);
}

void ServerNetworking::broadcast_30_s_left() const throw () {
    broadcast_simple_message(data_30_s_left);
}

void ServerNetworking::broadcast_time_out() const throw () {
    broadcast_simple_message(data_time_out);
}

void ServerNetworking::broadcast_extra_time_out() const throw () {
    broadcast_simple_message(data_extra_time_out);
}

void ServerNetworking::broadcast_normal_time_out(bool sudden_death) const throw () {
    BinaryBuffer<64> msg;
    msg.U8(data_normal_time_out);
    msg.U8(sudden_death ? 0x01 : 0x00);
    broadcast_message(msg);
    record_message(msg);
}

void ServerNetworking::broadcast_capture(const ServerPlayer& player, int flag_team, int assistant_pid) const throw () {
    BinaryBuffer<64> msg;
    msg.U8(data_capture);
    msg.U8(player.pid | (flag_team == 2 ? 0x80 : 0x00));
    if (assistant_pid != -1)
        msg.S8(assistant_pid);
    broadcast_message(msg);
    record_message(msg);
    if (shellssock.isOpen()) {
        BinaryBuffer<256> msg;
        msg.U32(STA_PLAYER_CAPTURES);
        msg.U32(player.cid);
        writeToAdminShell(msg);
    }
}

void ServerNetworking::broadcast_flag_take(const ServerPlayer& player, Statistics::FlagType flagType) const throw () {
    BinaryBuffer<64> old_msg;
    old_msg.U8(data_flag_take);
    BinaryBuffer<64> ext_msg = old_msg;
    old_msg.U8(player.pid | (flagType == Statistics::flagWild == 2 ? 0x80 : 0x00));

    uint8_t flag;
    if (flagType == Statistics::flagWild)
        flag = 0x80;
    else if (flagType == Statistics::flagOwn)
        flag = 0x40;
    else
        flag = 0x00;
    ext_msg.U8(player.pid | flag);

    for (int i = 0; i < maxplayers; i++) {
        const ServerPlayer& player = world.player[i];
        if (player.used)
            send_message(player.cid, player.protocolExtensionsLevel >= 2 ? ext_msg : old_msg);
    }
    record_message(ext_msg);
}

void ServerNetworking::broadcast_flag_return(const ServerPlayer& player) const throw () {
    BinaryBuffer<64> msg;
    msg.U8(data_flag_return);
    msg.U8(player.pid);
    broadcast_message(msg);
    record_message(msg);
}

// player dropped the flag on purpose
void ServerNetworking::broadcast_flag_drop(const ServerPlayer& player, Statistics::FlagType flagType, bool captureDrop) const throw () {
    BinaryBuffer<64> old_msg;
    old_msg.U8(data_flag_drop);
    BinaryBuffer<64> ext_msg = old_msg;
    old_msg.U8(player.pid | (flagType == Statistics::flagWild ? 0x80 : 0x00));

    uint8_t flag = captureDrop ? 0x20 : 0x00;
    if (flagType == Statistics::flagWild)
        flag |= 0x80;
    else if (flagType == Statistics::flagOwn)
        flag |= 0x40;
    ext_msg.U8(player.pid | flag);

    for (int i = 0; i < maxplayers; i++) {
        const ServerPlayer& player = world.player[i];
        if (player.used)
            send_message(player.cid, player.protocolExtensionsLevel >= 2 ? ext_msg : old_msg);
    }
    record_message(ext_msg);
}

void ServerNetworking::broadcast_kill(const ServerPlayer& attacker, const ServerPlayer& target,
                                      DamageType cause, Statistics::FlagType carriedFlag, bool carrier_defended, bool flag_defended) const throw () {
    BinaryBuffer<64> old_msg;
    old_msg.U8(data_kill);
    // first byte: deatbringer bit, carrier defended bit, flag defended bit, and attacker id
    uint8_t attacker_info = attacker.pid;
    if (cause == DT_deathbringer)
        attacker_info |= 0x80;
    if (carrier_defended)
        attacker_info |= 0x40;
    if (flag_defended)
        attacker_info |= 0x20;
    // second byte: flag bit, wild flag bit, collision bit, and target id
    uint8_t tar_flag = target.pid;
    if (carriedFlag != Statistics::flagNone)
        tar_flag |= 0x80;
    if (carriedFlag == Statistics::flagWild)
        tar_flag |= 0x40;
    if (cause == DT_collision)
        tar_flag |= 0x20;
    old_msg.U8(attacker_info);
    BinaryBuffer<64> ext_msg = old_msg;
    old_msg.U8(tar_flag);

    // Extension version of the second byte
    tar_flag = target.pid;
    if (carriedFlag == Statistics::flagWild || carriedFlag == Statistics::flagEnemy)
        tar_flag |= 0x80;
    if (carriedFlag == Statistics::flagWild || carriedFlag == Statistics::flagOwn)
        tar_flag |= 0x40;
    if (cause == DT_collision)
        tar_flag |= 0x20;
    ext_msg.U8(tar_flag);

    for (int i = 0; i < maxplayers; i++) {
        const ServerPlayer& player = world.player[i];
        if (player.used)
            send_message(player.cid, player.protocolExtensionsLevel >= 2 ? ext_msg : old_msg);
    }
    record_message(ext_msg);
    if (shellssock.isOpen()) {
        BinaryBuffer<256> msg;
        if (attacker.used) {
            msg.U32(STA_PLAYER_KILLS);
            msg.U32(attacker.cid);
        }
        if (target.used) {  // should be
            msg.U32(STA_PLAYER_DIES);
            msg.U32(target.cid);
        }
        writeToAdminShell(msg);
    }
}

void ServerNetworking::broadcast_suicide(const ServerPlayer& player, Statistics::FlagType carriedFlag) const throw () {
    BinaryBuffer<64> old_msg;
    old_msg.U8(data_suicide);
    BinaryBuffer<64> ext_msg = old_msg;

    uint8_t id_flag = player.pid;
    if (carriedFlag != Statistics::flagNone)
        id_flag |= 0x80;
    if (carriedFlag == Statistics::flagWild)
        id_flag |= 0x40;
    old_msg.U8(id_flag);

    id_flag = player.pid;
    if (carriedFlag == Statistics::flagWild || carriedFlag == Statistics::flagEnemy)
        id_flag |= 0x80;
    if (carriedFlag == Statistics::flagWild || carriedFlag == Statistics::flagOwn)
        id_flag |= 0x40;
    ext_msg.U8(id_flag);

    for (int i = 0; i < maxplayers; i++) {
        const ServerPlayer& player = world.player[i];
        if (player.used)
            send_message(player.cid, player.protocolExtensionsLevel >= 2 ? ext_msg : old_msg);
    }
    record_message(ext_msg);
    if (shellssock.isOpen()) {
        BinaryBuffer<256> msg;
        msg.U32(STA_PLAYER_DIES);
        msg.U32(player.cid);
        writeToAdminShell(msg);
    }
}

void ServerNetworking::send_waiting_time(const ServerPlayer& player) const throw () {
    nAssert(player.extra_frames_to_respawn >= 0);
    if (player.protocolExtensionsLevel < 0 || player.frames_to_respawn < 100 || player.extra_frames_to_respawn)
        return;
    BinaryBuffer<64> msg;
    msg.U8(data_waiting_time);
    msg.U32dyn8(player.frames_to_respawn);
    send_message(player.cid, msg);
}

void ServerNetworking::broadcast_new_player(const ServerPlayer& player) const throw () {
    BinaryBuffer<64> msg;
    msg.U8(data_new_player);
    msg.U8(player.pid);
    broadcast_message(msg);
    record_message(msg);
}

void ServerNetworking::new_player_to_admin_shell(int pid) const throw () {
    if (shellssock.isOpen()) {
        BinaryBuffer<256> msg;
        msg.U32(STA_PLAYER_IP);
        msg.U32(world.player[pid].cid);
        Network::Address addr = get_client_address(world.player[pid].cid);
        addr.setPort(0);
        msg.str(addr.toString());
        writeToAdminShell(msg);
    }
}

void ServerNetworking::broadcast_player_left(const ServerPlayer& player) const throw () {
    BinaryBuffer<256> msg;
    msg.U8(data_player_left);
    msg.U8(player.pid);
    broadcast_message(msg);
    record_message(msg);
}

void ServerNetworking::broadcast_spawn(const ServerPlayer& player) const throw () {
    BinaryBuffer<64> msg;
    msg.U8(data_spawn);
    msg.U8(player.pid);
    broadcast_message(msg);
    record_message(msg);
}

// Send player's movement and shots to everyone.
void ServerNetworking::broadcast_movements_and_shots(const ServerPlayer& player) const throw () {
    BinaryBuffer<64> ext_msg;
    ext_msg.U8(data_movements_shots);
    ext_msg.U8(player.pid);
    const Statistics& stats = player.stats();
    ext_msg.U32(static_cast<unsigned>(stats.movement()));
    BinaryBuffer<64> old_msg = ext_msg;
    old_msg.U16(min(stats.shots(), 65535));
    old_msg.U16(min(stats.hits(), 65535));
    old_msg.U16(min(stats.shots_taken(), 65535));
    ext_msg.U32dyn16(stats.shots());
    ext_msg.U32dyn16(stats.hits());
    ext_msg.U32dyn16(stats.shots_taken());
    for (int i = 0; i < maxplayers; i++)
        if (world.player[i].used)
            send_message(world.player[i].cid, world.player[i].protocolExtensionsLevel >= 0 ? ext_msg : old_msg);
    record_message(ext_msg);
}

// Send player's stats to everyone.
void ServerNetworking::broadcast_stats(const ServerPlayer& player) const throw () {
    for (int i = 0; i < maxplayers; i++)
        if (world.player[i].used)
            send_stats(player, world.player[i].cid);
}

// Send everyone's stats to player.
void ServerNetworking::send_stats(const ServerPlayer& player) const throw () {
    for (int i = 0; i < maxplayers; i++)
        if (world.player[i].used)
            send_stats(world.player[i], player.cid);
}

// Send player's stats to client cid.
void ServerNetworking::send_stats(const ServerPlayer& player, int cid) const throw () {
    BinaryBuffer<64> msg;
    msg.U8(data_stats);
    msg.U8(player.pid | (player.stats().has_flag() ? 0x80 : 0x00) | (player.stats().has_wild_flag() ? 0x40 : 0x00) | (player.dead ? 0x20 : 0x00));
    const Statistics& stats = player.stats();
    const bool e = cid == pid_record || world.player[ctop[cid]].protocolExtensionsLevel >= 0;
    msg.U32dyn8orU8(stats.kills(), e);
    msg.U32dyn8orU8(stats.deaths(), e);
    msg.U32dyn8orU8(stats.cons_kills(), e);
    msg.U32dyn8orU8(stats.current_cons_kills(), e);
    msg.U32dyn8orU8(stats.cons_deaths(), e);
    msg.U32dyn8orU8(stats.current_cons_deaths(), e);
    msg.U32dyn8orU8(stats.suicides(), e);
    msg.U32dyn8orU8(stats.captures(), e);
    msg.U32dyn8orU8(stats.flags_taken(), e);
    msg.U32dyn8orU8(stats.flags_dropped(), e);
    msg.U32dyn8orU8(stats.flags_returned(), e);
    msg.U32dyn8orU8(stats.carriers_killed(), e);
    msg.U32(static_cast<unsigned>(stats.playtime(get_time())));
    msg.U32(static_cast<unsigned>(stats.lifetime(get_time())));
    msg.U32(static_cast<unsigned>(stats.flag_carrying_time(get_time())));
    if (cid == pid_record)
        record_message(msg);
    else
        send_message(cid, msg);
}

void ServerNetworking::send_team_movements_and_shots(int cid) const throw () {
    BinaryBuffer<256> msg;
    msg.U8(data_team_movements_shots);
    const bool e = cid == pid_record || world.player[ctop[cid]].protocolExtensionsLevel >= 0;
    for (int i = 0; i < 2; i++) {
        const Team& team = world.teams[i];
        msg.U32(static_cast<unsigned>(team.movement()));
        msg.U32dyn16orU16(team.shots(), e);
        msg.U32dyn16orU16(team.hits(), e);
        msg.U32dyn16orU16(team.shots_taken(), e);
    }
    if (cid == pid_record)
        record_message(msg);
    else
        send_message(cid, msg);
}

void ServerNetworking::send_team_stats(const ServerPlayer& player) const throw () {
    BinaryBuffer<256> msg;
    msg.U8(data_team_stats);
    const bool e = player.protocolExtensionsLevel >= 0;
    for (int i = 0; i < 2; i++) {
        const Team& team = world.teams[i];
        msg.U32dyn8orU8(team.kills(), e);
        msg.U32dyn8orU8(team.deaths(), e);
        msg.U32dyn8orU8(team.suicides(), e);
        msg.U32dyn8orU8(team.flags_taken(), e);
        msg.U32dyn8orU8(team.flags_dropped(), e);
        msg.U32dyn8orU8(team.flags_returned(), e);
    }
    send_message(player.cid, msg);
}

void ServerNetworking::send_quick_map_list(const ServerPlayer& player) const throw () {
    BinaryBuffer<256> msg;
    msg.U8(data_quick_map_list);
    for (int offset = 0; offset < 20 && player.current_map_list_item + offset < host->maplist().size(); ++offset) {
        const MapInfo& map = host->maplist()[player.current_map_list_item + offset];
        if (map.random && map.width < 128 && map.height < 256)
            msg.U16(0x8000 | map.width << 8 | map.height);
        else {
            nAssert(map.infoHash < 0x8000);
            msg.U16(map.infoHash);
        }
    }
    send_message(player.cid, msg);
}

void ServerNetworking::send_map_info(const ServerPlayer& player) const throw () {
    BinaryBuffer<256> msg;
    msg.U8(data_map_list);
    const MapInfo& map = host->maplist()[player.current_map_list_item];
    msg.str(map.title);
    msg.str(map.author);
    msg.U8(map.width);
    msg.U8(map.height);
    msg.U8(map.votes);
    if (map.random)
        msg.U8(map.random);
    send_message(player.cid, msg);
}

void ServerNetworking::send_map_vote(const ServerPlayer& player) const throw () {
    BinaryBuffer<256> msg;
    msg.U8(data_map_vote);
    msg.S8(player.mapVote);
    send_message(player.cid, msg);
}

void ServerNetworking::broadcast_map_votes_update() throw () {
    // check changed votes
    vector<pair<uint8_t, uint8_t> > votes;    // map number and votes
    uint8_t i = 0;
    for (vector<MapInfo>::iterator mi = host->maplist().begin(); mi != host->maplist().end(); ++mi, ++i)
        if (mi->sentVotes != mi->votes) {
            votes.push_back(pair<uint8_t, uint8_t>(i, mi->votes));
            mi->sentVotes = mi->votes;
        }

    if (votes.empty())
        return;

    // build packet
    BinaryBuffer<256> msg;
    msg.U8(data_map_votes_update);
    msg.U8(votes.size());
    for (vector<pair<uint8_t, uint8_t> >::const_iterator vi = votes.begin(); vi != votes.end(); ++vi) {
        msg.U8(vi->first);
        msg.U8(vi->second);
    }

    // send packet
    broadcast_message(msg);
}

//send map time and time left
void ServerNetworking::send_map_time(int cid) const throw () {
    const uint32_t current_time = world.getMapTime() / 10;
    int32_t time_left;
    if (world.getTimeLeft() <= 0) {
        time_left = world.getExtraTimeLeft() / 10;
        if (time_left < 0)
            time_left = 0;
    }
    else
        time_left = world.getTimeLeft() / 10;
    BinaryBuffer<64> msg;
    msg.U8(data_map_time);
    msg.U32(current_time);
    msg.U32(time_left);
    if (cid == pid_all) {
        broadcast_message(msg);
        record_message(msg);
    }
    else
        send_message(cid, msg);
}

void ServerNetworking::send_server_settings(const ServerPlayer& player) const throw () {
    send_server_settings(player.cid);
}

void ServerNetworking::send_server_settings(int cid) const throw () {
    BinaryBuffer<256> msg;
    const WorldSettings& config = world.getConfig();
    const PowerupSettings& pupConfig = world.getPupConfig();
    msg.U8(data_server_settings);
    msg.U8(config.getCaptureLimit());
    msg.U8(config.getTimeLimit() / 600); // note: max time 255 mins ~ 4 hours
    msg.U8(config.getExtraTime() / 600);
    uint16_t settings = 0;
    int i = 0;
    if (config.balanceTeams())
        settings |= (1 << i);
    i++;
    if (pupConfig.pups_drop_at_death)
        settings |= (1 << i);
    i++;
    if (config.getShadowMinimum() == 0)
        settings |= (1 << i);
    i++;
    if (pupConfig.pup_deathbringer_switch)
        settings |= (1 << i);
    i++;
    if (pupConfig.pup_shield_hits == 1)
        settings |= (1 << i);
    i++;
    settings |= (pupConfig.pup_weapon_max << i);
    i += 4; // 4 bits are required to transfer pup_weapon_max, in range [1, 9]
    if (pupConfig.shadow_see_shadow)
        settings |= (1 << i);
    i++;
    if (config.suddenDeath())
        settings |= (1 << i);
    i++;
    nAssert(i <= 16);
    msg.U16(settings);
    msg.U16(pupConfig.pups_min + (pupConfig.pups_min_percentage ? 100 : 0));
    msg.U16(pupConfig.pups_max + (pupConfig.pups_max_percentage ? 100 : 0));
    msg.U16(pupConfig.pup_add_time);
    msg.U16(pupConfig.pup_max_time);
    world.physics.write(msg);
    msg.U16(static_cast<unsigned>(10 * config.flag_return_delay));
    msg.U8(config.getExtraTimePeriods());
    msg.U8(config.getWinScoreDifference());
    /* TODO: 1.0.4 send more settings
       - locked flags?
       - captureable flags?
    */
    if (cid == pid_record)
        record_message(msg);
    else
        send_message(cid, msg);
}

//enqueue a job to the master server to update a client's delta score
void ServerNetworking::client_report_status(int cid) throw () {
    if (!host->rankingLoginSet())
        return;

    ClientData& clid = host->getClientData(cid);

    if (!clid.token_have || !clid.token_valid)
        return;
    if (clid.delta_score == 0 && clid.neg_delta_score == 0)
        return;

    //submit-- create job
    MasterQuery* job = new MasterQuery();
    job->cid = cid;
    job->code = MasterQuery::JT_score;
    job->request = build_http_request(true, g_masterSettings.rankHost(), g_masterSettings.rankDataScript(),
                                      "serial=" + url_encode(host->getRankingID()) +
                                      "&password=" + url_encode(host->getRankingPassword()) +
                                      "&dscp=" + itoa(clid.delta_score) +
                                      "&dscn=" + itoa(clid.neg_delta_score) +
                                      "&name=" + url_encode(world.player[ctop[cid]].name) +
                                      "&token=" + url_encode(clid.token));

    {
        Lock ml(mjob_mutex);
        mjob_count++;
    }
    MemFun1<ServerNetworking, void, MasterQuery*> rmf(this, &ServerNetworking::run_masterjob_thread);
    Thread::startDetachedThread_assert("ServerNetworking::run_masterjob_thread",
                                       rmf, job,
                                       settings.lowerPriority());

    clid.delta_score = 0;
    clid.neg_delta_score = 0;
    clid.fdp = 0.0;
    clid.fdn = 0.0;
}

void ServerNetworking::broadcast_team_message(int team, const string& text) const throw () {
    nAssert(text.length() <= max_chat_message_length + maxPlayerNameLength + 2); // 2 = ": "

    BinaryBuffer<256> msg;
    msg.U8(data_text_message);
    msg.U8(msg_team);
    msg.str(text);
    const ConstDataBlockRef oldProtocolMsg = msg;
    msg.S8(team);

    for (int i = 0; i < maxplayers; i++)
        if (world.player[i].used && i / TSIZE == team)  // only to teammates
            if (world.player[i].protocolExtensionsLevel >= 0)
                send_message(world.player[i].cid, msg);
            else
                send_message(world.player[i].cid, oldProtocolMsg);

    record_message(msg);

    //send to the admin shell
    if (shellssock.isOpen()) {
        BinaryBuffer<256> msg;
        msg.U32(STA_GAME_TEXT);
        msg.U8('.');
        msg.str(text);
        writeToAdminShell(msg);
    }
}

//broadcast message to all players in one screen
void ServerNetworking::broadcast_screen_message(int px, int py, ConstDataBlockRef msg) const throw () {
    for (int i = 0; i < maxplayers; i++)
        if (world.player[i].used && world.player[i].room() == RoomCoords(px, py))
            send_message(world.player[i].cid, msg);

    if (host->recording_active()) {
        BinaryWriter& writer = host->recordMessageWriter();
        writer.U32dyn8(msg.size() + 2);
        writer.block(msg);
        writer.U8(px);
        writer.U8(py);
    }
}

// broadcast message with varargs
void ServerNetworking::bprintf(Message_type type, const char *fs, ...) const throw () {
    va_list argptr;
    char msg[1000];
    va_start(argptr, fs);
    platVsnprintf(msg, 1000, fs, argptr);
    va_end(argptr);

    broadcast_text(type, msg);
}

void ServerNetworking::plprintf(int pid, Message_type type, const char* fmt, ...) const throw () { // bprintf for a single player
    char buf[1000];
    va_list argptr;
    va_start(argptr, fmt);
    platVsnprintf(buf, 1000, fmt, argptr);
    va_end(argptr);
    player_message(pid, type, buf);
}

//send a single message player-printf
void ServerNetworking::player_message(int pid, Message_type type, const string& text) const throw () {
    if (pid >= 0 && !world.player[pid].used)
        return;
    if (text.length() <= max_chat_message_length + maxPlayerNameLength + 2) {    // 2 = ": "
        BinaryBuffer<256> msg;
        msg.U8(data_text_message);
        msg.U8(type);
        msg.str(text);
        const ConstDataBlockRef oldProtocolMsg = msg;
        if (type == msg_normal || type == msg_team) // It should really never be a team message in this method.
            msg.S8(pid / TSIZE);
        if (pid == pid_record)
            record_message(msg);
        else if (pid == shell_pid) {
            if (shellssock.isOpen()) {
                BinaryBuffer<256> msg;
                msg.U32(STA_GAME_TEXT);
                msg.str(text);
                writeToAdminShell(msg);
            }
        }
        else if (pid == pid_all) {
            for (int i = 0; i < maxplayers; ++i)
                if (world.player[i].used)
                    if (world.player[i].protocolExtensionsLevel >= 0)
                        send_message(world.player[i].cid, msg);
                    else
                        send_message(world.player[i].cid, oldProtocolMsg); // don't send the possible team info
            record_message(msg);
        }
        else if (world.player[pid].protocolExtensionsLevel >= 0)
            send_message(world.player[pid].cid, msg);
        else
            send_message(world.player[pid].cid, oldProtocolMsg);
    }
    else {
        vector<string> lines = split_to_lines(text, 79, 4); // this makes more sense than splitting to max_chat_message_length and letting it get split again on the client end
        for (vector<string>::const_iterator li = lines.begin(); li != lines.end(); ++li)
            player_message(pid, type, *li);
    }
}

void ServerNetworking::broadcast_text(Message_type type, const string& text) const throw () {
    player_message(pid_all, type, text);
    if (shellssock.isOpen()) {
        ExpandingBinaryBuffer msg;
        msg.U32(STA_GAME_TEXT);
        msg.str(text);
        writeToAdminShell(msg);
    }
}

void ServerNetworking::send_map_change_message(int pid, int reason, const char* mapname) const throw () {
    //send a show gameover plaque message, if that is the case
    if (reason != NEXTMAP_NONE) {
        BinaryBuffer<256> ext_msg;
        ext_msg.U8(data_gameover_show);
        ext_msg.U8(reason);      //capture limit plaque or vote exit plaque
        BinaryBuffer<256> old_msg = ext_msg;
        if (reason == NEXTMAP_CAPTURE_LIMIT || reason == NEXTMAP_VOTE_EXIT) {
            ext_msg.U32dyn8(world.teams[0].score());
            ext_msg.U32dyn8(world.teams[1].score());
            ext_msg.U8(world.getConfig().getCaptureLimit());
            ext_msg.U8(world.getConfig().getTimeLimit() / 600); // note: max time 255 mins ~ 4 hours
            old_msg.U8(min(world.teams[0].score(), 255));
            old_msg.U8(min(world.teams[1].score(), 255));
            old_msg.U8(world.getConfig().getCaptureLimit());
            old_msg.U8(world.getConfig().getTimeLimit() / 600); // note: max time 255 mins ~ 4 hours
        }
        if (pid == pid_record)
            record_message(ext_msg);
        else if (pid == pid_all) {
            for (int i = 0; i < maxplayers; i++)
                if (world.player[i].used)
                    send_message(world.player[i].cid, world.player[i].protocolExtensionsLevel >= 0 ? ext_msg : old_msg);
            record_message(ext_msg);
        }
        else
            send_message(world.player[pid].cid, world.player[pid].protocolExtensionsLevel >= 0 ? ext_msg : old_msg);
    }

    BinaryBuffer<256> msg;
    msg.U8(data_map_change);

    msg.U16(world.map.crc);
    msg.str(mapname);
    msg.str(world.map.title);
    msg.U8(host->current_map_nr());
    msg.U8(host->maplist().size());

    int8_t remove_flags = 0;
    remove_flags |= (world.map.tinfo[0].flags.empty() ? 0x01 : 0);
    remove_flags |= (world.map.tinfo[1].flags.empty() ? 0x02 : 0);
    remove_flags |= (world.map.wild_flags    .empty() ? 0x04 : 0);
    msg.S8(remove_flags);

    if (pid == pid_record) {
        ExpandingBinaryBuffer recMsg;
        recMsg.block(msg);
        recMsg.U32(host->record_map_data().length());
        recMsg.block(host->record_map_data());
        record_message(recMsg);
        return;
    }
    else if (pid == pid_all) {
        broadcast_message(msg);
        if (shellssock.isOpen()) {
            BinaryBuffer<256> msg;
            msg.U32(STA_GAME_OVER);
            writeToAdminShell(msg);
            sendTextToAdminShell("Map is " + host->current_map().title);
        }
    }
    else
        send_message(world.player[pid].cid, msg);

    //VERY IMPORTANT: flags the player as "awaiting map load" - client must confirm map to proceed
    if (pid == pid_all) {
        for (int i = 0; i < maxplayers; ++i)
            ++world.player[i].awaiting_client_readies;
    }
    else
        ++world.player[pid].awaiting_client_readies;
}

void ServerNetworking::broadcast_map_change_message(int reason, const char* mapname) const throw () {
    send_map_change_message(pid_all, reason, mapname);
}

void ServerNetworking::broadcast_map_change_info(int votes, int needed, int vote_block_time) const throw () {
    BinaryBuffer<256> msg;
    msg.U8(data_map_change_info);
    msg.U8(votes);
    msg.U8(needed);
    msg.U16(vote_block_time);

    broadcast_message(msg);
}

void ServerNetworking::send_too_much_talk(int pid) const throw () {
    send_simple_message(data_too_much_talk, pid);
}

void ServerNetworking::send_mute_notification(int pid) const throw () {
    send_simple_message(data_mute_notification, pid);
}

void ServerNetworking::send_ranking_update_failed(int pid) const throw () {
    send_simple_message(data_ranking_update_failed, pid);
}

void ServerNetworking::broadcast_mute_message(int pid, int mode, const string& admin, bool inform_target) const throw () {
    BinaryBuffer<256> msg;
    msg.U8(data_player_mute);
    msg.U8(pid);
    msg.U8(mode);
    msg.str(admin);

    for (int i = 0; i < maxplayers; i++)
        if (world.player[i].used && (inform_target || i != pid))
            send_message(world.player[i].cid, msg);
}

void ServerNetworking::broadcast_kick_message(int pid, int minutes, const string& admin) const throw () {
    nAssert(minutes >= 0);
    BinaryBuffer<256> msg;
    msg.U8(data_player_kick);
    msg.U8(pid);
    msg.U32(minutes);
    msg.str(admin);

    broadcast_message(msg);
    record_message(msg);
}

void ServerNetworking::send_idlekick_warning(int pid, int seconds) const throw () {
    BinaryBuffer<256> msg;
    msg.U8(data_idlekick_warning);
    msg.U8(seconds);
    send_message(world.player[pid].cid, msg);
}

void ServerNetworking::send_disconnecting_message(int pid, int seconds) const throw () {
    BinaryBuffer<256> msg;
    msg.U8(data_disconnecting);
    msg.U8(seconds);
    send_message(world.player[pid].cid, msg);
}

void ServerNetworking::broadcast_broken_map() const throw () {
    broadcast_simple_message(data_broken_map);
}

void ServerNetworking::set_relay_server(const string& address) throw () {
    if (!relay_address.tryResolve(address)) {
        log("Relay address could not be resolved from %s.", address.c_str());
        return;
    }
    const uint16_t port = relay_address.getPort();
    if (port == 0)
        log("Invalid or missing relay port in %s.", address.c_str());
    else
        log("Relay server set to %s.", address.c_str());
}

string ServerNetworking::get_relay_server() const throw () {
    return is_relay_used() ? relay_address.toString() : string();
}

bool ServerNetworking::is_relay_used() const throw () {
    return relay_address.valid() && relay_address.getPort() != 0;
}

bool ServerNetworking::is_relay_active() const throw () {
    return relayThread.isConnected();
}

void ServerNetworking::send_first_relay_data(ConstDataBlockRef data) throw () {
    relayThread.startNewGame(relay_address, data, settings.get_spectating_delay());
}

void ServerNetworking::send_relay_data(ConstDataBlockRef data) throw () {
    relayThread.pushFrame(data);
}

bool ServerNetworking::start() throw () {
    file_threads_quit = false;

    for (int i = 0; i < MAX_CLIENTS; ++i)
        ctop[i] = -1;
    player_count = 0;
    bot_count = 0;

    max_world_rank = 0;

    ping_send_client = 0;
    for (int i = 0; i < MAX_CLIENTS; ++i)
        fileTransfer[i].reset();

    server_identification = itoa(abs(rand()));

    // start leetnet
    leetServer = new_server_c(settings.networkPriority(), settings.minLocalPort(), settings.maxLocalPort());

    leetServer->setExtendedQueryCallback(sfunc_extended_query);
    leetServer->setHelloCallback(sfunc_leetnet_client_hello);
    leetServer->setConnectedCallback(sfunc_leetnet_client_connected);
    leetServer->setDisconnectedCallback(sfunc_client_disconnected);
    leetServer->setDataCallback(sfunc_client_data);
    leetServer->setLagStatusCallback(sfunc_client_lag_status);
    leetServer->setPingResultCallback(sfunc_client_ping_result);

    leetServer->setCallbackCustomPointer(this);

    leetServer->set_client_timeout(5, 10);

    if (!leetServer->start(settings.get_port())) {
        log.error(_("Can't start network server on port $1.", itoa(settings.get_port())));
        return false;
    }

    //v0.4.4 reset master jobs count
    mjob_count = 0;
    mjob_exit = false;              //flag for all pending master jobs to quit now
    mjob_fastretry = false;     //flag for all pending master jobs to stop waiting and retry immediately

    //start TCP shell master thread in the port number 500 less than server UDP port
    shellmthread.start_assert("ServerNetworking::run_shellmaster_thread",
                              MemFun1<ServerNetworking, void, int>(this, &ServerNetworking::run_shellmaster_thread), settings.get_srvmonit_port(),
                              settings.lowerPriority());

    //start TCP thread for talking with master server
    mthread.start_assert("ServerNetworking::run_mastertalker_thread",
                         MemFun0<ServerNetworking, void>(this, &ServerNetworking::run_mastertalker_thread),
                         settings.lowerPriority());

    //start website thread
    webthread.start_assert("ServerNetworking::run_website_thread",
                           MemFun0<ServerNetworking, void>(this, &ServerNetworking::run_website_thread),
                           settings.lowerPriority());

    relayThread.start(settings.lowerPriority());

    return true;
}

//update serverinfo
void ServerNetworking::update_serverinfo() throw () {
    //v0.4.8 UGLY FIX : count all players again, check for discrepancy
    int pc = 0;
    for (int i = 0; i < maxplayers; i++)
        if (world.player[i].used)
            pc++;
    nAssert(pc == player_count);

    ostringstream info;
    if (!settings.get_server_password().empty())
        info << "P ";
    else if (settings.dedicated())
        info << "D ";
    else
        info << "  ";
    info << setw(2) << get_human_count() << '/' << setw(2) << std::left << maxplayers << std::right << ' ' << setw(7) << getVersionString(true, 7, 8, true) << ' ' << settings.get_hostname();
    leetServer->set_server_info(info.str().c_str());
}

int ServerNetworking::client_connected(int cid, int customStoredData) throw () {
    nAssert(cid >= 0 && cid < MAX_CLIENTS);

    addPlayerMutex.lock();

    if (player_count >= maxplayers) {
        host->remove_bot();
        nAssert(player_count == maxplayers - 1);
    }

    //2TEAM: check wich team to put player
    int t1 = 0;     //red team count
    int t2 = 0;     //blue team count
    int red_humans = 0, blue_humans = 0;
    for (int i = 0; i < maxplayers; i++)
        if (world.player[i].used) {
            if (i / TSIZE == 0) {
                t1++;
                if (!world.player[i].is_bot())
                    red_humans++;
            }
            else {
                t2++;
                if (!world.player[i].is_bot())
                    blue_humans++;
            }
        }

    // put on red team, blue team, or randomize if same # of players in both teams
    int targ;
    if (t1 < t2)
        targ = 0;
    else if (t1 > t2)
        targ = TSIZE;
    else {
        const int red_bots = t1 - red_humans;
        const int blue_bots = t2 - blue_humans;
        // If only one team contains humans and also bots, put the new player there.
        if (red_humans > 0 && red_bots > 0 && blue_humans == 0)
            targ = 0;
        else if (blue_humans > 0 && blue_bots > 0 && red_humans == 0)
            targ = TSIZE;
        // Otherwise put the player to a team with less number of humans.
        else if (red_humans < blue_humans)
            targ = 0;
        else if (red_humans > blue_humans)
            targ = TSIZE;
        // Both teams contain the same number of humans and bots.
        else {
            host->refresh_team_score_modifiers();
            targ = TSIZE * host->getLessScoredTeam();
        }
    }

    // alloc new player : scans only slots of the team (up from targ)
    int myself = -1;

    for (int i = targ; i < targ + TSIZE; i++)
        if (!world.player[i].used) {
            myself = i;
            break;
        }

    nAssert(myself != -1);
    ctop[cid] = myself;

    // send players_present before "myself" is present, so new_player can be broadcast to "myself" too
    uint32_t players_present = 0;
    for (int i = 0; i < maxplayers; i++)
        if (world.player[i].used)
            players_present |= (1 << i);
    BinaryBuffer<8> msg;
    msg.U8(data_players_present);
    msg.U32(players_present);
    send_message(cid, msg);

    unsigned uniqueId;
    if (!freedUniqueIds.empty() && freedUniqueIds.front().second < get_time()) {
        uniqueId = freedUniqueIds.front().first;
        freedUniqueIds.pop();
    }
    else
        uniqueId = ++newUniqueId;
    world.player[myself].clear(true, myself, cid, "", myself / TSIZE, uniqueId);
    world.player[myself].protocolExtensionsLevel = min(customStoredData, PROTOCOL_EXTENSIONS_VERSION);

    addPlayerMutex.unlock();

    ++player_count;
    nAssert(reservedPlayerSlots);
    --reservedPlayerSlots;

    world.player[myself].localIP = isLocalIP(get_client_address(cid));
    if (world.player[myself].localIP)
        ++localPlayers;
    else {
        Network::Address ip = get_client_address(cid);
        ip.setPort(0);
        vector< pair<Network::Address, int> >::iterator pi;
        for (pi = distinctRemotePlayers.begin(); pi != distinctRemotePlayers.end(); ++pi)
            if (pi->first == ip) {
                ++pi->second;
                break;
            }
        if (pi == distinctRemotePlayers.end())
            distinctRemotePlayers.push_back(pair<Network::Address, int>(ip, 1));
    }

    const vector<string>& welcome_message = host->getWelcomeMessage();
    for (vector<string>::const_iterator line = welcome_message.begin(); line != welcome_message.end(); ++line)
        player_message(myself, msg_server, *line);

    send_map_change_message(myself, NEXTMAP_NONE, host->getCurrentMapFile().c_str());

    broadcast_new_player(world.player[myself]);

    // can't abort from this point on... anything that can abort should be above

    world.player[myself].respawn_to_base = true;
    world.respawnPlayer(myself, true);    // move to a spawn spot to wait for the game
    world.resetPlayer(myself, -1e10);   // kill; negative delay to cancel default delay, so that the player spawns as soon as he is ready; no need to tell clients because of suppressing the spawn message above
    world.player[myself].respawn_to_base = true;    // New players always spawn in the base.
    // don't actually spawn until the client has loaded the map and is in the game

    host->resetClient(cid);
    world.player[myself].stats().set_lifetime(0);

    //first update the ADMIN SHELL
    if (shellssock.isOpen()) {
        BinaryBuffer<256> msg;
        msg.U32(STA_PLAYER_CONNECTED);
        msg.U32(world.player[myself].cid);
        writeToAdminShell(msg);
    }

    host->check_fav_colors(myself);

    //update the player with world information

    //  - who is he (player #)
    send_me_packet(myself);

    if (world.player[myself].protocolExtensionsLevel >= 0) {
        send_acceleration_modes(myself);
        send_flag_modes(myself);
    }

    // - world ctf flags information
    ctf_net_flag_status(cid, 0);
    ctf_net_flag_status(cid, 1);
    ctf_net_flag_status(cid, 2);

    // - all other players' names
    // - all other players' frags

    for (int i = 0; i < maxplayers; i++) {
        if (!world.player[i].used)
            continue;
        if (i == myself)
            continue;

        send_player_name_update(cid, i);

        //frags update
        BinaryBuffer<256> msg;
        msg.U8(data_frags_update);
        msg.U8(i);       // what player id
        msg.U32(world.player[i].stats().frags());
        send_message(cid, msg);

        send_player_crap_update(cid, i);
    }

    send_server_settings(world.player[myself]);
    send_map_time(cid);
    send_stats(world.player[myself]);
    send_team_stats(world.player[myself]);

    if (player_count == 2) {
        host->ctf_game_restart();
        world.reset_time();
        sendStartGame();
    }

    host->check_team_changes();
    update_serverinfo();
    world.check_powerup_creation(false);
    host->set_check_bots();

    return myself;
}

void ServerNetworking::client_disconnected(int cid) throw () {
    if (ctop[cid] == -1)
        return;

    //what player
    const int pid = ctop[cid];

    const bool was_bot = world.player[pid].is_bot();

    //first update the ADMIN SHELL
    if (shellssock.isOpen()) {
        BinaryBuffer<256> msg;
        msg.U32(STA_PLAYER_DISCONNECTED);
        msg.U32(world.player[pid].cid);
        writeToAdminShell(msg);
    }

    //report the latest player achievements to the master server
    client_report_status(cid);

    if (world.player[pid].localIP)
        --localPlayers;
    else {
        Network::Address ip = get_client_address(cid);
        ip.setPort(0);
        vector< pair<Network::Address, int> >::iterator pi;
        for (pi = distinctRemotePlayers.begin(); pi->first != ip; ++pi)
            nAssert(pi != distinctRemotePlayers.end());
        --pi->second;
        if (pi->second == 0)
            distinctRemotePlayers.erase(pi);
    }

    freedUniqueIds.push(make_pair(world.player[pid].uniqueId, get_time() + 5 * 60.));

    fileTransfer[cid].reset();
    host->game_remove_player(pid, true);
    --player_count;
    if (was_bot)
        --bot_count;

    nAssert(clientConnection[cid]);
    delete clientConnection[cid];
    clientConnection[cid] = 0;

    broadcast_player_left(world.player[pid]);

    host->check_team_changes();
    host->check_map_exit();

    //update serverinfo
    update_serverinfo();

    host->set_check_bots();
}

//client ping result
void ServerNetworking::ping_result(int client_id, int ping_time) throw () {
    if (ctop[client_id] == -1)
        return;
    world.player[ctop[client_id]].ping = ping_time;
}

void ServerNetworking::forwardSayadminMessage(int cid, const string& message) const throw () {
    if (!shellssock.isOpen())
        return;
    BinaryBuffer<256> msg;
    msg.U32(STA_ADMIN_MESSAGE);
    msg.U32(cid);
    msg.str(message);
    writeToAdminShell(msg);
}

void ServerNetworking::sendTextToAdminShell(const string& text) const throw () {
    if (!shellssock.isOpen())
        return;
    ExpandingBinaryBuffer msg;
    msg.U32(STA_GAME_TEXT);
    msg.constLengthStr("| ", 2);
    msg.str(text);
    writeToAdminShell(msg);
}

void ServerNetworking::processMessage(int pid, ConstDataBlockRef data) throw (ClientDataError) {
 try {
    BinaryDataBlockReader msg(data);

    ServerPlayer& sender = world.player[pid];

    const uint8_t code = msg.U8();
    if (LOG_MESSAGE_TRAFFIC)
        log("Message from client, code = %i", code);
    switch (code) {
    /*break;*/ case data_name_update: {
        const string name = msg.str();
        const string password = msg.str();
        host->nameChange(sender.cid, pid, name, password);
    }
    break; case data_text_message: {
        const string text = msg.str();
        if (find_nonprintable_char(text)) {
            log("Received unprintable characters.");
            throw ClientDataError();
        }
        else if (text.length() > max_chat_message_length) {
            log("Received a too long message (%u characters).", static_cast<unsigned>(text.length()));
            throw ClientDataError();
        }
        else
            host->chat(pid, text);
    }
    break; case data_fire_on:
        sender.attackOnce = sender.attack = true;
        sender.attackGunDir = sender.gundir;
    break; case data_fire_off:
        sender.attack = false;
    break; case data_suicide:
        if (!sender.under_deathbringer_effect(get_time()))
            world.suicide(pid);
    break; case data_change_team_on:
        if (!sender.want_change_teams) {
            sender.want_change_teams = true;
            host->check_team_changes();
        }
    break; case data_change_team_off:
        sender.want_change_teams = false;
    break; case data_client_ready:
        #ifdef EXTRA_DEBUG
        nAssert(sender.awaiting_client_readies); // this is an abnormal condition, but it could be the client's fault so normally we can ignore it
        #endif
        if (sender.awaiting_client_readies)
            --sender.awaiting_client_readies;
    break; case data_map_exit_on:
        if (sender.want_map_exit == false) {
            sender.want_map_exit = true;
            // Make sure that this message matches with the one in guiclient.cpp.
            if (host->specific_map_vote_required() && sender.mapVote == -1)
                player_message(pid, msg_server, "Your vote has no effect until you vote for a specific map.");
            host->check_map_exit();
        }
    break; case data_map_exit_off:
        if (sender.want_map_exit == true) {
            sender.want_map_exit = false;
            host->check_map_exit();
        }
    break; case data_file_request: {
        const string ftype = msg.str();
        const string fname = msg.str();
        if (fileTransfer[sender.cid].serving_udp_file) {
            log("Another download already in progress.");
            throw ClientDataError();
        }
        else {
            //alloc to download
            fileTransfer[sender.cid].serving_udp_file = true;
            fileTransfer[sender.cid].data = get_download_file(ftype, fname);
            if (fileTransfer[sender.cid].data.empty()) {
                log("Invalid download attempt");
                throw ClientDataError();
            }
            else {
                fileTransfer[sender.cid].dp = 0;
                upload_next_file_chunk(sender.cid);
            }
        }
    }
    break; case data_file_ack:
        if (fileTransfer[sender.cid].dp >= fileTransfer[sender.cid].data.size()) {
            //no more data, this was the last ack. close stuff
            fileTransfer[sender.cid].reset();   //reset the download data structs
            //the client will carry on from here
        }
        else {
            //send next
            upload_next_file_chunk(sender.cid);
        }
    break; case data_old_registration_token:
        // just ignore
    break; case data_registration_token: {
        const string tok = msg.str();
        if (host->changeRegistration(sender.cid, tok)) {
            MasterQuery *job = new MasterQuery();
            job->cid = sender.cid;
            if (host->rankingLoginSet()) {
                job->code = MasterQuery::JT_login;
                job->request = build_http_request(true, g_masterSettings.rankHost(), g_masterSettings.rankDataScript(),
                                                  "serial=" + url_encode(host->getRankingID()) +
                                                  "&password=" + url_encode(host->getRankingPassword()) +
                                                  "&dscp=0" +
                                                  "&dscn=0" +
                                                  "&name=" + url_encode(sender.name) +
                                                  "&token=" + url_encode(tok));
            }
            else {
                job->code = MasterQuery::JT_nameCheck;
                job->request = build_http_request(true, g_masterSettings.rankHost(), g_masterSettings.rankDataScript(),
                                                  "name=" + url_encode(sender.name) +
                                                  "&token=" + url_encode(tok));
            }

            {
                Lock ml(mjob_mutex);
                mjob_count++;
            }

            MemFun1<ServerNetworking, void, MasterQuery*> rmf(this, &ServerNetworking::run_masterjob_thread);
            Thread::startDetachedThread_assert("ServerNetworking::run_masterjob_thread",
                                               rmf, job,
                                               settings.lowerPriority());
        }
    }
    break; case data_old_tournament_participation:
        // just ignore
    break; case data_ranking_participation: {
        const uint8_t data = msg.U8();
        ClientData& clid = host->getClientData(sender.cid);
        clid.next_participation = data;
        if (!clid.participation_info_received) {
            clid.current_participation = clid.next_participation;
            clid.participation_info_received = true;
            broadcast_player_crap(pid);
        }
    }
    break; case data_drop_flag:
        sender.drop_key = true;
        if (!sender.under_deathbringer_effect(get_time())) {
            sender.dropped_flag = true;
            world.dropFlagIfAny(pid, true);
        }
    break; case data_stop_drop_flag:
        sender.drop_key = false;
    break; case data_map_vote: {
        const uint8_t vote = msg.U8();
        if (sender.mapVote != vote) {
            if (vote < 255 && vote < static_cast<int>(host->maplist().size()))
                sender.mapVote = vote;
            else {
                sender.mapVote = -1;
                // Make sure that this message matches with the one in guiclient.cpp.
                if (host->specific_map_vote_required() && sender.want_map_exit)
                    player_message(pid, msg_server, "Your vote has no effect until you vote for a specific map.");
            }
            host->check_map_exit();
        }
    }
    break; case data_negative_map_votes:
        sender.negativeMapVotes.clear();
        while (msg.hasMore()) {
            uint8_t mask = msg.U8();
            for (int i = 0; i < 8; ++i, mask >>= 1)
                sender.negativeMapVotes.push_back(mask & 1);
        }
    break; case data_fav_colors: {
        const int8_t size = msg.S8();
        vector<char> fav_colors;
        // two colours in a byte
        for (int i = 0; i < size; i++) {
            const uint8_t cols = msg.U8();
            fav_colors.push_back(cols & 0x0F);
            if (++i < size)
                fav_colors.push_back(cols >> 4);
        }
        host->set_fav_colors(pid, fav_colors);
        broadcast_player_crap(pid);
    }
    break; case data_bot: {
        Network::Address address = get_client_address(sender.cid);
        address.setPort(0);
        const string addrStr = address.toString();
        if (addrStr != "127.0.0.1")
            log("Remote bot from %s.", addrStr.c_str());
        if (!sender.is_bot()) {
            ++bot_count;
            sender.set_bot();
            update_serverinfo();
        }
    }
    break; case data_set_minimap_player_bandwidth:
        sender.minimapPlayersPerFrame = msg.U8();
    break; default:
        if (code < data_reserved_range_first || code > data_reserved_range_last) {
            log("Invalid message code: %i, length %i.", code, data.size());
            throw ClientDataError();
        }
        // just ignore commands in reserved range: they're probably some extension we don't have to care about
    }
 } catch (BinaryReader::ReadError) {
     log("Invalid data from client.");
     throw ClientDataError();
 }
}

//process incoming client data (callback function)
void ServerNetworking::incoming_client_data(int cid, ConstDataBlockRef data) throw () {
    if (ctop[cid] == -1)
        return;

    int pid = ctop[cid];

 try {
  try {
    //1. process client's frame data

    BinaryDataBlockReader frame(data);

    const uint8_t clFrame = frame.U8();

    ServerPlayer& pl = world.player[pid];
    if (WATCH_CONNECTION && pl.lastClientFrame != clFrame) {
        if (static_cast<uint8_t>(pl.lastClientFrame - clFrame) < 128)
            plprintf(pid, msg_warning, "C>S packet order: prev %d this %d", pl.lastClientFrame, clFrame);
        else if (static_cast<uint8_t>(pl.lastClientFrame + 1) != clFrame)
            plprintf(pid, msg_warning, "C>S packet lost : prev %d this %d", pl.lastClientFrame, clFrame);
    }
    if (static_cast<uint8_t>(clFrame - pl.lastClientFrame) < 128) { // this frame is very likely newer or the same as the previous one
        if (clFrame != pl.lastClientFrame) {
            g_timeCounter.refresh(); // we prefer an exact time here
            pl.frameOffset = 10. * (get_time() - frameSentTime);
        }
        pl.lastClientFrame = clFrame;

        const uint8_t controlByte = frame.U8();
        ClientControls controls;
        controls.fromNetwork(controlByte, false);
        controls.clearModifiersIfIdle();
        if (pl.controls != controls) {
            pl.controls = controls;
            pl.record_controls = true;
        }

        GunDirection newDir;
        bool newDirReceived = false;
        if (frame.hasMore()) {
            const uint16_t gd = frame.U16();
            pl.accelerationMode = (gd & 0x800) != 0 && world.physics.allowFreeTurning ? AM_Gun : AM_World;
            newDir.fromNetworkLongForm(gd & 0x7FF);
            newDirReceived = true;
        }
        else
            pl.accelerationMode = AM_World;
        if (!pl.dead) {
            if (world.physics.allowFreeTurning && newDirReceived) {
                if (newDir != pl.gundir) {
                    pl.gundir = newDir;
                    pl.record_gundir = true;
                }
            }
            else
                if (!pl.controls.isStrafe()) {
                    newDir = pl.gundir;
                    newDir.updateFromControls(pl.controls);
                    if (newDir != pl.gundir) {
                        pl.gundir = newDir;
                        pl.record_gundir = true;
                    }
                }
        }
    }

    //2. process messages
    for (;;) {
        ConstDataBlockRef msg = clientConnection[cid]->receive_message();
        if (msg.data() == 0)
            break;
        try {
            processMessage(pid, msg);
        } catch (...) {
            clientConnection[cid]->received_message_read();
            throw;
        }
        clientConnection[cid]->received_message_read();
        pid = ctop[cid]; // the message might have affected the pid
    }
    if (!world.player[pid].attackOnce) // if the player started holding attack before this frame, he wants to shoot in the new direction, otherwise keep the direction when he started
        world.player[pid].attackGunDir = world.player[pid].gundir;
  } catch (BinaryReader::ReadError) {
    log("Format error in frame data received from client.");
    throw ClientDataError();
  }
 } catch (ClientDataError) {
    log("Kicked player %d for client misbehavior.", pid);
    host->disconnectPlayer(pid, disconnect_client_misbehavior);
 } 
}

void ServerNetworking::removePlayer(int pid) throw () {  // call only when moving players around; this actually does close to nothing
    ctop[world.player[pid].cid] = -1;
}

void ServerNetworking::disconnect_client(int cid, int timeout, Disconnect_reason reason) throw () {
    clientConnection[cid]->disconnect(timeout, reason);
}

void ServerNetworking::sendWorldReset() const throw () {
    broadcast_simple_message(data_world_reset);
}

void ServerNetworking::sendStartGame() const throw () {
    broadcast_simple_message(data_start_game);
    send_map_time(pid_all);
}

void ServerNetworking::writeMinimapPlayerPosition(BinaryWriter& writer, int pid) const throw () {
    const ServerPlayer& pl = world.player[pid];
    nAssert(pl.used);
    const int xmul = 255 / world.map.w;
    const int ymul = 255 / world.map.h;
    writer.U8(pl.room().x * xmul + static_cast<uint8_t>(xmul * (pl.pos.x - 1e-5) / plw));
    writer.U8(pl.room().y * ymul + static_cast<uint8_t>(ymul * (pl.pos.y - 1e-5) / plh));
}

//simulate and broadcast frame
void ServerNetworking::broadcast_frame(bool gameRunning) throw () {
    {
        // check if player acceleration modes have changed
        uint32_t newMask = 0;
        if (world.physics.allowFreeTurning)
            for (int i = 0; i < maxplayers; ++i)
                if (world.player[i].used && world.player[i].accelerationMode == AM_Gun)
                    newMask |= uint32_t(1) << i;
        if (newMask != accelerationModeMask) {
            accelerationModeMask = newMask;
            send_acceleration_modes(pid_all);
        }
    }
    {
        // check if flag lock/capture settings (and similar) have changed
        const uint8_t newMask = world.getConfig().see_minimap_player_properties << 6 |
                                world.carry_own_team_flag()             << 5 |
                                world.capture_away_from_base()          << 4 |
                                world.      lock_team_flags_in_effect() << 3 |
                                world.      lock_wild_flags_in_effect() << 2 |
                                world.capture_on_team_flags_in_effect() << 1 |
                                world.capture_on_wild_flags_in_effect();
        if (newMask != flagModeMask) {
            flagModeMask = newMask;
            send_flag_modes(pid_all);
        }
    }
    if (host->rankingLoginSet() != rankingLoginSetPreviously) { // check if ranking enable/disable has changed
        rankingLoginSetPreviously = host->rankingLoginSet();
        for (int pid = 0; pid < maxplayers; ++pid) {
            if (!world.player[pid].used)
                continue;
            const ClientData& clid = host->getClientData(world.player[pid].cid);
            if (clid.token_have && clid.current_participation) // else, the client's ranking-participation both was and is disabled
                broadcast_player_crap(pid);
        }
    }

    // ============================
    //   build common data buffer
    // ============================
    BinaryBuffer<4096> frame;       //common frame data

    //frame
    frame.U32(world.frame);

    //===============================
    //  build packet for each client
    //      with custom data
    //===============================
    static int normalViewI[2] = { 0, 0 };   // each team's normal view player iterator
    static int shadowViewI[2] = { 0, 0 };   // each team's shadow view player iterator
    uint32_t normalView[2];  // players shown on minimap to each team, without shadow
    uint32_t shadowView[2];  // players shown on minimap to each team, with shadow

    for (int t = 0; t < 2; ++t) {
        normalView[t] = shadowView[t] = 0;
        for (int i = 0; i < maxplayers; ++i) {
            if (!world.player[i].used || world.player[i].dead)  // dead enemies aren't seen, dead teammates don't see
                continue;
            if (i / TSIZE == t) {
                normalView[t] |= 1 << i;    // teammates always visible
                const int oppTeamStart = (1 - i / TSIZE) * TSIZE;
                for (int j = oppTeamStart; j < oppTeamStart + TSIZE; ++j)   // find out who this teammate sees (who are in the same room and visible)
                    if (world.player[j].used && !world.player[j].dead &&
                        (world.player[j].room() == world.player[i].room() && (world.player[j].visibility > 10 || world.player[j].stats().has_flag()) ||
                         world.getConfig().always_send_flag_location && world.player[j].stats().has_flag()))
                    {
                        normalView[t] |= 1 << j;
                    }
            }
            else if (world.getPupConfig().shadow_see_shadow || !world.player[i].item_shadow() || world.player[i].stats().has_flag())
                shadowView[t] |= 1 << i;
        }
        shadowView[t] |= normalView[t];
    }

    for (int t = 0; t < 2; ++t) {
        for (vector<Flag>::const_iterator fi = world.teams[t].flags().begin(); fi != world.teams[t].flags().end(); ++fi)
            if (fi->carried()) {
                normalView[!t] |= 1 << fi->carrier();
                shadowView[!t] |= 1 << fi->carrier();
            }
    }

    // send 2 players' coordinates each frame; pick those two for each team both with and without shadow
    int normalIters[2][2];  // [team][number]
    int shadowIters[2][2];  // [team][number]
    for (int round = 0; round < 2; ++round) {
        for (int t = 0; t < 2; ++t) {
            if (round >= settings.minimapSendLimit()) {
                normalIters[t][round] = shadowIters[t][round] = -1;
                continue;
            }
            if (normalView[t] == 0) // no visible players
                normalViewI[t] = -1;
            else
                do {
                    if (++normalViewI[t] == maxplayers)
                        normalViewI[t] = 0;
                } while (!(normalView[t] & (1 << normalViewI[t])));
            if (shadowView[t] == 0) // no visible players
                shadowViewI[t] = -1;
            else
                do {
                    if (++shadowViewI[t] == maxplayers)
                        shadowViewI[t] = 0;
                } while (!(shadowView[t] & (1 << shadowViewI[t])));
            normalIters[t][round] = normalViewI[t];
            shadowIters[t][round] = shadowViewI[t];
        }
    }
    for (int t = 0; t < 2; ++t) {
        if (normalIters[t][1] == normalIters[t][0])
            normalIters[t][1] = -1;
        if (shadowIters[t][1] == shadowIters[t][0])
            shadowIters[t][1] = -1;
    }

    const unsigned commonDataSize = frame.size();

    // ==================================================================
    //   BUILD AND SEND EVERY DAMN PACKET
    // ==================================================================
    for (int i = 0; i < maxplayers; i++) {
        ServerPlayer& recipient = world.player[i];

        if (!recipient.used)
            continue;

        // start writing at end of common data
        frame.setPosition(commonDataSize);

        // first send client prediction synchronization data
        frame.U8(recipient.lastClientFrame);

        frame.U8(static_cast<uint8_t>(bound<double>(recipient.frameOffset, 0., .999) * 256.));

        const bool skip_frame = recipient.awaiting_client_readies || !gameRunning;

        // first byte: player ID, tob bits of health and energy and a bit telling if the rest of the frame is skipped
        uint8_t xtra = i << 3;
        if (iround(recipient.health) & 256)
            xtra |= 1;
        if (iround(recipient.energy) & 256)
            xtra |= 2;
        if (skip_frame)
            xtra |= 4;
        frame.U8(xtra);

        // send almost empty frame if client not ready (leave bandwidth for data transfer) or if server showing gameover plaque
        if (!skip_frame) {
            // 2 bytes with the screen of self
            frame.U8(recipient.room().x);
            frame.U8(recipient.room().y);

            vector< pair<int, PlayerBase::RemotelyVisiblePropertiesData> > newProperties;

            // player data field to indicate which players are on screen (and therefore sent on the frame)
            uint32_t players_onscreen = 0;

            // players_onscreen will be written here in the end
            const unsigned players_onscreen_position = frame.getPosition();
            frame.U32(0);

            for (int j = 0; j < maxplayers; j++) {
                const ServerPlayer& h = world.player[j];
                if (!h.used || h.room() != recipient.room())
                    continue;
                const bool forceVisible = recipient.item_shadow() && world.getPupConfig().shadow_see_shadow && &h != &recipient;
                if (h.visibility == 0 && i / TSIZE != j / TSIZE && !h.stats().has_flag() && !forceVisible)
                    continue;

                players_onscreen |= (1 << j);

                // position in 3 bytes
                const int hx = iround(h.pos.x * (double(0xFFF) / plw));
                const int hy = iround(h.pos.y * (double(0xFFF) / plh));
                frame.U8(hx & 0x0FF);
                frame.U8(hy & 0x0FF);
                frame.U8((hx & 0xF00) >> 8 | (hy & 0xF00) >> 4);

                if (recipient.protocolExtensionsLevel < 0) {
                    // speed in 2 bytes
                    typedef SignedByteFloat<3, -2> SpeedType;   // exponent from -2 to +6, with 4 significant bits -> epsilon = .25, max representable 32 * 31 = enough :)
                    frame.U8(SpeedType::toByte(h.vel.x));
                    frame.U8(SpeedType::toByte(h.vel.y));
                }

                // flags in 1 byte : dead, has deathbringer, deathbringer-affected, has shield, has turbo, has power
                uint8_t extra = 0;
                if (h.dead)
                    extra |= 1;
                if (h.item_deathbringer)
                    extra |= 2;
                if (h.deathbringer_end > get_time())
                    extra |= 4;
                if (h.item_shield)
                    extra |= 8;
                if (h.item_turbo)
                    extra |= 16;
                if (h.item_power)
                    extra |= 32;
                const bool preciseGundir = recipient.protocolExtensionsLevel >= 0 && world.physics.allowFreeTurning;
                if (preciseGundir)
                    extra |= 64;
                frame.U8(extra);

                recipient.knownProperties[h.pid] = h.remotelyVisibleProperties(); // if the frame is dropped, this may rarely cause a failure to inform recipient about the pups until next on screen

                if (!h.dead && recipient.protocolExtensionsLevel >= 0) { // for unextended clients, speed was sent before the extra byte
                    // speed in 2 bytes
                    typedef SignedByteFloat<3, -2> SpeedType;   // exponent from -2 to +6, with 4 significant bits -> epsilon = .25, max representable 32 * 31 = enough :)
                    frame.U8(SpeedType::toByte(h.vel.x));
                    frame.U8(SpeedType::toByte(h.vel.y));
                }

                // controls and gundirection in 1 byte
                uint8_t ccb;
                if (!h.dead) // if dead player, don't send keys
                    ccb = h.controls.toNetwork(true);
                else
                    ccb = ClientControls().toNetwork(true);
                if (preciseGundir) {
                    const uint16_t gundir = h.gundir.toNetworkLongForm();
                    ccb |= (gundir >> 8) << 5;
                    frame.U8(ccb);
                    ccb = gundir & 0xFF;
                    frame.U8(ccb);
                }
                else {
                    ccb |= h.gundir.toNetworkShortForm() << 5;
                    frame.U8(ccb);
                }

                if (!h.dead || recipient.protocolExtensionsLevel < 0) {
                    // visibility in 1 byte
                    const bool safeAfterSpawn = world.frame < h.start_take_damage_frame;
                    if (safeAfterSpawn)
                        frame.U8(world.frame & 2 ? 128 : 220);
                    else
                        frame.U8(forceVisible ? max(128, h.visibility) : h.visibility);
                }
            }

            // write players_onscreen in its place (reserved before the above loop)
            {
                const unsigned pos = frame.getPosition();
                frame.setPosition(players_onscreen_position);
                frame.U32(players_onscreen);
                frame.setPosition(pos);
            }

            /* minimap player position protocol:
             * old protocol:
             *  2 * {
             *        byte1 = 255 -> no info
             *        byte1 in [0, 31] ->
             *          byte2,
             *          byte3 = coords of player byte1
             *  }
             * extended protocol:
             *  P = bitmask indicating visible players (32 bits long)
             *  to use the minimum amount of bytes, we use
             *   - 3 bits to indicate which 4-bit boundary the sent data begins from
             *   - 2 bits to tell how many extra bytes of mask are sent (1..4)
             *  this leaves us with free 3 bits in byte1 which we use to start the mask with
             *
             *  byte1 & 0xE0 == 4-bit boundary of P to start from
             *  byte1 & 0x18 == extra byte count - 1
             *  byte1 & 0x07 == first 3 bits of P
             *  extra byte count *
             *    byte == next 8 bits of P
             *  for each sent bit of P that's set {
             *    byte1,
             *    byte2 = coords of the player
             *  }
             */
            if (recipient.protocolExtensionsLevel >= 0) {
                uint32_t P = (recipient.item_shadow() ? shadowView : normalView)[i / TSIZE];
                for (int pi = 0; pi < maxplayers; ++pi)
                    if (world.player[pi].room() == recipient.room())
                        P &= ~(uint32_t(1) << pi);
                const unsigned maxPlayers = min(settings.minimapSendLimit(), recipient.minimapPlayersPerFrame);
                if (P == 0 || maxPlayers == 0) {
                    frame.U8(0x00); // start from bit 0 (irrelevant), only 1 (mandatory) extra byte
                    frame.U8(0x00);
                }
                else {
                    int nextPlayer = recipient.nextMinimapPlayer;
                    while ((P & (uint32_t(1) << nextPlayer)) == 0)
                        nextPlayer = (nextPlayer + 1) % MAX_PLAYERS;
                    const int sendBoundary = nextPlayer & ~3;
                    uint32_t rotP = rotateRight(P, sendBoundary);
                    nextPlayer -= sendBoundary; // now nextPlayer is relative to rotP
                    rotP &= ~uint32_t(0) << nextPlayer;
                    vector<int> players;
                    players.reserve(maxPlayers);
                    int bits;
                    for (bits = nextPlayer; bits < 32 && players.size() < maxPlayers; ++bits) {
                        if (rotP >> bits == 0)
                            break;
                        if ((rotP >> bits) & 1)
                            players.push_back((bits + sendBoundary) % 32);
                    }
                    recipient.nextMinimapPlayer = (sendBoundary + bits) % 32;
                    rotP &= ~uint32_t(0) >> (32 - bits);
                    const int extraBytes = max(1, (bits - 3 + 7) / 8);
                    frame.U8(((sendBoundary / 4) << 5) | ((extraBytes - 1) << 3) | (rotP & 7));
                    rotP >>= 3;
                    for (int eb = 0; eb < extraBytes; ++eb) {
                        frame.U8(rotP & 0xFF);
                        rotP >>= 8;
                    }
                    for (vector<int>::const_iterator pi = players.begin(); pi != players.end(); ++pi) {
                        const int pid = *pi;
                        writeMinimapPlayerPosition(frame, pid);
                        if (world.seesPropertiesRemotely(recipient, world.player[pid])) {
                            const PlayerBase::RemotelyVisiblePropertiesData pd = world.player[pid].remotelyVisibleProperties();
                            if (recipient.knownProperties[pid] != pd) {
                                newProperties.push_back(make_pair(pid, pd));
                                recipient.knownProperties[pid] = pd;
                            }
                        }
                    }
                }
            }
            else
                for (int round = 0; round < 2; ++round) {
                    const int who = (recipient.item_shadow() ? shadowIters : normalIters)[i / TSIZE][round];
                    if (who == -1)
                        frame.U8(255);
                    else {
                        frame.U8(who);
                        writeMinimapPlayerPosition(frame, who);
                        // no need to keep track of properties, they can't be sent to old clients anyway
                    }
                }

            // send 8 bits of player's health
            nAssert(recipient.health >= 0);
            nAssert((recipient.health == 0) == recipient.dead);
            frame.U8(iround(recipient.health) & 255);

            // send 8 bits of player's energy
            nAssert(recipient.energy >= 0);
            frame.U8(iround(recipient.energy) & 255);

            // ping of player frame# % maxplayers
            if (recipient.protocolExtensionsLevel < 0 || world.player[world.frame % maxplayers].used)
                frame.U16(static_cast<uint16_t>(world.player[world.frame % maxplayers].ping));

            if (!newProperties.empty() && recipient.protocolExtensionsLevel >= 3) {
                BinaryBuffer<100> msg;
                msg.U8(data_minimap_player_properties);
                for (vector< pair<int, PlayerBase::RemotelyVisiblePropertiesData> >::const_iterator npi = newProperties.begin(); npi != newProperties.end(); ++npi) {
                    msg.U8(npi->first);
                    npi->second.write(msg);
                }
                send_message(recipient.cid, msg);
            }
        }

        //send the packet
        clientConnection[recipient.cid]->send_frame(frame);

        //send server map list if not sent yet
        if (recipient.current_map_list_item < host->maplist().size() && world.frame % 2 == 0) {
            if (recipient.sendingQuickMapList && recipient.protocolExtensionsLevel >= 1) {
                send_quick_map_list(recipient);
                recipient.current_map_list_item += 20;
                if (recipient.current_map_list_item >= host->maplist().size()) {
                    recipient.current_map_list_item = 0;
                    recipient.sendingQuickMapList = false;
                }
            }
            else {
                send_map_info(recipient);
                ++recipient.current_map_list_item;
            }
        }
    }

    // map votes update
    if (world.frame % 10 == 0)
        broadcast_map_votes_update();

    // stats update
    if (gameRunning && world.frame / MAX_PLAYERS % 5 == 0) {
        const int pid = world.frame % MAX_PLAYERS;
        if (world.player[pid].used) {
            broadcast_movements_and_shots(world.player[pid]);   // player's stats to everyone
            send_team_movements_and_shots(world.player[pid].cid);   // team stats to player
        }
    }
    if (gameRunning && world.frame % 20 == 0)
        send_team_movements_and_shots(pid_record);

    ping_send_client++;
    if (ping_send_client >= maxplayers)
        ping_send_client = 0;
    if (world.player[ping_send_client].used)
        clientConnection[world.player[ping_send_client].cid]->ping();

    g_timeCounter.refresh(); // we prefer an exact time here
    frameSentTime = get_time();
}

double ServerNetworking::getTraffic() const throw () {
    return leetServer->get_socket_stat(Network::Socket::Stat_AvgBytesReceived) + leetServer->get_socket_stat(Network::Socket::Stat_AvgBytesSent);
}

void ServerNetworking::run_masterjob_thread(MasterQuery* job) throw () {
    int delay = 0;  // given a value in MS before each continue: this time will be waited before next round

    while (!mjob_exit) {
        if (delay > 0) {
            platSleep(500);
            delay -= 500;
            if (!mjob_fastretry)
                continue;
        }
        delay = 60000;  // default to one minute

        stringstream response;

        try {
            Network::TCPSocket sock(Network::NonBlocking, 0, true);
            sock.connect(g_masterSettings.rankAddress());
            sock.persistentWrite(job->request, &mjob_exit, 30000);
            save_http_response(sock, response, &mjob_exit, 30000);
            sock.close();
        } catch (Network::ExternalAbort) {
            break;
        } catch (const Network::Error& e) {
            log("Ranking thread: %s", e.str().c_str());
            delay = 15000; // faster retry
            continue;
        }

        Lock ml(threadLockMutex);

        string line;
        while (getline(response, line) && line != "\r") { } // skip HTTP headers

        getline_smart(response, line);
        if (line == "OK") {
            int v[4];
            string clanTag;
            getline(response, clanTag);
            if (job->code != MasterQuery::JT_nameCheck)
                for (int i = 0; i < 4; ++i)
                    response >> v[i];
            if (!response || response.peek() != std::istream::traits_type::eof()) {
                log("Ranking thread: Invalid response: \"%s\"", formatForLogging(response.str()).c_str());
                continue;
            }
            const int pid = ctop[job->cid];
            if (pid == -1)
                break; // all done, nothing to notify anyone about
            ClientData& clid = host->getClientData(job->cid);
            if (job->code == MasterQuery::JT_login || job->code == MasterQuery::JT_nameCheck) {
                log("Ranking thread: Player %s logged in successfully", world.player[pid].name.c_str());
                BinaryBuffer<128> msg;
                msg.U8(data_registration_response);
                msg.U8(1); // registration ok
                send_message(job->cid, msg);
                clid.token_valid = true;
            }
            else if (job->code == MasterQuery::JT_score)
                log("Ranking thread: Score for player %s updated successfully", world.player[pid].name.c_str());
            else
                nAssert(0);
            if (job->code != MasterQuery::JT_nameCheck) {
                clid.rank       = v[0];
                clid.score      = v[1];
                clid.neg_score  = v[2];
                max_world_rank  = v[3];
            }
            broadcast_player_crap(pid);
            world.player[pid].clanTag = clanTag;
            break; // all done
        }
        else if (line.substr(0, 7) == "ERROR: ") {
            const bool serverError = (line == "ERROR: server doesnt exist!");
            const bool playerError = (line == "ERROR: player doesnt exist!");
            if (!serverError && !playerError)
                log("Ranking thread: Invalid error response: \"%s\"", line.substr(7).c_str());
            if (serverError && !host->getRankingPassword().empty()) {
                log.error(_("Ranking server rejected the server id/password. No more ranking transactions will be attempted until ranking_password is set again."));
                host->clearRankingPassword();
            }
            const int pid = ctop[job->cid];
            if (pid == -1) {
                if (job->code == MasterQuery::JT_score)
                    log("Ranking thread: Score update lost for a player who has left the server");
                break;
            }
            if (serverError && job->code == MasterQuery::JT_login) {
                // we're still interested in the player status, so try again without the server login
                job->code = MasterQuery::JT_nameCheck;
                job->request = build_http_request(true, g_masterSettings.rankHost(), g_masterSettings.rankDataScript(),
                                                  "name=" + url_encode(world.player[pid].name) +
                                                  "&token=" + url_encode(host->getClientData(job->cid).token));
                delay = 0; // try again immediately
                continue;
            }
            if ((job->code == MasterQuery::JT_login || job->code == MasterQuery::JT_nameCheck) && playerError) {
                log.security("Ranking thread: Login failed for player %s (at %s), request: \"%s\"",
                             world.player[pid].name.c_str(), get_client_address(job->cid).toString().c_str(), formatForLogging(job->request).c_str());
            }
            if (!host->getClientData(job->cid).token_have) // if this operation was pending when a previous one completed with the failure
                break;
            if (playerError) {
                BinaryBuffer<128> msg;
                msg.U8(data_registration_response);
                msg.U8(0); // registration failed
                send_message(job->cid, msg);
                host->getClientData(job->cid).token_have = false;
                broadcast_player_crap(pid);
            }
            if (job->code == MasterQuery::JT_score) {
                send_ranking_update_failed(pid);
                log("Ranking thread: Score update for player %s failed!", world.player[pid].name.c_str());
            }
            break;  // request complete
        }
        else
            log("Ranking thread: Invalid response: \"%s\"", formatForLogging(response.str()).c_str());
    }
    {
        Lock ml(mjob_mutex);
        --mjob_count;
    }
    delete job;
}

void ServerNetworking::logTCPThreadError(const Network::Error& error, const string& text) throw () {
    if (dynamic_cast<const Network::ReadWriteError*>(&error) || dynamic_cast<const Network::Timeout*>(&error)) // these are too mundane errors to bother the user with
        log(text);
    else
        log.error(text);
}

void ServerNetworking::run_mastertalker_thread() throw () {
    string localAddress = settings.ip();
    if (!isValidIP(localAddress) || check_private_IP(localAddress)) {
        log("Master talker: No public IP address. Letting the master server decide.");
        localAddress.clear();
    }

    bool master_never_talked = true;
    double master_talk_time = get_time() + delay_to_report_server;  //give it a break

    while (!file_threads_quit) {
        platSleep(500);

        if (get_time() < master_talk_time)
            continue;

        master_talk_time = get_time() + 3 * 60.0 ;      //3 minutes
        if (settings.privateServer()) {
            if (!master_never_talked) {
                send_master_quit(localAddress);
                master_never_talked = true;
            }
            continue;
        }

        // note: most the code from here down is repeated in the quitting phase; make changes there too (//#fixme)

        if (!g_masterSettings.address().valid())
            continue;

        try {
            Network::TCPSocket msock(Network::NonBlocking, 0, true);
            msock.connect(g_masterSettings.address());

            //now we have talked
            master_never_talked = false;

            // build and send data
            map<string, string> parameters = master_parameters(localAddress);
            const string data = format_http_parameters(parameters);
            post_http_data(msock, &file_threads_quit, 30000, g_masterSettings.host(), g_masterSettings.submit(), data);
            stringstream response;
            save_http_response(msock, response, &file_threads_quit, 30000);
            // save transaction to a file
            ofstream out((whereuserdir + "log" + directory_separator + "master.log").c_str());
            out << "This file contains the server's latest successfully completed communications\nwith the server list master server\n\n";
            out << "--- Query ---\n";
            out << data << "\n";
            out << "\n--- Response ---\n";
            out << response.str();
            out.close();
            if (response.str().find("VERSION ERROR") != string::npos) {
                log.error(_("Master talker: You have a deprecated Outgun version. The server is not accepted on the master list. Please update."));
                return;
            }
            if (response.str().find("[ERROR]") != string::npos) // this means a more permanent problem
                log.error(_("Master talker: There was an unexpected error while sending information to the master list. See log/master.log. To suppress this error, make the server private by using the -priv argument."));
            else if (response.str().find("[OK]") == string::npos) // this happens when the master server has problems
                log("Master talker: There was an unexpected error while sending information to the master list. See log/master.log.");
        } catch (Network::ExternalAbort) {
            break;
        } catch (const Network::Error& e) {
            logTCPThreadError(e, _("Master talker: $1", e.str()));
            master_talk_time = get_time() + 30.0; // faster retry
        }
    }

    log("Master talker: time to say goodbye.");

    if (master_never_talked)
        return;

    send_master_quit(localAddress);
}

// Quitting: Delete my IP from the master so clients won't see it.
void ServerNetworking::send_master_quit(const string& localAddress) const throw () {
    if (!g_masterSettings.address().valid())
        return;

    try {
        Network::TCPSocket msock(Network::NonBlocking, 0, true);
        msock.connect(g_masterSettings.address());

        const map<string, string> parameters = master_parameters(localAddress, true); // true = quitting
        const string data = format_http_parameters(parameters);
        post_http_data(msock, 5000, g_masterSettings.host(), g_masterSettings.submit(), data); // only 5 seconds allowed; it's not so crucial
        stringstream response;
        save_http_response(msock, response, 5000);  // only 5 seconds allowed; it's not so crucial
        // save transaction to a file
        ofstream out((whereuserdir + "log" + directory_separator + "master.log").c_str());
        out << "This file contains the server's latest successfully completed communications\nwith the server list master server\n\n";
        out << "--- Query ---\n";
        out << data << "\n";
        out << "\n--- Response ---\n";
        out << response.str();
        out.close();
        if (response.str().find("[OK]") == string::npos)
            log.error(_("Master talker: (Quit) There was an unexpected error while sending information to the master list. See log/master.log."));
    } catch (const Network::Error& e) {
        log.error(_("Master talker: (Quit) $1", e.str()));
    }
}

void ServerNetworking::run_website_thread() throw () {
    if (settings.get_web_servers().empty() || settings.get_web_script().empty())
        return;

    const string& localAddress = settings.ip();
    // use it even if not public

    Network::Address website_address;
    string working_address_string;
    double website_talk_time = 0.0;
    bool first_connection = true;
    int sent_maplist_revision = -1;

    while (!file_threads_quit) {
        platSleep(500);

        if (get_time() < website_talk_time)
            continue;

        website_talk_time = get_time() + settings.get_web_refresh() * 60.0;

        // note: most of the code from here down is repeated in the quitting phase; make changes there too (//#fixme)

        bool success = false;
        for (vector<string>::const_iterator addri = settings.get_web_servers().begin(); addri != settings.get_web_servers().end(); ++addri) {
            Network::ResolveError err;
            if (website_address.tryResolve(*addri, &err)) {
                success = true;
                working_address_string = *addri;
                break;
            }
            else
                log("Website thread: Can't get address from %s. Reason: %s", addri->c_str(), err.str().c_str());
        }
        if (!success) {
            log("Website thread: Can't get any address from the list!");
            continue;
        }
        if (website_address.getPort() == 0)
            website_address.setPort(80);

        try {
            Network::TCPSocket websock(Network::NonBlocking, 0, true);
            websock.connect(website_address);

            // build and send data
            map<string, string> parameters = website_parameters(localAddress);
            const int sending_maplist_revision = maplist_revision;
            if (first_connection || sending_maplist_revision != sent_maplist_revision) {
                parameters["maplist"] = website_maplist();
                first_connection = false;
            }
            const string data = format_http_parameters(parameters);
            post_http_data(websock, &file_threads_quit, 30000, working_address_string, settings.get_web_script(), data, settings.get_web_auth());
            // save response to a file
            ofstream out((whereuserdir + "log" + directory_separator + "web.log").c_str());
            save_http_response(websock, out, &file_threads_quit, 30000);
            sent_maplist_revision = sending_maplist_revision;
        } catch (Network::ExternalAbort) {
            break;
        } catch (const Network::Error& e) {
            logTCPThreadError(e, _("Website thread: $1", e.str()));
            website_talk_time = get_time() + 30.0; // faster retry
        }
    }

    log("Website thread: time to say goodbye");

    if (first_connection)   // not send anything
        return;

    //quitting: send server shutdown message to web script
    try {
        Network::TCPSocket websock(Network::NonBlocking, 0, true);
        websock.connect(website_address);

        const string quit = "quit=1";
        post_http_data(websock, 5000, working_address_string, settings.get_web_script(), quit, settings.get_web_auth());  // only 5 seconds allowed; it's not so crucial
        log("Website thread: Sent information to server website: \"%s\"", formatForLogging(quit).c_str());

        // save response to a file
        ofstream out((whereuserdir + "log" + directory_separator + "web.log").c_str());
        save_http_response(websock, out, 5000);  // only 5 seconds allowed; it's not so crucial
    } catch (Network::Error& e) {
        log.error(_("Website thread: (Quit) $1", e.str()));
    }
}

map<string, string> ServerNetworking::master_parameters(const string& address, bool quitting) const throw () {
    map<string, string> parameters;
    if (!address.empty())
        parameters["ip"] = address;
    parameters["port"] = itoa(settings.get_port());
    if (quitting)
        parameters["quit"] = "1";
    else {
        parameters["name"] = settings.get_hostname();
        parameters["players"] = itoa(get_human_count());
        parameters["bots"] = itoa(bot_count);
        if (settings.dedicated())
            parameters["dedicated"] = "1";
        parameters["max_players"] = itoa(maxplayers);
        parameters["version"] = getVersionString(true, 1); // as short as possible
        const string longVersion = getVersionString();
        if (longVersion != parameters["version"])
            parameters["long_version"] = longVersion;
        parameters["protocol"] = GAME_PROTOCOL;
        parameters["uptime"] = itoa(world.frame / 10);
        parameters["map"] = host->current_map().title;
        parameters["link"] = host->server_website();
        if (!settings.get_server_password().empty())
            parameters["password"] = "1";
        if (world.physics.allowFreeTurning)
            parameters["free_turning"] = "1";
        if (world.physics.friendly_fire > 0)
            parameters["friendly_fire"] = "1";
        if (world.getPupConfig().pups_min > 0)
            parameters["powerups"] = "1";
        if (is_relay_active())
            parameters["spectator"] = "1";
    }
    parameters["id"] = server_identification;
    return parameters;
}

map<string, string> ServerNetworking::website_parameters(const string& address) const throw () {
    map<string, string> parameters;
    parameters["name"] = settings.get_hostname();
    parameters["ip"] = address;
    parameters["port"] = itoa(settings.get_port());
    parameters["players"] = itoa(get_human_count());
    parameters["bots"] = itoa(bot_count);
    if (settings.dedicated())
        parameters["dedicated"] = "1";
    parameters["max_players"] = itoa(maxplayers);
    parameters["version"] = getVersionString(true, 1); // as short as possible
    const string longVersion = getVersionString();
    if (longVersion != parameters["version"])
        parameters["long_version"] = longVersion;
    parameters["uptime"] = itoa(world.frame / 10);
    parameters["map"] = host->current_map().title;
    parameters["mapfile"] = host->getCurrentMapFile();
    if (!settings.get_server_password().empty())
        parameters["password"] = "1";
    if (world.physics.allowFreeTurning)
        parameters["free_turning"] = "1";
    if (world.physics.friendly_fire > 0)
        parameters["friendly_fire"] = "1";
    if (world.getPupConfig().pups_min > 0)
        parameters["powerups"] = "1";
    if (is_relay_active())
        parameters["spectator"] = "1";
    string players;
    for (int i = 0; i < maxplayers; i++)
        if (world.player[i].used) {
            if (!players.empty())
                players += '\n';
            players += world.player[i].name + '\t' + itoa(i / TSIZE) + '\t' + itoa(world.player[i].ping);
        }
    parameters["playerlist"] = players;
    return parameters;
}

string ServerNetworking::website_maplist() const throw () {
    ostringstream maps;
    for (vector<MapInfo>::const_iterator m = host->maplist().begin(); m != host->maplist().end(); m++) {
        if (m != host->maplist().begin())
            maps << '\n';
        maps << m->title;
    }
    return maps.str();
}

// read a string from a TCP stream, one char at a time; it doesn't tolerate breaks and is very slow but the admin shell system doesn't need more reliability
bool ServerNetworking::read_string_from_TCP(Network::TCPSocket& sock, string& resultStr) throw (Network::ReadWriteError) {
    for (;;) {
        uint8_t ch;
        const int result = sock.read(DataBlockRef(&ch, 1));
        if (result != 1)    // message not completely received
            return false;
        if (ch == '\0')
            return true;
        resultStr += static_cast<char>(ch);
    }
}

void ServerNetworking::handleNewAdminShell(Thread& slaveThread, volatile bool& slaveRunning) throw (Network::Error) {
    log("Incoming admin shell connection");

    // accept connections only from localhost
    Network::Address addr = shellssock.getRemoteAddress(), c1("127.0.0.1"), c2 = Network::getDefaultLocalAddress();
    addr.setPort(0);

    if (addr != c1 && addr != c2) {
        log("Attempt to connect a remote admin shell blocked.");
        shellssock.close();
        return;
    }

    log("Admin shell connection accepted");

    // tell about the current situation
    BinaryBuffer<4096> msg;
    for (int i = 0; i < maxplayers; i++)
        if (world.player[i].used) {
            msg.U32(STA_PLAYER_CONNECTED);
            msg.U32(world.player[i].cid);

            msg.U32(STA_PLAYER_NAME_UPDATE);
            msg.U32(world.player[i].cid);
            msg.str(world.player[i].name);

            msg.U32(STA_PLAYER_IP);
            msg.U32(world.player[i].cid);
            Network::Address addr = get_client_address(world.player[i].cid);
            addr.setPort(0);
            msg.str(addr.toString());

            msg.U32(STA_PLAYER_FRAGS);
            msg.U32(world.player[i].cid);
            msg.U32(world.player[i].stats().frags());
        }
    writeToAdminShell(msg);

    if (slaveThread.isRunning())
        slaveThread.join();
    slaveRunning = true;    // slave will set it false when exiting
    slaveThread.start("ServerNetworking::run_shellslave_thread",
                      MemFun1<ServerNetworking, void, volatile bool*>(this, &ServerNetworking::run_shellslave_thread), &slaveRunning,
                      settings.lowerPriority());
}

//run a admin shell master thread
void ServerNetworking::run_shellmaster_thread(int port) throw () {
    Thread slaveThread;
    volatile bool slaveRunning = false; // the slave thread will modify this flag when quitting

    log("Admin shell master thread running");

    Network::TCPListenerSocket shellmsock(true);
    try {
        shellmsock.open(Network::NonBlocking, port);
    } catch (const Network::Error& e) {
        log.error(_("Admin shell: $1", e.str()));
        return;
    }
    while (!file_threads_quit) {
        platSleep(1000); // this thread definitely is no priority

        if (shellssock.isOpen()) {
            Network::TCPSocket newSock(true);
            if (newSock.acceptConnection(Network::NonBlocking, shellmsock))
                log("Attempt to connect two simultaneous admin shells blocked.");
            continue;
        }

        if (!shellssock.acceptConnection(Network::NonBlocking, shellmsock))
            continue;

        try {
            handleNewAdminShell(slaveThread, slaveRunning);
        } catch (const Network::Error& e) {
            shellssock.close();
            log("Admin shell: %s", e.str().c_str());
        }
    }

    shellmsock.close();
    log("Admin shell master thread quitting");
    if (slaveThread.isRunning())
        slaveThread.join();
}


void ServerNetworking::executeAdminCommand(uint32_t code, uint32_t cid, int pid, uint32_t dwArg, BinaryWriter& answer) throw (Network::Error) {
    Lock ml(threadLockMutex);
    switch (code) {
    /*break;*/ case ATS_GET_PLAYER_FRAGS:
            answer.U32(STA_PLAYER_FRAGS);
            answer.U32(cid);
            answer.U32(world.player[pid].stats().frags());
        break; case ATS_GET_PLAYER_TOTAL_TIME:
            answer.U32(STA_PLAYER_TOTAL_TIME);
            answer.U32(cid);
            answer.U32(static_cast<unsigned>(get_time() - world.player[pid].stats().start_time()));
        break; case ATS_GET_PLAYER_TOTAL_KILLS:
            answer.U32(STA_PLAYER_TOTAL_KILLS);
            answer.U32(cid);
            answer.U32(world.player[pid].stats().kills());
        break; case ATS_GET_PLAYER_TOTAL_DEATHS:
            answer.U32(STA_PLAYER_TOTAL_DEATHS);
            answer.U32(cid);
            answer.U32(world.player[pid].stats().deaths());
        break; case ATS_GET_PLAYER_TOTAL_CAPTURES:
            answer.U32(STA_PLAYER_TOTAL_CAPTURES);
            answer.U32(cid);
            answer.U32(world.player[pid].stats().captures());
        break; case ATS_SERVER_CHAT: {
            string str;
            read_string_from_TCP(shellssock, str);
            if (str.empty())
                break;
            if (find_nonprintable_char(str))
                log.error(_("Admin shell: unprintable characters, message ignored."));
            else if (str[0] == '/')
                host->chat(shell_pid, str);
            else {
                bprintf(msg_normal, "ADMIN: %s", str.c_str());
                host->logChat(shell_pid, str);
            }
        }
        break; case ATS_GET_PINGS:
            for (int p = 0; p < maxplayers; ++p)
                if (world.player[p].used) {
                    answer.U32(STA_PLAYER_PING);
                    answer.U32(world.player[p].cid);
                    answer.U32(world.player[p].ping);
                }
        break; case ATS_MUTE_PLAYER:
            host->mutePlayer(pid, dwArg, shell_pid);
        break; case ATS_KICK_PLAYER:
            host->kickPlayer(pid, shell_pid);
        break; case ATS_BAN_PLAYER:
            host->banPlayer(pid, shell_pid, 60 * 24 * 365);    // ban for a year; this can be later adjusted in auth.txt
        break; case ATS_RESET_SETTINGS:
            host->reset_settings(true);
        break; default:
            nAssert(0);
    }
}

bool ServerNetworking::handleAdminCommand() throw (Network::Error, BinaryReader::ReadError) {
    char rbuf[256];

    //read request code
    int result = shellssock.read(DataBlockRef(rbuf, 4));

    if (result == 0) {
        platSleep(500);  // no need to be more responsive
        return true;
    }

    if (result != 4) {
        log.error("Admin shell: bad data length");
        return false;
    }

    BinaryDataBlockReader rd(rbuf, result);

    const uint32_t code = rd.U32();

    // parse the code
    if (code >= NUMBER_OF_ATS) {
        log.error("Admin shell: invalid command " + itoa(code));
        return false;
    }

    if (code == ATS_QUIT) {
        log("Admin shell: received quit command");
        return false;
    }

    uint32_t cid = 0;
    int pid = 0;    // pid and cid set if argPid[code]
    uint32_t dwArg = 0;  // set if argDw[code]
    //                                      noop, get-functions,ch,qu,pi,kckbanmte,reset
    static const int argPid[NUMBER_OF_ATS] = { 0, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 0 };
    static const int argDw [NUMBER_OF_ATS] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0 };
    const int argsLen = (argPid[code] + argDw[code]) * 4;

    if (argsLen) {
        result = shellssock.read(DataBlockRef(rbuf, argsLen));
        if (result != argsLen) {
            log.error("Admin shell: bad data length (args: " + itoa(result) + '/' + itoa(argsLen) + ')');
            return false;
        }
        rd = BinaryDataBlockReader(rbuf, result);
        if (argPid[code]) {
            cid = rd.U32();
            if (cid > 255) {
                log.error("Admin shell: bad client id");
                return false;
            }
            pid = ctop[cid];
            if (pid == -1 || !world.player[pid].used)   // player not in the game; just ignore the command
                return true;
        }
        if (argDw[code])
            dwArg = rd.U32();
    }

    ExpandingBinaryBuffer answer;
    executeAdminCommand(code, cid, pid, dwArg, answer);

    if (!answer.empty())
        return writeToAdminShell(answer);
    else
        return true;
}

void ServerNetworking::run_shellslave_thread(volatile bool* runningFlag) throw () {  // sets *runningFlag = true when quitting
    try {
        while (!file_threads_quit)
            if (!handleAdminCommand())
                break;
    } catch (const Network::Error& e) {
        log.error(_("Admin shell: $1", e.str()));
    } catch (BinaryReader::ReadError) {
        log.error(_("Admin shell: $1", "data format error"));
    }

    if (shellssock.isOpen()) {
        BinaryBuffer<4> msg;
        msg.U32(STA_QUIT);
        writeToAdminShell(msg);
        platSleep(100); // Give the admin shell chance to close the socket before the server.
    }

    shellssock.closeIfOpen();
    *runningFlag = false;
    log("Admin shell slave thread quitting");
}

void ServerNetworking::RelayThread::threadMain() throw () {
    Lock ml(mutex);
    for (;;) {
        while (!quitFlag && (dataQueue.empty() || dataQueue.front().time + delay > get_time()))
            wakeup.wait(mutex);
        if (quitFlag)
            break;
        nAssert(isConnected_locked());
        nAssert(!dataQueue.empty());
        const DataBlock data = dataQueue.front().data;
        dataQueue.pop();

        try {
            Unlock mu(mutex);
            socket.persistentWrite(data, &quitFlag, 5000, 50); // 5 second timeout
        } catch (Network::ExternalAbort) {
            break;
        } catch (const Network::Error& e) {
            log("Relay thread: %s", e.str().c_str());
            socket.close();
            dataQueue = queue<RelayData>();
        }
    }
}

void ServerNetworking::RelayThread::pushData_locked(ConstDataBlockRef data) throw () {
    dataQueue.push(RelayData(static_cast<int>(get_time()), data));
    wakeup.signal();
}

ServerNetworking::RelayThread::RelayThread(LogSet logs, volatile bool& quitFlag_) throw () :
    quitFlag(quitFlag_),
    wakeup("ServerNetworking::RelayThread::wakeup"),
    mutex("ServerNetworking::RelayThread::mutex"),
    log(logs)
{ }

void ServerNetworking::RelayThread::start(int priority) throw () {
    thread.start_assert("ServerNetworking::RelayThread::threadMain",
                        MemFun0<ServerNetworking::RelayThread, void>(this, &ServerNetworking::RelayThread::threadMain),
                        priority);
}

void ServerNetworking::RelayThread::stop() throw () {
    nAssert(quitFlag);
    wakeup.signal(mutex);
    thread.join();
}

void ServerNetworking::RelayThread::startNewGame(const Network::Address& relayAddress, ConstDataBlockRef initData, int gameDelay) throw () {
    Lock ml(mutex);

    newGame = true;
    delay = gameDelay;

    if (isConnected_locked())
        return; // initData already sent, too

    dataQueue = queue<RelayData>();

    try {
        socket.open(Network::NonBlocking, 0);
        socket.connect(relayAddress);
    } catch (const Network::Error& e) {
        log("Relay thread: %s", e.str().c_str());
        socket.closeIfOpen();
        return;
    }

    ExpandingBinaryBuffer msg;
    msg.str(GAME_STRING);
    msg.U32dyn8(RELAY_PROTOCOL);
    msg.U32dyn8(RELAY_PROTOCOL_EXTENSIONS_VERSION);
    msg.str("SERVER");
    msg.U32dyn8(initData.size());
    msg.block(initData);

    pushData_locked(msg);
}

void ServerNetworking::RelayThread::pushFrame(ConstDataBlockRef frame) throw () {
    Lock ml(mutex);
    if (!isConnected_locked())  // Try again in the next game.
        return;
    ExpandingBinaryBuffer data;
    data.U8(newGame ? relay_data_game_start : relay_data_frame);
    data.block(frame);
    pushData_locked(data);
    newGame = false;
}

void ServerNetworking::stop() throw () {
    //submit all pending player reports
    for (int i = 0; i < maxplayers; i++)
        if (world.player[i].used)
            client_report_status(world.player[i].cid);

    //v0.4.4 : flag master job threads to start trying to resolve themselves quickly
    mjob_fastretry = true;
    const double mjmaxtime = get_time() + 30.0;     //timeout : 30 seconds

    settings.statusOutput()(_("Shutdown: net server"));

    if (leetServer)
        leetServer->stop(3);
    else
        nAssert(0);

    for (int i = 0; i < MAX_CLIENTS; i++)
        fileTransfer[i].reset();

    file_threads_quit = true;   // flag so threads will quit themselves

    //close TCP connection with the server admin shell
    settings.statusOutput()(_("Shutdown: admin shell threads"));
    shellmthread.join();

    //wait for all master jobs to complete nicely
    while (mjob_count > 0 && get_time() < mjmaxtime) {
        settings.statusOutput()(_("Shutdown: waiting for $1 ranking updates", itoa(mjob_count)));
        platSleep(100);
    }

    //clean up jobs
    mjob_exit = true;       //MUST terminate -- abort
    while (mjob_count > 0) {
        settings.statusOutput()(_("Shutdown: ABORTING $1 ranking updates", itoa(mjob_count)));
        platSleep(100);
    }

    settings.statusOutput()(_("Shutdown: master talker thread"));
    mthread.join();

    settings.statusOutput()(_("Shutdown: website thread"));
    webthread.join();

    settings.statusOutput()(_("Shutdown: relay thread"));
    relayThread.stop();

    settings.statusOutput()(_("Shutdown: main thread"));
}

void ServerNetworking::sendWeaponPower(int pid) const throw () {
    BinaryBuffer<256> msg;
    msg.U8(data_weapon_change);
    msg.U8(world.player[pid].weapon);
    send_message(world.player[pid].cid, msg);
}

void ServerNetworking::sendRocketMessage(int shots, GunDirection gundir, uint8_t* sid, int pid, bool power,
                                         const WorldCoords& pos, uint32_t vislist) const throw () { // sid = shot-id; array of uint8_t[shots]
    for (int iProto = 0; iProto < 2; ++iProto) {
        const bool preciseGundir = iProto == 1 && world.physics.allowFreeTurning;
        BinaryBuffer<256> msg;
        msg.U8(data_rocket_fire);
        msg.U8(shots);
        if (preciseGundir) {
            const uint16_t dirData = gundir.toNetworkLongForm();
            msg.U8((dirData >> 8) | 0x80); // high bit signals extended form (it's never set in 1-byte directions)
            msg.U8(dirData & 0xFF);
        }
        else
            msg.U8(gundir.toNetworkShortForm());
        for (int i = 0; i < shots; i++)
            msg.U8(sid[i]);    // rocket-object id (needed because client-side rockets can be deleted by the server)
        msg.U32(world.frame);   // time of shot of the rocket: current (last simulated) frame
        uint8_t shotType = power;
        if (iProto == 0)
            shotType |= (pid / TSIZE) << 1;
        else
            shotType |= 2 | (pid << 2); // we're always sending the pid, but this might change in the future
        msg.U8(shotType);    // owner of all rockets
        writePos(msg, pos);

        for (int i = 0; i < maxplayers; i++)
            if ((vislist & (1 << i)) && ((iProto == 0) == (world.player[i].protocolExtensionsLevel == -1)))
                send_message(world.player[i].cid, msg);

        if (iProto == 1)
            record_message(msg);
    }
}

void ServerNetworking::sendOldRocketVisible(int pid, int rid, const Rocket& rocket) const throw () {
    const bool preciseGundir = world.player[pid].protocolExtensionsLevel >= 0 && world.physics.allowFreeTurning;
    BinaryBuffer<256> msg;
    const uint8_t shotType = (rocket.team << 1) | rocket.power;
    msg.U8(data_old_rocket_visible);
    msg.U8(rid);
    if (preciseGundir) {
        const uint16_t dirData = rocket.direction.toNetworkLongForm();
        msg.U8((dirData >> 8) | 0x80); // high bit signals extended form (it's never set in 1-byte directions)
        msg.U8(dirData & 0xFF);
    }
    else
        msg.U8(rocket.direction.toNetworkShortForm());
    msg.U32(world.frame);
    msg.U8(shotType);
    msg.U8(rocket.room().x);
    msg.U8(rocket.room().y);
    msg.S16(static_cast<int>(rocket.pos.x));
    msg.S16(static_cast<int>(rocket.pos.y));

    send_message(world.player[pid].cid, msg);
}

void ServerNetworking::sendRocketDeletion(uint32_t plymask, int rid, int16_t hitx, int16_t hity, int targ) const throw () {
    //assembly rocket delete message
    BinaryBuffer<256> msg;
    msg.U8(data_rocket_delete);
    msg.U8(rid);     // rocket-object id
    msg.U8(targ);        // player-target. if 255, no player in particular was hit

    msg.S16(hitx);     // HIT X,Y OF ROCKET
    msg.S16(hity);

    //send message to players that received the rocket
    for (int i = 0; i < maxplayers; i++)
        if (world.player[i].used && (plymask & (1 << i)))
            send_message(world.player[i].cid, msg);

    record_message(msg);
}

void ServerNetworking::sendDeathbringer(int pid, const ServerPlayer& ply) const throw () {
    BinaryBuffer<256> msg;
    msg.U8(data_deathbringer);
    msg.U8(pid / TSIZE); // team
    msg.U32(world.frame);                       // frame # of the bringer shot (message can be delayed)
    writePos(msg, ply.pos);

    broadcast_message(msg);
    record_message(msg);
}

void ServerNetworking::sendPowerupVisible(int pid, int pup_id, const Powerup& it) const throw () {
    BinaryBuffer<256> msg;
    msg.U8(data_pup_visible);
    msg.U8(pup_id);
    msg.U8(it.kind);
    writePos(msg, it.pos);
    if (pid == pid_record)
        record_message(msg);
    else
        send_message(world.player[pid].cid, msg);
}

void ServerNetworking::broadcastPowerupPicked(int roomx, int roomy, int pup_id) const throw () {
    BinaryBuffer<256> msg;
    msg.U8(data_pup_picked);
    msg.U8(pup_id);
    broadcast_screen_message(roomx, roomy, msg);
}

void ServerNetworking::sendPupTime(int pid, uint8_t pupType, double timeLeft) const throw () {
    BinaryBuffer<256> msg;
    msg.U8(data_pup_timer);
    msg.U8(pupType);
    msg.U16(static_cast<unsigned>(timeLeft));
    send_message(world.player[pid].cid, msg);
}

void ServerNetworking::sendFragUpdate(int pid, uint32_t frags) const throw () {
    BinaryBuffer<256> msg;
    msg.U8(data_frags_update);
    msg.U8(pid);       // what player id
    msg.U32(frags);
    broadcast_message(msg);
    record_message(msg);
}

void ServerNetworking::sendNameAuthenticationRequest(int pid) const throw () {
    BinaryBuffer<256> msg;
    msg.U8(data_name_authentication_request);
    send_message(world.player[pid].cid, msg);
}

Network::Address ServerNetworking::get_client_address(int cid) const throw () {
    return clientConnection[cid]->get_client_address();
}

bool ServerNetworking::clientHello(const Network::Address& address, ConstDataBlockRef data, BinaryWriter& reply, int& customStoredData, bool bot) throw () {
 try {
    BinaryDataBlockReader msg(data);

    // free reservedPlayerSlots that have been left unused, they might be needed now
    if (playerSlotReservationTime < get_time() - 60.) // in 60 seconds Leetnet should drop the client
        reservedPlayerSlots = 0;

    const string gameStr = msg.str();
    if (gameStr != GAME_STRING) {
        log("Rejected a client because game strings don't match: Server '%s' and player '%s'.", GAME_STRING.c_str(), gameStr.c_str());
        reply.str("This game is " + GAME_STRING);
        return false;
    }
    string protoStr = msg.str();
    const time_t tt = time(0);
    const tm* tmb = gmtime(&tt);
    const int seconds = tmb->tm_hour * 3600 + tmb->tm_min * 60 + tmb->tm_sec;
    const int join_start = settings.get_join_start(), join_end = settings.get_join_end();
    if (protoStr != GAME_PROTOCOL) {
        log("Rejected a client because protocol strings don't match: Server '%s' and player '%s'.", GAME_PROTOCOL.c_str(), protoStr.c_str());

        if (protoStr.length() > 50)
            protoStr = "<unknown>";
        reply.str("Protocol mismatch: server: " + GAME_PROTOCOL + ", client: " + protoStr); // this message shouldn't be altered: client detects this exact form and allows translation (it's been the same at least since 0.5.0)
        return false;
    }
    if (!bot && get_human_count() == 0 && (join_start < join_end && (seconds < join_start || seconds > join_end) ||
                                           join_start > join_end && (seconds < join_start && seconds > join_end))) {
        log("Rejected a client because the server is not open at this time.");

        ostringstream temp;
        temp << "This server is open from ";
        temp << join_start / 3600 << ':' << setfill('0') << setw(2) << join_start / 60 % 60 << " to ";
        temp << join_end   / 3600 << ':' << setfill('0') << setw(2) << join_end   / 60 % 60 << " GMT. ";
        temp << "Come again in " << formatDuration(positiveModulo(join_start - seconds, 24 * 3600), english) << '.';
        if (!settings.get_join_limit_message().empty())
            temp << ' ' << settings.get_join_limit_message();
        reply.str(temp.str());
        return false;
    }
    if (player_count + reservedPlayerSlots - bot_count >= maxplayers) {
        log("Rejected a client because the server is full.");
        reply.U8(reject_server_full);
        return false;
    }
    if (!bot && host->isBanned(address)) {
        log("Rejected a client because their IP is banned (%s).", address.toString().c_str());
        reply.U8(reject_banned);
        return false;
    }
    const string name = msg.str();
    const string password = msg.str();
    if (!check_name(name))
        return false; // no need to explain, the client must not allow this
    if (!bot && password != settings.get_server_password()) {
        if (password.empty())
            reply.U8(reject_server_password_needed);
        else {
            log.security("Wrong server password. Password \"%s\" tried from %s, using name \"%s\".",
                         password.c_str(), address.toString().c_str(), name.c_str());
            reply.U8(reject_wrong_server_password);
        }
        return false;
    }
    const string player_password = msg.str();
    if (!bot && !host->check_name_password(name, player_password, true)) {
        if (player_password.empty())
            reply.U8(reject_player_password_needed);
        else {
            log.security("Wrong player password. Name \"%s\", password \"%s\" tried from %s.",
                         name.c_str(), player_password.c_str(), address.toString().c_str());
            reply.U8(reject_wrong_player_password);
        }
        return false;
    }
    ++reservedPlayerSlots;
    playerSlotReservationTime = get_time();
    reply.U8(maxplayers);
    reply.str(settings.get_hostname());
    if (msg.hasMore()) {
        customStoredData = msg.U8(); // store client protocol extensions level
        reply.U8(PROTOCOL_EXTENSIONS_VERSION);
    }
    else
        customStoredData = -1;
    while (msg.hasMore()) {
        const uint32_t extensionId = msg.U32();
        if (extensionId == 0)
            break;
        BinaryDataBlockReader extData(msg.block(msg.U8()));
        switch (extensionId) {
            /* To negotiate unofficial extension "example" at connection time, insert something like this: (search for "unofficial extension" for other relevant parts)
             * break; case EXAMPLE_IDENTIFIER: // define this somewhere to a random (to avoid clashes with other extensions) 32-bit constant you've picked
             *    res->storedExampleLevel = extData.U8(); // or whatever else you sent in clientbase.cpp; also remember to flag the extension disabled before this "while (msg.hasMore())"
             *    // elsewhere, copy the mechanism that handles customStoredData for storedExampleLevel
             *    reply.U32(EXAMPLE_IDENTIFIER);
             *    reply.U8(1); // the number of bytes of what is added to the reply by this extension after this
             *    reply.U8(EXAMPLE_VERSION); // or whatever else you want to send
             */
        };
    }
    if (msg.hasMore() && customStoredData <= PROTOCOL_EXTENSIONS_VERSION) { // check that unofficial extensions behave: if client has a known protocol extensions level, it doesn't send anything after the unofficials
        reply.clear();
        return false;
    }
    return true;
 } catch (BinaryReader::ReadError) {
    log("Format error in hello data from client.");
    reply.clear();
    return false;
 }
}

void ServerNetworking::eraseStaleWaiters() throw () {
    for (map<string, GameWaiter>::iterator wi = waiters.begin(); wi != waiters.end(); ) {
        if (wi->second.refreshTime < get_time() - 75 || host->findPlayerByName(wi->second.name) >= 0) { // assuming a refresh interval of 1 minute, a request can be delayed 15 seconds before the waiter is dropped; also drop players that have already joined the game
            const map<string, GameWaiter>::iterator erased = wi;
            ++wi;
            waiters.erase(erased);
        }
        else
            ++wi;
    }
}

void ServerNetworking::extendedQuery(BinaryReader& read, BinaryWriter& write) throw (BinaryReader::ReadError) {
    const uint32_t version = min(read.U32dyn8(), EXTENDED_QUERY_PROTOCOL_VERSION);
    write.U32dyn8(version);
    // Be careful when extending the query format; bytes not expected in older protocol versions can only be added after everything else.
    // The answer format is more relaxed since the answer is tagged with the exact protocol version and it's one the client is guaranteed to understand. Just be sure to check the version when sending anything new in the middle.
    const uint32_t contents = read.U32dyn8();
    // Signal that future extensions haven't been handled. This is helpful even if that could be determined from the protocol version as well.
    // Future versions aren't forced to reply to all current requests either and would signal skipped ones here (but naturally must know how to read the requests enough to skip them).
    write.U32dyn8(contents & (EQC_SimpleQueries | EQC_RegisterWaiter));
    if (contents & EQC_Language)
        for (;;) {
            const uint32_t flags = read.U32dyn8();
            const string code = read.str();
            if (flags & 1)
                break;
        }
    if (contents & EQC_InitialExtensions)
        for (;;) {
            const uint8_t header = read.U8();
            const bool last = header & 0x80;
            const int bytes = header & 0x7F;
            read.block(bytes);
            if (last)
                break;
        }
    if (contents & EQC_UnofficialExtensions) {
        const uint32_t nExtensions = read.U32dyn8();
        for (unsigned ei = 0; ei < nExtensions; ++ei) {
            const uint32_t extId = read.U32();
            BinaryDataBlockReader extData(read.block(read.U8()));
            (void)extId;
            #if 0 // if enabled, add EQC_UnofficialExtensions to the reply contents mask
            switch (extId) {
                // Process unofficial extensions here, preferrably with same id's as in the main protocol. Search for "unofficial extension" for more information and other relevant parts.
            /*break;*/ default:
                    write.U8(255); // Signal that the extension isn't used in the reply. For extensions you handle, send any other first byte and modify the rest of the answer format freely when the extension is detected.
            }
            #endif
        }
    }
    if (contents & EQC_SimpleQueries) {
        const uint32_t queries = read.U32dyn8();
        write.U32dyn8(queries & EQSQ_ALL);
        if (queries & EQSQ_GameProtocol) {
            write.str(GAME_STRING);
            write.str(GAME_PROTOCOL);
        }
        if (queries & EQSQ_Version)
            for (;;) {
                const uint32_t flags = read.U32dyn8();
                const uint32_t softLimit = read.U32dyn8() & 0xFFF;
                const uint32_t hardLimit = read.U32dyn8() & 0xFFF;
                write.str(getVersionString(flags & 2, softLimit, hardLimit, flags & 4));
                if (flags & 1)
                    break;
            }
        if (queries & EQSQ_ServerName)
            write.str(settings.get_hostname());
        if (queries & EQSQ_BasicSettings) {
            const bool password = !settings.get_server_password().empty();
            const bool ded = settings.dedicated();
            const bool spect = is_relay_active();
            const bool ft = world.physics.allowFreeTurning;
            const bool ff = world.physics.friendly_fire > 0;
            const bool pups = world.getPupConfig().pups_min > 0;
            const bool carryOwn = world.getConfig().carry_own_team_flag;
            const bool unofficialExt = false; // should be enabled if the server has unofficial extensions that (with current settings) give a gameplay advantage to players with supporting clients (and the extension in question hasn't been signaled in the query above)
            write.U32dyn16(maxplayers / 2 - 1 | password << 4 | ded << 5 | spect << 6 | ft << 7 | ff << 8 | pups << 9 | carryOwn << 10 | unofficialExt << 11);
            write.U32dyn8(host->allAdvantagesExtensionsLevel() + 1);
        }
        if (queries & EQSQ_Uptime) // uptime
            write.U32dyn8(world.frame / 10);
        if (queries & EQSQ_Players) {
            write.U32dyn8(get_human_count());
            write.U32dyn8(bot_count);
            eraseStaleWaiters();
            write.U32dyn8(waiters.size());
            if (queries & EQSQ_PlayerNames) {
                for (int i = 0; i < maxplayers; ++i)
                    if (world.player[i].used && !world.player[i].is_bot())
                        write.str(world.player[i].name);
                for (map<string, GameWaiter>::const_iterator wi = waiters.begin(); wi != waiters.end(); ++wi) {
                    const GameWaiter& w = wi->second;
                    write.str(w.name);
                    write.U32dyn8(w.minPlayers);
                }
            }
            if (queries & EQSQ_PlayerPings) {
                for (int i = 0; i < maxplayers; ++i)
                    if (world.player[i].used && !world.player[i].is_bot())
                        write.U32dyn8(world.player[i].ping);
                for (int i = 0; i < maxplayers; ++i)
                    if (world.player[i].used &&  world.player[i].is_bot())
                        write.U32dyn8(world.player[i].ping);
            }
        }
        if (queries & EQSQ_CurrentMap)
            write.str(host->current_map().title);
        if (queries & EQSQ_CurrentGame) {
            write.U32dyn8(world.teams[0].score());
            write.U32dyn8(world.teams[1].score());
            write.U32dyn8(world.getConfig().getCaptureLimit());
            write.U32dyn8(world.getMapTime() / 10);
            write.U32dyn8(world.getConfig().getTimeLimit() / 10);
        }
    }
    if (contents & EQC_RegisterWaiter) {
        GameWaiter w;
        w.name = read.str();
        const string password = read.str();
        w.minPlayers = read.U32dyn8() & 0x1F;
        w.refreshTime = get_time();
        if (w.minPlayers == 0)
            throw BinaryReader::DataOutOfRange();
        if (host->check_name_password(w.name, password, false)) {
            waiters[w.name] = w;
            write.U32dyn8(1);
        }
        else {
            log("Wrong name/password for %s trying to register as waiting.", w.name.c_str());
            write.U32dyn8(0);
        }
    }
}

bool ServerNetworking::sfunc_extended_query(void* customp, BinaryReader& read, BinaryWriter& write) throw () {
    ServerNetworking* const sn = static_cast<ServerNetworking*>(customp);
    if (sn->threadLock)
        sn->threadLockMutex.lock();
    bool result;
    try {
        sn->extendedQuery(read, write);
        result = true;
    } catch (BinaryReader::ReadError) {
        sn->log("Format error in extended query from client.");
        result = false;
    }
    if (sn->threadLock)
        sn->threadLockMutex.unlock();
    return result;
}

void ServerNetworking::sfunc_client_hello(void* customp, const Network::Address& address, ConstDataBlockRef data, ServerHelloResult* res, bool bot) throw () {
    ServerNetworking* const sn = static_cast<ServerNetworking*>(customp);
    if (sn->threadLock)
        sn->threadLockMutex.lock();

    BinaryWriter reply(res->customData, sizeof(res->customData));
    res->accepted = sn->clientHello(address, data, reply, res->customStoredData, bot);
    res->customDataLength = reply.size();

    if (sn->threadLock)
        sn->threadLockMutex.unlock();
}

void ServerNetworking::sfunc_leetnet_client_hello(void* customp, const Network::Address& address, ConstDataBlockRef data, ServerHelloResult* res) throw () {
    sfunc_client_hello(customp, address, data, res, false);
}

ControlledPtr<LocalConnection> ServerNetworking::newLocalConnection() throw () {
    const int cid = leetServer->reserveClientId();
    if (cid < 0)
        return ControlledPtr<LocalConnection>(0);
    LocalConnection* const conn = new LocalConnection(*this, cid);
    nAssert(!clientConnection[cid]);
    clientConnection[cid] = new LocalClient(*this, *conn);
    return give_control(conn);
}

void ServerNetworking::leetnet_client_connected(int client_id, int customStoredData) throw () {
    nAssert(!clientConnection[client_id]);
    clientConnection[client_id] = new LeetnetClient(leetServer, client_id);
    client_connected(client_id, customStoredData);
}

void ServerNetworking::sfunc_leetnet_client_connected(void* customp, int client_id, int customStoredData) throw () {
    ServerNetworking* sn = static_cast<ServerNetworking*>(customp);
    if (sn->threadLock)
        sn->threadLockMutex.lock();
    sn->leetnet_client_connected(client_id, customStoredData);
    if (sn->threadLock)
        sn->threadLockMutex.unlock();
}

void ServerNetworking::local_client_connected(int client_id, int customStoredData) throw () {
    nAssert(clientConnection[client_id]); // set up at newLocalConnection
    client_connected(client_id, customStoredData);
}

void ServerNetworking::sfunc_local_client_connected(void* customp, int client_id, int customStoredData) throw () {
    ServerNetworking* sn = static_cast<ServerNetworking*>(customp);
    if (sn->threadLock)
        sn->threadLockMutex.lock();
    sn->local_client_connected(client_id, customStoredData);
    if (sn->threadLock)
        sn->threadLockMutex.unlock();
}

void ServerNetworking::sfunc_client_disconnected(void* customp, int client_id, bool reentrant) throw () {
    ServerNetworking* sn = static_cast<ServerNetworking*>(customp);
    if (sn->threadLock && !reentrant)
        sn->threadLockMutex.lock();
    sn->client_disconnected(client_id);
    if (sn->threadLock && !reentrant)
        sn->threadLockMutex.unlock();
}

void ServerNetworking::sfunc_client_lag_status(void* customp, int client_id, int status) throw () {
    (void)(customp && client_id && status);
}

void ServerNetworking::sfunc_client_data(void* customp, int client_id, ConstDataBlockRef data) throw () {
    ServerNetworking* sn = static_cast<ServerNetworking*>(customp);
    if (sn->threadLock)
        sn->threadLockMutex.lock();
    sn->incoming_client_data(client_id, data);
    if (sn->threadLock)
        sn->threadLockMutex.unlock();
}

void ServerNetworking::sfunc_client_ping_result(void* customp, int client_id, int pingtime) throw () {
    if (pingtime < 0)
        pingtime = 0;
    ServerNetworking* sn = static_cast<ServerNetworking*>(customp);
    if (sn->threadLock)
        sn->threadLockMutex.lock();
    sn->ping_result(client_id, pingtime);
    if (sn->threadLock)
        sn->threadLockMutex.unlock();
}

ServerNetworking::LocalClient::~LocalClient() throw () {
    conn.sc.disconnect();
}

void ServerNetworking::LocalClient::disconnect(int timeout, Disconnect_reason reason) throw () {
    (void)(timeout && reason);
    conn.sc.disconnect();
    conn.cs.disconnect(true);
}

Network::Address ServerNetworking::LocalClient::get_client_address() const throw () {
    try {
        return Network::Address("127.0.0.1");
    } catch (Network::BadIP) { nAssert(0); }
}

void ServerNetworking::LocalClient::ping() throw () {
    conn.cs.reportPing(conn.sc.getPing());
}

void ServerNetworking::LocalClient::send_frame(ConstDataBlockRef data) throw () {
    conn.sc.sendFrame(data);
}

void ServerNetworking::LocalClient::send_message(ConstDataBlockRef data) throw () {
    conn.sc.sendMessage(data);
}

ConstDataBlockRef ServerNetworking::LocalClient::receive_message() throw () {
    return conn.cs.openMessage();
}

void ServerNetworking::LocalClient::received_message_read() throw () {
    conn.cs.closeMessage();
}
