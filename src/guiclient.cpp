/*
 *  guiclient.cpp
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

#include <iomanip>

#include "incalleg.h"
#include "binaryaccess.h"
#include "language.h"
#include "leetnet/client.h"
#include "names.h"
#include "platform.h"
#include "protocol.h"
#include "timer.h"
#include "version.h"

#include "guiclient.h"

using std::endl;
using std::ifstream;
using std::ios;
using std::istream;
using std::istringstream;
using std::left;
using std::list;
using std::make_pair;
using std::map;
using std::max;
using std::min;
using std::ofstream;
using std::ostringstream;
using std::pair;
using std::right;
using std::setfill;
using std::setw;
using std::string;
using std::stringstream;
using std::vector;

//#define ROOM_CHANGE_BENCHMARK
//#define DEATHBRINGER_SPEED_TEST

const int PASSBUFFER = 32;  //size of password file

#ifdef ROOM_CHANGE_BENCHMARK
int benchmarkRuns = 0;
#endif

void GuiClient::processNameAuthenticationRequest() throw () {
    m_playerPassword.setup(playername, false);
    showMenu(m_playerPassword);
}

void GuiClient::createGunexploEffect(const WorldCoords& pos, int team, double time) throw () {
    graphics.create_gunexplo(pos, team, time);
}

void ServerThreadOwner::threadFn(const ServerExternalSettings& config) throw () {
    GameserverInterface gameserver(log, config, *log.accessError(), _("(server)") + ' ');
    if (!gameserver.start(config.server_maxplayers)) {
        log.error(_("Can't start listen server."));
        quitFlag = true;
    }
    else {
        log("Listen server running");
        gameserver.loop(&quitFlag, false);
        quitFlag = true;
        gameserver.stop();
        log("Listen server stopped");

        //restore client's windowtitle
        config.statusOutput(_("Outgun client"));    // note: this is the server's statusOutput not client's
    }
}

void ServerThreadOwner::start(int port, const ServerExternalSettings& config) throw () {
    nAssert(quitFlag && !threadFlag);
    runPort = port;
    quitFlag = false;
    threadFlag = true;
    MemFun1<ServerThreadOwner, void, const ServerExternalSettings&> rmf(this, &ServerThreadOwner::threadFn);
    serverThread.start_assert("ServerThreadOwner::threadFn", rmf, config, config.priority);
}

void ServerThreadOwner::stop() throw () {
    nAssert(threadFlag);
    quitFlag = true;
    threadFlag = false;
    serverThread.join();
}

void RankingPasswordManager::start() throw () {
    quitThread = false;
    thread.start_assert("RankingPasswordManager::threadFn",
                        MemFun0<RankingPasswordManager, void>(this, &RankingPasswordManager::threadFn),
                        priority);
}

void RankingPasswordManager::setToken(const string& newToken) throw () {
    if (token.read() != newToken) {
        token = newToken;
        tokenCallback(newToken);
    }
}

RankingPasswordManager::RankingPasswordManager(LogSet logs, TokenCallbackT tokenCallbackFunction, int threadPriority) throw () :
    log(logs),
    tokenCallback(tokenCallbackFunction),
    quitThread(true),
    passStatus(PS_noPassword),
    priority(threadPriority),
    servStatus(PS_noPassword), // no server
    token("RankingPasswordManager::token")
{ }

void RankingPasswordManager::stop() throw () {
    if (!quitThread) {
        log("Joining password-token thread");
        quitThread = true;
        thread.join();
    }
}

void RankingPasswordManager::changeData(const string& newName, const string& newPass) throw () {
    if (newName == name && newPass == password)
        return;

    stop();
    setToken("");
    if (newPass.empty()) {
        passStatus = PS_noPassword;
        return;
    }
    name = newName;
    password = newPass;
    passStatus = PS_starting;
    start();
}

string RankingPasswordManager::statusAsString() const throw () {
    switch (status()) {
    /*break;*/ case PS_noPassword:      return _("No password set");
        break; case PS_starting:        return _("Initializing...");
        break; case PS_socketError:     return _("Socket error");
        break; case PS_sending:         return _("Sending login...");
        break; case PS_sendError:       return _("Error sending");
        break; case PS_receiving:       return _("Waiting for response...");
        break; case PS_recvError:       return _("No response");
        break; case PS_invalidResponse: return _("Invalid response received");
        break; case PS_unavailable:     return _("Master server unavailable");
        break; case PS_tokenReceived:   return _("Logged in");
        break; case PS_badLogin:        return _("Login failed: check password");
        break; case PS_tokenSent:       return _("Logged in; sent to server");
        break; case PS_tokenAccepted:   return _("Logged in; server accepted");
        break; case PS_tokenRejected:   return _("Error: server rejected");
        break; default: nAssert(0); return 0;
    }
}

void RankingPasswordManager::threadFn() throw () {
    bool newToken = true;
    int delay = 0;  // given a value in MS before each continue: this time will be waited before next round

    while (!quitThread) {
        if (delay > 0) {
            platSleep(500);
            delay -= 500;
            continue;
        }
        delay = 60000;  // default to one minute

        stringstream response;
        try {
            Network::TCPSocket sock(Network::NonBlocking, 0, true);

            sock.connect(g_masterSettings.rankAddress());
            const string query = build_http_request(true, g_masterSettings.rankHost(), g_masterSettings.rankTokenScript(),
                                                    "&action=" + string(newToken ? "login" : "update") +
                                                    "&name=" + url_encode(name) +
                                                    "&password=" + url_encode(password));

            passStatus = PS_sending;
            if (newToken)
                log("Password thread: Sending login");
            sock.persistentWrite(query, &quitThread, 30000);

            passStatus = PS_receiving;
            sock.readAll(response, &quitThread, 30000);
            sock.close();
        } catch (Network::ExternalAbort) {
            break;
        } catch (const Network::Error& e) {
            log("Password thread: %s", e.str().c_str());
            passStatus = (passStatus == PS_sending ? PS_sendError : passStatus == PS_receiving ? PS_recvError : PS_socketError);
            delay = 15000; // retry faster
            continue;
        }

        string line;
        while (getline(response, line) && line != "\r") { } // skip HTTP headers

        getline_smart(response, line);
        if (line == "OK") {
            if (newToken) {
                getline_smart(response, line);
                if (line.length() >= 4 && line.length() <= 20) { // the initial ranking script uses fixed length tokens, but we only need to check for some sensibility here
                    log("Password thread: Login OK");
                    passStatus = PS_tokenReceived;
                    setToken(line);
                    delay = 10 * 60000; // refresh token after 10 minutes
                    newToken = false;
                }
                else {
                    log("Password thread: Invalid token \"%s\"", formatForLogging(line).c_str());
                    passStatus = PS_invalidResponse;
                }
            }
            else {
                log("Password thread: Login keepalive OK.");
                passStatus = PS_tokenReceived;
                delay = 10 * 60000; // refresh again after 10 minutes
            }
        }
        else if (line.substr(0, 7) == "ERROR: ") {
            log("Password thread: %s failed (%s)", newToken ? "Login" : "Login keepalive", line.substr(7).c_str());
            passStatus = PS_badLogin;
            setToken("");
            delay = 10 * 60000; // try again after 10 minutes (will probably fail then too)
        }
        else {
            log("Password thread: Invalid response: \"%s\"", formatForLogging(response.str()).c_str());
            passStatus = PS_invalidResponse;
        }
    }
}

bool ServerListEntry::setAddress(const string& address) throw () {
    if (!isValidIP(address, true, 1))
        return false;
    addr.fromValidIP(address);
    if (addr.getPort() == 0)
        addr.setPort(DEFAULT_UDP_PORT);
    return true;
}

void ServerListEntry::setAddress(const Network::Address& address) throw () {
    addr = address;
    if (addr.getPort() == 0)
        addr.setPort(DEFAULT_UDP_PORT);
}

string ServerListEntry::addressString() const throw () {
    if (addr.getPort() != DEFAULT_UDP_PORT)
        return addr.toString();
    else {
        Network::Address cpy = addr;
        cpy.setPort(0);
        return cpy.toString();
    }
}

FileDownload::FileDownload(const string& type, const string& name, const string& filename) throw () :
    fileType(type),
    shortName(name),
    fullName(filename),
    fp(0)
{ }

FileDownload::~FileDownload() throw () {
    if (fp) {
        fclose(fp);
        remove(fullName.c_str());
    }
}

int FileDownload::progress() const throw () {
    nAssert(fp);
    return ftell(fp);
}

bool FileDownload::start() throw () {
    nAssert(!fp);
    fp = fopen(fullName.c_str(), "wb");
    return (fp != 0);
}

bool FileDownload::save(ConstDataBlockRef data) throw () {
    nAssert(fp);
    return (fwrite(data.data(), 1, data.size(), fp) == data.size());
}

void FileDownload::finish() throw () {
    nAssert(fp);
    fclose(fp);
    fp = 0;
}

void TM_ServerSettings::addLine(GuiClient* cl, const string& caption, const string& value) const throw () {
    const int capWidth = 25;
    cl->m_serverInfo.addLine(pad_to_size_left(caption, capWidth), value);
}

void TM_ServerSettings::execute(ClientBase* pClBase) const throw () {
    GuiClient* const cl = dynamic_cast<GuiClient*>(pClBase);
    if (!cl)
        return;

    cl->m_serverInfo.clear();
    cl->m_serverInfo.menu.setCaption(cl->hostname);

    addLine(cl, _("Capture limit"       ), ( caplimit == 0) ? _("none") :             itoa( caplimit));
    addLine(cl, _("Time limit"          ), (timelimit == 0) ? _("none") : _("$1 min", itoa(timelimit)));
    if (timelimit != 0) {
        string value;
        if (extratime == 0)
            value = _("none");
        else if (extratime_periods > 1)
            value = _("$1×$2 min", itoa(extratime_periods), itoa(extratime));
        else
            value = _("$1 min", itoa(extratime));
        addLine(cl, _("Extra-time"      ), value);
    }
    if (flag_return_delay != -1)
        addLine(cl, _("Flag return delay"   ), _("$1 s", fcvt(flag_return_delay / 10., 1)));
    addLine(cl, _("Player collisions"   ),  cl->fx.physics.player_collisions == PhysicalSettings::PC_none ? _("off") :
                                            cl->fx.physics.player_collisions == PhysicalSettings::PC_normal ? _("on") : _("special"));
    addLine(cl, _("Friendly fire"       ), (cl->fx.physics.friendly_fire == 0.) ? _("off") : _("$1%", (itoa(iround(100. * cl->fx.physics.friendly_fire)))));

    const string caps[] = { _("Balance teams"), _("Drop power-ups"), _("Invisible shadow"), _("Shadowed see each other"), _("Switch deathbringer"), _("One hit shield") };
    const int bits[] = { 0, 1, 2, 9, 3, 4, -1 };
    for (int i = 0; bits[i] != -1; i++)
        addLine(cl, caps[i], (misc1 & (1 << bits[i])) ? _("on") : _("off"));

    addLine(cl, _("Maximum weapon level"), itoa((misc1 >> 5) & 0x0F));

    const bool pupMinP = pupMin >= 100,
               pupMaxP = pupMax >= 100;
    const int pupMinV = pupMinP ? pupMin - 100 : pupMin,
              pupMaxV = pupMaxP ? pupMax - 100 : pupMax;
    const bool constPowerups = (pupMaxV == 0 || (pupMinP == pupMaxP && pupMin >= pupMax));
    string maxTitle;
    if (!constPowerups)
        addLine(cl,                             _("Minimum powerups"), (pupMinP && pupMinV != 0) ? _("$1% of rooms", itoa(pupMinV)) : itoa(pupMinV));
    addLine(cl, constPowerups ? _("Powerups") : _("Maximum powerups"), (pupMaxP && pupMaxV != 0) ? _("$1% of rooms", itoa(pupMaxV)) : itoa(pupMaxV));

    if (pupMaxV != 0) {
        const bool constPowerupTime = (pupMaxTime <= pupAddTime);
        if (!constPowerupTime)
            addLine(cl,                                    _("Powerup add time"), _("$1 s", itoa(pupAddTime)));
        addLine(cl, constPowerupTime ? _("Powerup time") : _("Powerup max time"), _("$1 s", itoa(pupMaxTime)));
    }

    if (cl->menu.options.game.showServerInfo() && !cl->replaying)
        cl->showMenu(cl->m_serverInfo);
}

void GuiClient::ConstDisappearedFlagIterator::findValid() throw () {
    for (; valid(); next()) {
        const Flag& fl = flag();
        if (!fl.carried())
            continue;
        const ClientPlayer& pl = c.fx.player[fl.carrier()];
        const WorldCoords& pos = fl.position();
        nAssert(pos.room.x >= 0 && pos.room.y >= 0);
        if (pl.used && pos.room.x < c.fx.map.w && pos.room.y < c.fx.map.h && !pl.onscreen && pl.posUpdated > c.fx.frame - 300.)
            return;
    }
}

GuiClient::GuiClient(const ClientExternalSettings& config, const ServerExternalSettings& serverConfig, Log& clientLog, MemoryLog& externalErrorLog_) throw () :
    ClientBase(config, clientLog, externalErrorLog_),
    listenServer(log),
    downloadMutex("GuiClient::downloadMutex"),
    rankingPassword(log, new MemFun1<GuiClient, void, string>(this, &GuiClient::CB_rankingToken), config.lowerPriority),
    mapInfoMutex("Client::mapInfoMutex"),
    mapListReadPosition(0),
    mapListSortKey(MLSK_Number),
    mapListChangedAfterSort(false),
    current_map(-1),
    map_vote(-1),
    want_change_teams(false),
    menusel(menu_none), // must be valid before start() (which normally sets this) since language_selection_start() can run first and calls draw_game_menu()
    player_stats_page(0),
    lastAltEnterTime(0),
    FPS(0),
    framecount(0),
    frameCountStartTime(0),
    serverListMutex("GuiClient::serverListMutex"),
    refreshStatus(RS_none),
    password_file(whereuserdir + "config" + directory_separator + "passwd"),
    quickMessageFile(whereuserdir + "config" + directory_separator + "quickmessages.txt"),
    graphics(log),
    screenshot(false),
    visible_rooms(1),
    client_sounds(log),
    messageLogOpen(false),
    serverExtConfig(serverConfig)
{ }

GuiClient::~GuiClient() throw () {
    abortThreads = true;
    while (refreshStatus != RS_none && refreshStatus != RS_failed)  // wait for a possible refresh thread to abort itself
        platSleep(50);
}

bool GuiClient::start() throw () {
    extConfig.statusOutput(_("Outgun client"));
    initMenus();
    showMenu(menu);

    menusel = menu_none;

    framecount = 0;

    startBase(give_control(new_client_c(extConfig.networkPriority, "")));

    if (language.code() == "fi")
        playername = finnish_name(maxPlayerNameLength);
    else
        playername = RandomName();

    //try to load the client's password
    string fileName = whereuserdir + "config" + directory_separator + "password.bin";
    FILE* psf = fopen(fileName.c_str(), "rb");
    if (psf) {
        char pas[PASSBUFFER];
        for (int c = 0; c < PASSBUFFER; c++) {
            const int cha = fgetc(psf); // don't care about EOF yet
            pas[c] = static_cast<char>(255 - cha);
        }
        if (!feof(psf)) {
            pas[8] = 0;
            menu.options.player.password.set(pas);
        }
        fclose(psf);
    }

    //try to load client configuration
    vector<int> fav_colors;

    fileName = whereuserdir + "config" + directory_separator + "client.cfg";
    ifstream cfg(fileName.c_str());
    for (;;) {
        string line;
        if (!getline_skip_comments(cfg, line))
            break;

        istringstream command(line);

        int iSettingId;
        command >> iSettingId;
        command.ignore();   // eat separator (space)
        if (!command || iSettingId < 0) {
            log.error(_("Invalid syntax in client.cfg (\"$1\").", line));
            continue;
        }
        if (iSettingId >= CCS_EndOfCommands) {
            log.error(_("Unknown data in client.cfg (\"$1\").", line));
            continue;
        }

        string args;
        getline(command, args); // this might fail, but that only means there is an empty string

        const ClientCfgSetting settingId = static_cast<ClientCfgSetting>(iSettingId);

        SettingCollector::SaverLoader* sl = settings.find(settingId);
        if (sl) {
            sl->load(args);
            continue;
        }

        // some more complicated settings need to be handled here
        switch (settingId) {
        /*break;*/ case CCS_PlayerName:
                if (check_name(args))
                    playername = args;

            break; case CCS_FavoriteColors: {
                istringstream ist(args);
                int col;
                while (ist >> col)
                    if (col >= 0 && col < 16 && find(fav_colors.begin(), fav_colors.end(), col) == fav_colors.end())
                        fav_colors.push_back(col);
            }

            break; case CCS_KeyboardLayout:
                if (!menu.options.controls.keyboardLayout.set(args)) {  // it is possible to have a layout Outgun doesn't know about
                    menu.options.controls.keyboardLayout.addOption(_("unknown ($1)", args), args);
                    menu.options.controls.keyboardLayout.set(args);
                }

            break; case CCS_GFXMode: {
                if (extConfig.forceDefaultGfxMode)
                    break;
                istringstream is(args);
                int width, height, depth;
                is >> width >> height >> depth;
                bool ok = is;
                char nullc;
                is >> nullc;
                if (!ok || is || width < 320 || height < 200 || (depth != 16 && depth != 24 && depth != 32))
                    log("Bad screen mode in client.cfg");
                else {
                    menu.options.screenMode.colorDepth.set(depth);    // may fail if the previous depth isn't available
                    menu.options.screenMode.update(graphics);  // fetch resolutions according to the new depth
                    if (!menu.options.screenMode.resolution.set(ScreenMode(width, height)))
                        log("Previous screen mode not available (%d×%d×%d)", width, height, depth);
                }
            }

            break; case CCS_Antialiasing:
                menu.options.graphics.antialiasing.set(args == "2");

            break; default: nAssert(0); // all values up to the highest known must be handled
        }
    }
    cfg.close();

    loadQuickMessages();
    loadMapInfoCache();

    fileName = whereuserdir + "config" + directory_separator + "favorites.txt";
    ifstream fav(fileName.c_str());
    if (fav) {
        string addr;
        while (getline_skip_comments(fav, addr)) {
            ServerListEntry spy;
            if (spy.setAddress(addr))
                gamespy.push_back(spy);
        }
        fav.close();
    }

    // finalize and apply the settings

    // player
    rankingPassword.changeData(playername, menu.options.player.password());
    for (int i = 0; i < 16; i++)
        if (find(fav_colors.begin(), fav_colors.end(), i) == fav_colors.end())
            fav_colors.push_back(i);
    for (vector<int>::const_iterator col = fav_colors.begin(); col != fav_colors.end(); ++col)
        menu.options.player.favoriteColors.addOption(*col);

    // game
    if (menu.options.game.messageLogging() != Menu_game::ML_none)
        openMessageLog();

    // controls
    MCF_keyboardLayout();
    if (menu.options.controls.joystick())
        install_joystick(JOY_TYPE_AUTODETECT);

    // graphics
    if (extConfig.winclient != -1)
        menu.options.screenMode.windowed.set(extConfig.winclient);
    if (extConfig.trypageflip != -1)
        menu.options.screenMode.flipping.set(extConfig.trypageflip);
    if (extConfig.targetfps != -1)
        menu.options.graphics.fpsLimit.set(extConfig.targetfps);
    MCF_antialiasChange();
    visible_rooms = menu.options.graphics.visibleRoomsPlay();
    MCF_transpChange();
    MCF_statsBgChange();
    if (!screenModeChange())
        return false;
    MCF_gfxThemeChange();
    MCF_fontChange();

    // sounds
    if (extConfig.nosound)
        menu.options.sounds.enabled.set(false);
    MCF_sndEnableChange();
    client_sounds.setVolume(menu.options.sounds.volume());
    MCF_sndThemeChange();

    // local server
    if (serverExtConfig.privSettingForced)
        menu.ownServer.pub.set(!serverExtConfig.privateserver);
    if (serverExtConfig.portForced)
        menu.ownServer.port.set(serverExtConfig.port);  // assume caller to take care of limiting to proper range (1..65535)
    if (serverExtConfig.ipForced) {
        menu.ownServer.autoIP.set(false);
        menu.ownServer.address.set(serverExtConfig.ipAddress);
    }

    // message highlighting
    load_highlight_texts();

    load_fav_maps();

    if (menu.options.game.autoGetServerList())
        MCF_updateServers();

    return true;
}

void GuiClient::language_selection_start(volatile bool* quitFlag) throw () {
    log("language_selection_start()");
    extConfig.statusOutput(_("Outgun client"));

    if (!graphics.init(640, 480, desktop_color_depth(), true, false))
        return;

    typedef MenuCallback<GuiClient> MCB;
    m_initialLanguage.initialize(new MCB::A<Menu, &GuiClient::MCF_menuOpener>(this), settings);

    const int menu_selection_index = refreshLanguages(m_initialLanguage);
    m_initialLanguage.addHooks(new MCB::A<Textarea, &GuiClient::MCF_acceptInitialLanguage>(this));
    m_initialLanguage.menu.setSelection(menu_selection_index);

    showMenu(m_initialLanguage);

    while (!openMenus.empty()) {
        if (*quitFlag)
            return;
        if (keyboard_needs_poll())
            poll_keyboard();    // ignore return value
        // handle waiting keypresses
        while (keypressed()) {
            int ch = readkey();
            const int sc = (ch >> 8);
            ch &= 0xFF;
            if (sc == KEY_F11)
                screenshot = true;
            openMenus.handleKeypress(sc, ch);
        }

        // give other threads a chance (otherwise we're trying to run all the time if the FPS limit is not lower than what the machine can do)
        sched_yield();

        graphics.startDraw();
        graphics.draw_background(false);
        draw_game_menu();
        graphics.endDraw();
        graphics.draw_screen(false);
        if (screenshot) {
            save_screenshot();
            screenshot = false;
        }
    }
}

// incoming chunk of requested file by UDP
void GuiClient::process_udp_download_chunk(ConstDataBlockRef data, bool last) throw () {
    Lock ml(downloadMutex);
    if (downloads.empty() || !downloads.front().isActive()) {
        log.error("Server sent a file we aren't expecting");
        addThreadMessage(new TM_DoDisconnect());
        return;
    }
    FileDownload& dl = downloads.front();
    if (!dl.save(data)) {
        log.error(_("Error writing to '$1'.", dl.fullName));
        addThreadMessage(new TM_DoDisconnect());
        return;
    }
    //send the reply
    BinaryBuffer<256> msg;
    msg.U8(data_file_ack);
    client->send_message(msg);
    if (last) {
        dl.finish();
        log("Download complete: %s '%s' to %s", dl.fileType.c_str(), dl.shortName.c_str(), dl.fullName.c_str());
        if (dl.fileType == "map") {
            if (dl.shortName == servermap) {
                const bool ok = fd.load_map(log, CLIENT_MAPS_DIR, dl.shortName) && fx.load_map(log, CLIENT_MAPS_DIR, dl.shortName);
                remove_useless_flags();
                if (!ok) {
                    log.error("After download: map '" + dl.shortName + "' not found");
                    addThreadMessage(new TM_DoDisconnect());
                    return;
                }
                log("Map '%s' downloaded successfully", dl.shortName.c_str());
                mapChanged = true;
                map_ready = true;
            }
            ++clientReadiesWaiting;
        }
        else
            nAssert(0);
        downloads.pop_front();
        check_download();
    }
}

/* check_download: if there is a download pending, and nothing is downloading, activate it
 * call with downloadMutex locked
 */
void GuiClient::check_download() throw () { // call with downloadMutex locked
    if (downloads.empty())
        return;
    FileDownload& dl = downloads.front();
    if (dl.isActive())
        return;
    if (!dl.start()) {
        log.error(_("File download: Can't open '$1' for writing.", dl.fullName));
        addThreadMessage(new TM_DoDisconnect());
        return;
    }
    // request the file from server
    BinaryBuffer<256> msg;
    msg.U8(data_file_request);
    msg.str(dl.fileType);
    msg.str(dl.shortName);
    client->send_message(msg);
}

