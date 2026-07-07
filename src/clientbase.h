/*
 *  clientbase.h
 *
 *  Copyright (C) 2002 - Fabio Reis Cecin
 *  Copyright (C) 2003, 2004, 2005, 2006, 2008, 2009, 2010, 2011 - Niko Ritari
 *  Copyright (C) 2003, 2004, 2005, 2006, 2008 - Jani Rivinoja
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

#ifndef CLIENTBASE_H_INC
#define CLIENTBASE_H_INC

#include "binaryaccess.h"
#include "client_interface.h"
#include "function_utility.h"
#include "log.h"
#include "map_with_helpers.h"
#include "mutex.h"
#include "world.h"

class ClientBase;
class client_c; // of leetnet
class client_runes_t;

/* ThreadMessage represents a class of actions that can't be performed by callbacks within a network thread in a thread-safe way.
 * These are queued for the main thread to execute.
 * Note: they will be executed in order relative to each other but out of order relative to messages processed in the network thread.
 * Handling anything that isn't protected by mutexes (frameMutex and downloadMutex) should be applied only through the ThreadMessage queue.
 */
class ThreadMessage {   // the subclasses (named starting with TM_) except TM_ServerSettings are direct property of the ClientBase class and mostly internally declared in client.cpp
public:
    virtual ~ThreadMessage() throw () { }
    virtual void execute(ClientBase* cl) const throw () = 0;
};

class ClientBase {
    friend class ClientPhysicsCallbacks;
    friend class TM_DoDisconnect;
    friend class TM_Text;
    friend class TM_Sound;
    friend class TM_MapChange;
    #ifndef DEDICATED_SERVER_ONLY
    friend class TM_NameAuthenticationRequest;
    friend class TM_GunexploEffect;
    #endif
    friend class TM_ConnectionUpdate;

    WorldCoords readPosition(BinaryReader& read) const throw (BinaryReader::ReadError);

protected:

    class ServerDataError { }; // exception; also applies to replay data errors

    MemoryLog& externalErrorLog;    // this is emptied to the error dialog as we go; only rare leftovers are left to caller
    DualLog errorLog;
    // currently not in use:    SupplementaryLog<FileLog> securityLog;
    mutable LogSet log; // this is the only access to logs in normal operation

    // world (these variables all locked by frameMutex)
    ClientWorld fx; //#fix fx and fd: two maps, etc.
    #ifndef DEDICATED_SERVER_ONLY
    ClientWorld fd;
    std::vector<ClientPlayer*> players_sb;  // player pointers for scoreboard
    #endif
    int me;
    Mutex frameMutex;
    int maxplayers;

    // network
    client_c *client;
    double lastpackettime;
    uint8_t clFrameSent, clFrameWorld;
    double frameOffsetDeltaTotal;
    int frameOffsetDeltaNum;
    double netsendAdjustment;
    double averageLag;
    double frameReceiveTime;    // when fx was received
    ClientControls controlHistory[256]; // the section between clFrameWorld and clFrameSent (circularly) is in use on a given moment
    ClientControls sentControls;
    double svFrameHistory[256];    // the section between clFrameWorld and clFrameSent (circularly) is in use on a given moment

    GunDirection gunDir; // used only with allowFreeTurning
    double gunDirRefreshedTime;
    double next_respawn_time;
    int flag_return_delay;
    volatile bool connected;
    bool map_ready;
    int clientReadiesWaiting;
    std::string servermap;  //last map command from server

    int protocolExtensions; // -1 means unextended protocol, 0 up are extension version numbers (<= PROTOCOL_EXTENSIONS_VERSION)

    std::deque<ThreadMessage*> messageQueue;    // access with frameMutex locked; delete the object when removing from the queue

