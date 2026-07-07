/*
 *  guiclient.h
 *
 *  Copyright (C) 2002 - Fabio Reis Cecin
 *  Copyright (C) 2003, 2004, 2005, 2006, 2008, 2009, 2010, 2011 - Niko Ritari
 *  Copyright (C) 2003, 2004, 2005, 2006, 2008, 2010 - Jani Rivinoja
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

#ifndef GUICLIENT_H_INC
#define GUICLIENT_H_INC

#include <map>
#include <set>
#include <sstream>

#include "clientbase.h"
#include "client_menus.h"
#include "gameserver_interface.h"
#include "graphics.h"
#include "menu.h"
#include "sounds.h"
#include "thread.h"

//server record
class ServerListEntry {
public:
    bool refreshed;
    bool noresponse;
    int ping;
    std::string info;

    ServerListEntry() throw () : refreshed(false), noresponse(true), ping(0) { }

    const Network::Address& address() const throw () { return addr; }
    std::string addressString() const throw ();
    bool setAddress(const std::string& address) throw ();    // returns false if address is invalid
    void setAddress(const Network::Address& address) throw ();

private:
    Network::Address addr;
};

class FileDownload {
public:
    std::string fileType, shortName, fullName;

    FileDownload(const std::string& type, const std::string& name, const std::string& filename) throw ();
    ~FileDownload() throw ();
    bool isActive() const throw () { return (fp != 0); }
    int progress() const throw ();
    bool start() throw ();
    bool save(ConstDataBlockRef data) throw ();
    void finish() throw ();

private:
    FILE* fp;
};

enum Menu_selection {   // screens that aren't quite menus //#fix: get rid
    menu_none,
    menu_maps,
    menu_players,
    menu_teams
};

class ServerThreadOwner {
    LogSet log;
    bool threadFlag, quitFlag;  // threadFlag is true whenever the thread hasn't been joined, quitFlag false only when the server is successfully running
    int runPort;
    Thread serverThread;

    void threadFn(const ServerExternalSettings& config) throw ();

public:
    ServerThreadOwner(LogSet logs) throw () : log(logs), threadFlag(false), quitFlag(true) { }
    ~ServerThreadOwner() throw () { if (threadFlag) stop(); }
    bool running() throw () { if (quitFlag && threadFlag) stop(); nAssert(quitFlag != threadFlag); return !quitFlag; }
    int port() const throw () { return runPort; }
    void start(int port, const ServerExternalSettings& config) throw ();
    void stop() throw ();
};

class RankingPasswordManager {
public:
    typedef FunctionHolder1<void, std::string> TokenCallbackT;  // an empty string is given to indicate no token

    enum PasswordStatus {
        PS_noPassword,
        PS_starting,
        PS_socketError,
        PS_sending,
        PS_sendError,
        PS_receiving,
        PS_recvError,
        PS_invalidResponse,
        PS_unavailable,
        PS_tokenReceived,
        PS_badLogin,
        // these follow from server status and passStatus is never one of these:
        PS_tokenSent,
        PS_tokenAccepted,
        PS_tokenRejected
    };

    RankingPasswordManager(LogSet logs, TokenCallbackT tokenCallbackFunction, int threadPriority) throw ();   // warning: the callback will be called from a background thread
    ~RankingPasswordManager() throw () { stop(); }

    void stop() throw ();
    void changeData(const std::string& newName, const std::string& newPass) throw ();
    PasswordStatus status() const throw () { if (passStatus == PS_tokenReceived && servStatus != PS_noPassword) return servStatus; else return passStatus; }
    std::string statusAsString() const throw ();
    std::string getToken() const throw () { return token.read(); }

    void serverProcessingToken()  throw () { servStatus = PS_tokenSent;        }
    void serverAcceptsToken()     throw () { servStatus = PS_tokenAccepted;    }
    void serverRejectsToken()     throw () { servStatus = PS_tokenRejected;    }
    void disconnectedFromServer() throw () { servStatus = PS_noPassword;       }   // actually, no server

private:
    LogSet log;
    TokenCallbackT tokenCallback;

    volatile bool quitThread;   // set to quit the thread
    volatile PasswordStatus passStatus; // set by both the thread and the regular interface to indicate the status
    Thread thread;
    int priority;

    PasswordStatus servStatus;

    std::string name, password; // constant while the thread is running
    Threadsafe<std::string> token;

    void start() throw ();
    void threadFn() throw ();
    void setToken(const std::string& newToken) throw ();
};

class GuiClient : public ClientBase, public ClientInterface {
    friend class TM_ServerSettings;

    static const int disappearedFlagAlpha = 150;

    ServerThreadOwner listenServer;

    bool mapWrapsX, mapWrapsY;

    Mutex downloadMutex;
    std::list<FileDownload> downloads;

    RankingPasswordManager rankingPassword;
    uint32_t fdp;
    uint32_t max_world_rank;

    Mutex mapInfoMutex;
    std::vector<MapInfo> maps;
    std::vector< std::pair<const MapInfo*, int> > sortedMaps;
    unsigned mapListReadPosition;

    MapListSortKey mapListSortKey;
    bool mapListChangedAfterSort;

    typedef std::map<uint16_t, MapInfo> MapInfoCache;
    MapInfoCache mapInfoCache;

    std::map<std::string, int> fav_maps;
    int current_map;
    int map_vote;
    bool want_change_teams;
    bool want_map_exit;
    bool want_map_exit_delayed;

    // GUI
    Menu_main menu;
    Menu_text m_connectProgress;
    Menu_text m_dialog; // take care not to open multiple dialogs (same goes to other menus too); to allow that, a vector of Menu_text should be created
    Menu_text m_errors;
    Menu_playerPassword m_playerPassword;
    Menu_serverPassword m_serverPassword;
    Menu_text m_serverInfo;
    Menu_text m_notResponding; // not to be put to openMenus

    Menu_language m_initialLanguage; // only for setting the language at startup

    MenuStack openMenus;

    Menu_selection menusel; // a special screen rather than menu: maplist, stats
    bool stats_autoshowing;

    bool quitCommand;

    std::string hostname;
    int red_final_score, blue_final_score;

    std::string edit_map_vote;
    int player_stats_page;
    double lastAltEnterTime;
    bool deadAfterHighlighted;

    double FPS;
    int framecount;
    double frameCountStartTime;

    std::vector<ServerListEntry> gamespy;
    std::vector<ServerListEntry> mgamespy;  //gamespy of master server
    Mutex serverListMutex;

    RegisterMouseClicks mouseClicked;

    enum RefreshStatus { RS_none, RS_running, RS_failed, RS_contacting, RS_connecting, RS_receiving };
    volatile RefreshStatus refreshStatus;   // thread communication variable

    std::string password_file;

    std::string talkbuffer;
    int talkbuffer_cursor;
    std::list<Message> chatbuffer;
    bool show_all_messages;
    std::vector<std::string> highlight_text;

    const std::string quickMessageFile;

    Graphics graphics;
    bool screenshot;
    std::ifstream replay;
    double replay_rate, replayTime, replaySubFrame;
    bool replay_paused;
    bool replay_stopped;
    bool replay_buffer_end_reached;
    bool replay_first_frame_loaded;
    unsigned replay_start_frame;
    unsigned replay_length;
    uint32_t replay_players_present;
    std::pair<int, int> replayTopLeftRoom;
    double visible_rooms;

    Network::TCPSocket spectate_socket;
    bool spectate_data_received;
    std::stringstream spectate_buffer;

    Sounds client_sounds;

    std::pair<int, int> predrawnRoom;
    int predrawnVisibleRooms;
    bool predrawnWithScroll;

    std::ofstream message_log;
    bool messageLogOpen;

    ServerExternalSettings serverExtConfig;

    class GFXMode {
    public:
        int width, height, depth;
        bool windowed, flipping;

        GFXMode() throw () : width(-1) { }
        GFXMode(int w, int h, int d, bool win, int flip) throw () : width(w), height(h), depth(d), windowed(win), flipping(flip) { }
        bool used() const throw () { return width != -1; }
    };

    GFXMode workingGfxMode;

    class SettingManager : public SettingCollector {
    public:
        typedef std::map<ClientCfgSetting, SaverLoader*> MapT;

        ~SettingManager() throw () { for (MapT::iterator i = settings.begin(); i != settings.end(); ++i) delete i->second; }

        void add(ClientCfgSetting key, SaverLoader* sl) throw () { settings[key] = sl; }

        SettingCollector::SaverLoader* find(ClientCfgSetting key) const throw () { MapT::const_iterator i = settings.find(key); return i == settings.end() ? 0 : i->second; }
        const MapT& read() const throw () { return settings; }

    private:
        MapT settings;
    };
    SettingManager settings;

    std::vector<std::vector<std::string> > load_all_player_passwords() const throw ();
    std::string load_player_password(const std::string& name, const std::string& address) const throw ();
    void save_player_password(const std::string& name, const std::string& address, const std::string& password) const throw ();
    void remove_player_password(const std::string& name, const std::string& address) const throw ();
    int remove_player_passwords(const std::string& name) const throw ();

    void initMenus() throw ();

    struct ReplayDescriptor {
        std::string description;
        bool final;
        bool confirmed; // confirmed that the file exists

        ReplayDescriptor(const std::string& desc, bool final_) throw () : description(desc), final(final_), confirmed() { }

        bool operator<(const ReplayDescriptor&) const throw () { return false; }
    };
    typedef std::vector< std::pair<std::string, ReplayDescriptor> > ReplayList;
    typedef std::map<std::string, ReplayDescriptor> ReplayCache;

    static const uint32_t replayCacheVersionIdentifier = 0x56E0FED5, mapInfoCacheVersionIdentifier = 0x2F4348C9;

    std::string replayCacheFile() const throw ();
    ReplayCache loadReplayCache() const throw ();
    void        saveReplayCache(const ReplayList& replays) const throw ();

    // menu callback functions
    void MCF_menuOpener(Menu& menu) throw ();
    void MCF_menuCloser() throw ();
    void MCF_connect(Textarea& target) throw ();
    void MCF_cancelConnect() throw ();
    void MCF_disconnect() throw ();
    void MCF_exitOutgun() throw ();
    void MCF_replay(TreeItem& target) throw ();
    void MCF_prepareReplayMenu() throw ();
    void MCF_prepareMainMenu() throw ();
    void MCF_preparePlayerMenu() throw ();
    void MCF_prepareDrawPlayerMenu() throw ();
    void MCF_playerMenuClose() throw ();
    void MCF_nameChange() throw ();  // only function to clear the password
    void MCF_randomName() throw ();
    void MCF_removePasswords() throw ();
    void MCF_prepareGameMenu() throw ();
    void MCF_prepareControlsMenu() throw ();
    void MCF_keyboardLayout() throw ();
    void MCF_joystick() throw ();
    void MCF_messageLogging() throw ();
    void MCF_screenDepthChange() throw ();
    void MCF_screenModeChange() throw ();
    void MCF_gfxThemeChange() throw ();
    void MCF_fontChange() throw ();
    void MCF_visibleRoomsPlayChange() throw ();
    void MCF_visibleRoomsReplayChange() throw ();
    void MCF_antialiasChange() throw ();
    void MCF_transpChange() throw ();
    void MCF_statsBgChange() throw ();
    void MCF_prepareScrModeMenu() throw ();
    void MCF_prepareDrawScrModeMenu() throw ();
    void MCF_prepareGfxThemeMenu() throw ();
    void MCF_sndEnableChange() throw ();
    void MCF_sndVolumeChange() throw ();
    void MCF_sndThemeChange() throw ();
    void MCF_prepareSndMenu() throw ();
    void MCF_refreshLanguages() throw ();
    void MCF_acceptLanguage(Textarea& target) throw ();
    void MCF_acceptInitialLanguage(Textarea& target) throw ();
    void MCF_acceptBugReporting() throw ();
    void MCF_prepareServerMenu() throw ();
    void MCF_updateServers() throw ();
    void MCF_refreshServers() throw ();
    void MCF_prepareAddServer() throw ();
    void MCF_addServer() throw ();
    bool MCF_addressEntryKeyHandler(char scan, unsigned char chr) throw ();
    bool MCF_addRemoveServer(Textarea& target, char scan, unsigned char chr) throw ();
    bool MCF_spectateEntryKeyHandler(char scan, unsigned char chr) throw ();
    void MCF_playerPasswordAccept() throw ();
    void MCF_serverPasswordAccept() throw ();
    void MCF_clearErrors() throw ();
    void MCF_prepareOwnServerMenu() throw ();
    void MCF_startServer() throw ();
    void MCF_playServer() throw ();
    void MCF_stopServer() throw ();

    int refreshLanguages(Menu_language& lang_menu) throw ();
    void acceptLanguage(const std::string& lang, bool restart_message) throw ();

    void load_highlight_texts() throw ();
    void load_fav_maps() throw ();
    void apply_fav_maps() throw ();
    void updateMapPreference(MapInfo& mi) const throw ();

    void saveSettings() const throw ();

    void loadQuickMessages() throw ();
    void saveQuickMessages() const throw ();

    std::string mapInfoCacheFile() const throw ();
    void loadMapInfoCache() throw ();
    void saveMapInfoCache() const throw ();

    void loadHelp() throw ();
    void addSplashLine(std::string line) throw (); // internal to loadSplashScreen
    void loadSplashScreen() throw ();
    void openMessageLog() throw ();
    void closeMessageLog() throw ();
    void CB_rankingToken(std::string token) throw (); // callback called by rankingPassword from another thread

    bool screenModeChange() throw ();    // the return value should be tested at the first call

    // network
    void connect_command(bool loadPassword) throw ();    // call with frameMutex locked

    void client_connected(ConstDataBlockRef data) throw ();    // call with frameMutex locked
    void client_disconnected(ConstDataBlockRef data) throw ();
    void connect_failed_denied(ConstDataBlockRef data) throw ();
    void connect_failed_unreachable() throw ();
    void connect_failed_socket() throw ();

    void send_player_token() throw ();
    void send_ranking_participation() throw ();
    void sendFavoriteColors() throw ();
    void sendMinimapBandwidth() throw ();
    void sendNegativeVotes() throw ();

    void change_name_command() throw ();

    void send_chat(const std::string& msg) throw ();
    void send_frame(bool newFrame, bool forceSend) throw ();

    void process_replay_packet(ConstDataBlockRef data) throw (ServerDataError);
    int process_replay_frame_data(ConstDataBlockRef data) throw (BinaryReader::ReadError); // returns number of bytes read - not necessarily all of data
    int process_replay_frame_data_version_0(ConstDataBlockRef data) throw (BinaryReader::ReadError); // returns number of bytes read - not necessarily all of data

    std::string refreshStatusAsString() const throw ();
    void getServerListThread() throw ();
    void refreshThread() throw ();
    bool refresh_all_servers() throw ();
    bool getServerList() throw ();
    bool get_local_servers() throw ();
    bool parseServerList(std::istream& response) throw ();

    void check_download() throw ();  // call with downloadMutex locked
    void process_udp_download_chunk(ConstDataBlockRef, bool last) throw ();
    void download_server_file(const std::string& type, const std::string& name) throw ();

    void processNameAuthenticationRequest() throw ();

    const WorldCoords& playerPos(int pid) const throw ();
    WorldCoords viewTopLeft() const throw ();
    std::pair<int, int> topLeftRoom() const throw ();

    bool seePropertiesRemotely(int pid) const throw ();

    // GUI
    void erase_first_message() throw ();
    void print_message(Message_type type, const FormattedText& msg, int sender_team = -1) throw ();

    void save_screenshot() throw ();
    void toggle_help() throw ();
    template<class MenuT> void showMenu(MenuT& menu) throw () { openMenus.open(&menu.menu); }
    void predraw() throw ();

    bool on_screen(int x, int y) const throw (); // returns true if any part of room (x,y) is on screen
    bool on_screen(int rx, int ry, double lx, double ly, double fudge = 0) const throw (); // coordinates within "fudge" local units from screen border are also considered on screen
    bool on_screen_exact(int x, int y) const throw ();
    bool on_screen_exact(int rx, int ry, double lx, double ly, double fudge = 0) const throw ();
    bool player_on_screen(int pid) const throw ();
    bool player_on_screen_exact(int pid) const throw ();

    bool repeatMapX() const throw ();
    bool repeatMapY() const throw ();

    typedef Graphics::VisibilityMap VisibilityMap;

    void draw_game_frame() throw ();
    void draw_map(const VisibilityMap& roomVis) throw ();
    void draw_playfield() throw ();
    VisibilityMap calculateVisibilities() throw (); // calculates how well each player's position is known (applies to out-of-screen players only) and returns a map of how well each room is known (according to the most visible player there)
    int calculatePlayerAlpha(int pid) const throw ();
    void draw_player(int pid, double time, bool live) throw ();
    void draw_game_menu() throw ();

    ClientControls readControls(bool canUseKeypad, bool useCursorKeys) const throw ();
    ClientControls readControlsInGame() const throw ();
    bool firePressed() throw ();
    void refreshGunDir() throw ();

    void handleKeypress(int sc, int ch, bool withControl, bool alt_sequence) throw ();   // sc = scancode, ch = character, as returned by readkey
    bool handleInfoScreenKeypress(int sc, int ch, bool withControl, bool alt_sequence) throw ();  // sc = scancode, ch = character, as returned by readkey
    void handleGameKeypress(int sc, int ch, bool withControl, bool alt_sequence) throw ();   // sc = scancode, ch = character, as returned by readkey

    void play_sound(int sample) throw ();

    void start_replay(const std::string& filename) throw ();
    bool start_replay(std::istream& in) throw ();
    void continue_replay(bool controls = false) throw ();
    void continue_replay(std::istream& in, bool controls = false) throw ();
    void process_replay_controls() throw ();
    void stop_replay() throw ();
    void start_spectating(const std::string& host) throw ();
    void start_spectating(const Network::Address& address) throw ();
    void continue_spectating() throw ();

    void read_replay_controls(ConstDataBlockRef data) throw (ServerDataError);
    static void read_replay_player_controls(BinaryDataBlockReader& read, ClientPlayer& player, bool preciseGundir) throw (BinaryReader::ReadError);
    void read_replay_player_position(BinaryDataBlockReader& read, ClientPlayer* player) throw (BinaryReader::ReadError);
    void read_replay_player_position(BinaryDataBlockReader& read, ClientPlayer& player) throw (BinaryReader::ReadError);
    void skip_replay_player_position(BinaryDataBlockReader& read) throw (BinaryReader::ReadError);

    void createGunexploEffect(const WorldCoords& pos, int team, double time) throw ();

    void netRocketFired(const WorldCoords& pos, bool power) throw ();
    void netRocketHitPlayer(int rockid, int rokx, int roky, double time) throw ();
    void netPowerCollision(int target, double time) throw ();
    void net_data_sound(BinaryReader& read) throw (BinaryReader::ReadError);
    void net_data_registration_response(BinaryReader& read) throw (BinaryReader::ReadError);
    void net_data_quick_map_list(BinaryReader& read) throw (BinaryReader::ReadError);
    void net_data_map_list(BinaryReader& read) throw (BinaryReader::ReadError);
    void net_data_crap_update(BinaryReader& read) throw (BinaryReader::ReadError);
    void net_data_reset_map_list(BinaryReader& read) throw (BinaryReader::ReadError);
    void net_data_map_vote(BinaryReader& read) throw (BinaryReader::ReadError);
    void net_data_map_votes_update(BinaryReader& read) throw (BinaryReader::ReadError);
    void net_text_message(Message_type type, int sender_team, const std::string& text) throw ();
    void netKill(int attacker, int target, DamageType cause, bool carrier_defended, bool flag_defended, Statistics::FlagType, bool spree_ended, bool spree_started) throw ();
    void netSuicide(int pid, Statistics::FlagType, bool spree_ended) throw ();
    void netFlagTake(int pid, Statistics::FlagType) throw ();
    void netFlagReturn(int pid) throw ();
    void netFlagDrop(int pid, Statistics::FlagType) throw ();
    void netTeamChange(int pl1, int pl2 = -1) throw ();
    void netStatsReady() throw ();
    void netMapChange(const std::string& maptitle, const int map_number, const int total_maps) throw ();
    void netGameoverPeriodStart(uint32_t redScore, uint32_t blueScore, int caplimit, int timelimit) throw ();
    void netGameoverPeriodEnd() throw ();
    void netGameStarted() throw ();
    void netPhysicsChanged() throw ();
    void netSetHostname(const std::string& name) throw () { hostname = name; }
    void netSetCurrentMap(int idx) throw () { current_map = idx; }

    void rocketHitWallCallback(int rid, bool power, const WorldCoords& pos) throw ();
    void playerHitWallCallback(int pid) throw ();
    void playerHitPlayerCallback(int pid1, int pid2) throw ();

    std::string getPlayerPassword() const throw () { return m_playerPassword.password(); }

    class ConstDisappearedFlagIterator : public ConstFlagIterator {
        const GuiClient& c;

        void findValid() throw ();

    public:
        ConstDisappearedFlagIterator(const GuiClient* host) throw () : ConstFlagIterator(host->fx), c(*host) { findValid(); }
        ConstDisappearedFlagIterator& operator++() throw () { next(); findValid(); return *this; }
    };

public:
    GuiClient(const ClientExternalSettings& config, const ServerExternalSettings& serverConfig, Log& clientLog, MemoryLog& externalErrorLog_) throw ();
    ~GuiClient() throw ();

    bool start() throw ();
    void stop() throw ();
    void loop(volatile bool* quitFlag, bool firstTimeSplash) throw ();
    void language_selection_start(volatile bool* quitFlag) throw ();
};

#endif