void GuiClient::download_server_file(const string& type, const string& name) throw () {
    nAssert(type == "map");
    if (name.find_first_of("./:\\") != string::npos) {
        log.error("Illegal file download request: map \"" + name + "\"");
        addThreadMessage(new TM_DoDisconnect());
        return;
    }

    Lock ml(downloadMutex);
    const string fileName = whereuserdir + CLIENT_MAPS_DIR + directory_separator + name + ".txt";
    downloads.push_back(FileDownload(type, name, fileName));
    check_download();
}

void GuiClient::sendFavoriteColors() throw () {
    if (menu.options.player.favoriteColors.values().empty())
        return;
    BinaryBuffer<256> msg;
    msg.U8(data_fav_colors);
    msg.U8(menu.options.player.favoriteColors.values().size());
    // send two colours in a byte
    for (vector<int>::const_iterator col = menu.options.player.favoriteColors.values().begin();
                                    col != menu.options.player.favoriteColors.values().end(); ++col) {
        uint8_t byte = static_cast<uint8_t>(*col) & 0x0F;
        if (++col != menu.options.player.favoriteColors.values().end())
            byte |= static_cast<uint8_t>(*col) << 4;
        msg.U8(byte);
    }
    client->send_message(msg);
}

void GuiClient::sendMinimapBandwidth() throw () {
    sendMinimapBandwidthAny(menu.options.game.minimapBandwidth() / 20);
}

void GuiClient::client_connected(ConstDataBlockRef data) throw () {   // call with frameMutex locked
    ClientBase::client_connected(data);

    fd.reset();
    fd.frame = -1;
    fd.skipped = true;
    fd.physics = fx.physics;

    m_serverInfo.clear();
    m_serverInfo.addLine("");   // can't draw a totally empty menu; this will be overwritten with config information

    if (!menu.options.player.useRandomColor())
        sendFavoriteColors();
    sendMinimapBandwidth();

    extConfig.statusOutput(_("Connected to $1 ($2)", hostname.substr(0, 32), serverIP.toString()));

    show_all_messages = false;
    stats_autoshowing = false;

    //don't want to change teams by default
    want_change_teams = false;

    //don't want to exit map by default
    want_map_exit = false;
    want_map_exit_delayed = false;

    deadAfterHighlighted = true;

    openMenus.clear();  // connect progress menu is showing; exceptions are when it's been closed and the disconnect is still pending, and when help is opened on top of it

    talkbuffer.clear();
    talkbuffer_cursor = 0;
    chatbuffer.clear();

    players_sb.clear();

    //reset FPS count vars
    framecount = 0;
    frameCountStartTime = get_time();
    FPS = 0;

    //reset map time
    map_time_limit = false;
    map_start_time = 0;
    map_end_time = 0;
    extra_time_running = false;
    map_vote = -1;

    // send registration token (if any)
    const string s = rankingPassword.getToken();
    if (!s.empty())
        CB_rankingToken(s);
    send_ranking_participation();

    {
        Lock ml(mapInfoMutex);
        maps.clear();
        mapListReadPosition = 0;
        mapListChangedAfterSort = true;
    }

    //clear client side effects
    graphics.clear_fx();

    mouseClicked.clear();

    send_frame(true, true);
}

void GuiClient::send_ranking_participation() throw () {
    BinaryBuffer<8> msg;
    msg.U8(data_ranking_participation);
    msg.U8(menu.options.player.ranking() ? 1 : 0);
    client->send_message(msg);
}

void GuiClient::client_disconnected(ConstDataBlockRef data) throw () {
    //restore window title
    extConfig.statusOutput(_("Outgun client"));

    menusel = menu_none;

    string description;
    if (data.size() == 1)
        try {
            BinaryDataBlockReader read(data);
            switch (read.U8()) {
            /*break;*/ case server_c::disconnect_client_initiated: // user knows why, so no description
                break; case server_c::disconnect_server_shutdown:  description = _("Server was shut down.");
                break; case server_c::disconnect_timeout:          description = _("Connection timed out.");
                break; case disconnect_kick:                       description = _("You were kicked.");
                break; case disconnect_idlekick:                   description = _("You were kicked for being idle.");
                break; case disconnect_client_misbehavior:         description = _("Internal error (client misbehaved).");
                break; default:;
            }
        } catch (BinaryReader::ReadError) { nAssert(0); }

    m_connectProgress.clear();
    m_connectProgress.wrapLine(_("You have been disconnected."));
    if (!description.empty())
        m_connectProgress.wrapLine(description);
    showMenu(m_connectProgress);

    if (description.empty())
        log("Disconnection successful");
    else
        log("Disconnected: %s", description.c_str());

    rankingPassword.disconnectedFromServer();

    {
        Lock ml(downloadMutex);
        downloads.clear();
    }
}

void GuiClient::connect_failed_denied(ConstDataBlockRef data) throw () {
    string message;
    bool userHandled = false;
    try {
        if (data.size() > 1) {
            BinaryDataBlockReader read(data);
            message = read.str();
            const string str1 = "Protocol mismatch: server: ";
            const string str2 = ", client: " + GAME_PROTOCOL;
            const string::size_type str2pos = message.length() - str2.length();
            if (message.compare(0, str1.length(), str1) == 0 && str2pos > 0 && message.compare(str2pos, str2.length(), str2) == 0) {
                const string serverProtocol = message.substr(str1.length(), str2pos - str1.length());
                message = _("Protocol mismatch. Server: $1, client: $2.", serverProtocol, GAME_PROTOCOL);
            }
            // otherwise leave message at its value of whatever the server sent
        }
        else if (data.size() == 1) {
            BinaryDataBlockReader read(data);
            const uint8_t rb = read.U8();
            if (rb > reject_last)
                message = _("Unknown reason code ($1).", itoa(rb));
            else {
                switch (static_cast<Connect_rejection_reason>(rb)) {
                /*break;*/ case reject_server_full:
                        message = _("The server is full.");
                    break; case reject_banned:
                        message = _("You are banned from this server.");
                    break; case reject_player_password_needed:
                        openMenus.close(&m_connectProgress.menu);
                        m_playerPassword.setup(playername, false);
                        showMenu(m_playerPassword);
                        userHandled = true;
                        message = "Asking for player password."; // just for logging
                    break; case reject_wrong_player_password:
                        message = _("Wrong player password.");
                        remove_player_password(playername, serverIP.toString());
                    break; case reject_server_password_needed:
                        openMenus.close(&m_connectProgress.menu);
                        showMenu(m_serverPassword);
                        userHandled = true;
                        message = "Asking for server password."; // just for logging
                    break; case reject_wrong_server_password:
                        message = _("Wrong server password.");
                        m_serverPassword.password.set("");
                    break; default: nAssert(0);
                }
            }
        }
        else
            message = _("No reason given.");
    } catch (BinaryReader::ReadError) {
        log("Format error in connection denial data.");
        message.clear();
        userHandled = false;
    }

    log("Connecting failed: %s", message.c_str());

    if (!userHandled) {
        m_connectProgress.wrapLine(_("Connection refused."));
        m_connectProgress.wrapLine(message);
        // under normal circumstances, the connect progress menu is showing; even otherwise putting this text there doesn't harm
    }
}

void GuiClient::connect_failed_unreachable() throw () {
    m_connectProgress.wrapLine(_("No response from server."));
    // under normal circumstances, the connect progress menu is showing; even otherwise putting this text there doesn't harm
    log("Connecting failed: no response");
}

void GuiClient::connect_failed_socket() throw () {
    m_connectProgress.wrapLine(_("Can't open socket."));
    // under normal circumstances, the connect progress menu is showing; even otherwise putting this text there doesn't harm
    log("Connecting failed: no response");
}

string GuiClient::load_player_password(const string& name, const string& address) const throw () {
    ifstream in(password_file.c_str());
    while (in) {
        string load_name, load_address, load_password;
        getline_smart(in, load_name);
        getline_smart(in, load_address);
        getline_smart(in, load_password);
        if (load_name == name && load_address == address)
            return load_password;
    }
    return string();
}

vector<vector<string> > GuiClient::load_all_player_passwords() const throw () {
    vector<vector<string> > passwords;
    ifstream in(password_file.c_str());
    while (1) {
        string name, address, password;
        getline_smart(in, name);
        getline_smart(in, address);
        getline_smart(in, password);
        if (in) {
            vector<string> entry;
            entry.push_back(name);
            entry.push_back(address);
            entry.push_back(password);
            passwords.push_back(entry);
        }
        else
            break;
    }
    return passwords;
}

void GuiClient::save_player_password(const string& name, const string& address, const string& password) const throw () {
    nAssert(!name.empty() && !address.empty() && !password.empty());    // empty lines cause trouble
    vector<vector<string> > passwd_list = load_all_player_passwords();
    // check if player already has a password
    string test = load_player_password(name, address);
    if (test.empty()) {
        vector<string> entry;
        entry.push_back(name);
        entry.push_back(address);
        entry.push_back(password);
        passwd_list.push_back(entry);
    }
    ofstream out(password_file.c_str());
    if (!out) {
        log.error(_("Can't save player password to '$1'!", password_file));
        return;
    }
    for (vector<vector<string> >::const_iterator item = passwd_list.begin(); item != passwd_list.end(); ++item) {
        out << (*item)[0] << '\n';
        out << (*item)[1] << '\n';
        if ((*item)[0] == name && (*item)[1] == address) {
            out << password;
            log("New player password saved for %s at %s.", name.c_str(), address.c_str());
        }
        else
            out << (*item)[2];
        out << '\n';
    }
}

void GuiClient::remove_player_password(const string& name, const string& address) const throw () {
    // check if player has a password
    const string test = load_player_password(name, address);
    if (test.empty())
        return;
    const vector<vector<string> > passwd_list = load_all_player_passwords();
    ofstream out(password_file.c_str());
    if (!out)
        return;
    for (vector<vector<string> >::const_iterator item = passwd_list.begin(); item != passwd_list.end(); ++item)
        if ((*item)[0] == name && (*item)[1] == address)
            log("%s's player password at %s removed.", name.c_str(), address.c_str());
        else
            for (int i = 0; i < 3; i++)
                out << (*item)[i] << '\n';
}

int GuiClient::remove_player_passwords(const string& name) const throw () {
    vector<vector<string> > passwd_list = load_all_player_passwords();
    ofstream out(password_file.c_str());
    if (!out)
        return 0;
    int removed = 0;
    for (vector<vector<string> >::iterator item = passwd_list.begin(); item != passwd_list.end(); ++item)
        if ((*item)[0] == name)
            removed++;
        else
            for (int i = 0; i < 3; i++)
                out << (*item)[i] << '\n';
    log("%s's player passwords removed.", name.c_str());
    return removed;
}

void GuiClient::connect_command(bool loadPassword) throw () {   // call with frameMutex locked
    stop_replay();
    prepareForConnect();
    openMenus.close(&m_connectProgress.menu);

    const string strAddress = serverIP.toString();

    if (loadPassword)
        m_playerPassword.password.set(load_player_password(playername, strAddress));

    log("Connecting to %s... passwords: server %s, player %s", strAddress.c_str(),
        m_serverPassword.password().empty() ? "no" : "yes",
        m_playerPassword.password().empty() ? "no" : "yes");

    connect(strAddress, m_serverPassword.password(), m_playerPassword.password());

    m_connectProgress.clear();
    m_connectProgress.wrapLine(_("Trying to connect..."), true);
    showMenu(m_connectProgress);
}

void GuiClient::change_name_command() throw () {
    //set new name, close menu
    menu.options.player.name.set(trim(menu.options.player.name()));
    const string& newName = menu.options.player.name();
    if (!check_name(newName))
        return;
    openMenus.close(&menu.options.player.menu);

    playername = newName;
    if (serverIP.valid())
        m_playerPassword.password.set(load_player_password(playername, serverIP.toString()));
    issue_change_name_command();
    rankingPassword.changeData(playername, menu.options.player.password());
}

ClientControls GuiClient::readControls(bool canUseKeypad, bool useCursorKeys) const throw () {
    ClientControls ctrl;
    ctrl.fromKeyboard(canUseKeypad && menu.options.controls.keypadMoving(), useCursorKeys);
    if (menu.options.controls.joystick())
        ctrl.fromJoystick(menu.options.controls.joyMove() - 1, menu.options.controls.joyRun(), menu.options.controls.joyStrafe());
    if (mouse_b & (1 << menu.options.controls.mouseRun() - 1))
        ctrl.setRun();
    return ctrl;
}

ClientControls GuiClient::readControlsInGame() const throw () {
    if (!openMenus.empty()) // don't move at all when a real menu is open
        return ClientControls();
    const bool text_input_in_use = !talkbuffer.empty() && menu.options.controls.arrowKeysInTextInput();
    const bool forceUseCursorKeys = menu.options.controls.arrowKeysInStats() == Menu_controls::AS_movePlayer;
    ClientControls ctrl = readControls(menusel != menu_maps,
                                       menusel == menu_none && !text_input_in_use || forceUseCursorKeys); // reserve cursor keys for stats screen or similar unless forced
    ctrl.clearModifiersIfIdle();
    return ctrl;
}

bool GuiClient::firePressed() throw () {
    static double checkTime = 0;
    if (get_time() > checkTime + 1.)
        mouseClicked.clear();
    checkTime = get_time();
    const bool click = mouseClicked.wasClicked(menu.options.controls.mouseShoot() - 1);
    return key[KEY_LCONTROL] || key[KEY_RCONTROL] || (menu.options.controls.joystick() && readJoystickButton(menu.options.controls.joyShoot())) ||
           (mouse_b & (1 << menu.options.controls.mouseShoot() - 1)) || click;
}

//send the client's frame to server (keypresses)
void GuiClient::send_frame(bool newFrame, bool forceSend) throw () {
    static double keyFilterTimeout = 0;

    ClientControls currentControls = readControlsInGame();
    if (menu.options.controls.aimMode() == Menu_controls::AM_Turn && !currentControls.isStrafe()) {
        currentControls.clearLeft();
        currentControls.clearRight();
    }

    if (!forceSend && currentControls == sentControls)
        return;

    bool filtered;
    // filtering is applied when first going diagonally and then one of the directions is dropped
    if (sentControls.isUpDown() && sentControls.isLeftRight() && currentControls.isUpDown() != currentControls.isLeftRight()) {
        if (currentControls.isUpDown() ? currentControls.isUp() != sentControls.isUp() : currentControls.isLeft() != sentControls.isLeft())
            filtered = false;
        else if (keyFilterTimeout == 0) {
            filtered = true;
            keyFilterTimeout = get_time() + .02;
        }
        else
            filtered = get_time() < keyFilterTimeout;
    }
    else
        filtered = false;

    if (!filtered) {
        keyFilterTimeout = 0;
        sentControls = currentControls;
    }
    else if (!forceSend)
        return;

    if (newFrame) {
        ++clFrameSent;
        controlHistory[clFrameSent] = sentControls;
        svFrameHistory[clFrameSent] = fx.frame + (get_time() - frameReceiveTime) * 10.;
    }
    else if (fx.frame + (get_time() - frameReceiveTime) * 10. < svFrameHistory[clFrameSent] + .5) // guess that these controls get to the server during the same frame that the first sent controls do
        controlHistory[clFrameSent] = sentControls;

    BinaryBuffer<256> msg;
    msg.U8(clFrameSent);
    msg.U8(sentControls.toNetwork(false));
    if (fx.physics.allowFreeTurning && menu.options.controls.aimMode() != Menu_controls::AM_8way) {
        refreshGunDir();
        const AccelerationMode am = menu.options.controls.aimMode() == Menu_controls::AM_MouseTurn || menu.options.controls.aimMode() == Menu_controls::AM_MousePos
            ? menu.options.controls.moveRelativity() : AM_Gun;
        msg.U16((am == AM_Gun ? 0x800 : 0) | gunDir.toNetworkLongForm());
    }
    client->send_frame(msg);
}

void GuiClient::refreshGunDir() throw () {
    if (!fx.physics.allowFreeTurning)
        return;
    if (menu.options.controls.aimMode() == Menu_controls::AM_MouseTurn) {
        int mx, my;
        get_mouse_mickeys(&mx, &my);
        gunDir.adjust(mx * menu.options.controls.mouseSensitivity() / 5000.);
    }
    else if (menu.options.controls.aimMode() == Menu_controls::AM_MousePos) {
        const int mx = mouse_x - SCREEN_W / 2;
        const int my = mouse_y - SCREEN_H / 2;
        if (mx != 0 || my != 0)
            gunDir.fromRad(atan2(my, mx));
    }
    else if (menu.options.controls.aimMode() == Menu_controls::AM_Turn) {
        g_timeCounter.refresh();
        const double time = get_time() - gunDirRefreshedTime;
        gunDirRefreshedTime = get_time();
        const ClientControls ctrl = readControlsInGame();
        if (!ctrl.isStrafe() && ctrl.isLeftRight()) {
            // turningSpeed ranges from 0 to 100; we want to scale it so that the minimal speed is 1/4 of a revolution per second (2 in GunDirection units); a reasonable maximum is 4 revolutions (32)
            static const double sMin = 1, sMax = 32;
            // we want to have e^100x == sMax/sMin => 100x == ln(sMax/sMin)
            static const double X = ::log(sMax / sMin) / 100.;
            const double mul = ctrl.isLeft() ? -1 : +1;
            gunDir.adjust(mul * time * sMin * exp(menu.options.controls.turningSpeed() * X));
        }
    }
}

int GuiClient::process_replay_frame_data(ConstDataBlockRef data) throw (BinaryReader::ReadError) { // returns number of bytes read - not necessarily all of data
    if (replay_version == 0)
        return process_replay_frame_data_version_0(data);

    BinaryDataBlockReader read(data);

    const uint8_t byte = read.U8();
    const bool frameNumberFlag    = (byte & (1 << 0));
    const bool playersPresentFlag = (byte & (1 << 1));
    const bool pingFlag           = (byte & (1 << 2));
    const bool preciseGundir      = (byte & (1 << 3));

    if (frameNumberFlag) {
        const uint32_t svframe = read.U32(static_cast<unsigned>(fx.frame) + 1, uint32_t(-1));    //server's frame
        nAssert(svframe == fx.frame + 1 || fx.frame == -1);
        fx.frame = svframe;
        if (!replay_first_frame_loaded) {
            replay_start_frame = svframe;
            replay_first_frame_loaded = true;
        }
    }
    else
        fx.frame++;

    frameReceiveTime = get_time();

    fx.skipped = false;
    if (playersPresentFlag)
        replay_players_present = read.U32();
    if (pingFlag)
        fx.player[int(fx.frame) % maxplayers].ping = read.U32dyn8();

    for (int i = 0; i < maxplayers; i++) {
        ClientPlayer& pl = fx.player[i];
        if (!(replay_players_present & (1 << i)))
            continue;
        else if (!fx.player[i].used) { // New player
            fx.player[i].clear(true, i, " ", i / TSIZE);
            players_sb.push_back(&fx.player[i]);
        }

        const uint8_t byte = read.U8();
        const bool powerupFlag    = (byte & (1 << 0));
        const bool visibilityFlag = (byte & (1 << 1));
        const bool positionFlag   = (byte & (1 << 2));
        const bool controlFlag    = (byte & (1 << 3));
        const bool gundirFlag     = (byte & (1 << 4));

        uint16_t preciseGundirData = 0; // initialized to please GCC
        if (gundirFlag) {
            pl.dead = false;
            if (preciseGundir)
                preciseGundirData = ((byte >> 5) << 8); // high bits
            else
                pl.gundir.fromNetworkShortForm(byte >> 5);
        }
        else
            pl.dead = (byte & (1 << 5));

        if (powerupFlag) {
            const uint8_t byte = read.U8();
            pl.deathbringer_affected = (byte & (1 << 0));
            pl.item_deathbringer     = (byte & (1 << 1));
            pl.item_shield           = (byte & (1 << 2));
            pl.item_turbo            = (byte & (1 << 3));
            pl.item_power            = (byte & (1 << 4));
        }

        if (visibilityFlag)
            pl.visibility = read.U8();

        if (positionFlag)
            read_replay_player_position(read, pl);

        if (controlFlag)
            pl.controls.fromNetwork(read.U8(), true);

        if (preciseGundir && gundirFlag) {
            preciseGundirData |= read.U8(); // add low bits
            pl.gundir.fromNetworkLongForm(preciseGundirData);
        }

        pl.posUpdated = fx.frame;
    }

    return read.getPosition();
}

int GuiClient::process_replay_frame_data_version_0(ConstDataBlockRef data) throw (BinaryReader::ReadError) { // returns number of bytes read - not necessarily all of data
    BinaryDataBlockReader read(data);

    const uint32_t svframe = read.U32(static_cast<unsigned>(fx.frame) + 1, uint32_t(-1));    //server's frame
    nAssert(svframe == fx.frame + 1 || fx.frame == -1);

    ClientPhysicsCallbacks cb(*this);
    fx.rocketFrameAdvance(static_cast<int>(svframe - fx.frame), cb);
    fx.frame = svframe;

    frameReceiveTime = get_time();

    if (!replay_first_frame_loaded) {
        replay_start_frame = svframe;
        replay_first_frame_loaded = true;
    }

    fx.skipped = false;
    const uint32_t players_present = read.U32();
    for (int i = 0; i < maxplayers; i++) {
        ClientPlayer& pl = fx.player[i];
        if (!(players_present & (1 << i)))
            continue;

        // Dead and powerup flags
        const uint8_t byte = read.U8();
        pl.dead = (byte & (1 << 0)) != 0;
        pl.item_deathbringer = (byte & (1 << 1)) != 0;
        pl.deathbringer_affected = (byte & (1 << 2)) != 0;
        pl.item_shield = (byte & (1 << 3)) != 0;
        pl.item_turbo = (byte & (1 << 4)) != 0;
        pl.item_power = (byte & (1 << 5)) != 0;
        const bool preciseGundir = (byte & (1 << 6)) != 0;

        const bool position_data = (byte & (1 << 7)) != 0;
        if (position_data)
            read_replay_player_position(read, pl);

        read_replay_player_controls(read, pl, preciseGundir);

        pl.visibility = read.U8();

        pl.posUpdated = svframe;
    }
    fx.player[svframe % maxplayers].ping = read.U16();

    return read.getPosition();
}

void GuiClient::read_replay_controls(ConstDataBlockRef data) throw (ServerDataError) {
 try {
    BinaryDataBlockReader read(data);

    if (replay_version == 0) {
        read.U32(static_cast<unsigned>(fx.frame) + 1, uint32_t(-1));    //server's frame

        const uint32_t players_present = read.U32();
        for (int i = 0; i < maxplayers; i++) {
            ClientPlayer& pl = fx.player[i];
            if (!(players_present & (1 << i)))
                continue;

            // Flags and position data
            const uint8_t byte = read.U8();
            const bool preciseGundir = (byte & (1 << 6)) != 0;
            const bool position_data = (byte & (1 << 7)) != 0;
            if (position_data)
                skip_replay_player_position(read);

            read_replay_player_controls(read, pl, preciseGundir);

            read.U8(); // visibility
        }
    }
    else {
        const uint8_t byte = read.U8();
        const bool frameNumberFlag    = (byte & (1 << 0));
        const bool playersPresentFlag = (byte & (1 << 1));
        const bool pingFlag           = (byte & (1 << 2));
        const bool preciseGundir      = (byte & (1 << 3));

        if (frameNumberFlag)
            read.U32();

        const uint32_t playersPresent = playersPresentFlag ? read.U32() : replay_players_present;

        if (pingFlag)
            read.U32dyn8();

        for (int i = 0; i < maxplayers; i++) {
            ClientPlayer& pl = fx.player[i];
            if (!(playersPresent & (1 << i)))
                continue;

            const uint8_t byte = read.U8();
            const bool powerupFlag    = (byte & (1 << 0));
            const bool visibilityFlag = (byte & (1 << 1));
            const bool positionFlag   = (byte & (1 << 2));
            const bool controlFlag    = (byte & (1 << 3));
            const bool gundirFlag     = (byte & (1 << 4));

            if (powerupFlag)
                read.U8();
            if (visibilityFlag)
                read.U8();
            if (positionFlag)
                skip_replay_player_position(read);
            if (controlFlag)
                pl.controls.fromNetwork(read.U8(), true);
            if (preciseGundir && gundirFlag)
                read.U8();
        }
    }
 } catch (BinaryReader::ReadError) {
    throw ServerDataError();
 }
}