    #ifndef DEDICATED_SERVER_ONLY
    bool map_time_limit;
    int map_start_time; // in get_time() seconds -> can be negative
    int map_end_time;
    bool extra_time_running;
    #endif
    int8_t remove_flags;
    bool carry_own_team_flag;
    bool capture_away_from_base;
    bool lock_team_flags_in_effect;
    bool lock_wild_flags_in_effect;
    bool capture_on_team_flags_in_effect;
    bool capture_on_wild_flags_in_effect;
    int see_minimap_player_properties;

    bool gameshow;
    int gameover_plaque;

    std::string playername;
    Network::Address serverIP;

    #ifndef DEDICATED_SERVER_ONLY
    bool botmode;
    #else
    static const bool botmode = true;
    #endif

    volatile bool abortThreads;

    #ifndef DEDICATED_SERVER_ONLY
    bool replaying;
    bool spectating;
    unsigned replay_version;
    #else
    static const bool replaying = false; // To avoid lots of ifdefs.
    #endif
    volatile bool mapChanged;

    const ClientExternalSettings extConfig;
    #ifndef DEDICATED_SERVER_ONLY
    SimpleGameSettings gameSettings; // for stats only
    #endif

    void addThreadMessage(ThreadMessage* msg) throw () { messageQueue.push_back(msg); }
    void setMaxPlayers(int num) throw () {
        maxplayers = num;
        fx.setMaxPlayers(num);
        #ifndef DEDICATED_SERVER_ONLY
        fd.setMaxPlayers(num);
        #endif
    }

    FormattedText formatName(int pid) const throw ();

    // world    //#fix: should these be moved to ClientWorld?
    virtual void rocketHitWallCallback(int rid, bool power, const WorldCoords& pos) throw ();
    void rocketOutOfBoundsCallback(int rid) throw ();
    virtual void playerHitWallCallback(int pid) throw () { (void)pid; }
    virtual void playerHitPlayerCallback(int pid1, int pid2) throw () { (void)(pid1 && pid2); }
    bool shouldApplyPhysicsToPlayerCallback(int pid) throw ();

    void remove_useless_flags() throw ();

    // network
    void prepareForConnect() throw (); // call with frameMutex locked
    void connect(const std::string& serverAddress, const std::string& serverPassword, const std::string& playerPassword) throw (); // call with frameMutex locked
    void disconnect_command() throw ();  // do not call from a network thread
    void connection_update(int connect_result, ConstDataBlockRef data) throw ();
    virtual void client_connected(ConstDataBlockRef data) throw ();    // call with frameMutex locked
    void disconnected_base(ConstDataBlockRef data) throw ();
    void sendMinimapBandwidthAny(int players) throw ();
    void issue_change_name_command() throw ();
    void send_client_ready() throw ();
    void readMinimapPlayerPosition(BinaryReader& reader, int pid) throw (BinaryReader::ReadError);
    void process_live_frame_data(ConstDataBlockRef data) throw (ServerDataError);
    void process_message(ConstDataBlockRef data) throw (ServerDataError);
    void process_incoming_data(ConstDataBlockRef data) throw (ServerDataError);
    void process_server_data(ConstDataBlockRef data) throw ();

    void server_map_command(const std::string& mapname, uint16_t server_crc) throw ();
    bool load_map(const std::string& directory, const std::string& mapname, uint16_t server_crc) throw ();

    void handlePendingThreadMessages() throw (); // should only be called by the main thread; call with frameMutex locked

    // client callbacks
    static void cfunc_connection_update(void* customp, int connect_result, ConstDataBlockRef data) throw ();
    static void cfunc_server_data(void* customp, ConstDataBlockRef data) throw ();

