/*
 *  mapeditor_doc.h
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

#ifndef MAPEDITOR_DOC_H_INC
#define MAPEDITOR_DOC_H_INC

#include <iosfwd>
#include <string>
#include <vector>

class Map;

/* Editor-side mutable map document model: Phase 1 ("foundation") of the in-game map editor
 * feature (see TODO.md "Add an in-game map editor" and the plan file for the full multi-phase
 * design). This is importable from and exportable to the same map-text format that
 * Map::parse_file (world.cpp) reads, but -- unlike world.h's Map/Room/WallBase, which are
 * read-mostly and used directly by rendering/collision/networking -- everything here is freely
 * mutable. Nothing in this file is wired into gameplay, menus, or rendering yet; it exists to be
 * unit-tested (see tests/mapeditor_roundtrip.cpp) ahead of any editor UI work.
 *
 * All coordinates in this document are in the engine's fixed internal per-room unit space
 * (0..plw x 0..plh, see commont.h) rather than whatever "S x y" scale a source map file used --
 * see the large comment on EditorMap::exportText for why, and how that keeps every export
 * conversion-free.
 */

class EditorWall {
public:
    EditorWall() throw () : texture(0), alpha(255) { }
    EditorWall(int texture_, int alpha_) throw () : texture(texture_), alpha(alpha_) { }
    virtual ~EditorWall() throw () { }

    virtual EditorWall* clone() const throw () = 0;

    // Writes this shape's map-text-format command line to 'out', ending in '\n'. 'kind' is 'W'
    // for a wall or 'G' for a ground texture area -- that distinction is a property of which
    // EditorRoom list a wall lives in, not of the shape itself, so it's passed in rather than
    // stored.
    virtual void exportLine(std::ostream& out, char kind) const throw () = 0;

    int texture, alpha;
};

class EditorRectWall : public EditorWall {
public:
    EditorRectWall() throw () : x1(0), y1(0), x2(0), y2(0) { }
    EditorRectWall(double x1_, double y1_, double x2_, double y2_, int texture_, int alpha_) throw ()
        : EditorWall(texture_, alpha_), x1(x1_), y1(y1_), x2(x2_), y2(y2_) { }

    EditorWall* clone() const throw () { return new EditorRectWall(*this); }
    void exportLine(std::ostream& out, char kind) const throw ();

    double x1, y1, x2, y2;
};

class EditorTriWall : public EditorWall {
public:
    EditorTriWall() throw () : x1(0), y1(0), x2(0), y2(0), x3(0), y3(0) { }
    EditorTriWall(double x1_, double y1_, double x2_, double y2_, double x3_, double y3_, int texture_, int alpha_) throw ()
        : EditorWall(texture_, alpha_), x1(x1_), y1(y1_), x2(x2_), y2(y2_), x3(x3_), y3(y3_) { }

    EditorWall* clone() const throw () { return new EditorTriWall(*this); }
    void exportLine(std::ostream& out, char kind) const throw ();

    double x1, y1, x2, y2, x3, y3;
};

class EditorCircWall : public EditorWall {
public:
    EditorCircWall() throw () : x(0), y(0), radiusOuter(0), radiusInner(0), angle1(0), angle2(0) { }
    EditorCircWall(double x_, double y_, double radiusOuter_, double radiusInner_, double angle1_, double angle2_, int texture_, int alpha_) throw ()
        : EditorWall(texture_, alpha_), x(x_), y(y_), radiusOuter(radiusOuter_), radiusInner(radiusInner_), angle1(angle1_), angle2(angle2_) { }

    EditorWall* clone() const throw () { return new EditorCircWall(*this); }
    void exportLine(std::ostream& out, char kind) const throw ();

    double x, y, radiusOuter, radiusInner, angle1, angle2;
};

// A room-local point, e.g. a spawn or flag position (mirrors world.h's Coords).
struct EditorPoint {
    double x, y;
    EditorPoint() throw () : x(0), y(0) { }
    EditorPoint(double x_, double y_) throw () : x(x_), y(y_) { }
};

// A point or rectangle somewhere on the map (mirrors world.h's WorldCoords/WorldRect, which are
// kept as two separate types there but are combined into one struct here since a "V respawn"
// entry can be either, per below).
struct EditorMapArea {
    int roomX, roomY;
    double x1, y1, x2, y2; // for a point-form entry, x1==x2 && y1==y2