void GuiClient::read_replay_player_controls(BinaryDataBlockReader& read, ClientPlayer& player, bool preciseGundir) throw (BinaryReader::ReadError) {
    const uint8_t controlByte = read.U8();
    player.controls.fromNetwork(controlByte, true);

    if (preciseGundir)
        player.gundir.fromNetworkLongForm(((controlByte >> 5) << 8) | read.U8());
    else
        player.gundir.fromNetworkShortForm(controlByte >> 5);
}

void GuiClient::read_replay_player_position(BinaryDataBlockReader& read, ClientPlayer* player) throw (BinaryReader::ReadError) {
    const uint8_t roomx = read.U8();
    const uint8_t roomy = read.U8();
    double lx, ly, sx, sy;
    if (replay_version == 0) {
        lx = read.U16();
        ly = read.U16();
        sx = read.flt();
        sy = read.flt();
    }
    else {
        lx = read.dbl();
        ly = read.dbl();
        sx = read.dbl();
        sy = read.dbl();
    }

    if (player) {
        player->pos = WorldCoords(roomx, roomy, lx, ly);
        player->vel = Vec(sx, sy);
    }
}

void GuiClient::read_replay_player_position(BinaryDataBlockReader& read, ClientPlayer& player) throw (BinaryReader::ReadError) {
    read_replay_player_position(read, &player);
}

void GuiClient::skip_replay_player_position(BinaryDataBlockReader& read) throw (BinaryReader::ReadError) {
    read_replay_player_position(read, 0);
}

void GuiClient::netRocketFired(const WorldCoords& pos, bool power) throw () {
    if (on_screen_exact(pos.room.x, pos.room.y, pos.x, pos.y))
        addThreadMessage(new TM_Sound(power ? SAMPLE_POWER_FIRE : SAMPLE_FIRE));
}

void GuiClient::netRocketHitPlayer(int rockid, int rokx, int roky, double time) throw () {
    addThreadMessage(new TM_GunexploEffect(fx.rock[rockid].team, time, WorldCoords(fx.rock[rockid].room(), rokx, roky)));
    if (on_screen_exact(fx.rock[rockid].room().x, fx.rock[rockid].room().y, rokx, roky))
        addThreadMessage(new TM_Sound(SAMPLE_HIT));
}

void GuiClient::netPowerCollision(int target, double time) throw () {
    fx.player[target].hitfx = time + .3;
    if (!replaying || player_on_screen(target))
        addThreadMessage(new TM_Sound(client_sounds.sampleExists(SAMPLE_COLLISION_DAMAGE) ? SAMPLE_COLLISION_DAMAGE : SAMPLE_HIT));
}

void GuiClient::net_data_sound(BinaryReader& read) throw (BinaryReader::ReadError) {
    const uint8_t sample = read.U8();
    if (replaying && read.hasMore()) {
        const uint8_t rx = read.U8(0, fx.map.w - 1), ry = read.U8(0, fx.map.h - 1);
        if (!on_screen(rx, ry))
            return;
    }
    if (sample < NUM_OF_SAMPLES)
        addThreadMessage(new TM_Sound(sample));
}

void GuiClient::net_data_registration_response(BinaryReader& read) throw (BinaryReader::ReadError) {
    if (read.U8() == 1)  // success
        rankingPassword.serverAcceptsToken();
    else
        rankingPassword.serverRejectsToken();
}

void GuiClient::net_data_quick_map_list(BinaryReader& read) throw (BinaryReader::ReadError) {
    Lock ml(mapInfoMutex);
    while (read.hasMore()) {
        const uint16_t hash = read.U16();
        if (hash & 0x8000) {
            MapInfo mi;
            mi.title = "<Random>";
            mi.author = "Outgun";
            mi.width = (hash & 0x7F00) >> 8;
            mi.height = hash & 0xFF;
            mi.votes = 0;
            mi.random = true;
            updateMapPreference(mi);
            maps.push_back(mi);
        }
        else {
            const MapInfoCache::const_iterator ci = mapInfoCache.find(hash);
            if (ci != mapInfoCache.end()) {
                MapInfo mi = ci->second;
                nAssert(mi.infoHash == hash);
                updateMapPreference(mi);
                maps.push_back(mi);
            }
            else {
                MapInfo mi;
                mi.title = '?';
                mi.author = '?';
                mi.width = mi.height = 0;
                mi.votes = 0;
                mi.random = false;
                mi.preference = 0;
                maps.push_back(mi);
            }
        }
        mapListChangedAfterSort = true;
    }
    mapListReadPosition = 0;
}

void GuiClient::updateMapPreference(MapInfo& mi) const throw () {
    const map<string, int>::const_iterator fmi = fav_maps.find(toupper(mi.title));
    if (fmi != fav_maps.end())
        mi.preference = fmi->second;
    else
        mi.preference = menu.options.graphics.highlightUnknownMaps() ? +1 : 0;
}

void GuiClient::sendNegativeVotes() throw () {
    if (protocolExtensions < 1)
        return;
    ExpandingBinaryBuffer msg;
    msg.U8(data_negative_map_votes);
    if (menu.options.game.skipMaps() != Menu_game::SM_none) {
        const int treshold = menu.options.game.skipMaps() == Menu_game::SM_dimmed ? 0 : +1;
        Lock ml(mapInfoMutex);
        for (vector<MapInfo>::const_iterator mi = maps.begin(); mi != maps.end(); ) {
            uint8_t mask = 0;
            for (int bi = 0; bi < 8 && mi != maps.end(); ++bi, ++mi)
                mask |= (mi->preference < treshold) << bi;
            msg.U8(mask);
        }
    }
    client->send_message(msg);
}

void GuiClient::net_data_map_list(BinaryReader& read) throw (BinaryReader::ReadError) {
    MapInfo mapinfo;
    mapinfo.title = read.str();
    mapinfo.author = read.str();
    mapinfo.width = read.U8();
    mapinfo.height = read.U8();
    mapinfo.votes = read.U8();
    mapinfo.random = read.hasMore() ? read.U8() : false;
    updateMapPreference(mapinfo);
    mapinfo.updateInfoHash();

    bool firstOrLast;
    {
        Lock ml(mapInfoMutex);
        if (!mapinfo.random)
            mapInfoCache[mapinfo.infoHash] = mapinfo;
        nAssert(mapListReadPosition <= maps.size());
        firstOrLast = mapListReadPosition == 0 || mapListReadPosition >= maps.size() - 1;
        if (mapListReadPosition >= maps.size())
            maps.push_back(mapinfo);
        else
            maps[mapListReadPosition] = mapinfo;
        ++mapListReadPosition;
        mapListChangedAfterSort = true;
    }
    if (firstOrLast)
        sendNegativeVotes(); // send at the first map because that's when the quick map list is complete, and receiving the full list will take time
}

void GuiClient::net_data_crap_update(BinaryReader& read) throw (BinaryReader::ReadError) {
    const uint8_t pid = read.U8();
    fx.player[pid].set_color(read.U8(0, PlayerBase::invalid_color - 1));
    ClientLoginStatus ls;
    ls.fromNetwork(read.U8());
    const uint32_t prank = read.U32(0, INT_MAX), pscore = read.U32(0, INT_MAX), nscore = read.U32(0, INT_MAX);
    max_world_rank = read.U32();

    const ClientLoginStatus& os = fx.player[me].reg_status;
    const bool newMePrintout =
            !replaying &&
            pid == me &&
            (ls.token() != os.token() ||
             (ls.token() && (ls.masterAuth() != os.masterAuth() || ls.ranking() != os.ranking())) ||
             ls.localAuth() != os.localAuth() ||
             ls.admin() != os.admin());
    if (newMePrintout) {
        ostringstream msg;
        msg << _("Status") << ": ";
        if (ls.token()) {
            if (ls.masterAuth()) {
                msg << _("master authenticated") << ", ";
                if (ls.ranking())
                    msg << _("recording");
                else
                    msg << _("not recording");
            }
            else {
                msg << _("master auth pending") << ", ";
                if (ls.ranking())
                    msg << _("will record");
                else
                    msg << _("will not record");
            }
        }
        else
            msg << _("no ranking login");
        if (ls.localAuth())
            msg << "; " << _("locally authenticated");
        if (ls.admin())
            msg << "; " << _("administrator");
        addThreadMessage(new TM_Text(msg_info, msg.str()));
    }
    fx.player[pid].reg_status = ls;
    fx.player[pid].rank = prank;
    fx.player[pid].score = pscore;
    fx.player[pid].neg_score = nscore;
    // update new team powers
    double power[2] = { 0, 0 };
    for (int i = 0; i < fx.maxplayers; i++)
        if (fx.player[i].used)
            power[fx.player[i].team()] += (fx.player[i].score + 1.) / (fx.player[i].neg_score + 1.);
    for (int t = 0; t < 2; t++)
        fx.teams[t].set_power(power[t]);
}

void GuiClient::net_data_reset_map_list(BinaryReader& read) throw (BinaryReader::ReadError) {
    (void)read;
    Lock ml(mapInfoMutex);
    maps.clear();
    mapListReadPosition = 0;
    mapListChangedAfterSort = true;
    map_vote = -1;
}

void GuiClient::net_data_map_vote(BinaryReader& read) throw (BinaryReader::ReadError) {
    map_vote = read.S8();
}

void GuiClient::net_data_map_votes_update(BinaryReader& read) throw (BinaryReader::ReadError) {
    const uint8_t total = read.U8();
    Lock ml(mapInfoMutex);
    for (int i = 0; i < total; i++) {
        const uint8_t map_nr = read.U8(), votes = read.U8();
        if (map_nr < maps.size()) {
            maps[map_nr].votes = votes;
            mapListChangedAfterSort = true;
        }
    }
}

void GuiClient::net_text_message(Message_type type, int sender_team, const string& text) throw () {
    string translated;
    // This is a kludge because of compatibility.
    // Make sure that the messages here match with the ones in server.cpp and servnet.cpp.
    if (type == msg_server) {
        if (text == "Your vote has no effect until you vote for a specific map.") {
            translated = _("Your vote has no effect until you vote for a specific map.");
            want_map_exit_delayed = true;
        }
        string::size_type pos = text.find(" decided it's time for a map change.");
        if (pos != string::npos) {
            const string name = text.substr(0, pos);
            translated = _("$1 decided it's time for a map change.", name);
        }
        pos = text.find(" decided it's time for a restart.");
        if (pos != string::npos) {
            const string name = text.substr(0, pos);
            translated = _("$1 decided it's time for a restart.", name);
        }
    }
    addThreadMessage(new TM_Text(type, translated.empty() ? text : translated, sender_team));
    if (type == msg_team || type == msg_normal)
        addThreadMessage(new TM_Sound(SAMPLE_TALK));
}

void GuiClient::netStatsReady() throw () {
    if (menu.options.game.showStats() != Menu_game::SS_none && menusel == menu_none && openMenus.empty()) {
        switch (menu.options.game.showStats()) {
        /*break;*/ case Menu_game::SS_teams:   menusel = menu_teams;
            break; case Menu_game::SS_players: menusel = menu_players;
            break; default: nAssert(0);
        }
        stats_autoshowing = true;
    }
    if (players_sb.size() > 1 && (menu.options.game.saveStats() && !replaying || menu.options.game.saveReplayStats() && replaying))
        fx.save_stats("client_stats", fx.map.title, gameSettings);
}

void GuiClient::netMapChange(const string& maptitle, const int map_number, const int total_maps) throw () {
    want_map_exit = false;
    want_map_exit_delayed = false;
    deadAfterHighlighted = true;

    // make sure the server knows that want_map_exit = false (in case data_map_exit_on was sent and not yet received when the data_map_change was sent)
    if (!replaying) {
        BinaryBuffer<16> msg;
        msg.U8(data_map_exit_off);
        client->send_message(msg);
    }

    current_map = map_number;
    if (map_vote == current_map)
        map_vote = -1;

    string msg;
    if (!replaying)
        msg = _("This map is $1 ($2 of $3).", maptitle, itoa(current_map + 1), itoa(total_maps));
    else
        msg = _("This map is $1.", maptitle);
    addThreadMessage(new TM_Text(msg_info, msg));
}

void GuiClient::netGameoverPeriodStart(uint32_t redScore, uint32_t blueScore, int caplimit, int timelimit) throw () {
    red_final_score = redScore;
    blue_final_score = blueScore;

    FormattedText msg = tf("CTF GAME OVER - FINAL SCORE: $RRED $1$> - $BBLUE $2$>", itoa(red_final_score), itoa(blue_final_score));
    addThreadMessage(new TM_Text(msg_info, msg));
    addThreadMessage(new TM_Sound(SAMPLE_CTF_GAMEOVER));
    if (!(replaying && !spectating)) { // avoid text related to the next game at the end of a single game replay
        string msg;
        if (caplimit > 0)
            msg = _("CAPTURE $1 FLAGS TO WIN THE GAME.", itoa(caplimit));
        if (timelimit > 0) {
            if (!msg.empty())
                msg += ' ';
            msg += _("TIME LIMIT IS $1 MINUTES.", itoa(timelimit));
        }
        if (!msg.empty())
            addThreadMessage(new TM_Text(msg_info, msg));
    }
}

void GuiClient::netGameoverPeriodEnd() throw () {
    if (stats_autoshowing) {
        menusel = menu_none;
        stats_autoshowing = false;
    }
}

void GuiClient::netGameStarted() throw () {
    if (stats_autoshowing) {
        menusel = menu_none;
        stats_autoshowing = false;
    }
    deadAfterHighlighted = true;
}

void GuiClient::netPhysicsChanged() throw () {
    log("Server friction/drag/acceleration %f/%f/%f",
        fx.physics.fric, fx.physics.drag, fx.physics.accel);
    log("Server brake/turn/run/turbo/flag-modifier %f/%f/%f/%f/%f",
        fx.physics.brake_mul, fx.physics.turn_mul, fx.physics.run_mul, fx.physics.turbo_mul, fx.physics.flag_mul);
    log("Server ff/dbff/rocketspeed %f/%f/%f",
        fx.physics.friendly_fire, fx.physics.friendly_db, fx.physics.rocket_speed);

    ofstream out((whereuserdir + "log" + directory_separator + "physics.log").c_str());
    out << hostname << '\n';
    out << "friction     " << fx.physics.fric << '\n';
    out << "drag         " << fx.physics.drag << '\n';
    out << "acceleration " << fx.physics.accel << '\n';
    out << "brake_acceleration " << fx.physics.brake_mul << '\n';
    out << "turn_acceleration  " << fx.physics.turn_mul << '\n';
    out << "run_acceleration   " << fx.physics.run_mul << '\n';
    out << "turbo_acceleration " << fx.physics.turbo_mul << '\n';
    out << "flag_acceleration  " << fx.physics.flag_mul << '\n';
    out << "rocket_speed " << fx.physics.rocket_speed << '\n';
    out.close();
}

void GuiClient::netKill(int attacker, int target, DamageType cause, bool carrier_defended, bool flag_defended, Statistics::FlagType carriedFlag, bool spree_ended, bool spree_started) throw () {
    if (target == me)
        deadAfterHighlighted = true;

    const bool attacker_team = attacker / TSIZE;
    const bool target_team = target / TSIZE;
    const bool same_team = (attacker_team == target_team);
    const bool known_attacker = fx.player[attacker].used;

    FormattedText msg;
    if (cause == DT_deathbringer) {
        if (!known_attacker)
            msg = tf("$1 was choked.", formatName(target));
        else if (same_team)
            msg = tf("$1 was choked by teammate $2.", formatName(target), formatName(attacker));
        else
            msg = tf("$1 was choked by $2.", formatName(target), formatName(attacker));
        if (player_on_screen_exact(target))
            addThreadMessage(new TM_Sound(SAMPLE_DIEDEATHBRINGER));
    }
    else if (cause == DT_collision) {
        if (!known_attacker)    // this should never happen with the current code, probably not in the future either, but it's still here...
            msg = tf("$1 received a mortal blow.", formatName(target));
        else if (same_team) // this shouldn't happen with the current special collisions either, but we're ready for changes
            msg = tf("$1 received a mortal blow from teammate $2.", formatName(target), formatName(attacker));
        else
            msg = tf("$1 received a mortal blow from $2.", formatName(target), formatName(attacker));
        if (player_on_screen_exact(target))
            addThreadMessage(new TM_Sound(SAMPLE_DEATH + rand() % 2));
    }
    else {
        nAssert(cause == DT_rocket);
        if (!known_attacker)    // this should never happen with the current code, but it's here for future
            msg = tf("$1 was nailed.", formatName(target));
        else if (same_team)
            msg = tf("$1 was nailed by teammate $2.", formatName(target), formatName(attacker));
        else
            msg = tf("$1 was nailed by $2.", formatName(target), formatName(attacker));
        if (player_on_screen_exact(target))
            addThreadMessage(new TM_Sound(SAMPLE_DEATH + rand() % 2));
    }
    if (menu.options.game.showKillMessages())
        addThreadMessage(new TM_Text(msg_info, msg));

    #ifdef DEFENDING_MESSAGES
    if (carrier_defended && known_attacker) {
        if (attacker_team == 0)
            msg = tf("$1 defends the red carrier.", formatName(attacker));
        else
            msg = tf("$1 defends the blue carrier.", formatName(attacker));
        addThreadMessage(new TM_Text(msg_info, msg));
    }
    if (flag_defended && known_attacker) {
        if (attacker_team == 0)
            msg = tf("$1 defends the red flag.", formatName(attacker));
        else
            msg = tf("$1 defends the blue flag.", formatName(attacker));
        addThreadMessage(new TM_Text(msg_info, msg));
    }
    #else
    (void)(carrier_defended && flag_defended);
    #endif // DEFENDING_MESSAGES
    if (spree_ended) {
        if (!known_attacker)
            msg = tf("$1's killing spree was ended.", formatName(target));
        else
            msg = tf("$1's killing spree was ended by $2.", formatName(target), formatName(attacker));
        if (menu.options.game.showKillMessages())
            addThreadMessage(new TM_Text(msg_info, msg));
    }

    if (carriedFlag != Statistics::flagNone) {
        if (carriedFlag == Statistics::flagWild)
            msg = tf("$1 LOST THE $GWILD FLAG$>!", formatName(target));
        else if (target_team == 0 && carriedFlag == Statistics::flagOwn || target_team == 1 && carriedFlag == Statistics::flagEnemy)
            msg = tf("$1 LOST THE $RRED FLAG$>!", formatName(target));
        else
            msg = tf("$1 LOST THE $BBLUE FLAG$>!", formatName(target));
        if (menu.options.game.showFlagMessages())
            addThreadMessage(new TM_Text(msg_info, msg));
        addThreadMessage(new TM_Sound(SAMPLE_CTF_LOST));
    }
    if (spree_started) {
        if (attacker == me)
            addThreadMessage(new TM_Sound(SAMPLE_KILLING_SPREE));
        msg = tf("$1 is on a killing spree!", formatName(attacker));
        if (menu.options.game.showKillMessages())
            addThreadMessage(new TM_Text(msg_info, msg));
    }
}

void GuiClient::netSuicide(int pid, Statistics::FlagType carriedFlag, bool spree_ended) throw () {
    if (pid == me)
        deadAfterHighlighted = true;

    const int team = pid / TSIZE;
    if (spree_ended && menu.options.game.showKillMessages())
        addThreadMessage(new TM_Text(msg_info, tf("$1's killing spree was ended.", formatName(pid))));
    if (carriedFlag != Statistics::flagNone) {
        FormattedText msg;
        if (carriedFlag == Statistics::flagWild)
            msg = tf("$1 LOST THE $GWILD FLAG$>!", formatName(pid));
        else if (team == 0 && carriedFlag == Statistics::flagOwn || team == 1 && carriedFlag == Statistics::flagEnemy)
            msg = tf("$1 LOST THE $RRED FLAG$>!", formatName(pid));
        else
            msg = tf("$1 LOST THE $BBLUE FLAG$>!", formatName(pid));
        if (menu.options.game.showFlagMessages())
            addThreadMessage(new TM_Text(msg_info, msg));
        addThreadMessage(new TM_Sound(SAMPLE_CTF_LOST));
    }
    if (player_on_screen_exact(pid))
        addThreadMessage(new TM_Sound(SAMPLE_DEATH + rand() % 2));
}

void GuiClient::netFlagTake(int pid, Statistics::FlagType carriedFlag) throw () {
    const int team = pid / TSIZE;
    FormattedText msg;
    if (carriedFlag == Statistics::flagWild)
        msg = tf("$1 GOT THE $GWILD FLAG$>!", formatName(pid));
    else if (team == 0 && carriedFlag == Statistics::flagOwn || team == 1 && carriedFlag == Statistics::flagEnemy)
        msg = tf("$1 GOT THE $RRED FLAG$>!", formatName(pid));
    else
        msg = tf("$1 GOT THE $BBLUE FLAG$>!", formatName(pid));
    if (menu.options.game.showFlagMessages())
        addThreadMessage(new TM_Text(msg_info, msg));
}

void GuiClient::netFlagReturn(int pid) throw () {
    FormattedText msg;
    if (pid / TSIZE == 0)
        msg = tf("$1 RETURNED THE $RRED FLAG$>!", formatName(pid));
    else
        msg = tf("$1 RETURNED THE $BBLUE FLAG$>!", formatName(pid));
    if (menu.options.game.showFlagMessages())
        addThreadMessage(new TM_Text(msg_info, msg));
    addThreadMessage(new TM_Sound(SAMPLE_CTF_RETURN));
}

void GuiClient::netFlagDrop(int pid, Statistics::FlagType carriedFlag) throw () {
    const int team = pid / TSIZE;
    FormattedText msg;
    if (carriedFlag == Statistics::flagWild)
        msg = tf("$1 DROPPED THE $GWILD FLAG$>!", formatName(pid));
    else if (team == 0 && carriedFlag == Statistics::flagOwn || team == 1 && carriedFlag == Statistics::flagEnemy)
        msg = tf("$1 DROPPED THE $RRED FLAG$>!", formatName(pid));
    else
        msg = tf("$1 DROPPED THE $BBLUE FLAG$>!", formatName(pid));
    if (menu.options.game.showFlagMessages())
        addThreadMessage(new TM_Text(msg_info, msg));
    addThreadMessage(new TM_Sound(SAMPLE_CTF_LOST));
}

void GuiClient::netTeamChange(int pl1, int pl2) throw () {
    if (pl1 == me || pl2 == me) {
        want_change_teams = false;
        deadAfterHighlighted = true;
    }

    FormattedText msg;
    if (pl2 != -1)
        msg = tf("$1 and $2 swapped teams.", formatName(pl1), formatName(pl2));
    else if (pl1 / TSIZE == 1)
        msg = tf("$1 moved to red team.", formatName(pl1));
    else
        msg = tf("$1 moved to blue team.", formatName(pl1));
    addThreadMessage(new TM_Text(msg_info, msg));
    addThreadMessage(new TM_Sound(SAMPLE_CHANGETEAM));
}