    // functionality from subclasses; if there is a default implementation (doing nothing), it's used for Robot
    virtual void print_message(Message_type type, const FormattedText& msg, int sender_team = -1) throw () { (void)type; (void)msg; (void)sender_team; }
    virtual void play_sound(int sample) throw () { (void)sample; }
    virtual void client_disconnected(ConstDataBlockRef data) throw () = 0;
    virtual void connect_failed_denied(ConstDataBlockRef data) throw () { nAssert(0); (void)data; }
    virtual void connect_failed_unreachable() throw () { nAssert(0); }
    virtual void connect_failed_socket() throw () { nAssert(0); }
    virtual void download_server_file(const std::string& type, const std::string& name) throw () { nAssert(0); (void)type; (void)name; } // ### FIX: Override in Robot to disconnect bot or something.
    virtual void process_udp_download_chunk(ConstDataBlockRef data, bool last) throw () { nAssert(0); (void)data; (void)last; }
    virtual void processNameAuthenticationRequest() throw () { nAssert(0); }
    virtual void createGunexploEffect(const WorldCoords& pos, int team, double time) throw () { (void)pos; (void)team; (void)time; }
    virtual void process_replay_packet(ConstDataBlockRef data) throw (ServerDataError) { nAssert(0); (void)data; }

    virtual void netRocketFired(const WorldCoords& pos, bool power) throw () { (void)pos; (void)power; }
    virtual void netRocketHitPlayer(int rockid, int rokx, int roky, double time) throw () { (void)(rockid && rokx && roky && time); }
    virtual void netPowerCollision(int target, double time) throw () { (void)(target && time); }
    virtual void net_data_sound(BinaryReader& read) throw (BinaryReader::ReadError) { (void)read; }
    virtual void net_data_registration_response(BinaryReader& read) throw (BinaryReader::ReadError) { nAssert(0); (void)read; }
    virtual void net_data_quick_map_list(BinaryReader& read) throw (BinaryReader::ReadError) { (void)read; }
    virtual void net_data_map_list(BinaryReader& read) throw (BinaryReader::ReadError) { (void)read; }
    virtual void net_data_crap_update(BinaryReader& read) throw (BinaryReader::ReadError) { (void)read; }
    virtual void net_data_reset_map_list(BinaryReader& read) throw (BinaryReader::ReadError) { (void)read; }
    virtual void net_data_map_vote(BinaryReader& read) throw (BinaryReader::ReadError) { (void)read; }
    virtual void net_data_map_votes_update(BinaryReader& read) throw (BinaryReader::ReadError) { (void)read; }
    virtual void net_text_message(Message_type type, int sender_team, const std::string& text) throw () = 0;
    virtual void netKill(int attacker, int target, DamageType cause, bool carrier_defended, bool flag_defended, Statistics::FlagType, bool spree_ended, bool spree_started) throw (); // empty
    virtual void netSuicide(int pid, Statistics::FlagType, bool spree_ended) throw () { (void)(pid && spree_ended); }
    virtual void netFlagTake(int pid, Statistics::FlagType) throw () { (void)(pid); }
    virtual void netFlagReturn(int pid) throw () { (void)pid; }
    virtual void netFlagDrop(int pid, Statistics::FlagType) throw () { (void)(pid); }
    virtual void netTeamChange(int pl1, int pl2 = -1) throw () { (void)(pl1 && pl2); }
    virtual void netStatsReady() throw () { }
    virtual void netMapChange(const std::string& maptitle, const int map_number, const int total_maps) throw () { (void)maptitle; (void)(map_number && total_maps); }
    virtual void netGameoverPeriodStart(uint32_t redScore, uint32_t blueScore, int caplimit, int timelimit) throw () { (void)(redScore && blueScore && caplimit && timelimit); }
    virtual void netGameoverPeriodEnd() throw () { }
    virtual void netGameStarted() throw () { }
    virtual void netPhysicsChanged() throw () { }
    virtual void netSetHostname(const std::string& name) throw () { (void)name; }
    virtual void netSetCurrentMap(int idx) throw () { (void)idx; }

    virtual std::string getPlayerPassword() const throw () = 0;