    // True if this came from (or should be saved as) the map format's point-form "V respawn t rx
    // ry x y" (only x1/y1 meaningful, no width/height) rather than the rectangle form "V respawn
    // t rx ry x1 y1 x2 y2". This distinction matters for round-tripping: the engine pads a
    // rectangle-form respawn area by +-PLAYER_RADIUS on load but leaves a point-form one alone
    // (see world.cpp's "V respawn" parsing) -- get it wrong and re-exporting silently grows or
    // shrinks the area. A rectangle that happens to be exactly 2*PLAYER_RADIUS wide/tall in both
    // dimensions is indistinguishable from a point after that padding is undone on import; this
    // is an inherent ambiguity in the file format itself (not introduced by this code) and is not
    // expected to occur in practice.
    bool isPoint;

    EditorMapArea() throw () : roomX(0), roomY(0), x1(0), y1(0), x2(0), y2(0), isPoint(false) { }
};

struct EditorTeamMapData {
    std::vector<EditorMapArea> flags;    // only x1,y1/roomX,roomY meaningful (always point-form)
    std::vector<EditorMapArea> spawns;   // ditto
    std::vector<EditorMapArea> respawnAreas;
};

class EditorRoom {
public:
    EditorRoom() throw () { }
    EditorRoom(const EditorRoom& o) throw () { *this = o; }
    ~EditorRoom() throw ();
    EditorRoom& operator=(const EditorRoom& o) throw ();

    void addWall(EditorWall* w) throw () { walls.push_back(w); }
    void addGround(EditorWall* w) throw () { ground.push_back(w); }

    const std::vector<EditorWall*>& readWalls() const throw () { return walls; }
    const std::vector<EditorWall*>& readGround() const throw () { return ground; }

    // In-place mutation/removal for the map editor's move/resize/texture-cycle/delete tools
    // (Phase 3). Indices refer to the same order as readWalls()/readGround(); an index only
    // remains valid until the next eraseWall/eraseGround/addWall/addGround call.
    size_t wallCount() const throw () { return walls.size(); }
    size_t groundCount() const throw () { return ground.size(); }
    EditorWall& wallAt(size_t i) throw () { return *walls[i]; }
    EditorWall& groundAt(size_t i) throw () { return *ground[i]; }
    void eraseWall(size_t i) throw () { delete walls[i]; walls.erase(walls.begin() + i); }
    void eraseGround(size_t i) throw () { delete ground[i]; ground.erase(ground.begin() + i); }

private:
    std::vector<EditorWall*> walls, ground; // owned
};

class EditorMap {
public:
    EditorMap() throw () : width(0), height(0) { }

    std::string title, author;
    int width, height; // in rooms
    std::vector<std::vector<EditorRoom> > rooms; // [x][y], matching Map::rooms

    EditorTeamMapData team[2]; // red=0, blue=1
    std::vector<EditorMapArea> wildFlags; // only x1,y1/roomX,roomY meaningful

    EditorRoom& room(int x, int y) throw () { return rooms[x][y]; }
    const EditorRoom& room(int x, int y) const throw () { return rooms[x][y]; }

    // Rebuilds this document (discarding any previous content) from an already- and validly-
    // parsed Map (see world.h / Map::parse_file). Assumes 'map' is valid, since that's the only
    // way to construct one.
    void importFrom(const Map& map) throw ();

    // Rebuilds this document (discarding any previous content) into a blank width x height room
    // grid with no walls/ground/flags/spawns/respawn areas -- the "New map" starting point (Phase
    // 3). This is a legal, exportable document: Map::parse_file only requires a non-empty title
    // and width/height != 0, nothing about walls/flags/spawns existing. 'width'/'height' are
    // assumed already validated by the caller (in range and > 0); 'title' is assumed non-empty
    // (Map::parse_file would otherwise reject the exported text, so the caller should check first
    // rather than relying on that rejection).
    void initBlank(int width, int height, const std::string& title, const std::string& author = std::string()) throw ();

    // Writes this document out in the map-text format that Map::parse_file understands.
    void exportText(std::ostream& out) const throw ();
};

#endif