void GuiClient::process_replay_packet(ConstDataBlockRef data) throw (ServerDataError) {
    try {
        const int frameSize = process_replay_frame_data(data);
        BinaryDataBlockReader read(data);
        read.block(frameSize);
        while (read.hasMore()) {
            const uint32_t size = replay_version == 0 ? read.U32() : read.U32dyn8();
            process_message(read.block(size));
        }
    } catch (BinaryReader::ReadError) {
        throw ServerDataError();
    }
}

//send chat message
void GuiClient::send_chat(const string& text) throw () {
    if (text.empty() || text == "." || isFlood(text))
        return;
    BinaryBuffer<256> msg;
    msg.U8(data_text_message);
    msg.str(text);
    client->send_message(msg);
}

//print message to "console"
void GuiClient::print_message(Message_type type, const FormattedText& msg, int sender_team) throw () {
    if (menu.options.game.messageLogging() != Menu_game::ML_none && !replaying) {
        if (menu.options.game.messageLogging() == Menu_game::ML_full || type == msg_normal || type == msg_team)
            message_log << date_and_time() << "  " << msg.unformatted() << endl;
    }

    bool highlight = false;
    if (type == msg_normal || type == msg_team) {
        const string uppercase = toupper(msg.unformatted());
        for (vector<string>::const_iterator hi = highlight_text.begin(); hi != highlight_text.end(); ++hi)
            if (uppercase.find(*hi) != string::npos) {
                highlight = true;
                break;
            }
    }
    const vector<FormattedText> lines = split_to_lines(msg, 79, 4);
    while (chatbuffer.size() > graphics.chat_max_lines() + lines.size())
        chatbuffer.pop_front();
    for (vector<FormattedText>::const_iterator li = lines.begin(); li != lines.end(); ++li) {
        Message message(type, *li, static_cast<int>(fx.frame / 10), sender_team);
        if (highlight)
            message.highlight();
        chatbuffer.push_back(message);
    }
}

void GuiClient::save_screenshot() throw () {
    string filename;
    for (int i = 0; i < 1000; i++) {
        // filename: screens/outgxxx.ext
        ostringstream fname;
        fname << whereuserdir << "screens" << directory_separator;
        fname << "outg" << setfill('0') << setw(3) << i << Graphics::save_extension;
        if (!file_exists(fname.str().c_str(), FA_ARCH | FA_DIREC | FA_HIDDEN | FA_RDONLY | FA_SYSTEM, 0)) {
            filename = fname.str();
            break;
        }
    }

    string message;
    if (graphics.save_screenshot(filename))
        message = _("Saved screenshot to $1.", filename);
    else
        message = _("Could not save screenshot to $1.", filename);
    print_message(msg_warning, message);
}

//toggle help screen
void GuiClient::toggle_help() throw () {
    if (openMenus.safeTop() == &menu.help.menu)
        openMenus.close();
    else
        showMenu(menu.help);
}

string GuiClient::refreshStatusAsString() const throw () {
    switch (refreshStatus) {
    /*break;*/ case RS_none:       return _("Inactive");
        break; case RS_running:    return _("Running");
        break; case RS_failed:     return _("Failed");
        break; case RS_contacting: return _("Contacting the servers...");
        break; case RS_connecting: return _("Getting server list: connecting...");
        break; case RS_receiving:  return _("Getting server list: receiving...");
        break; default: nAssert(0);
    }
    nAssert(0); return 0;
}

void GuiClient::getServerListThread() throw () {
    nAssert(refreshStatus == RS_running);

    // get server list and refresh
    bool ok = getServerList();
    if (!get_local_servers())
        ok = false;
    if (!abortThreads)
        if (!refresh_all_servers())
            ok = false;

    refreshStatus = ok ? RS_none : RS_failed;
}

void GuiClient::refreshThread() throw () {
    nAssert(refreshStatus == RS_running);
    refreshStatus = refresh_all_servers() ? RS_none : RS_failed;
}

class TempPingData {    // internal to GuiClient::refresh_all_servers
    double st[4];   // send time
    int rc;         // count of received packets
    double rt;      // sum of pings (for averaging)

public:
    TempPingData() throw () : rc(0), rt(0) { }
    void send(int pack) throw () { g_timeCounter.refresh(); st[pack] = get_time(); }
    void receive(int pack) throw () { g_timeCounter.refresh(); ++rc; rt += get_time() - st[pack]; }
    int received() const throw () { return rc; }
    int ping() const throw () { return static_cast<int>(1000 * rt / rc); }
};

bool GuiClient::refresh_all_servers() throw () {
    refreshStatus = RS_contacting;

    serverListMutex.lock();

    vector<ServerListEntry*> servers;
    for (vector<ServerListEntry>::iterator si = gamespy.begin(); si != gamespy.end(); ++si)
        servers.push_back(&*si);
    if (!menu.connect.favorites())
        for (vector<ServerListEntry>::iterator si = mgamespy.begin(); si != mgamespy.end(); ++si)
            servers.push_back(&*si);

    const int nServers = static_cast<int>(servers.size());
    vector<TempPingData> tempd(nServers);
    int pending = nServers;         // count of valid entries still waiting for a response

    for (int i = 0; i < nServers; i++) {
        servers[i]->refreshed = true;
        servers[i]->ping = 0;
    }

    serverListMutex.unlock();

    if (pending == 0)
        return true;

    Network::UDPSocket sock(true);
    try {
        sock.open(Network::NonBlocking, 0);
    } catch (const Network::Error& e) {
        log.error(_("Can't open socket for refreshing servers. $1", e.str()));
        return false;
    }

    for (int round = 0; round < 20; round++) {  // each round takes .1 seconds
        if (abortThreads) {
            log("Refreshing servers aborted: client exiting.");
            return false;
        }

        if (round < 4) {    // on first 4 rounds, packets are sent to each server
            Lock ml(serverListMutex);
            for (int i = 0; i < nServers; i++) {
                BinaryBuffer<512> msg;
                msg.U32(0);         //special packet
                msg.U32(200);       //serverinfo request
                msg.U8(i);        //connect entry (am I lazy or what)
                msg.U8(round);        //packet number

                try {
                    sock.write(servers[i]->address(), msg);
                    tempd[i].send(round);
                } catch (Network::Error&) { } //#fix: report?
            }
        }

        if (pending == 0)
            break;

        // parse received responses
        for (int subRound = 0; subRound < 20; subRound++) {
            platSleep(5);

            for (;;) {  // continue while there are new packets
                char buffer[512];
                Network::UDPSocket::ReadResult result;
                try {
                    result = sock.read(buffer, 512);
                } catch (Network::Error&) {
                    break; //#fix: report?
                }
                if (result.length == 0)
                    break;
                if (result.length < 10)
                    continue;

                try {
                    BinaryDataBlockReader msg(buffer, result.length);

                    const uint32_t dw1 = msg.U32(), dw2 = msg.U32();
                    if (dw1 != 0 || dw2 != 200)
                        continue;

                    const uint8_t index = msg.U8(); // entry number echoed by the server
                    const uint8_t pack = msg.U8();  // packet #

                    if (index >= nServers || pack >= 4 || pack > round)  // don't have to worry about < 0 because they're unsigned
                        continue;

                    Lock ml(serverListMutex);

                    if (result.source != servers[index]->address())
                        continue;

                    servers[index]->info = msg.str();

                    if (tempd[index].received() == 0)   // first reply -> server has changed to being valid
                        pending--;

                    tempd[index].receive(pack);
                    servers[index]->ping = tempd[index].ping();

                    servers[index]->noresponse = false;  // set here in advance so that the main thread will already show it
                } catch (BinaryReader::ReadError) { }
            }
        }
    }

    // mark those that got no responses
    {
        Lock ml(serverListMutex);
        for (int i = 0; i < nServers; i++)
            if (tempd[i].received() == 0)
                servers[i]->noresponse = true;
    }

    return true;
}

bool GuiClient::getServerList() throw () {
    if (!g_masterSettings.address().valid())
        return false;

    refreshStatus = RS_connecting;

    try {
        Network::TCPSocket sock(Network::NonBlocking, 0, true);
        sock.connect(g_masterSettings.address());

        const string request = build_http_request(false, g_masterSettings.host(), g_masterSettings.query(),
                                                  "simple"
                                                  "&branch=" + url_encode(GAME_BRANCH) +
                                                  "&master=" + itoa(g_masterSettings.crc()) +
                                                  "&protocol=" + url_encode(GAME_PROTOCOL));
        sock.persistentWrite(request, &abortThreads, 30000);
        log("Successfully sent query to master: '%s'", formatForLogging(request).c_str());

        refreshStatus = RS_receiving;

        stringstream response;
        sock.readAll(response, &abortThreads, 30000);
        sock.close();

        log("Full response: '%s'", formatForLogging(response.str()).c_str());
        if (parseServerList(response))
            return true;
        else {
            log.error(_("Incorrect data received from master server."));
            return false;
        }
    } catch (Network::ExternalAbort) {
        return false;
    } catch (const Network::Error& e) {
        log.error(_("Getting server list: $1", e.str()));
        return false;
    }
}

bool GuiClient::get_local_servers() throw () {
    refreshStatus = RS_connecting;

    try {
        Network::UDPSocket sock(Network::NonBlocking, 0, true, true);

        static const char broadcast_string[] = "Outgun";

        BinaryBuffer<512> msg;
        msg.U32(0);
        msg.str(broadcast_string);
        sock.write("255.255.255.255:" + itoa(DEFAULT_UDP_PORT), msg);
        log("Successfully sent broadcast query.");

        refreshStatus = RS_receiving;

        platSleep(500);
        for (;;) {
            char buffer[512];
            const Network::UDPSocket::ReadResult result = sock.read(buffer, sizeof(buffer));
            if (result.length == 0)
                break;

            log("Response from %s: '%s'", result.source.toString().c_str(), formatForLogging(buffer).c_str());

            try {
                BinaryDataBlockReader read(buffer, result.length);
                if (read.str() != broadcast_string)
                    continue;   // Not an Outgun server.
            } catch (BinaryReader::ReadError) {
                continue;
            }

            ServerListEntry spy;
            spy.setAddress(result.source);
            mgamespy.push_back(spy);
        }
        return true;
    } catch (const Network::Error& e) {
        log("Querying LAN servers: %s", e.str().c_str());
        return false;
    }
}

bool GuiClient::parseServerList(istream& response) throw () {
    static const istream::traits_type::int_type eof_ch = istream::traits_type::eof();

    string line, empty;

    // Skip HTTP headers.
    while (getline(response, line, '\r') && getline(response, empty, '\n'))
        if (line.empty())
            break;

    // The first line is the newest version.
    string newest_version;
    getline_smart(response, newest_version);
    if (newest_version.empty())
        return false;

    // The second line is the number of lines in the new master.txt, or 0 if there is no new master.txt.
    getline_smart(response, line);
    int masterTxtLen;
    {
        istringstream is(line);
        is >> masterTxtLen;
        if (!is || is.peek() != eof_ch || masterTxtLen < 0 || masterTxtLen > 30)
            return false;
    }
    if (masterTxtLen != 0) {
        vector<string> masterTxt;
        for (int i = 0; i < masterTxtLen; ++i) {
            getline_smart(response, line);
            masterTxt.push_back(line);
        }
        if (!getline_smart(response, line) || line != "--- end")
            return false;
        ofstream os((whereuserdir + "config" + directory_separator + "master.txt").c_str());
        if (os) {
            for (vector<string>::const_iterator li = masterTxt.begin(); li != masterTxt.end(); ++li)
                os << *li << '\n';
            const bool err = !os;
            os.close();
            if (err)    // try to remove the failed file and therefore return to default settings (which may be different from what were previously, but what can we do)
                delete_file((whereuserdir + "config" + directory_separator + "master.txt").c_str());
            else
                g_masterSettings.load(log);
        }
    }

    // After master.txt is the total number of servers.
    getline_smart(response, line);
    istringstream is(line);
    int total_servers;
    is >> total_servers;
    if (!is || is.peek() != eof_ch || total_servers < 0 || total_servers > 10000)
        return false;

    Lock ml(serverListMutex);

    // Parse the successful response into the gamespy screen.

    mgamespy.clear();

    int servers_read;
    for (servers_read = 0; getline_smart(response, line); servers_read++) {
        ServerListEntry spy;
        if (spy.setAddress(line))
            mgamespy.push_back(spy);
        else
            return false;
    }

    if (servers_read != total_servers)
        return false;

    if (newest_version != GAME_RELEASED_VERSION)
        menu.newVersion.set(_("New version: $1", newest_version));
    else
        menu.newVersion.set("");

    return true;
}

void GuiClient::handleKeypress(int sc, int ch, bool withControl, bool alt_sequence) throw () {  // sc = scancode, ch = character, as returned by readkey
    // handle global keys first
    bool handled = true;
    switch (sc) {   // if the key isn't handled, set handled = false
    /*break;*/ case KEY_F1:
            toggle_help();
        break; case KEY_F11:
            screenshot = true;
        break; case KEY_F5:
            if (openMenus.safeTop() == &m_serverInfo.menu)
                openMenus.close();
            else if (connected || replaying)
                showMenu(m_serverInfo);
            stats_autoshowing = false;
        break; case KEY_F3: case KEY_N:
            if (withControl)
                menu.options.graphics.showNames.set(menu.options.graphics.showNames() == Menu_graphics::N_Never ? Menu_graphics::N_SameRoom : Menu_graphics::N_Never);
            else
                handled = false;
        break; case KEY_ENTER:
            if (ch == 0) {  // Alt+Enter
                if (get_time() > lastAltEnterTime + .5) {
                    menu.options.screenMode.windowed.toggle();
                    screenModeChange(); // ignore error
                    lastAltEnterTime = get_time();
                }
            }
            else
                handled = false;
        break; case KEY_M:
            if (withControl) {
                menu.options.sounds.enabled.toggle();
                MCF_sndEnableChange();
            }
            else
                handled = false;
        break; default:
            handled = false;
    }
    if (handled)
        return;
    if (!openMenus.empty()) {
        Lock ml(frameMutex);   // some menus need access
        if (!openMenus.handleKeypress(sc, ch)) {
            if (sc == KEY_ESC)
                MCF_menuCloser();
        }
        return;
    }
    handled = true;
    switch (sc) {
    /*break;*/ case KEY_ESC:
            if (menusel != menu_none) {
                menusel = menu_none;
                stats_autoshowing = false;
            }
            else if (!talkbuffer.empty()) { // cancel chat
                talkbuffer.clear();
                talkbuffer_cursor = 0;
            }
            else
                showMenu(menu);
        break; case KEY_F2:
            if (!replaying) {
                menusel = (menusel == menu_maps ? menu_none : menu_maps);
                stats_autoshowing = false;
                if (menusel == menu_maps) {
                    load_fav_maps();
                    apply_fav_maps();
                }
            }
        break; case KEY_F3:
            menusel = (menusel == menu_teams ? menu_none : menu_teams);
            stats_autoshowing = false;
        break; case KEY_F4:
            menusel = (menusel == menu_players ? menu_none : menu_players);
            stats_autoshowing = false;
        break; case KEY_F8: {
            if (!replaying) {
                want_map_exit = !want_map_exit;
                want_map_exit_delayed = false;

                BinaryBuffer<16> msg;
                if (want_map_exit)
                    msg.U8(data_map_exit_on);
                else
                    msg.U8(data_map_exit_off);
                client->send_message(msg);
            }
        }
        break; case KEY_F12:
            graphics.toggle_full_playfield();
        break; default:
            handled = false;
    }
    if (handled)
        return;
    if (menusel != menu_none)
        if (handleInfoScreenKeypress(sc, ch, withControl, alt_sequence))
            return;
    handleGameKeypress(sc, ch, withControl, alt_sequence);
}

bool GuiClient::handleInfoScreenKeypress(int sc, int ch, bool withControl, bool alt_sequence) throw () {  // sc = scancode, ch = character, as returned by readkey
    (void)(withControl & alt_sequence);
    if (menu.options.controls.arrowKeysInStats() != Menu_controls::AS_useMenu && (sc == KEY_UP || sc == KEY_DOWN || sc == KEY_LEFT || sc == KEY_RIGHT))
        return false;
    switch (menusel) {
    /*break;*/ case menu_maps:
            switch (sc) {
            /*break;*/ case KEY_UP:     graphics.map_list_prev();
                break; case KEY_DOWN:   graphics.map_list_next();
                break; case KEY_PGUP:   graphics.map_list_prev_page();
                break; case KEY_PGDN:   graphics.map_list_next_page();
                break; case KEY_HOME:   graphics.map_list_begin();
                break; case KEY_END:    graphics.map_list_end();
                break; case KEY_BACKSPACE:
                    if (!edit_map_vote.empty())
                        edit_map_vote.erase(edit_map_vote.end() - 1);
                break; case KEY_ENTER: case KEY_ENTER_PAD: {
                    int new_vote = atoi(edit_map_vote) - 1;
                    if (new_vote >= 255)
                        new_vote = -1;
                    edit_map_vote.clear();
                    if (new_vote != map_vote && (new_vote >= 0 || map_vote >= 0)) {
                        map_vote = new_vote;
                        want_map_exit_delayed = false;
                        // send map vote
                        BinaryBuffer<16> msg;
                        msg.U8(data_map_vote);
                        msg.U8(map_vote < 0 ? 255 : map_vote);
                        client->send_message(msg);
                    }
                }
                break; case KEY_SPACE: case KEY_RIGHT:
                    do {
                        mapListSortKey = static_cast<MapListSortKey>((mapListSortKey + 1) % MLSK_COUNT);
                    } while (mapListSortKey == MLSK_Favorite && fav_maps.empty());
                    mapListChangedAfterSort = true;
                break; case KEY_LEFT:
                    do {
                        mapListSortKey = static_cast<MapListSortKey>((mapListSortKey - 1 + MLSK_COUNT) % MLSK_COUNT);
                    } while (mapListSortKey == MLSK_Favorite && fav_maps.empty());
                    mapListChangedAfterSort = true;
                break; default:
                    if (!isdigit(ch))
                        return false;
                    if (edit_map_vote.size() < 3)
                        edit_map_vote += ch;
            }
            return true;
        break; case menu_players:
            if (sc == KEY_UP || sc == KEY_LEFT || sc == KEY_PGUP)
                player_stats_page = max(0, player_stats_page - 1);
            else if (sc == KEY_DOWN || sc == KEY_RIGHT || sc == KEY_PGDN)
                player_stats_page = min(3, player_stats_page + 1);
            else if (sc == KEY_TAB)
                player_stats_page = (player_stats_page + (key[KEY_LSHIFT] || key[KEY_RSHIFT] ? -1 + 4 : +1)) % 4;
            else
                return false;
            return true;
        break; case menu_teams:
            if (sc == KEY_UP || sc == KEY_PGUP)
                graphics.team_captures_prev();
            else if (sc == KEY_DOWN || sc == KEY_PGDN)
                graphics.team_captures_next();
            else
                return false;
            return true;
        break; case menu_none: // regular menu, if any, handled elsewhere
            return false;
        break; default:
            nAssert(0);
            return false;
    }
}

void GuiClient::handleGameKeypress(int sc, int ch, bool withControl, bool alt_sequence) throw () {  // sc = scancode, ch = character, as returned by readkey
    if (key[KEY_P] && !replaying)         // ping setting
        if (sc == KEY_PLUS_PAD) {
            print_message(msg_info, "Ping +" + itoa(iround(client->increasePacketDelay() * 1000)));
            return;
        }
        else if (sc == KEY_MINUS_PAD) {
            print_message(msg_info, "Ping +" + itoa(iround(client->decreasePacketDelay() * 1000)));
            return;
        }

    if (toupper(ch) == 'P' && replaying) {
        replay_paused = !replay_paused;
        return;
    }

    // Quick messages
    if (!replaying && ch == 0 && sc >= KEY_0 && sc <= KEY_9 && menu.options.quickMessages.enabled() && talkbuffer.empty()) {
        int messageIndex = sc - KEY_1;
        if (messageIndex == -1)
            messageIndex = 9;
        const string& message = menu.options.quickMessages.messages[messageIndex]();
        if (!message.empty()) {
            talkbuffer = message;
            talkbuffer_cursor = message.length();
            if (menu.options.quickMessages.sendImmediately())
                sc = KEY_ENTER; // send the message
            else
                return;
        }
    }

    switch (sc) {   // Allow these keys to be used also for typing text.
    /*break;*/ case KEY_MINUS_PAD:
        if (!replaying && !withControl)
            break;
        if (visible_rooms < fx.map.w || visible_rooms < fx.map.h || (repeatMapX() || repeatMapY()) && visible_rooms < 100) {
            if (replaying) {
                ++visible_rooms;
                if (replayTopLeftRoom.first == fx.map.w + 1 - visible_rooms && replayTopLeftRoom.first > 0) // if map border wasn't broken, don't break it either
                    --replayTopLeftRoom.first;
                if (replayTopLeftRoom.second == fx.map.h + 1 - visible_rooms && replayTopLeftRoom.second > 0)
                    --replayTopLeftRoom.second;
            }
            else
                visible_rooms += visible_rooms < 3 ? .25 : visible_rooms < 5 ? .5 : 1.;
        }
        return;
    break; case KEY_PLUS_PAD:
        if (!replaying && !withControl)
            break;
        if (visible_rooms > 1) {
            if (replaying)
                --visible_rooms;
            else
                visible_rooms -= visible_rooms <= 3 ? .25 : visible_rooms <= 5 ? .5 : 1.;
        }
        return;
    }

    switch (sc) {
    /*break;*/ case KEY_HOME:   // change colours
            if (replaying)
                replay_rate = 1;
            else {
                if (withControl)
                    graphics.reset_playground_colors();
                else if (menu.options.controls.arrowKeysInTextInput() && !talkbuffer.empty())
                    talkbuffer_cursor = 0;
                else
                    graphics.random_playground_colors();
            }
        break; case KEY_INSERT:
            show_all_messages = !show_all_messages;
        break; case KEY_BACKSPACE:
            if (!talkbuffer.empty() && talkbuffer_cursor > 0) {
                talkbuffer.erase(talkbuffer_cursor - 1, 1);
                talkbuffer_cursor--;
            }
        break; case KEY_ENTER: case KEY_ENTER_PAD:
            if (!talkbuffer.empty()) {
                send_chat(trim(talkbuffer));
                talkbuffer.clear();
                talkbuffer_cursor = 0;
            }
        break; case KEY_DEL: {
            if (menu.options.controls.arrowKeysInTextInput() && !talkbuffer.empty())
                talkbuffer.erase(talkbuffer_cursor, 1);
            else if (!replaying) {
                BinaryBuffer<16> msg;
                msg.U8(data_suicide);
                client->send_message(msg);
            }
        }
        break; case KEY_LEFT: {
            if (replaying)
                replayTopLeftRoom.first = (replayTopLeftRoom.first - 1 + fx.map.w) % fx.map.w;
            else if (menu.options.controls.arrowKeysInTextInput() && !talkbuffer.empty() && talkbuffer_cursor > 0)
                talkbuffer_cursor--;
        }
        break; case KEY_RIGHT: {
            if (replaying)
                replayTopLeftRoom.first = (replayTopLeftRoom.first + 1) % fx.map.w;
            else if (menu.options.controls.arrowKeysInTextInput() && !talkbuffer.empty() && talkbuffer_cursor < static_cast<int>(talkbuffer.size()))
                talkbuffer_cursor++;
        }
        break; case KEY_UP: {
            if (replaying)
                replayTopLeftRoom.second = (replayTopLeftRoom.second - 1 + fx.map.h) % fx.map.h;
        }
        break; case KEY_DOWN: {
            if (replaying)
                replayTopLeftRoom.second = (replayTopLeftRoom.second + 1) % fx.map.h;
        }
        break; case KEY_PGUP: {
            if (replaying && (replay_rate *= 2) > 128)
                replay_rate = 128;
        }
        break; case KEY_PGDN: {
            if (replaying && (replay_rate /= 2) < 1 / 32.)
                replay_rate = 1 / 32.;
        }
        break; case KEY_END: {
            if (!replaying && withControl) {
                want_change_teams = !want_change_teams;
                BinaryBuffer<16> msg;
                msg.U8(want_change_teams ? data_change_team_on : data_change_team_off);
                client->send_message(msg);
            }
            else if (menu.options.controls.arrowKeysInTextInput() && !talkbuffer.empty())
                talkbuffer_cursor = talkbuffer.size();
        }
        break; case KEY_TAB:    // This also prevents annoying Control+Tab character from chat input.
            if (replaying) {
                const int dir = key[KEY_LSHIFT] || key[KEY_RSHIFT] ? -1 : 1;
                me += dir;
                if (me < -1)
                    me = maxplayers - 1;
                for (; me >= 0 && me < maxplayers; me += dir)
                    if (fx.player[me].used)
                        break;
                if (me >= maxplayers)
                    me = -1;
            }
        break; default:
            // Add character to text
            if (!replaying && talkbuffer.length() < max_chat_message_length && !is_nonprintable_char(ch) &&
                    (!menu.options.controls.keypadMoving() || (!is_keypad(sc) && !alt_sequence))) {
                talkbuffer.insert(talkbuffer_cursor, 1, static_cast<char>(ch));
                talkbuffer_cursor++;
            }
    }
}