    void startBase(ControlledPtr<client_c> networkProvider) throw ();
    virtual void stop() throw ();

public:
    ClientBase(const ClientExternalSettings& config, Log& clientLog, MemoryLog& externalErrorLog_) throw ();
    virtual ~ClientBase() throw ();
};

class ClientPhysicsCallbacks : public PhysicsCallbacksBase {
    ClientBase& c;

public:
    ClientPhysicsCallbacks(ClientBase& c_) throw () : c(c_) { }

    bool collideToRockets() const throw () { return false; }
    bool collidesToRockets(int) const throw () { return false; }
    bool collidesToPlayers(int) const throw () { return true; }
    bool gatherMovementDistance() const throw () { return false; }
    bool allowRoomChange() const throw () { return false; }
    void addMovementDistance(int, double) throw () { }
    void playerScreenChange(int) throw () { }
    void rocketHitWall(int rid, bool power, const WorldCoords& pos) throw () { c.rocketHitWallCallback(rid, power, pos); }
    bool rocketHitPlayer(int, int) throw () { return false; }
    void playerHitWall(int pid) throw () { c.playerHitWallCallback(pid); }
    PlayerHitResult playerHitPlayer(int pid1, int pid2, double) throw () { c.playerHitPlayerCallback(pid1, pid2); return PlayerHitResult(false, false, 1., 1.); }
    void rocketOutOfBounds(int rid) throw () { c.rocketOutOfBoundsCallback(rid); }
    bool shouldApplyPhysicsToPlayer(int pid) throw () { return c.shouldApplyPhysicsToPlayerCallback(pid); }
};

class TM_DoDisconnect : public ThreadMessage {
public:
    void execute(ClientBase* cl) const throw () { cl->disconnect_command(); }
};

class TM_Text : public ThreadMessage {
    Message_type type;
    FormattedText text;
    int team;   // -1 for non-team messages

public:
    TM_Text(Message_type type_, const FormattedText& text_, int team_ = -1) throw () : type(type_), text(text_), team(team_) { }
    ~TM_Text() throw () { }
    void execute(ClientBase* cl) const throw () {
        cl->print_message(type, text, team);
    }
};

class TM_Sound : public ThreadMessage {
    int sample;

public:
    TM_Sound(int sample_) throw () : sample(sample_) { }
    void execute(ClientBase* cl) const throw () {
        cl->play_sound(sample);
    }
};

#ifndef DEDICATED_SERVER_ONLY
class TM_GunexploEffect : public ThreadMessage {
    int team;
    WorldCoords pos;
    double time;

public:
    TM_GunexploEffect(int team_, double time_, const WorldCoords& pos_) throw () : team(team_), pos(pos_), time(time_) { }
    void execute(ClientBase* cl) const throw () {
        cl->createGunexploEffect(pos, team, time);
    }
};

class GuiClient;

class TM_ServerSettings : public ThreadMessage { // implementation in guiclient.cpp
    uint8_t caplimit, timelimit, extratime, extratime_periods;
    uint16_t misc1, pupMin, pupMax, pupAddTime, pupMaxTime;
    int flag_return_delay;

    void addLine(GuiClient* cl, const std::string& caption, const std::string& value) const throw ();

public:
    TM_ServerSettings(uint8_t caplimit_, uint8_t timelimit_, uint8_t extratime_, uint8_t extratime_periods_, uint16_t misc1_,
                      uint16_t pupMin_, uint16_t pupMax_, uint16_t pupAddTime_, uint16_t pupMaxTime_, int flag_return_delay_) throw () :
        caplimit(caplimit_), timelimit(timelimit_), extratime(extratime_), extratime_periods(extratime_periods_), misc1(misc1_),
        pupMin(pupMin_), pupMax(pupMax_), pupAddTime(pupAddTime_), pupMaxTime(pupMaxTime_), flag_return_delay(flag_return_delay_) { }
    void execute(ClientBase* cl) const throw ();
};
#endif

#endif
