/*
 *  graphics.h
 *
 *  Copyright (C) 2002 - Fabio Reis Cecin
 *  Copyright (C) 2003, 2004, 2005, 2006, 2008 - Niko Ritari
 *  Copyright (C) 2003, 2004, 2005, 2006, 2008, 2009 - Jani Rivinoja
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

#ifndef GRAPHICS_H_INC
#define GRAPHICS_H_INC

#include <string>
#include <list>
#include <vector>

#include "incalleg.h"
#include "colour.h"
#include "mutex.h"
#include "utility.h"
#include "world.h"

// ---- client screen layout ----

class Map;
class MapInfo;
class Room;
class RectWall;
class TriWall;
class CircWall;
class ClientPlayer;
class Sounds;
class GraphicsEffect;
class Team;
class Flag;
class Powerup;
class Message;
class WorldCoords;
class Rocket;

class ScreenMode {
public:
    int width, height;

    ScreenMode(int w, int h) throw () : width(w), height(h) { }
    bool operator==(const ScreenMode& o) const throw () { return width == o.width && height == o.height; }
    bool operator<(const ScreenMode& o) const throw () { return width < o.width || (width == o.width && height < o.height); }
};

enum MapListSortKey { MLSK_Number, MLSK_Votes, MLSK_Title, MLSK_Size, MLSK_Author, MLSK_Favorite, MLSK_COUNT };

class Message {
public:
    Message(Message_type type_, const FormattedText& txt, int time_, int team_ = -1) throw ()
        : msg_type(type_), msg_text(txt), msg_time(time_), sender_team(team_), is_highlighted(false) { }

    void highlight() throw () { is_highlighted = true; }

    Message_type type() const throw () { return msg_type; }
    const FormattedText& text() const throw () { return msg_text; }
    int time() const throw () { return msg_time; }
    int team() const throw () { return sender_team; }
    bool highlighted() const throw () { return is_highlighted; }

private:
    Message_type msg_type;
    FormattedText msg_text;
    int msg_time;
    int sender_team; // -1 for non-team messages or unknown team
    bool is_highlighted;
};

// Map editor picker screen (see TODO.md "Add an in-game map editor"): one row of the left-column
// list, already flattened by the caller (GuiClient::mapEditor_pickerScreen) into group-header rows
// interleaved with the matching map entries beneath each -- Graphics only draws what it's given,
// it doesn't know about the grouping/filtering itself.
struct MapEditorPickerRow {
    bool isHeader;
    std::string text; // header caption (with a live match count) or a map's bare filename

    MapEditorPickerRow() throw () : isHeader(false) { }
    MapEditorPickerRow(bool isHeader_, const std::string& text_) throw () : isHeader(isHeader_), text(text_) { }
};

// Right-column stats for whichever map is currently highlighted.
struct MapEditorPickerStats {
    std::string title, author;
    int width, height;
    int flagsRed, flagsBlue, flagsWild;
    int spawns;

    MapEditorPickerStats() throw () : width(0), height(0), flagsRed(0), flagsBlue(0), flagsWild(0), spawns(0) { }
};

// Map editor Phase 3 (rect/point editing, see TODO.md and the plan file): room-local geometry for
// one shape, passed to Graphics for screen-space projection, hit-testing, and overlay drawing --
// Graphics doesn't know about EditorMap/EditorRoom's own types, only this plain geometry, keeping
// the document model and the renderer decoupled (guiclient.cpp extracts these from the document).
struct MapEditorOverlayRect {
    int roomX, roomY;
    double x1, y1, x2, y2;
    MapEditorOverlayRect() throw () : roomX(0), roomY(0), x1(0), y1(0), x2(0), y2(0) { }
    MapEditorOverlayRect(int roomX_, int roomY_, double x1_, double y1_, double x2_, double y2_) throw ()
        : roomX(roomX_), roomY(roomY_), x1(x1_), y1(y1_), x2(x2_), y2(y2_) { }
};
struct MapEditorOverlayPoint {
    int roomX, roomY;
    double x, y;
    MapEditorOverlayPoint() throw () : roomX(0), roomY(0), x(0), y(0) { }
    MapEditorOverlayPoint(int roomX_, int roomY_, double x_, double y_) throw () : roomX(roomX_), roomY(roomY_), x(x_), y(y_) { }
};

// Result of Graphics::mapEditorHitTest -- which candidate (if any) a screen click landed on. Rects
// are checked before points (the caller pre-orders each list -- walls before ground within rects,
// flags before spawns within points -- so "rects checked first" alone gives the full documented
// walls -> ground -> flags -> spawns tie-break).
struct MapEditorHitResult {
    enum Kind { None, Rect, Point } kind;
    int index; // index into whichever of the caller's rects/points lists matched; -1 if kind == None
    MapEditorHitResult() throw () : kind(None), index(-1) { }
};

class Graphics {
public:
    static const int
                  item_max_size_in_world = 30,
         extended_item_max_size_in_world = 60, // include some space for deathbringer smoke
                  flag_max_size_in_world = 100,
         extended_flag_max_size_in_world = 120, // include space for emphasis markers
                rocket_max_size_in_world = 2 * 2 * ROCKET_RADIUS,
       extended_rocket_max_size_in_world = 2 * (POWER_ROCKET_RADIUS + 11), // max(rocket_max_size_in_world, 2 * (POWER_ROCKET_RADIUS + 11)) (include space for shadow)
                player_max_size_in_world = 2 * 2 * PLAYER_RADIUS,
       extended_player_max_size_in_world = extended_flag_max_size_in_world; // max(extended_flag_max_size_in_world, player_max_size_in_world + 50) (include space for carried flag and deathbringer smoke)

    static const std::string save_extension; // file extension like ".pcx", depending on configured libraries (and potentially selectable by user in the future)

    Graphics(LogSet logs) throw ();
    ~Graphics() throw ();

    bool depthAvailable(int depth) const throw ();
    std::vector<ScreenMode> getResolutions(int depth, bool forceTryIfNothing = true) const throw (); // returns a sorted list of unique resolutions
    bool init(int width, int height, int depth, bool windowed, bool flipping) throw ();
    void videoMemoryCorrupted() throw ();    // call this when that happens with page flipping

    void setRoomLayout(const Map& map, double visible_rooms, bool repeatMapX, bool repeatMapY) throw (); // call before any playfield draw operation, including the playfield version of draw_background
    typedef std::vector<std::vector<uint8_t> > VisibilityMap;
    void draw_background(bool map_ready) throw ();
    void draw_background(const Map& map, const VisibilityMap& roomVis, const WorldCoords& topLeft, bool continuousTextures, bool mapInfoMode) throw ();

    void startDraw() throw ();   // call startDraw before any drawing operations are done and endDraw when done, before drawScreen
    void endDraw() throw ();
    void startPlayfieldDraw() throw ();  // between calls to startPlayfieldDraw and endPlayfieldDraw, anything is only drawn to within the playfield area
    void endPlayfieldDraw() throw ();

    void draw_screen(bool acquireWithFlipping) throw ();
    bool save_screenshot(const std::string& filename) const throw ();

    void reset_playground_colors() throw ();
    void random_playground_colors() throw ();

    void draw_flag(int team, const WorldCoords& pos, bool flash, int alpha = 255, bool emphasize = false, double timeUnknown = 0) throw ();
    void draw_mini_flag(int team, const Flag& flag, const Map& map, bool flash = false, bool old = false) throw ();
    void update_minimap_background(const Map& map) throw ();
    void draw_minimap_player(const Map& map, const ClientPlayer& player) throw ();
    void draw_minimap_me(const Map& map, const ClientPlayer& player, double time) throw ();
    void draw_minimap_room(const Map& map, int rx, int ry, float visibility) throw ();
    void highlight_minimap_rooms() throw ();

    void draw_neighbor_marker(bool flag, const WorldCoords& pos, int team, bool old = false) throw ();

    void draw_player(const WorldCoords& pos, int team, int colorId, GunDirection gundir, double hitfx, bool power, int alpha, double time) throw ();
    void draw_player_name(const std::string& name, const WorldCoords& pos, int team, bool highlight = false) throw ();
    void draw_player_dead(const ClientPlayer& player, double respawn_delay = 0.) throw ();
    void draw_me_highlight(const WorldCoords& pos, double size) throw ();
    void draw_aim(const Room& room, const WorldCoords& pos, GunDirection gundir, int aimDist, int team) throw ();

    void draw_rocket(const Rocket& rocket, bool shadow, double time) throw ();
    void draw_gun_explosion(const WorldCoords& pos, int rad, int team) throw ();
    void draw_deathbringer_smoke(const WorldCoords& pos, double time, double alpha, int team) throw ();
    void draw_deathbringer(const DeathbringerExplosion& db, double frame) throw ();

    void draw_player_health(int value) throw ();
    void draw_player_energy(int value) throw ();

    void draw_deathbringer_affected(const WorldCoords& pos, int team, int alpha) throw ();
    void draw_deathbringer_carrier_circle(const WorldCoords& pos, int alpha) throw ();
    void draw_shield(const WorldCoords& pos, int r, bool live, int alpha = 255, int team = -1, GunDirection direction = GunDirection()) throw ();

    void draw_virou_sorvete(const WorldCoords& pos, double respawn_delay = 0.) throw ();

    void draw_waiting_map_message(const std::string& caption, const std::string& map) throw ();
    void draw_loading_map_message(const std::string& text) throw ();
    void draw_scores(const std::string& text, int col, int score1, int score2) throw ();
    void print_chat_messages(std::list<Message>::const_iterator begin, const std::list<Message>::const_iterator& end,
                             const std::string& talkbuffer, int cursor_pos) throw ();

    void draw_scoreboard(const std::vector<ClientPlayer*>& players, const Team* teams, int maxplayers, bool pings = false, bool underlineMasterAuthenticated = false, bool underlineServerAuthenticated = false) throw ();

    void team_statistics(const Team* teams) throw ();
    void draw_statistics(const std::vector<ClientPlayer*>& players, int page, int time, int maxplayers, int max_world_rank = 0) throw ();
    void debug_panel(const std::vector<ClientPlayer>& players, int me, int bpsin, int bpsout,
                     const std::vector<std::vector<std::pair<int, int> > >& sticks, const std::vector<int>& buttons) throw ();

    void map_list(const std::vector< std::pair<const MapInfo*, int> >& maps, MapListSortKey sortedBy, int current = -1,
                  int own_vote = -1, const std::string& edit_vote = "") throw ();

    void map_list_prev() throw () { --map_list_start; }
    void map_list_next() throw () { ++map_list_start; }
    void map_list_prev_page() throw () { map_list_start -= map_list_size; }
    void map_list_next_page() throw () { map_list_start += map_list_size; }
    void map_list_begin() throw () { map_list_start = 0; }
    void map_list_end() throw () { map_list_start = INT_MAX; }

    void team_captures_prev() throw () { --team_captures_start; }
    void team_captures_next() throw () { ++team_captures_start; }

    void draw_player_power(double val) throw ();
    void draw_player_turbo(double val) throw ();
    void draw_player_shadow(double val) throw ();
    void draw_player_weapon(int level) throw ();
    void draw_map_time(int seconds, bool extra_time = false) throw ();
    void draw_fps(double fps) throw ();
    void draw_change_team_message(double time) throw ();
    void draw_change_map_message(double time, bool delayed = false) throw ();

    void draw_pup(const Powerup& pup, double time, bool live) throw ();

    // client side effects
    void draw_effects(double time) throw ();
    void draw_turbofx(double time) throw ();

    void clear_fx() throw ();

    void create_wallexplo(const WorldCoords& pos, int team, double time) throw ();
    void create_powerwallexplo(const WorldCoords& pos, int team, double time) throw ();
    void create_deathbringer_smoke(WorldCoords pos, int team, int alpha, double time, bool for_item = false) throw ();
    void create_turbofx(const WorldCoords& pos, int col1, int col2, GunDirection gundir, int alpha, double time) throw ();
    void create_gunexplo(const WorldCoords& pos, int team, double time) throw ();

    bool save_map_picture(const std::string& filename, const Map& map) throw ();

    // Renders a map's minimap-style preview into a caller-supplied, caller-owned bitmap at
    // whatever size the caller wants -- independent of minimap_place_w/h, which is sized for the
    // small in-game minimap corner widget. Mirrors save_map_picture()'s own approach (temporarily
    // override minimap_place_w/h around a call to the private update_minimap_background(BITMAP*,
    // ...) overload) but targets an in-memory bitmap instead of a saved file. Used by the map
    // editor's picker screen (see TODO.md "Add an in-game map editor") for its live preview panel;
    // safe to call every time the highlighted map selection changes -- no allocation happens here,
    // the buffer is entirely caller-owned.
    void update_minimap_preview(BITMAP* buffer, const Map& map) throw ();

    // How many list rows fit in the picker's left column -- callers need this to clamp their own
    // scroll-offset bookkeeping in response to Up/Down navigation *before* the next draw call, not
    // just inside it, so it's exposed as its own query rather than only being an internal detail of
    // draw_mapeditor_picker() below. Uses the exact same layout math that draw call uses.
    int mapEditorPickerVisibleRows() const throw ();

    // The size the picker's right-column preview bitmap should be created at, so it fits the
    // layout exactly (draw_mapeditor_picker() centers whatever size it's given, but a size
    // mismatch would mean visible margins or -- if too big -- the safety check in that function
    // silently skipping the blit). Uses the same layout math as the two methods above.
    void mapEditorPickerPreviewSize(int& width, int& height) const throw ();

    // Draws the map editor's two-column picker screen for one frame: the search box (already-typed
    // filterText, cursor always at the end -- this screen has no mid-string cursor positioning),
    // the left-column grouped/filtered list (rows already flattened+filtered by the caller, one
    // selectedRow highlighted, scrolled to keep it visible), and the right-column stats block +
    // preview bitmap (already rendered by the caller via update_minimap_preview -- this call only
    // blits it, it doesn't know how to render a map itself).
    void draw_mapeditor_picker(const std::string& filterText, const std::vector<MapEditorPickerRow>& rows, int selectedRow,
                                int scrollOffset, const MapEditorPickerStats& stats, BITMAP* preview) throw ();

    void search_themes(LineReceiver& dst_theme, LineReceiver& dst_bg, LineReceiver& dst_colours) const throw ();
    void select_theme(const std::string& name, const std::string& bg_dir, bool use_theme_bg, const std::string& colour_dir, bool use_theme_colours) throw ();

    void search_fonts(LineReceiver& dst_font) const throw ();
    void select_font(const std::string& file) throw ();

    void set_antialiasing(bool enable) throw () { antialiasing = enable; roomGraphicsChanged(); }

    void set_min_transp(bool enable) throw () { min_transp = enable; }

    int player_color(int index) const throw () { nAssert(index >= 0 && index <= MAX_PLAYERS / 2); return col[index]; }

    // How many lines fit on the chat area and screen.
    int chat_lines() const throw ();
    int chat_max_lines() const throw ();

    BITMAP* drawbuffer() throw () { return drawbuf; }

    // public only for Mappic
    void setColors() throw ();

    void set_stats_alpha(int alpha) throw () { stats_alpha = alpha; }

    const Colour_manager& colours() const throw () { return colour; }

    void draw_replay_info(float rate, unsigned position, unsigned length, bool stopped) throw ();
    void toggle_full_playfield() throw ();

    bool needRedrawMap() const throw () { return mapNeedsRedraw; }

    void mapChanged() throw () { roomGraphicsChanged(); mapNeedsRedraw = true; }

    double get_visible_rooms_x() const throw () { return roomLayout.visibleRoomsX(); }
    double get_visible_rooms_y() const throw () { return roomLayout.visibleRoomsY(); }

    // Inverse of scale_x/scale_y: which world position (if any) a screen pixel corresponds to.
    // Returns an "unknown" WorldCoords (see WorldCoords::unknown()) if the point is outside the
    // playfield area entirely. Needed starting with the map editor's Phase 2 (see TODO.md) --
    // nothing before it needed a screen->world direction.
    WorldCoords screenToWorld(int screenX, int screenY) const throw () { return roomLayout.screenToWorld(screenX, screenY); }

    // Small map-editor-only overlay: a cursor mark at (screenX, screenY) plus a text readout
    // (e.g. the world coordinates under it). Kept as a Graphics method, not raw Allegro calls from
    // the caller, matching how every other bit of drawbuf access lives inside this class.
    void draw_mapeditor_overlay(int screenX, int screenY, const std::string& coordText) throw ();

    // Map editor Phase 3 (see TODO.md and the plan file): editing additions to the Phase 2 viewer.
    // These stay additive siblings of draw_mapeditor_overlay above -- its signature/behavior is
    // unchanged.

    // Like screenToWorld, but the result is guaranteed to land inside 'room' (clamping the pixel to
    // that room's own on-screen rectangle first) instead of returning "unknown" or resolving into a
    // neighboring/wrapped room near an edge. Used while a mouse drag is confined to the room it
    // started in -- see mapEditor_start()'s drag state machine. If 'room' isn't on screen at all
    // right now, returns an "unknown" WorldCoords (shouldn't normally happen mid-drag).
    WorldCoords screenToWorldClampedToRoom(int screenX, int screenY, RoomCoords room) const throw () { return roomLayout.screenToWorldClampedToRoom(screenX, screenY, room); }

    // Select-tool hit-testing: which candidate (if any) a screen click landed on. 'rects' and
    // 'points' must already be caller-ordered by priority (walls before ground; flags before
    // spawns) -- rects are checked first, in order, then points, in order, giving the documented
    // walls -> ground -> flags -> spawns tie-break with no extra bookkeeping here.
    MapEditorHitResult mapEditorHitTest(int screenX, int screenY, const std::vector<MapEditorOverlayRect>& rects, const std::vector<MapEditorOverlayPoint>& points) const throw ();

    // Whether a screen click landed near one of 'rect's 4 corners (used only for the already-
    // selected shape, to decide whether a press starts a resize-corner drag instead of a move).
    // Returns -1 for no corner, else 0=(x1,y1) 1=(x2,y1) 2=(x1,y2) 3=(x2,y2).
    int mapEditorHitTestCorner(int screenX, int screenY, const MapEditorOverlayRect& rect) const throw ();

    // Draws the editing overlay for one frame: a status line (tool/texture/team/save-or-error
    // message, pre-formatted by the caller -- same "Graphics draws, guiclient formats" convention
    // as draw_mapeditor_overlay above), the rubber-band preview of an in-progress drag (at most one
    // of previewRect/previewPoint is non-NULL at a time), and an outline around the current
    // selection, if any (at most one of selectedRect/selectedPoint non-NULL). Any pointer may be
    // NULL to mean "nothing to draw there".
    void draw_mapeditor_edit_overlay(const std::string& statusLine,
                                      const MapEditorOverlayRect* previewRect, const MapEditorOverlayPoint* previewPoint,
                                      const MapEditorOverlayRect* selectedRect, const MapEditorOverlayPoint* selectedPoint) throw ();

    // Draws the "New map" dialog (width/height/title prompt shown before mapEditor_start() opens a
    // blank map -- see GuiClient::mapEditor_newMapDialog). focusField: 0=width, 1=height, 2=title.
    void draw_mapeditor_newmap_dialog(int width, int height, const std::string& title, int focusField, bool showTitleRequiredError) throw ();
    void draw_mapeditor_saveas_dialog(const std::string& title, const std::string& author, int focusField, bool showTitleRequiredError) throw ();

    // Draws the "unsaved changes" prompt shown when Escape is pressed in the editor with pending
    // edits (see GuiClient::mapEditor_confirmDiscardDialog) -- Save/Discard/Cancel, no navigable
    // field, just three direct hotkeys.
    void draw_mapeditor_confirm_discard_dialog(bool showSaveFailedError) throw ();

private:
    void unload_bitmaps() throw ();

    bool reset_video_mode(int width, int height, int depth, bool windowed, int pages = 1) throw ();

    void make_layout() throw ();

    void update_minimap_background(BITMAP* buffer, const Map& map, bool save_map_pic = false) throw ();

    void roomGraphicsChanged() throw () { background.invalidateRoomCache(); }

    void drawRoomBackground(BITMAP* roombg, const Map& map, int roomx, int roomy, int texRoomX, int texRoomY, bool mapInfoMode) throw ();

    void draw_room_ground(BITMAP* buffer, const Room& room, int x, int y, int texOffsetBaseX, int texOffsetBaseY, double scale) throw ();
    void draw_room_walls(BITMAP* buffer, const Room& room, int x, int y, int texOffsetBaseX, int texOffsetBaseY, double scale) throw ();

    static void draw_wall(BITMAP* buffer, WallBase* wall, double x, double y, int texOffsetBaseX, int texOffsetBaseY, double scale, int color, BITMAP* tex) throw ();
    static void draw_rect_wall(BITMAP* buffer, const RectWall& wall, double x0, double y0, int texOffsetBaseX, int texOffsetBaseY, double scale, int color, BITMAP* texture) throw ();
    static void draw_tri_wall (BITMAP* buffer, const TriWall & wall, double x0, double y0, int texOffsetBaseX, int texOffsetBaseY, double scale, int color, BITMAP* texture) throw ();
    static void draw_circ_wall(BITMAP* buffer, const CircWall& wall, double x0, double y0, int texOffsetBaseX, int texOffsetBaseY, double scale, int color, BITMAP* texture) throw ();

    void draw_flagpos_mark(BITMAP* roombg, int team, int flag_x, int flag_y) throw ();

    void draw_pup_shield(const WorldCoords& pos, bool live) throw ();
    void draw_pup_turbo(const WorldCoords& pos, bool live) throw ();
    void draw_pup_shadow(const WorldCoords& pos, double time) throw ();
    void draw_pup_power(const WorldCoords& pos, double time) throw ();
    void draw_pup_weapon(const WorldCoords& pos, double time) throw ();
    void draw_pup_health(const WorldCoords& pos, double time) throw ();
    void draw_pup_deathbringer(const WorldCoords& pos, double time, bool live) throw ();

    std::pair<int, int> calculate_minimap_coordinates(const Map& map, const ClientPlayer& player) const throw ();

    void draw_bar(int x, const std::string& caption, int value, int c100, int c200, int c300) throw ();
    void draw_powerup_time(int line, const std::string& caption, double val, int c) throw ();

    // Shared by mapEditorHitTest/mapEditorHitTestCorner/draw_mapeditor_edit_overlay: projects a
    // room-local point to its primary (first) on-screen pixel, or returns false if that room isn't
    // on screen at all right now. Only the primary wraparound-repeat instance is considered -- an
    // accepted simplification for the rare case of being zoomed out far enough to see the same room
    // more than once (see the plan file).
    bool projectMapEditorPoint(int roomX, int roomY, double x, double y, int& outX, int& outY) const throw ();
    void drawMapEditorRectOutline(const MapEditorOverlayRect& r, int col) throw ();

    void draw_player_statistics(const FONT* stfont, const ClientPlayer& player, int x, int y, int page, int time) throw ();

    void draw_scoreboard_name(const FONT* sbfont, const std::string& name, int x, int y, int pcol, bool underline) throw ();
    void draw_scoreboard_points(const FONT* sbfont, int points, int x, int y, int team) throw ();

    void print_chat_message(Message_type type, const FormattedText& message, int team, int x, int y, bool highlight = false) throw ();
    void print_chat_input(const std::string& message, int x, int y, int cursor) throw ();

    void print_text(const std::string& text, int x, int y, int textcol, int bgcol) throw ();
    void print_text_centre(const std::string& text, int x, int y, int textcol, int bgcol) throw ();

    void print_text_border(const std::string& text, int x, int y, int textcol, int bordercol, int bgcol) throw ();
    void print_text_border_centre(const std::string& text, int x, int y, int textcol, int bordercol, int bgcol) throw ();

    void print_text_border_check_bg(const std::string& text, int x, int y, int textcol, int bordercol, int bgcol) throw ();
    void print_text_border_centre_check_bg(const std::string& text, int x, int y, int textcol, int bordercol, int bgcol) throw ();

    void print_text_border(const std::string& text, int x, int y, int textcol, int bordercol, int bgcol, bool centring) throw ();

    void scrollbar(int x, int y, int height, int bar_y, int bar_h, int col1, int col2) throw ();

    void make_db_effect() throw ();

    BITMAP* load_bitmap(const std::string& file) const throw ();

    void load_background() throw ();
    void load_generic_pictures() throw ();
    void load_playfield_pictures() throw ();
    void reload_playfield_pictures() throw ();

    void load_floor_textures(const std::string& filename) throw ();
    void load_wall_textures(const std::string& filename) throw ();
    BITMAP* get_floor_texture(int texid) throw ();
    BITMAP* get_wall_texture(int texid) throw ();

    void load_player_sprites(const std::string& path) throw ();
    void load_shield_sprites(const std::string& path) throw ();
    void load_dead_sprites(const std::string& path) throw ();
    void load_rocket_sprites(const std::string& path) throw ();
    void load_flag_sprites(const std::string& path) throw ();
    void load_pup_sprites(const std::string& path) throw ();

    BITMAP* scale_sprite(const std::string& filename, int x, int y) const throw ();
    BITMAP* scale_alpha_sprite(const std::string& filename, int x, int y) const throw ();
    static void overlayColor(BITMAP* bmp, BITMAP* alpha, int color) throw ();    // alpha must be an 8-bit bitmap; give the color in same format as bmp
    static void combine_sprite(BITMAP* sprite, BITMAP* common, BITMAP* team, BITMAP* personal, int tcol, int pcol) throw (); // give the colors in same format as sprite

    void unload_pictures() throw ();
    void unload_generic_pictures() throw ();
    void unload_playfield_pictures() throw ();
    void unload_floor_textures() throw ();
    void unload_wall_textures() throw ();
    void unload_player_sprites() throw ();
    void unload_shield_sprites() throw ();
    void unload_dead_sprites() throw ();
    void unload_rocket_sprites() throw ();
    void unload_flag_sprites() throw ();
    void unload_pup_sprites() throw ();

    void load_font(const std::string& file) throw ();

    int scale(double value) const throw ();
    double scaled(double value) const throw ();
    int pf_scale(double value) const throw () { return roomLayout.pf_scale(value); }
    double pf_scaled(double value) const throw () { return roomLayout.pf_scaled(value); }

    class RoomLayoutManager {
        Graphics& g;

        int plx, ply;
        double playfield_scale;
        WorldCoords topLeft;
        double visible_rooms, visible_rooms_x, visible_rooms_y;
        int map_w, map_h;
        int room_w, room_h;

    public:
        RoomLayoutManager(Graphics& host) throw () : g(host) { }

        bool set(int mapWidth, int mapHeight, double visibleRooms, bool repeatMapX, bool repeatMapY) throw (); // returns true if scale has changed
        void setTopLeft(const WorldCoords& topLeftCoords) throw () { topLeft = topLeftCoords; }

        int x0() const throw () { return plx; }
        int y0() const throw () { return ply; }
        int xMax() const throw () { return plx + static_cast<int>(visible_rooms_x * room_w) - 1; }
        int yMax() const throw () { return ply + static_cast<int>(visible_rooms_y * room_h) - 1; }

        int mapWidth() const throw () { return map_w; }
        int mapHeight() const throw () { return map_h; }
        int roomWidth() const throw () { return room_w; }
        int roomHeight() const throw () { return room_h; }

        const WorldCoords& topLeftCoords() const throw () { return topLeft; }
        double visibleRoomsX() const throw () { return visible_rooms_x; }
        double visibleRoomsY() const throw () { return visible_rooms_y; }

        int pf_scale(double value) const throw ();
        double pf_scaled(double value) const throw ();

        std::vector<int> scale_x(const WorldCoords& pos) const throw () { return scale_x(pos.room.x, pos.x); }
        std::vector<int> scale_y(const WorldCoords& pos) const throw () { return scale_y(pos.room.y, pos.y); }
        std::vector<int> scale_x(int roomx, double lx) const throw ();
        std::vector<int> scale_y(int roomy, double ly) const throw ();

        std::vector<int> room_offset_x(int rx) const throw (); // returns the pixel position(s) where room rx begins
        std::vector<int> room_offset_y(int ry) const throw (); // returns the pixel position(s) where room ry begins

        double distanceFromScreenX(int rx, double lx) const throw (); // 0 = on screen, < 0 = as many rooms to the left from screen, > 0 = to the right (wrapping around map edges)
        double distanceFromScreenY(int ry, double ly) const throw ();

        bool on_screen(int rx, int ry) const throw (); // returns true if some part of the room may be on screen

        WorldCoords screenToWorld(int screenX, int screenY) const throw (); // inverse of scale_x/scale_y; see Graphics::screenToWorld
        WorldCoords screenToWorldClampedToRoom(int screenX, int screenY, RoomCoords room) const throw (); // see Graphics::screenToWorldClampedToRoom
    };
    RoomLayoutManager roomLayout;

    class ScaledCoordSet {
        const std::vector<int> vx, vy;
        CartesianProductIterator i;

    public:
        ScaledCoordSet(const WorldCoords& pos, const Graphics* g) throw () : vx(g->roomLayout.scale_x(pos)), vy(g->roomLayout.scale_y(pos)), i(vx.size(), vy.size()) { }
        ScaledCoordSet(int roomx, int roomy, double lx, double ly, const Graphics* g) throw () : vx(g->roomLayout.scale_x(roomx, lx)), vy(g->roomLayout.scale_y(roomy, ly)), i(vx.size(), vy.size()) { }
        bool next() throw () { return i.next(); }
        int x() const throw () { return vx[i.i1()]; }
        int y() const throw () { return vy[i.i2()]; }
    };

    class RoomPosSet {
        const std::vector<int> vx, vy;
        CartesianProductIterator i;

    public:
        RoomPosSet(int roomx, int roomy, const Graphics* g) throw () : vx(g->roomLayout.room_offset_x(roomx)), vy(g->roomLayout.room_offset_y(roomy)), i(vx.size(), vy.size()) { }
        bool next() throw () { return i.next(); }
        int x() const throw () { return vx[i.i1()]; }
        int y() const throw () { return vy[i.i2()]; }
    };

    class BackgroundManager {
    public:
        BackgroundManager(Graphics& host) throw () : g(host) { }

        void draw_background(BITMAP* drawbuf, bool draw_map, bool reserve_playfield) throw ();
        void draw_playfield_background(BITMAP* drawbuf, const Map& map, const VisibilityMap& roomVis, bool continuousTextures, bool mapInfoMode) throw ();

        void invalidateRoomCache() throw () { roomCache.clear(); roomCacheIndex.clear(); roomCacheMemoryBitmap.free(); cacheTimestamp = 0; }

        bool allocate(bool videoMemory, int cachePages) throw ();
        void free() throw () { roomCacheBitmap.free(); invalidateRoomCache(); }

    private:
        void allocateRoomCache(int room_w, int room_h, int minRooms, int maxRooms) throw ();

        void cacheRoom(const Map& map, int roomx, int roomy, bool continuousTextures, bool mapInfoMode) throw ();
        void drawRoom(const Map& map, int roomx, int roomy, bool continuousTextures, bool mapInfoMode, bool fogged, BITMAP* target, int tx0, int ty0) throw ();

        struct BitmapRegion {
            BITMAP* b;
            int x0, y0, w, h;

            BitmapRegion() throw () : b(0) { }
            BitmapRegion(BITMAP* bm, int x0_, int y0_, int w_, int h_) throw () : b(bm), x0(x0_), y0(y0_), w(w_), h(h_) { }

            void blitTo(BITMAP* target, int tx0, int ty0) const throw ();
        };

        class CachedRoomGfx {
            int roomx, roomy;
            BitmapRegion baseArea;
            BitmapRegion foggedArea;
            bool baseGenerated;
            mutable bool foggedGenerated;
            int fogColor;

            void generateFogged(BITMAP* target, int tx0, int ty0, bool fastFog) const throw ();

        public:
            bool locked;
            unsigned lastUse;

            CachedRoomGfx() throw () { } // uninitialized CachedRoomGfx's should be used nowhere
            CachedRoomGfx(const BitmapRegion& b1, const BitmapRegion& b2, int fogColor_) throw () :
                    roomx(0), roomy(0), baseArea(b1), foggedArea(b2), baseGenerated(false), foggedGenerated(false),
                    fogColor(fogColor_), locked(false), lastUse(0) { nAssert(baseArea.w == foggedArea.w && baseArea.h == foggedArea.h || !foggedArea.b); }

            bool used() const throw () { return baseGenerated; }
            int x() const throw () { return roomx; }
            int y() const throw () { return roomy; }

            BitmapRegion& getAreaForWriting(int roomx_, int roomy_) throw () { roomx = roomx_; roomy = roomy_; baseGenerated = true; foggedGenerated = false; locked = true; return baseArea; }

            void drawUnfogged(BITMAP* target, int tx0, int ty0) const throw () { nAssert(baseGenerated); baseArea.blitTo(target, tx0, ty0); }
            void drawFogged  (BITMAP* target, int tx0, int ty0) const throw ();
            const BitmapRegion& fogged() const throw ();
        };

        std::vector<CachedRoomGfx> roomCache;
        std::vector<std::vector<CachedRoomGfx*> > roomCacheIndex;
        unsigned cacheTimestamp;

        Graphics& g;

        Bitmap roomCacheBitmap, roomCacheMemoryBitmap; // the memory one is never from video memory, and is used (and allocated) only when enough rooms don't fit in roomCacheBitmap

        bool previousContinuousTextures, previousMapInfoMode;
        int previousRoom0x, previousRoom0y, previousXrooms, previousYrooms;
    };

    BackgroundManager background;

    std::vector<std::string> file_extensions;

    // drawing screens
    Bitmap vidpage1;
    Bitmap vidpage2;
    Bitmap backbuf;
    bool page_flipping;
    bool min_transp;

    BITMAP* drawbuf;    // main draw buffer (points to vidpage# or backbuf at a given time)
    Bitmap minibg;      // minimap draw buffer
    Bitmap minibg_fog;  // minimap with fog in every room

    int playfield_x, playfield_y; // playground area position on the screen
    int playfield_w, playfield_h;

    int scoreboard_x1, scoreboard_x2;  // scoreboard position
    int scoreboard_y1, scoreboard_y2;

    int minimap_place_x, minimap_place_y;
    int minimap_place_w, minimap_place_h;
    int minimap_x, minimap_y;
    int minimap_w, minimap_h;
    int indicators_y;
    int health_x, energy_x, pups_x, pups_val_x, weapon_x, time_x, time_y;

    bool show_chat_messages;
    bool show_scoreboard;
    bool show_minimap;

    static const int flagpos_radius = 30;
    double scr_mul; // screen size multiplier

    Bitmap bg_texture;

    std::vector<Bitmap> floor_texture;
    std::vector<Bitmap> wall_texture;

    Bitmap player_sprite_power;
    std::vector<Bitmap> pup_sprite;

    // Team specific sprites
    std::vector<Bitmap> player_sprite[2];
    Bitmap shield_sprite[2];
    Bitmap dead_sprite[2];
    Bitmap rocket_sprite[2];
    Bitmap power_rocket_sprite[2];

    Bitmap flag_sprite[3];  // red, blue and wild flag
    Bitmap flag_flash_sprite[3];

    Bitmap ice_cream;

    Bitmap db_effect;       // the darkening of the ground around the player

    FONT* default_font;
    FONT* border_font;

    int map_list_size;
    int map_list_start;

    int team_captures_start;

    std::list<GraphicsEffect> cfx;

    std::string theme_path;
    std::string bg_path;
    std::string colour_file;

    bool antialiasing;

    int stats_alpha;

    int teamcol[3];
    int teamflashcol[3]; // flag flashing colours
    int teamlcol[2];     // light colours
    int teamdcol[2];     // dark colours

    int col[MAX_PLAYERS / 2 + 1]; // player colours, one extra used for unknown colour
    Colour groundCol, wallCol;

    static const int fogOfWarMaxAlpha = 0x38, playfieldFogOfWarAlpha = 0x38;

    Colour_manager colour;

    bool mapNeedsRedraw, needReloadPlayfieldPictures;

    mutable LogSet log;
};

#endif // GRAPHICS_H_INC