void GuiClient::loop(volatile bool* quitFlag, bool firstTimeSplash) throw () {
    nAssert(quitFlag);
    quitCommand = false;

    menusel = menu_none;
    openMenus.clear();
    if (firstTimeSplash) {
        menu.options.bugReports.policy.set(ABR_disabled);
        showMenu(menu.options.bugReports);
    }
    else
        showMenu(menu);
    gameshow = false;
    replaying = false;
    spectating = false;

    g_timeCounter.refresh();
    double nextSend = get_time();
    double nextClientFrame = get_time();

    if (!extConfig.autoPlay.empty()) {
        Network::ResolveError err;
        if (!serverIP.tryResolve(extConfig.autoPlay, &err))
            log.error(err.str());
        else {
            if (serverIP.getPort() == 0)
                serverIP.setPort(DEFAULT_UDP_PORT);
            connect_command(true);
        }
    }
    else if (!extConfig.autoSpectate.empty())
        start_spectating(extConfig.autoSpectate);
    else if (!extConfig.autoReplay.empty())
        start_replay(extConfig.autoReplay);

    bool prevFire = false, prevDropFlag = false;
    while (!quitCommand && !*quitFlag) {
        // (1) loop doing input/sleep before next send or draw time
        for (;;) {
            if (keyboard_needs_poll())
                poll_keyboard();    // ignore return value
            if (mouse_needs_poll())
                poll_mouse();

            const bool controlPressed = key[KEY_LCONTROL] || key[KEY_RCONTROL];

            //quit key Control-F12
            if (controlPressed && key[KEY_F12]) {
                quitCommand = true;
                break;
            }

            static bool alt_sequence = false;

            if (menu.options.controls.keypadMoving()) {
                // Check Alt+keypad sequences
                if (key_shifts & KB_INALTSEQ_FLAG)
                    alt_sequence = true;
            }

            // handle waiting keypresses
            while (keypressed()) {
                const int ch = readkey();
                handleKeypress(ch >> 8, ch & 0xFF, controlPressed, alt_sequence);
            }

            if (!(key_shifts & KB_INALTSEQ_FLAG))
                alt_sequence = false;

            // handle current keypresses (only used in game)
            if (openMenus.empty() && !replaying) {
                bool sendnow = false;

                // control == fire
                const bool fire = firePressed();
                if (fire != prevFire) {
                    prevFire = fire;

                    BinaryBuffer<16> msg;
                    msg.U8(fire ? data_fire_on : data_fire_off);
                    client->send_message(msg);

                    //send early keys packet
                    sendnow = true;
                }

                if (menusel == menu_none) { // page down is reserved for stats screens
                    const bool dropFlag = key[KEY_PGDN];
                    if (dropFlag != prevDropFlag) {
                        prevDropFlag = dropFlag;
                        BinaryBuffer<16> msg;
                        msg.U8(dropFlag ? data_drop_flag : data_stop_drop_flag);
                        client->send_message(msg);
                    }
                }

                send_frame(false, sendnow);
            }

            while (!replaying && (clientReadiesWaiting > 1 ||
                   (clientReadiesWaiting && (!menu.options.game.stayDead() ||
                                             openMenus.empty() && menusel == menu_none)))) {
                send_client_ready();
                --clientReadiesWaiting;
            }

            {
                Lock ml(frameMutex);
                handlePendingThreadMessages();

                if (GlobalDisplaySwitchHook::readAndClear() && menu.options.screenMode.flipping())
                    graphics.videoMemoryCorrupted();
            }

            g_timeCounter.refresh();
            if (get_time() >= nextSend || get_time() >= nextClientFrame)
                break;

            platSleep(2);
        }

        if (get_time() >= nextSend) {
            nextSend += .1; // match 10 Hz frame frequency of server
            nextSend += netsendAdjustment;
            netsendAdjustment = 0;  // losing a value due to concurrency is vaguely possible but affordable
            if (get_time() > nextSend)   // don't accumulate lag
                nextSend = get_time();
            if (!replaying)
                send_frame(true, true);
        }

        if (get_time() < nextClientFrame)
            continue;

        // give other threads a chance (otherwise we're trying to run all the time if the FPS limit is not lower than what the machine can do)
        sched_yield();

        int fpsLimit = menu.options.graphics.fpsLimit();
        if (!gameshow && fpsLimit > 30)
            fpsLimit = 30;
        nextClientFrame += 1. / fpsLimit;
        if (get_time() > nextClientFrame)    // don't accumulate lag
            nextClientFrame = get_time();

        if (replaying && !replay_stopped) {
            if (spectating)
                continue_spectating();
            const double time = get_time();
            if (!replay_paused)
                replaySubFrame += (time - replayTime) * 10. * replay_rate;
            replayTime = time;
            for (; replaySubFrame >= 1.; replaySubFrame -= 1.) {
                process_replay_controls();
                if (replay_version >= 1 && !replay_buffer_end_reached) { // Version 0 has the needed physics data in every frame.
                    ClientPhysicsCallbacks cb(*this);
                    fx.applyPhysics(cb, PLAYER_RADIUS, 1.);
                }
                continue_replay();
            }
        }

        // the rest is drawing

        if (gameshow && (replaying || me >= 0)) {
            Lock ml(frameMutex);

            ClientPhysicsCallbacks cb(*this);
            if (replaying)
                fd.extrapolate(fx, cb, -1, controlHistory, 0, 0, replay_buffer_end_reached ? 0.0 : replaySubFrame); // Some extrapolation is needed even if replay buffer is at the end so that player properties and such are copied from fx to fd.
            else if (menu.options.game.lagPrediction()) {
                const double lagWanted = 2. * (1. - menu.options.game.lagPredictionAmount() / 10.); // lagPredictionAmount() is in range [0, 10]
                double timeDelta = max<double>(0., averageLag - lagWanted) + (get_time() - frameReceiveTime) * 10.;
                uint8_t firstFrame, lastFrame;
                if (clFrameSent == clFrameWorld)
                    firstFrame = lastFrame = clFrameWorld;
                else {
                    firstFrame = lastFrame = clFrameWorld + 1;
                    while (lastFrame != clFrameSent && timeDelta > 1.) {
                        ++lastFrame;
                        timeDelta -= 1.;
                    }
                }
                if (timeDelta > 3.)
                    timeDelta = 3.;
                if (!fx.player[me].dead) {
                    if (fx.physics.allowFreeTurning && menu.options.controls.aimMode() != Menu_controls::AM_8way)
                        fx.player[me].gundir = gunDir;
                    else
                        for (uint8_t controlFrame = lastFrame; controlFrame != clFrameWorld; --controlFrame) {
                            if (controlHistory[controlFrame].isStrafe())
                                continue;
                            const int dir = controlHistory[controlFrame].getDirection();
                            if (dir != -1) {
                                fx.player[me].gundir.from8way(dir);
                                break;
                            }
                        }
                }
                fd.extrapolate(fx, cb, me, controlHistory, firstFrame, lastFrame, timeDelta);
            }
            else {
                if (fx.physics.allowFreeTurning && !fx.player[me].dead && menu.options.controls.aimMode() != Menu_controls::AM_8way)
                    fx.player[me].gundir = gunDir;
                double timeDelta = (get_time() - frameReceiveTime) * 10.;
                fd.extrapolate(fx, cb, me, controlHistory, clFrameWorld, clFrameWorld, timeDelta);
            }

            if (mapChanged) {
                mapChanged = false;
                if (current_map < int(maps.size()))
                    maps[current_map].update(fx.map);
                graphics.mapChanged();
                if (replaying)
                    visible_rooms = menu.options.graphics.visibleRoomsReplay();
                else
                    visible_rooms = menu.options.graphics.visibleRoomsPlay();
                if (visible_rooms > fx.map.w && visible_rooms > fx.map.h)
                    visible_rooms = max(fx.map.w, fx.map.h);
                if (replaying) {
                    const int team = rand() % 2;
                    replayTopLeftRoom = pair<int, int>();
                    if (!fx.map.tinfo[team].flags.empty()) {
                        const WorldCoords& pos = fx.map.tinfo[team].flags[rand() % fx.map.tinfo[team].flags.size()];
                        replayTopLeftRoom = pair<int, int>(max(0, pos.room.x + 1 - static_cast<int>(visible_rooms)),
                                                           max(0, pos.room.y + 1 - static_cast<int>(visible_rooms)));
                    }
                    else if (!fx.map.wild_flags.empty()) {
                        const WorldCoords& pos = fx.map.wild_flags[rand() % fx.map.wild_flags.size()];
                        replayTopLeftRoom = pair<int, int>(max(0, pos.room.x + 1 - static_cast<int>(visible_rooms)),
                                                           max(0, pos.room.y + 1 - static_cast<int>(visible_rooms)));
                    }
                }

                mapWrapsX = mapWrapsY = false;
                static const int testSkip = PLAYER_RADIUS / 2;
                for (int side = 0; side < 2; ++side) {
                    for (int rx = 0; rx < fx.map.w && !mapWrapsY; ++rx) {
                        const int ry = side ? fx.map.h - 1 : 0;
                        const int ly = side ? plh : 0;
                        for (int lx = 0; lx < plw; lx += testSkip)
                            if (!fx.map.fall_on_wall(rx, ry, lx, ly, PLAYER_RADIUS)) {
                                mapWrapsY = true;
                                break;
                            }
                    }
                    for (int ry = 0; ry < fx.map.h && !mapWrapsX; ++ry) {
                        const int rx = side ? fx.map.w - 1 : 0;
                        const int lx = side ? plw : 0;
                        for (int ly = 0; ly < plh; ly += testSkip)
                            if (!fx.map.fall_on_wall(rx, ry, lx, ly, PLAYER_RADIUS)) {
                                mapWrapsX = true;
                                break;
                            }
                    }
                }
            }

            if (graphics.needRedrawMap())
                graphics.update_minimap_background(fx.map);

            refreshGunDir();

            // update carried flags' positions
            for (int t = 0; t < 3; t++) {
                const vector<Flag>& flags = t == 2 ? fx.wild_flags : fx.teams[t].flags();
                int f = 0;
                for (vector<Flag>::const_iterator fi = flags.begin(); fi != flags.end(); ++fi, ++f) {
                    if (!fi->carried())
                        continue;
                    const ClientPlayer& pl = fx.player[fi->carrier()];
                    const WorldCoords& pos = playerPos(fi->carrier());
                    nAssert(pos.room.x >= 0 && pos.room.y >= 0);
                    if (!pl.used || pos.room.x >= fx.map.w || pos.room.y >= fx.map.h)
                        continue;
                    if (t < 2)
                        fx.teams[t].move_flag(f, pos);
                    else
                        fx.wild_flags[f].move(pos);
                }
            }

            graphics.startDraw();
            draw_game_frame();

            #ifdef ROOM_CHANGE_BENCHMARK
            if (benchmarkRuns >= 500)
                quitCommand = true;
            #endif
        } else {
            graphics.startDraw();
            graphics.draw_background(false);
            if (!gameshow && openMenus.empty())
                showMenu(menu);
        }

        const int errors = externalErrorLog.size();
        if (errors) {
            for (int count = 0; count < errors; ++count)
                m_errors.wrapLine(externalErrorLog.pop());
            if (openMenus.safeTop() != &m_errors.menu)
                showMenu(m_errors);
        }

        if (menusel != menu_none || !openMenus.empty()) {
            Lock ml(frameMutex);   // some menus need access
            draw_game_menu();
        }

        graphics.endDraw();
        graphics.draw_screen(!menu.options.screenMode.alternativeFlipping());
        if (screenshot) {
            save_screenshot();
            screenshot = false;
        }
    }

    //client exit cleanup: done at stop wich needs to be called after loop
}

void GuiClient::start_replay(const std::string& filename) throw () {
    stop_replay();
    prepareForConnect(); // Handles the disconnection part in this case.
    openMenus.clear();
    replay.clear();
    replay.open(filename.c_str(), ios::binary);
    if (!replay)
        log.error(_("Could not open replay file $1.", filename.c_str()));
    else if (!start_replay(replay))
        replay.close();
}

bool GuiClient::start_replay(istream& replay) throw () {
    try {
        BinaryStreamReader read(replay);

        const string identification = read.constLengthStr(REPLAY_IDENTIFICATION.length());
        log("Replay identification: %s", identification.c_str());
        if (identification != REPLAY_IDENTIFICATION) {
            log.error(_("This is not an Outgun replay."));
            return false;
        }

        replay_version = read.U32();
        log("Replay version: %u", replay_version);
        if (replay_version > REPLAY_VERSION) {   // incompatible replay
            log.error(_("This is a newer replay version ($1).", itoa(replay_version)));
            return false;
        }

        replay_length = read.U32();
        replay_first_frame_loaded = false;

        hostname = read.str();
        string caption;
        if (spectating)
            caption = _("Spectating on $1", hostname.substr(0, 32));
        else
            caption = _("Replay on $1", hostname.substr(0, 32));
        extConfig.statusOutput(caption);

        setMaxPlayers(read.U32());
        read.str(); // ignore map name
    } catch (BinaryReader::ReadError) {
        log.error(_("Format error in replay file."));
        return false;
    }

    replaying = true;
    replay_rate = 1;
    replay_paused = false;
    replay_stopped = false;
    replay_buffer_end_reached = false;
    replayTime = get_time();
    replaySubFrame = 0;
    replayTopLeftRoom = pair<int, int>();

    show_all_messages = false;
    stats_autoshowing = false;

    m_serverInfo.clear();
    m_serverInfo.addLine("");   // can't draw a totally empty menu; this will be overwritten with config information

    lastpackettime = get_time() + 4.0;
    averageLag = 0;
    clFrameSent = clFrameWorld = 0;
    frameReceiveTime = 0;

    gameshow = false;

    fd.frame = -1;
    fd.skipped = true;
    fx.frame = -1;
    fx.skipped = true;
    me = -1;

    talkbuffer.clear();
    talkbuffer_cursor = 0;
    chatbuffer.clear();

    for (int i = 0; i < 2; i++) {
        fx.teams[i].clear_stats();
        fx.teams[i].remove_flags();
    }
    remove_flags = 0;

    for (int i = 0; i < MAX_PLAYERS; i++)
        fx.player[i].clear(false, i, "", i / TSIZE);
    players_sb.clear();

    fx.reset();
    fd.reset();

    framecount = 0;
    frameCountStartTime = get_time();
    FPS = 0;

    map_time_limit = false;
    map_start_time = 0;
    map_end_time = 0;
    extra_time_running = false;

    map_ready = false;
    clientReadiesWaiting = 0;

    gameover_plaque = NEXTMAP_NONE;

    graphics.clear_fx();
    gameshow = true;

    return true;
}

void GuiClient::process_replay_controls() throw () {
    continue_replay(true);
}

void GuiClient::continue_replay(bool controls) throw () {
    if (spectating)
        continue_replay(spectate_buffer, controls);
    else
        continue_replay(replay, controls);
}

void GuiClient::continue_replay(istream& in, bool controls) throw () {
    const istream::pos_type pos = in.tellg();
    BinaryStreamReader read(in);
    try {
        if (controls) {
            read_replay_controls(read.block(read.U32()));
            in.seekg(pos);
        }
        else
            process_incoming_data(read.block(read.U32()));
        replay_buffer_end_reached = false;
    } catch (BinaryReader::ReadOutside) {
        replay_buffer_end_reached = true;
        in.clear();
        in.seekg(pos);
        if (replay_length > 0)
            replay_stopped = true;
        else if (replay_rate > 1)
            replay_rate = 1;
    } catch (ServerDataError) {
        log.error(_("Format error in replay file."));
        stop_replay();
    }
}

void GuiClient::stop_replay() throw () {
    if (!replaying)
        return;

    replay.close();
    spectate_socket.closeIfOpen();

    if (spectating)
        spectate_buffer.str("");

    replaying = false;
    spectating = false;
    gameshow = false;

    extConfig.statusOutput(_("Outgun client"));

    menusel = menu_none;
}

void GuiClient::start_spectating(const string& host) throw () {
    Network::Address addr;
    Network::ResolveError err;
    if (!addr.tryResolve(host, &err))
        log.error(err.str());
    else if (addr.getPort() == 0)
        log.error(_("Port is missing from $1.", host));
    else
        start_spectating(addr);
}

void GuiClient::start_spectating(const Network::Address& address) throw () {
    stop_replay();
    prepareForConnect();

    log("Start spectating.");
    serverIP = address;

    try {
        spectate_socket.open(Network::NonBlocking, 0);
        spectate_socket.connect(serverIP);

        BinaryBuffer<512> request;
        request.str(GAME_STRING);
        request.U32dyn8(RELAY_PROTOCOL);
        request.U32dyn8(RELAY_PROTOCOL_EXTENSIONS_VERSION);
        request.str("SPECTATOR");
        request.U32dyn8(REPLAY_VERSION);
        request.str(string()); // username
        request.str(string()); // password
        request.U32dyn8(0); // no extension data

        spectate_socket.persistentWrite(request, 500, 5);
        log("Init data sent to the relay (%u bytes).", request.size());
    } catch (const Network::Error& e) {
        spectate_socket.closeIfOpen();
        log.error(_("Connecting to relay: $1", e.str()));
        return;
    }

    spectating = true;
    replaying = true;
    replay_stopped = false;
    replay_buffer_end_reached = false;
    replay_rate = 1;
    replay_length = 0;
    spectate_data_received = false;

    openMenus.clear();
    m_connectProgress.clear();
    m_connectProgress.wrapLine(_("Waiting for the game to start."));
    showMenu(m_connectProgress);
}

void GuiClient::continue_spectating() throw () {
    if (!spectate_socket.isOpen()) {
        log.error(_("Connection to the server closed."));
        openMenus.close(&m_connectProgress.menu);
        stop_replay();
        return;
    }

    const int max_buffer_size = 20000;
    char buffer[max_buffer_size];
    int result;
    try {
        result = spectate_socket.read(buffer, max_buffer_size);
    } catch (const Network::Error& e) {
        log.error(_("Connection to the server closed: $1", e.str()));
        openMenus.close(&m_connectProgress.menu);
        stop_replay();
        return;
    }
    if (result == 0)
        return;

    if (!spectate_data_received) {
        log("First data from relay, %d bytes.", result);
        spectate_data_received = true;
        spectate_buffer.clear();
        spectate_buffer.write(buffer + 1, result - 1); // the first byte is the relay's protocol extensions level, about which we don't have to care so far
        if (!start_replay(spectate_buffer)) {
            log.error(_("Could not start spectating."));
            stop_replay();
        }
        // Keep the waiting dialog still on as the relay may delay the actual game frames.
    }
    else {
        openMenus.close(&m_connectProgress.menu);
        spectate_buffer.write(buffer, result);
    }
}

void GuiClient::stop() throw () {
    log("Client exiting: stop() called");
    stop_replay();
    ClientBase::stop();
    rankingPassword.stop();

    saveSettings();

    string fileName = whereuserdir + "config" + directory_separator + "favorites.txt";
    ofstream fav(fileName.c_str());
    if (fav) {
        for (vector<ServerListEntry>::const_iterator spy = gamespy.begin(); spy != gamespy.end(); ++spy)
            fav << spy->addressString() << '\n';
        fav.close();
    }

    saveMapInfoCache();

    //save client's password
    log("Saving password file...");
    fileName = whereuserdir + "config" + directory_separator + "password.bin";
    FILE* psf = fopen(fileName.c_str(), "wb");
    if (psf) {
        const string& password = menu.options.player.password();
        for (int c = 0; c < PASSBUFFER; c++) {
            if (c < (int)password.length())
                fputc(static_cast<unsigned char>(255 - password[c]), psf);
            else
                fputc(255, psf);    // 255 = 0 toggled (important)
        }
        fclose(psf);
    }
    else
        log.error(_("Can't open $1 for writing.", fileName));

    {
        Lock ml(downloadMutex);
        downloads.clear();
    }

    if (menu.options.game.messageLogging() != Menu_game::ML_none)
        closeMessageLog();

    if (listenServer.running())
        listenServer.stop();

    log("Client stop() completed");
}

void GuiClient::rocketHitWallCallback(int rid, bool power, const WorldCoords& pos) throw () {
    ClientBase::rocketHitWallCallback(rid, power, pos);

    const double time = fx.frame / 10;
    const bool sound = on_screen_exact(pos.room.x, pos.room.y, pos.x, pos.y);
    if (power) {
        graphics.create_powerwallexplo(pos, fx.rock[rid].team, time);
        if (sound)
            play_sound(SAMPLE_POWERWALLHIT);
    }
    else {
        graphics.create_wallexplo(pos, fx.rock[rid].team, time);
        if (sound)
            play_sound(SAMPLE_WALLHIT);
    }
}

void GuiClient::playerHitWallCallback(int pid) throw () {
    // play bounce sample if minimum time elapsed
    const double currTime = fx.frame / 10.;
    if (currTime > fx.player[pid].wall_sound_time && (!replaying || player_on_screen(pid))) {
        fx.player[pid].wall_sound_time = currTime + 0.2;
        play_sound(SAMPLE_WALLBOUNCE);
    }
}

void GuiClient::playerHitPlayerCallback(int pid1, int pid2) throw () {
    // play bounce sample if minimum time elapsed
    const double currTime = fx.frame / 10.;
    if ((currTime > fx.player[pid1].player_sound_time || currTime > fx.player[pid2].player_sound_time) &&
            (!replaying || player_on_screen(pid1) || player_on_screen(pid2))) {
        fx.player[pid1].player_sound_time = fx.player[pid2].player_sound_time = currTime + 0.2;
        play_sound(SAMPLE_PLAYERBOUNCE);
    }
}

void GuiClient::play_sound(int sample) throw () {
    int freq = 1000;
    if (replaying) {
        if (replay_rate > 10)
            return;
        freq = int(freq * replay_rate);
    }
    client_sounds.play(sample, freq);
}

const WorldCoords& GuiClient::playerPos(int pid) const throw () {
    const ClientWorld& world = fx.player[pid].onscreen || replaying ? fd : fx;
    return world.player[pid].pos;
}

bool GuiClient::repeatMapX() const throw () {
    if (!menu.options.graphics.repeatMap())
        return false;
    if (menu.options.graphics.viewOverMapBorder() == Menu_graphics::VOB_Always)
        return true;
    return mapWrapsX;
}

bool GuiClient::repeatMapY() const throw () {
    if (!menu.options.graphics.repeatMap())
        return false;
    if (menu.options.graphics.viewOverMapBorder() == Menu_graphics::VOB_Always)
        return true;
    return mapWrapsY;
}

static double getViewStartCoord(int myRoom, double myLocal, int roomSize, int mapSize, bool mapWraps, double visibleRooms, Menu_graphics::ViewOverBorderMode view, bool scroll) throw () {
    double coordBase = 0;
    if (visibleRooms >= mapSize) {
        coordBase = - (visibleRooms - mapSize) * .5; // as this is added to the view start coordinates, effectively that position then starts the map sized region in the middle of the screen
        visibleRooms = mapSize; // since we are now operating on the map-sized region
        if (view != Menu_graphics::VOB_Always && (view != Menu_graphics::VOB_MapWraps || !mapWraps))
            return coordBase;
    }
    const double center = myRoom + (scroll ? myLocal / roomSize : .5);
    const double halfw = visibleRooms * .5;
    if (view == Menu_graphics::VOB_Never || view != Menu_graphics::VOB_Always && !mapWraps) {
        if (center - halfw < 0)
            return coordBase;
        if (center + halfw >= mapSize)
            return coordBase + mapSize - visibleRooms;
    }
    return coordBase + center - halfw;
}

static pair<int, double> splitCompositeCoord(double comp, int roomSize, int mapSize) throw () {
    const double local = positiveFmod(comp, 1.);
    return pair<int, double>(positiveModulo(iround(comp - local), mapSize), local * roomSize); // iround because of inaccuracies; it should be very close to integral really
}

WorldCoords GuiClient::viewTopLeft() const throw () {
    if (!map_ready)
        return WorldCoords(0, 0, 0, 0);
    else if (me < 0)
        return WorldCoords(replayTopLeftRoom.first, replayTopLeftRoom.second, 0, 0);
    else {
        const Menu_graphics::ViewOverBorderMode view = menu.options.graphics.viewOverMapBorder();
        const bool scroll = menu.options.graphics.scroll();
        const double xVis = graphics.get_visible_rooms_x(), yVis = graphics.get_visible_rooms_y();
        const double x = getViewStartCoord(fd.player[me].room().x, fd.player[me].pos.x, plw, fx.map.w, mapWrapsX, repeatMapX() ? xVis : min<double>(fx.map.w, xVis), view, scroll);
        const double y = getViewStartCoord(fd.player[me].room().y, fd.player[me].pos.y, plh, fx.map.h, mapWrapsY, repeatMapY() ? yVis : min<double>(fx.map.h, yVis), view, scroll);
        const pair<int, double> xp = splitCompositeCoord(x, plw, fx.map.w), yp = splitCompositeCoord(y, plh, fx.map.h);
        return WorldCoords(xp.first, yp.first, xp.second, yp.second);
    }
}

pair<int, int> GuiClient::topLeftRoom() const throw () {
    const WorldCoords c = viewTopLeft();
    return pair<int, int>(c.room.x, c.room.y);
}

//draw the whole game screen
void GuiClient::draw_game_frame() throw () {    // call with frameMutex locked
    // hide stuff if frame skipped
    const bool hide_game = !map_ready || gameover_plaque != NEXTMAP_NONE || fx.skipped || !replaying && me < 0;

    const double time = fd.frame >= 0 ? fd.frame / 10 : 0;

    if (hide_game) {
        graphics.draw_background(map_ready);

        if (gameover_plaque == NEXTMAP_CAPTURE_LIMIT || gameover_plaque == NEXTMAP_VOTE_EXIT) {
            if (red_final_score > blue_final_score)
                graphics.draw_scores(_("RED TEAM WINS"), 0, red_final_score, blue_final_score);
            else if (blue_final_score > red_final_score)
                graphics.draw_scores(_("BLUE TEAM WINS"), 1, blue_final_score, red_final_score);
            else
                graphics.draw_scores(_("GAME TIED"), -1, blue_final_score, red_final_score);
        }

        if (map_ready)
            graphics.draw_waiting_map_message(_("Waiting game start - next map is"), fx.map.title);
        else {
            Lock ml(downloadMutex);
            if (!downloads.empty() && downloads.front().isActive()) {
                const string text = _("Loading map: $1 bytes", itoa(downloads.front().progress()));
                graphics.draw_loading_map_message(text);
            }
        }
    }
    else {
        #ifdef ROOM_CHANGE_BENCHMARK
        graphics.videoMemoryCorrupted(); // evil trick to invalidate room cache
        ++benchmarkRuns;
        #endif
        const VisibilityMap roomVis = calculateVisibilities();
        graphics.setRoomLayout(fx.map, visible_rooms, repeatMapX(), repeatMapY());
        graphics.draw_background(fx.map,
                                 roomVis,
                                 viewTopLeft(),
                                 menu.options.graphics.contTextures(),
                                 menu.options.graphics.mapInfoMode());
        draw_playfield();
        draw_map(roomVis);
    }

    graphics.draw_scoreboard(players_sb, fx.teams, maxplayers, key[KEY_TAB], menu.options.game.underlineMasterAuth(), menu.options.game.underlineServerAuth());

    graphics.draw_fps(FPS);

    // Time left if time limit is on and the game is running.
    if (map_time_limit && gameover_plaque == NEXTMAP_NONE && players_sb.size() > 1)
        if (map_end_time > time)
            graphics.draw_map_time(map_end_time - static_cast<int>(time), extra_time_running);
        else
            graphics.draw_map_time(0);

    if (replaying && replay_first_frame_loaded)
        graphics.draw_replay_info(replay_paused ? 0 : replay_rate, static_cast<unsigned>(fx.frame - replay_start_frame), replay_length, replay_stopped);
    else if (me >= 0) {
        // player's power-ups
        double val = fx.player[me].item_power_time - time;
        if (val > 0)
            graphics.draw_player_power(val);
        val = fx.player[me].item_turbo_time - time;
        if (val > 0)
            graphics.draw_player_turbo(val);
        val = fx.player[me].item_shadow_time - time;
        if (val > 0)
            graphics.draw_player_shadow(val);
        graphics.draw_player_weapon(fx.player[me].weapon);

        if (want_change_teams)
            graphics.draw_change_team_message(time);
        if (want_map_exit)
            graphics.draw_change_map_message(time, want_map_exit_delayed);
        graphics.draw_player_health(fx.player[me].health);
        graphics.draw_player_energy(fx.player[me].energy);
    }

    // the HUD: message output
    const int chat_visible = show_all_messages ? graphics.chat_max_lines() : graphics.chat_lines();
    int start = static_cast<int>(chatbuffer.size()) - static_cast<int>(chat_visible);
    if (start < 0)
        start = 0;
    list<Message>::const_iterator msg = chatbuffer.begin();
    for (int i = 0; i < start; ++i, ++msg) { }
    if (!show_all_messages) // drop old messages
        for (; msg != chatbuffer.end(); ++msg)
            if (time < msg->time() + 80)
                break;
    graphics.print_chat_messages(msg, chatbuffer.end(), talkbuffer, talkbuffer_cursor);

    //"server not responding... connection may have dropped" plaque
    if (get_time() > lastpackettime + 1.0 && !replaying)
        m_notResponding.menu.draw(graphics.drawbuffer(), graphics.colours());

    // debug panel
    if (key[KEY_F9]) {
        const int bpsin = client->get_socket_stat(Network::Socket::Stat_AvgBytesReceived) + spectate_socket.getStat(Network::Socket::Stat_AvgBytesReceived);
        const int bpsout = client->get_socket_stat(Network::Socket::Stat_AvgBytesSent) + spectate_socket.getStat(Network::Socket::Stat_AvgBytesSent);

        vector<vector<pair<int, int> > > sticks;
        vector<int> buttons;
        if (menu.options.controls.joystick()) {
            const JOYSTICK_INFO& joystick = joy[0];
            for (int i = 0; i < joystick.num_sticks; i++) {
                vector<pair<int, int> > axes;
                for (int j = 0; j < joystick.stick[i].num_axis; j++)
                    axes.push_back(pair<int, int>(joystick.stick[i].axis[j].d1, joystick.stick[i].axis[j].d2));
                sticks.push_back(axes);
            }
            for (int i = 0; i < joystick.num_buttons; i++)
                buttons.push_back(joystick.button[i].b);
        }

        graphics.debug_panel(fx.player, me, bpsin, bpsout, sticks, buttons);
    }

    // another frame, calculate FPS
    framecount++;
    const double baixo = get_time() - frameCountStartTime;
    if (baixo > 1.0) {
        FPS = ((double)framecount) / baixo;
        frameCountStartTime = get_time();
        framecount = 0;
    }
}

bool GuiClient::on_screen(int x, int y) const throw () {
    const WorldCoords topLeft = viewTopLeft();
    return positiveModulo(x - topLeft.room.x, fx.map.w) < graphics.get_visible_rooms_x() + topLeft.x / plw &&
           positiveModulo(y - topLeft.room.y, fx.map.h) < graphics.get_visible_rooms_y() + topLeft.y / plh;
}

bool GuiClient::on_screen(int rx, int ry, double lx, double ly, double fudge) const throw () {
    const int mapw = fx.map.w * plw,
              maph = fx.map.h * plh;
    const double x = rx * plw + lx,
                 y = ry * plh + ly;
    const WorldCoords topLeft = viewTopLeft();
    const double tlx = topLeft.room.x * plw + topLeft.x - fudge,
                 tly = topLeft.room.y * plh + topLeft.y - fudge;
    const double relX = positiveFmod(x - tlx, mapw),
                 relY = positiveFmod(y - tly, maph);
    return relX < graphics.get_visible_rooms_x() * plw + 2 * fudge && relY < graphics.get_visible_rooms_y() * plh + 2 * fudge;
}

bool GuiClient::on_screen_exact(int x, int y) const throw () {
    if (replaying)
        return on_screen(x, y);
    else
        return me >= 0 && RoomCoords(x, y) == fx.player[me].room();
}

bool GuiClient::on_screen_exact(int rx, int ry, double lx, double ly, double fudge) const throw () {
    if (!on_screen(rx, ry, lx, ly, fudge))
        return false;
    return replaying || me >= 0 && RoomCoords(rx, ry) == fx.player[me].room();
}

bool GuiClient::player_on_screen(int pid) const throw () {
    if (!fx.player[pid].used)
        return false;
    else if (fx.player[pid].posUpdated >= fx.frame - 20 || fx.player[pid].dead && fx.player[pid].posUpdated >= 0)
        return on_screen(fx.player[pid].room().x, fx.player[pid].room().y, fx.player[pid].pos.x, fx.player[pid].pos.y, Graphics::extended_player_max_size_in_world / 2);
    else
        return false;
}

bool GuiClient::player_on_screen_exact(int pid) const throw () {
    if (!fx.player[pid].used)
        return false;
    else if (replaying)
        return on_screen(fx.player[pid].room().x, fx.player[pid].room().y, fx.player[pid].pos.x, fx.player[pid].pos.y, PLAYER_RADIUS);
    else
        return fx.player[pid].onscreen;
}

void GuiClient::draw_map(const VisibilityMap& roomVis) throw () {
    const double time = fd.frame / 10;

    // paint fog of war in all invisible rooms
    for (int ry = 0; ry < fx.map.h; ry++)
        for (int rx = 0; rx < fx.map.w; rx++)
            graphics.draw_minimap_room(fx.map, rx, ry, roomVis[rx][ry] / 255.);

    if (replaying || (graphics.get_visible_rooms_x() > 1 || graphics.get_visible_rooms_y() > 1 || menu.options.graphics.scroll()) && menu.options.graphics.boxRoomsWhenPlaying())
        graphics.highlight_minimap_rooms();

    if (!replaying && menu.options.graphics.oldFlagPositions()) {
        set_trans_mode(disappearedFlagAlpha);
        for (ConstDisappearedFlagIterator fi(this); fi; ++fi)
            if (fx.player[fi->carrier()].alpha != 255)
                graphics.draw_mini_flag(fi.team(), *fi, fx.map, false, true);
        solid_mode();
    }

    // draw all teammates and enemies on screens where there are teammates
    if ((me >= 0 || replaying) && fx.frame >= 0)
        for (int i = 0; i < maxplayers; i++) {
            const ClientPlayer& pl = fx.player[i];
            const WorldCoords& pos = playerPos(i);
            if (!pl.used || pos.room.x >= fx.map.w || pos.room.y >= fx.map.h || pl.alpha <= 0)
                continue;
            set_trans_mode(pl.alpha);
            for (ConstFlagIterator fi(fx); fi; ++fi)
                if (fi->carrier() == i)
                    graphics.draw_mini_flag(fi.team(), *fi, fx.map);
            const ClientPlayer& exactPl = replaying || fx.player[i].onscreen ? fd.player[i] : pl;
            if (i != me)
                graphics.draw_minimap_player(fx.map, exactPl);
            else // myself: draw differently
                graphics.draw_minimap_me(fx.map, exactPl, time);
            solid_mode();
        }

    // draw the miniflags (in the base and on the ground but not carried)
    for (ConstFlagIterator fi(fx); fi; ++fi)
        if (!fi->carried()) {
            const bool flash = menu.options.graphics.highlightReturnedFlag() &&
                               time < fi->return_time() + 2 && static_cast<int>(fmod(time * 15, 3)) == 0;
            graphics.draw_mini_flag(fi.team(), *fi, fx.map, flash);
        }
}

void GuiClient::draw_playfield() throw () {
    const double time = fd.frame / 10;
    const bool live = !replaying || !replay_paused && !replay_stopped;

    graphics.startPlayfieldDraw();

    // draw dead players
    for (int i = 0; i < maxplayers; i++)
        if (fx.player[i].dead && player_on_screen(i)) {
            const double respawn_delay = i == me ? max(next_respawn_time - get_time(), 0.) : 0.;
            if (fx.player[i].stats().frags() % 100 == 0 && fx.player[i].stats().frags() >= 100)
                graphics.draw_virou_sorvete(playerPos(i), respawn_delay);
            else
                graphics.draw_player_dead(fx.player[i], respawn_delay);
        }

    // draw disappeared flags
    if (!replaying && menu.options.graphics.oldFlagPositions())
        for (ConstDisappearedFlagIterator fi(this); fi; ++fi) {
            const WorldCoords& pos = fi->position();
            if (on_screen(pos.room.x, pos.room.y, pos.x, pos.y, Graphics::extended_flag_max_size_in_world / 2))
                graphics.draw_flag(fi.team(), pos, false, 100, false, (fx.frame - fx.player[fi->carrier()].posUpdated) * .1);
        }

    // draw powerups
    for (int i = 0; i < MAX_POWERUPS; i++)
        if (fx.item[i].real() && on_screen_exact(fx.item[i].room().x, fx.item[i].room().y, fx.item[i].pos.x, fx.item[i].pos.y, Graphics::extended_item_max_size_in_world / 2))
            graphics.draw_pup(fx.item[i], time, live);

    // draw turbo effects
    graphics.draw_turbofx(time);

    // draw dropped flags (use fx since flags don't move)
    for (ConstFlagIterator fi(fx); fi; ++fi) {
        if (fi->carried())
            continue;
        const WorldCoords& pos = fi->position();
        if (on_screen(pos.room.x, pos.room.y, pos.x, pos.y, Graphics::extended_flag_max_size_in_world / 2)) {
            const bool flash = menu.options.graphics.highlightReturnedFlag() &&
                               time < fi->return_time() + 2 && static_cast<int>(fmod(time * 15, 3)) == 0;
            const double return_delay = fi.team() != 2 ? fi->drop_time() + flag_return_delay / 10 - time : 0;
            graphics.draw_flag(fi.team(), pos, flash, 255, menu.options.graphics.emphasizeFlag(visible_rooms), return_delay);
        }
    }

    // draw rockets
    for (int i = 0; i < MAX_ROCKETS; i++) {
        const Rocket& r = fd.rock[i];
        if (fx.rock[i].owner != -1 && on_screen(fx.rock[i].room().x, fx.rock[i].room().y, r.pos.x, r.pos.y, Graphics::extended_rocket_max_size_in_world / 2)) {
            const int radius = r.power ? ROCKET_RADIUS : POWER_ROCKET_RADIUS;
            const WorldCoords shadowPos(r.room(), r.pos.x, r.pos.y + radius + 8);
            const bool shadow = shadowPos.y < plh && !fd.map.fall_on_wall(shadowPos, radius / 2);
            graphics.draw_rocket(r, shadow, time);
        }
    }

    // draw players
    for (int k = 0; k < maxplayers; k++) {
        const int i = (me / TSIZE == 0 ? k : maxplayers - k - 1);   // own team first

        if (!player_on_screen(i))
            continue;
        if (!replaying && !fx.player[i].onscreen && fx.player[i].room() == fx.player[me].room())
            continue; // don't draw players whose last known location is in this room but who aren't really here

        //HACK REMENDEX: predict item_shadow
        if (player_on_screen_exact(i) && fx.player[i].item_shadow()) {
            const int hspd = static_cast<int>((fd.frame - fx.frame) * 10.);
            fd.player[i].visibility = fx.player[i].visibility - hspd;
            const int limit = (fx.player[i].visibility >= 7) ? 7 : 0;   // this produces an error of at most one server frame if total invisibility is enabled
            if (fd.player[i].visibility < limit)
                fd.player[i].visibility = limit;
        }

        if (i != me && !fx.player[i].dead)
            draw_player(i, time, live);
    }
    // last draw me
    if (me != -1) {
        if (fx.player[me].dead)
            deadAfterHighlighted = true;
        else {
            draw_player(me, time, live);
            static double spawnTime = 0;
            if (deadAfterHighlighted) {
                deadAfterHighlighted = false;
                spawnTime = time;
            }
            static const double highlightTime = .5;
            if (menu.options.graphics.spawnHighlight() && time - spawnTime < highlightTime)
                graphics.draw_me_highlight(playerPos(me), 1. - (time - spawnTime) / highlightTime);
            if (fx.physics.allowFreeTurning && menu.options.controls.aimMode() != Menu_controls::AM_8way && !replaying) {
                int aimDist;
                if (menu.options.controls.aimMode() == Menu_controls::AM_MousePos) {
                    const int mx = mouse_x - SCREEN_W / 2;
                    const int my = mouse_y - SCREEN_H / 2;
                    aimDist = static_cast<int>(sqrt(mx * mx + my * my));
                }
                else
                    aimDist = -1;
                graphics.draw_aim(fx.map[fx.player[me].room()], playerPos(me), gunDir, aimDist, me / TSIZE);
            }
        }
    }

    for (int i = 0; i < maxplayers; i++)
        if (fx.player[i].item_deathbringer && (player_on_screen_exact(i) || seePropertiesRemotely(i)))
            graphics.draw_deathbringer_carrier_circle(playerPos(i), calculatePlayerAlpha(i));

    graphics.draw_effects(time);

    fx.cleanOldDeathbringerExplosions();
    #ifdef DEATHBRINGER_SPEED_TEST
    DeathbringerExplosion dbe(0, WorldCoords(0, 0, 0, 0), 0);
    for (int i = 0; i < 50; ++i)
        graphics.draw_deathbringer(dbe, 14);
    #else
    for (list<DeathbringerExplosion>::const_iterator dbi = fx.deathbringerExplosions().begin(); dbi != fx.deathbringerExplosions().end(); ++dbi)
        graphics.draw_deathbringer(*dbi, fd.frame);
    #endif

    if (menu.options.graphics.showNeighborMarkers(replaying, visible_rooms)) {
        // neighbor markers: disappeared flags first
        set_trans_mode(disappearedFlagAlpha);
        for (ConstDisappearedFlagIterator fi(this); fi; ++fi)
            graphics.draw_neighbor_marker(true, fi->position(), fi.team(), true);
        solid_mode();
        // players
        for (int i = 0; i < maxplayers; ++i) {
            const ClientPlayer& pl = fx.player[i];
            if (!pl.used || pl.alpha <= 0)
                continue;
            set_trans_mode(pl.alpha);
            graphics.draw_neighbor_marker(false, playerPos(i), pl.team());
        }
        solid_mode();
        // flags
        for (ConstFlagIterator fi(fx); fi; ++fi) {
            if (fi->carried()) {
                if (fx.player[fi->carrier()].alpha <= 0)
                    continue;
                set_trans_mode(fx.player[fi->carrier()].alpha);
            }
            else
                solid_mode();
            graphics.draw_neighbor_marker(true, fi->position(), fi.team());
        }
        solid_mode();
    }

    if (menu.options.graphics.showName(true))
        for (int i = 0; i < maxplayers; i++) {
            if (fx.player[i].dead || !player_on_screen(i))
                continue;
            if (!replaying && !fx.player[i].onscreen && fx.player[i].room() == fx.player[me].room())
                continue; // don't draw players whose last known location is in this room but who aren't really here
            bool visible;
            if (replaying)
                visible = true;
            else if (fx.player[i].onscreen)
                visible = fx.player[i].visibility >= 200 || i / TSIZE == me / TSIZE;
            else
                visible = fx.player[i].alpha >= 200 && menu.options.graphics.showName(false);
            if (visible)
                graphics.draw_player_name(fx.player[i].name, playerPos(i), i / TSIZE, i == me);
        }

    graphics.endPlayfieldDraw();
}

GuiClient::VisibilityMap GuiClient::calculateVisibilities() throw () {
    const double time = fd.frame / 10;

    VisibilityMap roomVis(fx.map.w);
    uint8_t initVal = (replaying || me >= 0 && fx.player[me].item_shadow_time > time) ? 255 : 0;
    for (int x = 0; x < fx.map.w; ++x)
        roomVis[x].resize(fx.map.h, initVal);

    if (me < 0 && !replaying || fx.frame < 0)
        return roomVis;

    int max_time, start_fadeout;    // in frames
    switch (menu.options.graphics.minimapPlayers()) {
    /*break;*/ case Menu_graphics::MP_EarlyCut: max_time =     start_fadeout = 12;
        break; case Menu_graphics::MP_LateCut:  max_time =     start_fadeout = 20;
        break; case Menu_graphics::MP_Fade:     max_time = 20; start_fadeout = 10;
        break; default: nAssert(0); max_time = start_fadeout = 0;
    }
    for (int i = 0; i < maxplayers; i++) {
        ClientPlayer& pl = fx.player[i];
        if (pl.used && (!pl.dead && pl.posUpdated > fx.frame - max_time || i == me) && pl.room().x < fx.map.w && pl.room().y < fx.map.h) {
            if (fx.frame > pl.posUpdated + start_fadeout && i != me)
                pl.alpha = 255 - static_cast<int>((fx.frame - pl.posUpdated - start_fadeout) * 255 / (max_time - start_fadeout));
            else
                pl.alpha = 255;
            if (roomVis[pl.room().x][pl.room().y] < pl.alpha)
                roomVis[pl.room().x][pl.room().y] = pl.alpha;
        }
        else
            pl.alpha = 0;
    }
    return roomVis;
}

int GuiClient::calculatePlayerAlpha(int pid) const throw () {
    static const int min_alpha_friends = 128;
    int alpha = fd.player[pid].visibility;
    if (replaying || fx.player[pid].team() == fx.player[me].team())
        alpha = max(alpha, min_alpha_friends);

    if (player_on_screen_exact(pid))
        return alpha;
    else {
        if (!seePropertiesRemotely(pid))
            alpha = 255;
        return alpha * fx.player[pid].alpha / 255 * 2 / 3;
    }
}

bool GuiClient::seePropertiesRemotely(int pid) const throw () {
    if (see_minimap_player_properties == 0)
        return false;
    else if (see_minimap_player_properties == 1)
        return fx.player[pid].team() == fx.player[me].team();
    else
        return true;
}

void GuiClient::draw_player(int pid, double time, bool live) throw () {
    ClientPlayer& player = fx.player[pid];
    const bool fullyVisible = player_on_screen_exact(pid);
    const bool propertiesVisible = fullyVisible || seePropertiesRemotely(pid);
    const int alpha = calculatePlayerAlpha(pid);
    const int flagAlpha = fullyVisible ? 255 : alpha;
    const WorldCoords& pos = playerPos(pid);
    // draw flag if player is carrier of a flag
    for (ConstFlagIterator fi(fx); fi; ++fi)
        if (fi->carrier() == pid) {
            graphics.draw_flag(fi.team(), pos, false, flagAlpha, menu.options.graphics.emphasizeFlag(visible_rooms));
            break;
        }
    if (!propertiesVisible) {
        graphics.draw_player(pos, player.team(), player.color(), player.gundir, 0, false, alpha, time);
        return;
    }

    // turbo effect
    if (player.item_turbo && player.vel.mag2() > sqr(fx.physics.max_run_speed) && time > player.next_turbo_effect_time) {
        player.next_turbo_effect_time = time + 0.05;
        graphics.create_turbofx(pos, player.team(), player.color(), player.gundir, alpha, time);
    }

    //draw player
    graphics.draw_player(pos, player.team(), player.color(), player.gundir, player.hitfx, player.item_power, alpha, time);

    //draw deathbringer carrier smoke effect
    if (player.item_deathbringer && time > player.next_smoke_effect_time) {
        player.next_smoke_effect_time = time + 0.01;
        for (int i = 0; i < 2; i++)
            graphics.create_deathbringer_smoke(pos, player.team(), alpha, time);
    }
    // draw deathbringer affected effect
    if (player.deathbringer_affected)
        graphics.draw_deathbringer_affected(pos, player.team(), alpha);
    // shield
    if (player.item_shield)
        graphics.draw_shield(pos, PLAYER_RADIUS + SHIELD_RADIUS_ADD, live, alpha, player.team(), player.gundir);
}

class MapListSorter { // helper for draw_game_menu
public:
    MapListSorter(MapListSortKey key_) throw () : key(key_) { }
    bool operator()(const std::pair<const MapInfo*, int>& m1, const std::pair<const MapInfo*, int>& m2) const throw ();

private:
    MapListSortKey key;
};

bool MapListSorter::operator()(const pair<const MapInfo*, int>& m1, const pair<const MapInfo*, int>& m2) const throw () {
    const MapInfo& m1mi = *m1.first, &m2mi = *m2.first;
    switch (key) {
    /*break;*/ case MLSK_Votes: return m1mi.votes > m2mi.votes; // reverse: get high vote counts first
        break; case MLSK_Title: return cmp_case_ins(m1mi.title, m2mi.title);
        break; case MLSK_Size: {
            const int m1s = m1mi.width * m1mi.height;
            const int m2s = m2mi.width * m2mi.height;
            return m1s < m2s || m1s == m2s && m1mi.width < m2mi.width;
        }
        break; case MLSK_Author:   return cmp_case_ins(m1mi.author, m2mi.author);
        break; case MLSK_Favorite: return m1mi.preference > m2mi.preference;
        break; default: nAssert(0);
    }
    return false;
}

//draws the game menu
void GuiClient::draw_game_menu() throw () {
    switch (menusel) {
    /*break;*/ case menu_maps: {
            Lock ml(mapInfoMutex);
            if (mapListChangedAfterSort) {
                mapListChangedAfterSort = false;
                sortedMaps.clear();
                sortedMaps.reserve(maps.size());
                for (unsigned mi = 0; mi < maps.size(); ++mi)
                    sortedMaps.push_back(pair<MapInfo*, int>(&maps[mi], mi));
                if (mapListSortKey != MLSK_Number) {
                    MapListSorter sorter(mapListSortKey);
                    stable_sort(sortedMaps.begin(), sortedMaps.end(), sorter);
                }
            }
            graphics.map_list(sortedMaps, mapListSortKey, current_map, map_vote, edit_map_vote);
        }
        break; case menu_players:
            graphics.draw_statistics(players_sb, player_stats_page, static_cast<int>(fx.frame / 10), maxplayers, max_world_rank);
        break; case menu_teams:
            graphics.team_statistics(fx.teams);
        break; case menu_none: // regular menus are drawn below, regardless of menusel
        break; default:
            numAssert(0, menusel);
    }
    if (!openMenus.empty())
        openMenus.draw(graphics.drawbuffer(), graphics.colours());
}

void GuiClient::initMenus() throw () {
    typedef MenuCallback<GuiClient> MCB;
    typedef MenuKeyCallback<GuiClient> MKC;
    menu.connect.addHooks(new MCB::A<Textarea, &GuiClient::MCF_connect>(this),
                          new MKC::A<Textarea, &GuiClient::MCF_addRemoveServer>(this));

    menu.initialize(new MCB::A<Menu, &GuiClient::MCF_menuOpener>(this), settings);

    menu.menu                       .setDrawHook(new MCB::N<Menu,           &GuiClient::MCF_prepareMainMenu        >(this));

    menu.disconnect                     .setHook(new MCB::N<Textarea,       &GuiClient::MCF_disconnect             >(this));
    menu.exitOutgun                     .setHook(new MCB::N<Textarea,       &GuiClient::MCF_exitOutgun             >(this));

    menu.connect.menu               .setOpenHook(new MCB::N<Menu,           &GuiClient::MCF_prepareServerMenu      >(this));
    menu.connect.menu               .setDrawHook(new MCB::N<Menu,           &GuiClient::MCF_prepareServerMenu      >(this));   //#fix: inefficient!
    menu.connect.favorites              .setHook(new MCB::N<Checkbox,       &GuiClient::MCF_prepareServerMenu      >(this));
    menu.connect.update                 .setHook(new MCB::N<Textarea,       &GuiClient::MCF_updateServers          >(this));
    menu.connect.refresh                .setHook(new MCB::N<Textarea,       &GuiClient::MCF_refreshServers         >(this));
    menu.connect.manualEntry         .setKeyHook(new MKC::N<IPfield,        &GuiClient::MCF_addressEntryKeyHandler >(this));

    menu.connect.addServer.menu     .setOpenHook(new MCB::N<Menu,           &GuiClient::MCF_prepareAddServer       >(this));
    menu.connect.addServer.menu       .setOkHook(new MCB::N<Menu,           &GuiClient::MCF_addServer              >(this));

    menu.spectate.manualEntry        .setKeyHook(new MKC::N<Textfield,      &GuiClient::MCF_spectateEntryKeyHandler>(this));

    menu.options.menu              .setCloseHook(new MCB::NC<Menu,          &GuiClient::saveSettings               >(this));

    menu.options.player.menu        .setOpenHook(new MCB::N<Menu,           &GuiClient::MCF_preparePlayerMenu      >(this));
    menu.options.player.menu        .setDrawHook(new MCB::N<Menu,           &GuiClient::MCF_prepareDrawPlayerMenu  >(this));
    menu.options.player.menu       .setCloseHook(new MCB::N<Menu,           &GuiClient::MCF_playerMenuClose        >(this));
    menu.options.player.name            .setHook(new MCB::N<Textfield,      &GuiClient::MCF_nameChange             >(this));
    menu.options.player.randomName      .setHook(new MCB::N<Textarea,       &GuiClient::MCF_randomName             >(this));
    menu.options.player.favoriteColors  .setHook(new MCB::N<Colorselect,    &GuiClient::sendFavoriteColors         >(this));
    menu.options.player.removePasswords .setHook(new MCB::N<Textarea,       &GuiClient::MCF_removePasswords        >(this));

    menu.options.game.menu          .setOpenHook(new MCB::N<Menu,           &GuiClient::MCF_prepareGameMenu        >(this));
    menu.options.game.minimapBandwidth  .setHook(new MCB::N<Slider,         &GuiClient::sendMinimapBandwidth       >(this));
    typedef Select<Menu_game::MessageLoggingMode> mlComponentT;
    menu.options.game.messageLogging    .setHook(new MCB::N<mlComponentT,   &GuiClient::MCF_messageLogging         >(this));
    typedef Select<Menu_game::SkipMapsMode> smComponentT;
    menu.options.game.skipMaps          .setHook(new MCB::N<smComponentT,   &GuiClient::sendNegativeVotes          >(this));

    menu.options.quickMessages.menu.setCloseHook(new MCB::NC<Menu,          &GuiClient::saveQuickMessages          >(this));

    menu.options.controls.menu      .setDrawHook(new MCB::N<Menu,           &GuiClient::MCF_prepareControlsMenu    >(this));
    menu.options.controls.keyboardLayout.setHook(new MCB::N<Select<string>, &GuiClient::MCF_keyboardLayout         >(this));
    menu.options.controls.joystick      .setHook(new MCB::N<Checkbox,       &GuiClient::MCF_joystick               >(this));

    menu.options.screenMode.menu    .setOpenHook(new MCB::N<Menu,           &GuiClient::MCF_prepareScrModeMenu     >(this));
    menu.options.screenMode.menu    .setDrawHook(new MCB::N<Menu,           &GuiClient::MCF_prepareDrawScrModeMenu >(this));
    menu.options.screenMode.menu   .setCloseHook(new MCB::N<Menu,           &GuiClient::MCF_screenModeChange       >(this));
    menu.options.screenMode.menu      .setOkHook(new MCB::N<Menu,           &GuiClient::MCF_screenModeChange       >(this));
    menu.options.screenMode.colorDepth  .setHook(new MCB::N<Select<int>,    &GuiClient::MCF_screenDepthChange      >(this));
    menu.options.screenMode.apply       .setHook(new MCB::N<Textarea,       &GuiClient::MCF_screenModeChange       >(this));

    menu.options.theme.menu          .setOpenHook(new MCB::N<Menu,           &GuiClient::MCF_prepareGfxThemeMenu     >(this));
    menu.options.theme.theme             .setHook(new MCB::N<Select<string>, &GuiClient::MCF_gfxThemeChange          >(this));
    menu.options.theme.useThemeBackground.setHook(new MCB::N<Checkbox,       &GuiClient::MCF_gfxThemeChange          >(this));
    menu.options.theme.background        .setHook(new MCB::N<Select<string>, &GuiClient::MCF_gfxThemeChange          >(this));
    menu.options.theme.colours           .setHook(new MCB::N<Select<string>, &GuiClient::MCF_gfxThemeChange          >(this));
    menu.options.theme.useThemeColours   .setHook(new MCB::N<Checkbox,       &GuiClient::MCF_gfxThemeChange          >(this));
    menu.options.theme.font              .setHook(new MCB::N<Select<string>, &GuiClient::MCF_fontChange              >(this));

    menu.options.graphics.visibleRoomsPlay  .setHook(new MCB::N<Slider,         &GuiClient::MCF_visibleRoomsPlayChange  >(this));
    menu.options.graphics.visibleRoomsReplay.setHook(new MCB::N<Slider,         &GuiClient::MCF_visibleRoomsReplayChange>(this));
    menu.options.graphics.antialiasing      .setHook(new MCB::N<Checkbox,       &GuiClient::MCF_antialiasChange         >(this));
    menu.options.graphics.minTransp         .setHook(new MCB::N<Checkbox,       &GuiClient::MCF_transpChange            >(this));
    menu.options.graphics.statsBgAlpha      .setHook(new MCB::N<Slider,         &GuiClient::MCF_statsBgChange           >(this));

    menu.options.sounds.menu        .setOpenHook(new MCB::N<Menu,           &GuiClient::MCF_prepareSndMenu         >(this));
    menu.options.sounds.enabled         .setHook(new MCB::N<Checkbox,       &GuiClient::MCF_sndEnableChange        >(this));
    menu.options.sounds.volume          .setHook(new MCB::N<Slider,         &GuiClient::MCF_sndVolumeChange        >(this));
    menu.options.sounds.theme           .setHook(new MCB::N<Select<string>, &GuiClient::MCF_sndThemeChange         >(this));

    menu.options.language.menu      .setOpenHook(new MCB::N<Menu,           &GuiClient::MCF_refreshLanguages       >(this));

    menu.options.bugReports.menu   .setCloseHook(new MCB::N<Menu,           &GuiClient::MCF_acceptBugReporting     >(this));   // save instantly because it has its own file
    menu.options.bugReports.menu      .setOkHook(new MCB::N<Menu,           &GuiClient::MCF_menuCloser             >(this));

    menu.ownServer.menu             .setDrawHook(new MCB::N<Menu,           &GuiClient::MCF_prepareOwnServerMenu   >(this));
    menu.ownServer.start                .setHook(new MCB::N<Textarea,       &GuiClient::MCF_startServer            >(this));
    menu.ownServer.play                 .setHook(new MCB::N<Textarea,       &GuiClient::MCF_playServer             >(this));
    menu.ownServer.stop                 .setHook(new MCB::N<Textarea,       &GuiClient::MCF_stopServer             >(this));

    menu.replays.menu               .setOpenHook(new MCB::N<Menu,           &GuiClient::MCF_prepareReplayMenu      >(this));

    m_playerPassword.menu             .setOkHook(new MCB::N<Menu,           &GuiClient::MCF_playerPasswordAccept   >(this));
    m_serverPassword.menu             .setOkHook(new MCB::N<Menu,           &GuiClient::MCF_serverPasswordAccept   >(this));
    m_connectProgress.accept            .setHook(new MCB::N<Textarea,       &GuiClient::MCF_menuCloser             >(this));
    m_connectProgress.cancel            .setHook(new MCB::N<Textarea,       &GuiClient::MCF_menuCloser             >(this));
    m_connectProgress.menu         .setCloseHook(new MCB::N<Menu,           &GuiClient::MCF_cancelConnect          >(this));
    m_dialog.accept                     .setHook(new MCB::N<Textarea,       &GuiClient::MCF_menuCloser             >(this));   // cancel not used
    m_errors.accept                     .setHook(new MCB::N<Textarea,       &GuiClient::MCF_clearErrors            >(this));   // cancel not used
    m_serverInfo.accept                 .setHook(new MCB::N<Textarea,       &GuiClient::MCF_menuCloser             >(this));   // cancel not used

    m_errors.menu.setCaption(_("Errors"));

    m_notResponding.menu.setCaption(_("Server not responding"));
    m_notResponding.addLine(_("May be heavy packet loss,"));
    m_notResponding.addLine(_("or the server disconnected."), "", false, true); // make the dialog passive

    loadHelp();
    loadSplashScreen();

    menu.options.screenMode.init(graphics);
    menu.options.theme.init(graphics);
    menu.options.sounds.init(client_sounds);
    menu.ownServer.init(serverExtConfig.ipAddress);
}

void GuiClient::MCF_menuOpener(Menu& menu) throw () {
    openMenus.open(&menu);
}

void GuiClient::MCF_menuCloser() throw () {
    openMenus.close();
}

void GuiClient::MCF_prepareMainMenu() throw () {
    menu.ownServer.refreshCaption(listenServer.running());
    menu.disconnect.setEnable(connected || spectating);
}

void GuiClient::MCF_disconnect() throw () {
    disconnect_command();
    stop_replay();
}

void GuiClient::MCF_exitOutgun() throw () {
    quitCommand = true;
}

void GuiClient::MCF_cancelConnect() throw () {
    if (!connected)
        disconnect_command();   // will cancel the (probably) ongoing connect attempt
}

void GuiClient::MCF_preparePlayerMenu() throw () {
    menu.options.player.name.set(playername);
    menu.options.player.favoriteColors.setGraphicsCallBack(graphics);
}

void GuiClient::MCF_prepareDrawPlayerMenu() throw () {
    menu.options.player.namestatus.set(rankingPassword.statusAsString());
}

void GuiClient::MCF_playerMenuClose() throw () {
    change_name_command();
    send_ranking_participation();
}

void GuiClient::MCF_nameChange() throw () { // only function to clear the password
    menu.options.player.password.set("");
    rankingPassword.changeData(playername, "");
}

void GuiClient::MCF_randomName() throw () {
    const string name = language.code() == "fi" ? finnish_name(maxPlayerNameLength) : RandomName();
    menu.options.player.name.set(name);
    MCF_nameChange();
}

void GuiClient::MCF_removePasswords() throw () {
    const int removed = remove_player_passwords(menu.options.player.name());
    string dialog;
    if (removed == 1)
        dialog = _("1 password removed.");
    else if (removed > 1)
        dialog = _("$1 passwords removed.", itoa(removed));
    else
        dialog = _("No passwords found.");
    m_dialog.clear();
    m_dialog.addLine(dialog);
    showMenu(m_dialog);
}

void GuiClient::MCF_prepareGameMenu() throw () {
}

void GuiClient::MCF_prepareControlsMenu() throw () {
    ClientControls ctrl = readControls(true, true);
    string active;
    if (ctrl.isUp())
        active += _("up")     + ' ';
    if (ctrl.isDown())
        active += _("down")   + ' ';
    if (ctrl.isLeft())
        active += _("left")   + ' ';
    if (ctrl.isRight())
        active += _("right")  + ' ';
    if (ctrl.isRun())
        active += _("run")    + ' ';
    if (ctrl.isStrafe())
        active += _("strafe") + ' ';
    if (firePressed())
        active += _("shoot")  + ' ';
    menu.options.controls.activeControls.set(active);
    active.clear();
    if (menu.options.controls.joystick()) {
        for (int button = 1; button <= 16; ++button)
            if (readJoystickButton(button))
                active += itoa(button) + ' ';
    }
    menu.options.controls.activeJoystick.set(active);
    active.clear();
    for (int button = 1; button <= 16; ++button)
        if (mouse_b & (1 << button - 1))
            active += itoa(button) + ' ';
    menu.options.controls.activeMouse.set(active);
}

void GuiClient::MCF_keyboardLayout() throw () {
    const string cfg = string("[system]\nkeyboard=") + menu.options.controls.keyboardLayout() + '\n';
    remove_keyboard();
    override_config_data(cfg.data(), cfg.length());
    install_keyboard();
}

void GuiClient::MCF_joystick() throw () {
    if (menu.options.controls.joystick())
        install_joystick(JOY_TYPE_AUTODETECT);
    else
        remove_joystick();
}

void GuiClient::MCF_messageLogging() throw () {
    if (menu.options.game.messageLogging() != Menu_game::ML_none)
        openMessageLog();
    else
        closeMessageLog();
}

void GuiClient::MCF_prepareScrModeMenu() throw () {
    menu.options.screenMode.update(graphics);
}

void GuiClient::MCF_prepareDrawScrModeMenu() throw () {
    menu.options.screenMode.flipping.setEnable(!menu.options.screenMode.windowed());
    menu.options.screenMode.alternativeFlipping.setEnable(!menu.options.screenMode.windowed() && menu.options.screenMode.flipping());
}

void GuiClient::MCF_prepareGfxThemeMenu() throw () {
    menu.options.theme.update(graphics);
}

void GuiClient::MCF_gfxThemeChange() throw () {
    graphics.select_theme(menu.options.theme.theme(),
                          menu.options.theme.background(), menu.options.theme.useThemeBackground(),
                          menu.options.theme.colours(), menu.options.theme.useThemeColours());
}

void GuiClient::MCF_fontChange() throw () {
    graphics.select_font(menu.options.theme.font());
}

void GuiClient::MCF_screenDepthChange() throw () {
    menu.options.screenMode.update(graphics);  // fetch resolutions according to the new depth
}

void GuiClient::MCF_screenModeChange() throw () {   // used to lose the return value
    const bool ret = screenModeChange();
    nAssert(ret); // it should return true unless it's out of memory, because this function is only used when there is a working mode to revert to
}

bool GuiClient::screenModeChange() throw () {   // returns true whenever Graphics is usable (even when reverted back to current (workingGfxMode) mode)
    if (!menu.options.screenMode.newMode())
        return true;

    const ScreenMode res = menu.options.screenMode.resolution();
    const int depth = menu.options.screenMode.colorDepth();

    Checkbox& win  = menu.options.screenMode.windowed;
    Checkbox& flip = menu.options.screenMode.flipping;
    const bool owin = win(), oflip = flip();

    for (int nTry = 0;; ++nTry) {
        if (graphics.init(res.width, res.height, depth, win(), flip())) {
            if (nTry != 0)
                log.error(_("Couldn't initialize resolution $1×$2×$3 in $4 mode; reverted to $5.",
                            itoa(res.width), itoa(res.height), itoa(depth),
                            owin  ? _("windowed") : (oflip  ? _("flipped fullscreen") : _("backbuffered fullscreen")),
                            win() ? _("windowed") : (flip() ? _("flipped fullscreen") : _("backbuffered fullscreen"))));
            break;
        }
        switch (nTry) { // try in order: [switch flip], switch windowed, [switch flip]
        /*break;*/ case 0:
            if (!win()) {
                flip.set(!flip());
                break;
            }
            nTry = 1;   // no point in changing flipping when windowed, skip round
        /*no break*/ case 1:
            win.set(!win());
        break; case 2:
            if (!win()) {
                flip.set(!flip());
                break;
            }
            nTry = 3;   // no point in changing flipping when windowed, skip round
        /*no break*/ case 3:
            log.error(_("Couldn't initialize resolution $1×$2×$3 in any mode.", itoa(res.width), itoa(res.height), itoa(depth)));
            if (workingGfxMode.used()) {    // revert to working mode
                const GFXMode& wm = workingGfxMode;
                nAssert(menu.options.screenMode.colorDepth.set(wm.depth));
                menu.options.screenMode.update(graphics);  // fetch resolutions according to the new depth
                menu.options.screenMode.resolution.set(ScreenMode(wm.width, wm.height));  // ignore potential error here; we couldn't do anything about it anyway
                win.set(wm.windowed);
                flip.set(wm.flipping);
                return graphics.init(wm.width, wm.height, wm.depth, wm.windowed, wm.flipping);
            }
            return false;
        }
    }
    workingGfxMode = GFXMode(res.width, res.height, depth, win(), flip());
    const int rate = get_refresh_rate();
    ostringstream ost;
    if (rate == 0)
        ost << _("unknown");
    else
        ost << _("$1 Hz", itoa(rate));
    menu.options.screenMode.refreshRate.set(ost.str());
    return true;
}

void GuiClient::MCF_visibleRoomsPlayChange() throw () {
    if (!replaying) {
        visible_rooms = menu.options.graphics.visibleRoomsPlay();
        if (visible_rooms > fx.map.w && visible_rooms > fx.map.h && !repeatMapX() && !repeatMapY())
            visible_rooms = max(fx.map.w, fx.map.h);
    }
}

void GuiClient::MCF_visibleRoomsReplayChange() throw () {
    if (replaying) {
        visible_rooms = menu.options.graphics.visibleRoomsReplay();
        if (visible_rooms > fx.map.w && visible_rooms > fx.map.h && !repeatMapX() && !repeatMapY())
            visible_rooms = max(fx.map.w, fx.map.h);
    }
}

void GuiClient::MCF_antialiasChange() throw () {
    graphics.set_antialiasing(menu.options.graphics.antialiasing());
}

void GuiClient::MCF_transpChange() throw () {
    graphics.set_min_transp(menu.options.graphics.minTransp());
}

void GuiClient::MCF_statsBgChange() throw () {
    graphics.set_stats_alpha(menu.options.graphics.statsBgAlpha());
}

void GuiClient::MCF_prepareSndMenu() throw () {
    menu.options.sounds.update(client_sounds);
}

void GuiClient::MCF_sndEnableChange() throw () {
    client_sounds.setEnable(menu.options.sounds.enabled());
}

void GuiClient::MCF_sndVolumeChange() throw () {
    client_sounds.setVolume(menu.options.sounds.volume());
    client_sounds.play(SAMPLE_POWER_FIRE, 1000);
}

void GuiClient::MCF_sndThemeChange() throw () {
    client_sounds.select_theme(menu.options.sounds.theme());
}

void GuiClient::MCF_refreshLanguages() throw () {
    const int menu_selection_index = refreshLanguages(menu.options.language);
    typedef MenuCallback<GuiClient> MCB;
    menu.options.language.addHooks(new MCB::A<Textarea, &GuiClient::MCF_acceptLanguage>(this));
    menu.options.language.menu.setSelection(menu_selection_index);
}

// Helper to refreshLanguages. Each pair consists of language code and name, in that order.
bool translationSort(const pair<string, string>& t1, const pair<string, string>& t2) throw () {
    // don't care about the language code (it isn't visible anyway), and use a case insensitive order
    return platStricmp(t1.second.c_str(), t2.second.c_str()) < 0;
}

int GuiClient::refreshLanguages(Menu_language& lang_menu) throw () {
    lang_menu.reset();

    // search the languages directory for translations to add
    vector< pair<string, string> > translations;
    translations.push_back(pair<string, string>("en", "English"));   // global default when there's nothing in language.txt
    FileFinder* languageFiles = platMakeFileFinder(wheregamedir + "languages", ".txt", false);
    while (languageFiles->hasNext()) {
        string code = FileName(languageFiles->next()).getBaseName();
        if (code.find('.') != string::npos || code == "en")   // skip help.language.txt and possible similar files, and of course English which was added first
            continue;
        // fetch language name
        const string langFile = wheregamedir + "languages" + directory_separator + code + ".txt";
        ifstream in(langFile.c_str());
        string name;
        if (getline_skip_comments(in, name))
            translations.push_back(pair<string, string>(code, name));
        else
            log.error(_("Translation $1 can't be read.", langFile));
    }
    delete languageFiles;

    // fetch the currently chosen language from language.txt (because after changing it can be different from the loaded language)
    string lang;
    ifstream langConfig((whereuserdir + "config" + directory_separator + "language.txt").c_str());
    if (!getline_skip_comments(langConfig, lang))
        lang = "en";
    langConfig.close();

    // add found languages to options
    sort(translations.begin(), translations.end(), translationSort);
    int i = lang_menu.menu.selection();
    int current_lang_index = i;
    for (vector< pair<string, string> >::const_iterator ti = translations.begin(); ti != translations.end(); ++ti, ++i) {
        lang_menu.add(ti->first, ti->second);
        if (ti->first == lang)
            current_lang_index = i;
    }
    return current_lang_index;
}

void GuiClient::MCF_acceptLanguage(Textarea& target) throw () {
    const string lang_code = menu.options.language.getCode(target);
    MCF_menuCloser();
    acceptLanguage(lang_code, true);
}

void GuiClient::MCF_acceptInitialLanguage(Textarea& target) throw () {
    const string lang_code = m_initialLanguage.getCode(target);
    MCF_menuCloser();
    acceptLanguage(lang_code, false);
}

void GuiClient::acceptLanguage(const string& lang, bool restart_message) throw () {
    Language newLang;
    if (!newLang.load(lang, log))
        return; // load already logs an error message
    ofstream langConfig((whereuserdir + "config" + directory_separator + "language.txt").c_str());
    if (langConfig) {
        langConfig << lang << '\n';
        langConfig.close();
        if (lang != language.code() && restart_message) {  // what is currently loaded; what was previously in language.txt has no significance
            m_dialog.clear();
            m_dialog.wrapLine(_("Please close and restart Outgun to complete the change of language.", newLang));
            showMenu(m_dialog);
        }
    }
    else
        log.error(_("config/language.txt can't be written."));
}

void GuiClient::MCF_acceptBugReporting() throw () {
    g_autoBugReporting = menu.options.bugReports.policy();
    const string main_cfg_file = whereuserdir + "config" + directory_separator + "maincfg.txt";
    ofstream os(main_cfg_file.c_str());
    if (os) {
        switch (g_autoBugReporting) {
        /*break;*/ case ABR_disabled: os << "autobugreporting disabled";
            break; case ABR_minimal:  os << "autobugreporting minimal" ;
            break; case ABR_withDump: os << "autobugreporting complete";
            break; default: nAssert(0);
        }
        os << '\n';
    }
    else
        log.error(_("Can't open $1 for writing.", main_cfg_file));
}

void GuiClient::MCF_playerPasswordAccept() throw () {
    if (m_playerPassword.password().empty()) // if no password is needed, we're never asked for one (even if we gave a wrong one)
        return;
    openMenus.close(&m_playerPassword.menu);
    if (m_playerPassword.save())
        save_player_password(playername, serverIP.toString(), m_playerPassword.password());
    if (connected)
        issue_change_name_command();
    else
        connect_command(false);
}

void GuiClient::MCF_serverPasswordAccept() throw () {
    openMenus.close(&m_serverPassword.menu);
    nAssert(!connected);
    connect_command(false);
}

void GuiClient::MCF_clearErrors() throw () {
    openMenus.close(&m_errors.menu);
    m_errors.clear();
}

void GuiClient::MCF_prepareServerMenu() throw () {
    const int oldSel = menu.connect.menu.selection();

    menu.connect.reset();
    vector<Network::Address> addresses;
    const vector<ServerListEntry>& servers = (menu.connect.favorites() ? gamespy : mgamespy);
    serverListMutex.lock();
    for (vector<ServerListEntry>::const_iterator spy = servers.begin(); spy != servers.end(); ++spy) {
        ostringstream info;
        info << setw(21) << left << spy->addressString() << right << ' ';
        info << setw(4);
        if (spy->ping > 0)
            info << spy->ping;
        else
            info << '?';
        if (spy->refreshed) {
            info << ' ';
            if (spy->noresponse)
                info << _("no response");
            else
                info << spy->info;
        }
        menu.connect.add(spy->address(), info.str());
        addresses.push_back(spy->address());
    }
    if (!menu.connect.favorites())
        for (vector<ServerListEntry>::const_iterator spy = gamespy.begin(); spy != gamespy.end(); ++spy)
            if (!spy->noresponse && find(addresses.begin(), addresses.end(), spy->address()) == addresses.end()) {
                ostringstream info;
                info << setw(21) << left << spy->addressString() << right << ' ';
                info << setw(4);
                if (spy->ping > 0)
                    info << spy->ping;
                else
                    info << '?';
                if (spy->refreshed) {
                    info << ' ';
                    if (spy->noresponse)
                        info << _("no response");
                    else
                        info << spy->info;
                }
                menu.connect.add(spy->address(), info.str());
                addresses.push_back(spy->address());
            }
    serverListMutex.unlock();

    typedef MenuCallback<GuiClient> MCB;
    typedef MenuKeyCallback<GuiClient> MKC;
    menu.connect.addHooks(new MCB::A<Textarea, &GuiClient::MCF_connect>(this),
                          new MKC::A<Textarea, &GuiClient::MCF_addRemoveServer>(this));
    const bool refreshActive = (refreshStatus != RS_none && refreshStatus != RS_failed);
    menu.connect.update.setEnable(!menu.connect.favorites() && !refreshActive);
    menu.connect.refresh.setEnable(!refreshActive);
    menu.connect.refreshStatus.set(refreshStatusAsString());

    menu.connect.menu.setSelection(oldSel);
}

void GuiClient::MCF_prepareAddServer() throw () {
    menu.connect.addServer.save.set(menu.connect.favorites());
    menu.connect.addServer.address.set("");
}

void GuiClient::MCF_addServer() throw () {
    if (!menu.connect.addServer.address().empty()) {
        ServerListEntry spy;
        if (!spy.setAddress(menu.connect.addServer.address())) {
            m_dialog.clear();
            m_dialog.addLine(_("Invalid IP address."));
            showMenu(m_dialog);
            return;
        }
        mgamespy.push_back(spy);
        if (menu.connect.addServer.save())
            gamespy.push_back(spy);
        MCF_prepareServerMenu();
    }
    MCF_menuCloser();
}

bool GuiClient::MCF_addressEntryKeyHandler(char scan, unsigned char chr) throw () {
    (void)chr;
    if (scan != KEY_ENTER && scan != KEY_INSERT)
        return false;
    if (menu.connect.manualEntry().empty())
        return true;    // the key is considered handled even if it has no effect in this case
    ServerListEntry spy;
    if (!spy.setAddress(menu.connect.manualEntry())) {
        m_dialog.clear();
        m_dialog.addLine(_("Invalid IP address."));
        showMenu(m_dialog);
        return true;
    }
    if (scan == KEY_ENTER) {    // connect to the address
        serverIP = spy.address();
        m_serverPassword.password.set("");
        connect_command(true);
    }
    else if (scan == KEY_INSERT) {  // add the server to the list shown below
        if (menu.connect.favorites())
            gamespy.push_back(spy);
        else
            mgamespy.push_back(spy);
    }
    return true;
}

bool GuiClient::MCF_addRemoveServer(Textarea& target, char scan, unsigned char chr) throw () {
    (void)chr;
    if (scan == KEY_DEL) {
        vector<ServerListEntry>& servers = (menu.connect.favorites() ? gamespy : mgamespy);
        const Network::Address address = menu.connect.getAddress(target);
        for (vector<ServerListEntry>::iterator spy = servers.begin(); spy != servers.end(); ++spy)
            if (address == spy->address()) {
                servers.erase(spy);
                break;
            }
        return true;
    }
    else if (scan == KEY_INSERT && !menu.connect.favorites()) {
        const Network::Address address = menu.connect.getAddress(target);
        for (vector<ServerListEntry>::const_iterator spy = mgamespy.begin(); spy != mgamespy.end(); ++spy)
            if (address == spy->address()) {
                gamespy.push_back(*spy);
                break;
            }
        return true;
    }
    return false;
}

void GuiClient::MCF_connect(Textarea& target) throw () {
    serverIP = menu.connect.getAddress(target);
    m_serverPassword.password.set("");
    connect_command(true);
}

void GuiClient::MCF_updateServers() throw () {
    if (refreshStatus == RS_none || refreshStatus == RS_failed) {
        refreshStatus = RS_running;
        Thread::startDetachedThread_assert("GuiClient::getServerListThread",
                                           MemFun0<GuiClient, void>(this, &GuiClient::getServerListThread),
                                           extConfig.lowerPriority);
    }
}

void GuiClient::MCF_refreshServers() throw () {
    if (refreshStatus == RS_none || refreshStatus == RS_failed) {
        refreshStatus = RS_running;
        Thread::startDetachedThread_assert("GuiClient::refreshThread",
                                           MemFun0<GuiClient, void>(this, &GuiClient::refreshThread),
                                           extConfig.lowerPriority);
    }
}

bool GuiClient::MCF_spectateEntryKeyHandler(char scan, unsigned char chr) throw () {
    (void)chr;
    if (scan != KEY_ENTER)
        return false;
    if (!menu.spectate.manualEntry().empty())
        start_spectating(menu.spectate.manualEntry());
    return true; // the key is considered handled even if it has no effect when the field is empty
}

void GuiClient::MCF_prepareOwnServerMenu() throw () {
    menu.ownServer.refreshCaption(listenServer.running());
    menu.ownServer.refreshEnables(listenServer.running(), connected);
}

void GuiClient::MCF_startServer() throw () {
    if (!listenServer.running()) {
        serverExtConfig.privateserver = menu.ownServer.pub() ? 0 : 1;
        serverExtConfig.port = menu.ownServer.port();
        serverExtConfig.ipAddress = menu.ownServer.address();
        serverExtConfig.privSettingForced = serverExtConfig.portForced = serverExtConfig.ipForced = true;
        serverExtConfig.showErrorCount = false;
        listenServer.start(serverExtConfig.port, serverExtConfig);
    }
}

void GuiClient::MCF_playServer() throw () {
    if (listenServer.running()) {
        serverIP.fromValidIP("127.0.0.1");
        serverIP.setPort(listenServer.port());
        openMenus.clear();
        m_serverPassword.password.set("");
        connect_command(true);
    }
}

void GuiClient::MCF_stopServer() throw () {
    if (listenServer.running())
        listenServer.stop();
}

void GuiClient::MCF_replay(TreeItem& target) throw () {
    //const string& replay_name = menu.replays.getFile(target.key());
    const string& replay_name = target.key();
    const string filename = whereuserdir + "replay" + directory_separator + replay_name + ".replay";
    start_replay(filename);
}

string GuiClient::replayCacheFile() const throw () {
    return whereuserdir + "replay" + directory_separator + "index.bin";
}

GuiClient::ReplayCache GuiClient::loadReplayCache() const throw () {
    ifstream in(replayCacheFile().c_str(), ios::binary);
    if (!in)
        return ReplayCache();
    BinaryStreamReader read(in);
    try {
        if (read.U32() != replayCacheVersionIdentifier) {
            log("Replay cache version mismatch. Contents ignored and will be overwritten.");
            return ReplayCache();
        }
        ReplayCache cache;
        while (read.hasMore()) {
            const string fileName = read.str();
            const string infoString = read.str();
            cache.insert(cache.end(), make_pair(fileName, ReplayDescriptor(infoString, true)));
        }
        return cache;
    } catch (BinaryReader::ReadOutside) {
        log("Replay cache file corrupt. Contents ignored and will be overwritten.");
        return ReplayCache();
    }
}

void GuiClient::saveReplayCache(const ReplayList& replays) const throw () {
    ofstream out(replayCacheFile().c_str(), ios::binary);
    if (!out) {
        log("Can't write to %s", replayCacheFile().c_str());
        return;
    }
    ExpandingBinaryBuffer write;
    write.U32(replayCacheVersionIdentifier);
    for (ReplayList::const_iterator ii = replays.begin(); ii != replays.end(); ++ii) {
        if (!ii->second.final)
            continue;
        write.str(ii->first);
        write.str(ii->second.description);
        out << write;
        write.clear();
    }
}

void GuiClient::MCF_prepareReplayMenu() throw () {
    ReplayCache cache = loadReplayCache();
    ReplayList replays;
    FileFinder* replay_files = platMakeFileFinder(whereuserdir + "replay", ".replay", false);
    while (replay_files->hasNext()) {
        const string name = FileName(replay_files->next()).getBaseName();
        ReplayCache::iterator iCache = cache.find(name);
        if (iCache != cache.end()) {
            iCache->second.confirmed = true;
            replays.push_back(*iCache);
            continue;
        }
        const string replay_file = whereuserdir + "replay" + directory_separator + name + ".replay";
        ifstream in(replay_file.c_str(), ios::binary);
        BinaryStreamReader read(in);
        try {
            // whenever anything here changes, you should probably change replayCacheVersionIdentifier to invalidate cached information
            if (read.constLengthStr(REPLAY_IDENTIFICATION.length()) != REPLAY_IDENTIFICATION) {
                log.error(_("$1 is not an Outgun replay.", replay_file));
                continue;
            }
            const unsigned replay_version = read.U32();
            if (replay_version > REPLAY_VERSION) {
                log.error(_("Replay $1 is a newer version ($2).", replay_file, itoa(replay_version)));
                continue;
            }
            const unsigned length = read.U32();
            const string server_name = read.str();
            read.U32(); // skip maxplayers
            const string map_name = read.str();

            ostringstream text;
            text << name << ' ' << server_name << " - " << map_name;
            if (length > 0)
                text << ' ' << length / 600 << ':' << setw(2) << setfill('0') << length / 10 % 60;
            replays.push_back(make_pair(name, ReplayDescriptor(text.str(), length > 0)));
        } catch (BinaryReader::ReadOutside) {
            log("Replay file %s is invalid.", replay_file.c_str());
        }
    }
    delete replay_files;
    log("%lu replays found.", static_cast<long unsigned>(replays.size()));

    sort(replays.begin(), replays.end());
    for (ReplayCache::const_iterator ri = cache.begin(); ri != cache.end(); ri++)
        if (!ri->second.confirmed)
            menu.replays.remove(ri->first);

    for (ReplayList::reverse_iterator ri = replays.rbegin(); ri != replays.rend(); ++ri) // const_reverse_iterator does not work in GCC 3.4.2
        menu.replays.add(ri->first, ri->second.description);

    typedef MenuCallback<GuiClient> MCB;
    menu.replays.addHooks(new MCB::A<TreeItem, &GuiClient::MCF_replay>(this));

    menu.replays.expandLatest();

    saveReplayCache(replays);
}

void GuiClient::load_highlight_texts() throw () {
    highlight_text.clear();
    const string configFile = whereuserdir + "config" + directory_separator + "texts.txt";
    ifstream in(configFile.c_str());
    string line;
    while (getline_skip_comments(in, line))
        highlight_text.push_back(toupper(trim(line)));
}

void GuiClient::load_fav_maps() throw () {
    fav_maps.clear();
    const string configFile = whereuserdir + "config" + directory_separator + "maps.txt";
    ifstream in(configFile.c_str());
    string line;
    while (getline_skip_comments(in, line)) {
        if (line[0] == '-')
            fav_maps[toupper(trim(line.substr(1)))] = -1;
        else if (line[0] == '+')
            fav_maps[toupper(trim(line.substr(1)))] = +1;
        else if (line[0] == '0' && (line[1] == ' ' || line[1] == '\t'))
            fav_maps[toupper(trim(line.substr(2)))] =  0;
        else
            fav_maps[toupper(trim(line          ))] = +1;
    }
}

void GuiClient::saveSettings() const throw () {
    //save configuration file
    const string fileName = whereuserdir + "config" + directory_separator + "client.cfg";
    log("Saving client configuration in %s", fileName.c_str());
    ofstream cfg(fileName.c_str());
    if (!cfg) {
        log("Can't open %s for writing", fileName.c_str());
        return;
    }

    for (SettingManager::MapT::const_iterator si = settings.read().begin(); si != settings.read().end(); ++si) {
        nAssert(si->second);
        cfg << si->first << ' ';
        si->second->save(cfg);
        cfg << '\n';
    }

    // some more complicated settings need to be handled here
    cfg << CCS_PlayerName << ' ' << playername << '\n';
    {   // favorite colors
        cfg << CCS_FavoriteColors << ' ';
        if (menu.options.player.favoriteColors.values().empty())
            cfg << -1;
        else {
            const vector<int>& colVec = menu.options.player.favoriteColors.values();
            for (vector<int>::const_iterator col = colVec.begin(); col != colVec.end(); ++col)
                cfg << *col << ' ';
        }
        cfg << '\n';
    }
    cfg << CCS_KeyboardLayout << ' ' << menu.options.controls.keyboardLayout() << '\n';
    {
        ScreenMode mode = menu.options.screenMode.resolution();
        cfg << CCS_GFXMode << ' ' <<  mode.width << ' ' << mode.height << ' ' << menu.options.screenMode.colorDepth() << '\n';
    }
    cfg << CCS_Antialiasing << ' ' << (menu.options.graphics.antialiasing() ? 2 : 1) << '\n';

    log("Client configuration saved");
}

void GuiClient::loadQuickMessages() throw () {
    vector<string> messages;
    ifstream in(quickMessageFile.c_str());
    string line;
    while (getline_skip_comments(in, line))
        messages.push_back(trim(line));
    menu.options.quickMessages.loadMessages(messages);
}

void GuiClient::saveQuickMessages() const throw () {
    ofstream out(quickMessageFile.c_str());
    const vector<Textfield>& messages = menu.options.quickMessages.messages;
    for (vector<Textfield>::const_iterator field = messages.begin(); field != messages.end(); field++)
        out << (*field)() + " " << '\n'; // Save at least one space so that the line is not skipped when loading.
}

std::string GuiClient::mapInfoCacheFile() const throw () {
    return whereuserdir + "config" + directory_separator + "mapinfocache.bin";
}

void GuiClient::loadMapInfoCache() throw () {
    mapInfoCache.clear();
    ifstream in(mapInfoCacheFile().c_str(), ios::binary);
    if (!in)
        return;
    BinaryStreamReader read(in);
    try {
        if (read.U32() != mapInfoCacheVersionIdentifier) {
            log("Map info cache version mismatch. Contents ignored and will be overwritten.");
            return;
        }
        while (read.hasMore()) {
            MapInfo mi;
            mi.infoHash = read.U16();
            mi.title = read.str();
            mi.author = read.str();
            mi.width = read.U32dyn8();
            mi.height = read.U32dyn8();
            mi.votes = 0;
            mi.random = false;
            mapInfoCache.insert(mapInfoCache.end(), make_pair(mi.infoHash, mi));
        }
    } catch (BinaryReader::ReadOutside) {
        log("Map info cache file corrupt. Contents ignored and will be overwritten.");
        mapInfoCache.clear();
    }
}

void GuiClient::saveMapInfoCache() const throw () {
    ofstream out(mapInfoCacheFile().c_str(), ios::binary);
    if (!out) {
        log("Can't write to %s", mapInfoCacheFile().c_str());
        return;
    }
    ExpandingBinaryBuffer write;
    write.U32(mapInfoCacheVersionIdentifier);
    for (MapInfoCache::const_iterator ii = mapInfoCache.begin(); ii != mapInfoCache.end(); ++ii) {
        const MapInfo& mi = ii->second;
        nAssert(mi.infoHash == ii->first);
        write.U16(mi.infoHash);
        write.str(mi.title);
        write.str(mi.author);
        write.U32dyn8(mi.width);
        write.U32dyn8(mi.height);
        nAssert(!mi.random);
        out << write;
        write.clear();
    }
}

void GuiClient::apply_fav_maps() throw () {
    for (vector<MapInfo>::iterator mi = maps.begin(); mi != maps.end(); ++mi)
        updateMapPreference(*mi);
    mapListChangedAfterSort = true;
    sendNegativeVotes();
}

void GuiClient::loadHelp() throw () {
    menu.help.clear();
    const string configFile = wheregamedir + "languages" + directory_separator + "help." + language.code() + ".txt";
    ifstream in(configFile.c_str());
    if (!in) {
        menu.help.addLine(_("No help found. It should be in $1", configFile));
        return;
    }
    string line;
    while (getline_smart(in, line))
        menu.help.addLine(line);
}

void GuiClient::addSplashLine(string line) throw () { // internal to loadSplashScreen
    replace_all_in_place(line, "@VERSION@", getVersionString());
    replace_all_in_place(line, "@YEAR@", GAME_COPYRIGHT_YEAR);
    menu.options.bugReports.addLine(line);
}

void GuiClient::loadSplashScreen() throw () {
    menu.options.bugReports.clear();
    const string splashFile = wheregamedir + "languages" + directory_separator + "splash." + language.code() + ".txt";
    ifstream in(splashFile.c_str());
    if (in) {
        string line;
        while (getline_smart(in, line))
            addSplashLine(line);
    }
    else {
        static const char* msg[] = {
            "Outgun @VERSION@, copyright © 2002-@YEAR@ multiple authors.",
            "",
            "Outgun is free software under the GNU GPL, and you are welcome to "
            "redistribute it under certain conditions. Outgun comes with ABSOLUTELY "
            "NO WARRANTY. For details, see the accompanying file COPYING.",
            "",
            "To help us remove any remaining bugs, you can let Outgun automatically "
            "send us a notification when an unexpected failure occurs. You can choose "
            "between no reporting, minimal information, and a complete report. The "
            "minimal information includes no more than the file name and line number "
            "of the failing assertion, and the version of Outgun. The complete report "
            "also includes a copy of Outgun's stack. This information is only used to "
            "find the cause of the failure. We can't contact you for more information, "
            "so it is recommended to also send an e-mail with more details.",
            "",
            "Choose the preferred mode below with left and right arrow keys, and close "
            "the menu with Enter or Esc. After the first time of starting Outgun, you "
            "can find this screen in the Options menu.",
            0
        };
        for (const char** line = msg; *line; ++line)
            addSplashLine(*line);
    }
}

void GuiClient::openMessageLog() throw () {
    if (!messageLogOpen) {
        message_log.clear();    // necessary: http://gcc.gnu.org/onlinedocs/libstdc++/faq/index.html#4_4_iostreamclear
        message_log.open((whereuserdir + "log" + directory_separator + "message.log").c_str(), ios::app);
        messageLogOpen = true;
    }
}

void GuiClient::closeMessageLog() throw () {
    if (messageLogOpen) {
        message_log.close();
        messageLogOpen = false;
    }
}

void GuiClient::CB_rankingToken(string token) throw () { // callback called by rankingPassword from another thread
    if (connected) {
        BinaryBuffer<256> msg;
        msg.U8(data_registration_token);
        msg.str(token);
        client->send_message(msg);
        rankingPassword.serverProcessingToken();
    }
}

ClientInterface* ClientInterface::newClient(const ClientExternalSettings& config, const ServerExternalSettings& serverConfig, Log& clientLog, MemoryLog& externalErrorLog_) throw () {
    return new GuiClient(config, serverConfig, clientLog, externalErrorLog_);
}
