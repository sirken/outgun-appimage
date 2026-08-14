/*
 *  mapeditor_doc.cpp
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

#include <ostream>

#include "mapeditor_doc.h"

#include "commont.h"    // plw, plh, PLAYER_RADIUS
#include "nassert.h"
#include "world.h"

using std::ostream;
using std::string;
using std::vector;

// -- EditorWall subtypes: text export --
//
// Every optional field the map-text grammar allows (texture, alpha, circle inner radius/angles)
// is always written out explicitly here, never omitted, even when it equals the format's default.
// Map::parse_line (world.cpp) treats an explicit value equal to the default identically to
// omitting it, so this loses nothing and avoids needing to duplicate the grammar's default rules
// here.

void EditorRectWall::exportLine(ostream& out, char kind) const throw () {
    out << kind << ' ' << x1 << ' ' << y1 << ' ' << x2 << ' ' << y2 << ' ' << texture << ' ' << alpha << '\n';
}

void EditorTriWall::exportLine(ostream& out, char kind) const throw () {
    out << "T " << kind << ' ' << x1 << ' ' << y1 << ' ' << x2 << ' ' << y2 << ' ' << x3 << ' ' << y3 << ' ' << texture << ' ' << alpha << '\n';
}

void EditorCircWall::exportLine(ostream& out, char kind) const throw () {
    out << "C " << kind << ' ' << x << ' ' << y << ' ' << radiusOuter << ' ' << radiusInner << ' ' << angle1 << ' ' << angle2 << ' ' << texture << ' ' << alpha << '\n';
}

// -- EditorRoom: owning deep-copy semantics for its wall/ground lists --

EditorRoom::~EditorRoom() throw () {
    for (vector<EditorWall*>::iterator i = walls.begin(); i != walls.end(); ++i)
        delete *i;
    for (vector<EditorWall*>::iterator i = ground.begin(); i != ground.end(); ++i)
        delete *i;
}

EditorRoom& EditorRoom::operator=(const EditorRoom& op) throw () {
    if (this == &op)
        return *this;
    for (vector<EditorWall*>::iterator i = walls.begin(); i != walls.end(); ++i)
        delete *i;
    for (vector<EditorWall*>::iterator i = ground.begin(); i != ground.end(); ++i)
        delete *i;
    walls.clear();
    ground.clear();
    for (vector<EditorWall*>::const_iterator i = op.walls.begin(); i != op.walls.end(); ++i)
        walls.push_back((*i)->clone());
    for (vector<EditorWall*>::const_iterator i = op.ground.begin(); i != op.ground.end(); ++i)
        ground.push_back((*i)->clone());
    return *this;
}

// -- EditorMap::importFrom: rebuild this document from an already-parsed, valid Map --
//
// Dispatches on the source WallBase's runtime type via dynamic_cast, mirroring the existing
// pattern in Room::operator= (world.cpp) and Graphics::draw_wall (graphics.cpp) -- world.h's
// WallBase hierarchy has no clone()/visitor of its own to hook into instead.

static void importWallList(const vector<WallBase*>& src, EditorRoom& dst, bool asWall) throw () {
    for (vector<WallBase*>::const_iterator i = src.begin(); i != src.end(); ++i) {
        const WallBase* w = *i;
        EditorWall* ew;
        if (const RectWall* rw = dynamic_cast<const RectWall*>(w))
            ew = new EditorRectWall(rw->x1(), rw->y1(), rw->x2(), rw->y2(), rw->texture(), rw->alpha());
        else if (const TriWall* tw = dynamic_cast<const TriWall*>(w))
            ew = new EditorTriWall(tw->point1().x, tw->point1().y, tw->point2().x, tw->point2().y, tw->point3().x, tw->point3().y, tw->texture(), tw->alpha());
        else if (const CircWall* cw = dynamic_cast<const CircWall*>(w))
            ew = new EditorCircWall(cw->center().x, cw->center().y, cw->radius(), cw->radius_in(), cw->angles()[0], cw->angles()[1], cw->texture(), cw->alpha());
        else {
            nAssert(0);
            continue;
        }
        if (asWall)
            dst.addWall(ew);
        else
            dst.addGround(ew);
    }
}

static EditorMapArea pointArea(const WorldCoords& p) throw () {
    EditorMapArea a;
    a.roomX = p.room.x;
    a.roomY = p.room.y;
    a.x1 = a.x2 = p.x;
    a.y1 = a.y2 = p.y;
    a.isPoint = true;
    return a;
}

// Undoes the +-PLAYER_RADIUS padding Map::parse_line applies to a rectangle-form "V respawn"
// area on load (see world.cpp), or recognizes an unpadded point-form entry -- see the comment on
// EditorMapArea::isPoint in mapeditor_doc.h for why this distinction has to be preserved.
static EditorMapArea respawnArea(const WorldRect& r) throw () {
    EditorMapArea a;
    a.roomX = r.room.x;
    a.roomY = r.room.y;
    if (r.x1 == r.x2 && r.y1 == r.y2) {
        a.isPoint = true;
        a.x1 = a.x2 = r.x1;
        a.y1 = a.y2 = r.y1;
    }
    else {
        a.isPoint = false;
        a.x1 = r.x1 - PLAYER_RADIUS;
        a.y1 = r.y1 - PLAYER_RADIUS;
        a.x2 = r.x2 + PLAYER_RADIUS;
        a.y2 = r.y2 + PLAYER_RADIUS;
    }
    return a;
}

void EditorMap::importFrom(const Map& map) throw () {
    title = map.title;
    author = map.author;
    width = map.w;
    height = map.h;

    rooms.assign(width, vector<EditorRoom>(height));
    for (int x = 0; x < width; ++x)
        for (int y = 0; y < height; ++y) {
            const Room& src = map[RoomCoords(x, y)];
            EditorRoom& dst = rooms[x][y];
            importWallList(src.readWalls(), dst, true);
            importWallList(src.readGround(), dst, false);
        }

    for (int t = 0; t < 2; ++t) {
        team[t].flags.clear();
        for (vector<WorldCoords>::const_iterator i = map.tinfo[t].flags.begin(); i != map.tinfo[t].flags.end(); ++i)
            team[t].flags.push_back(pointArea(*i));
        team[t].spawns.clear();
        for (vector<WorldCoords>::const_iterator i = map.tinfo[t].spawn.begin(); i != map.tinfo[t].spawn.end(); ++i)
            team[t].spawns.push_back(pointArea(*i));
        team[t].respawnAreas.clear();
        for (vector<WorldRect>::const_iterator i = map.tinfo[t].respawn.begin(); i != map.tinfo[t].respawn.end(); ++i)
            team[t].respawnAreas.push_back(respawnArea(*i));
    }

    wildFlags.clear();
    for (vector<WorldCoords>::const_iterator i = map.wild_flags.begin(); i != map.wild_flags.end(); ++i)
        wildFlags.push_back(pointArea(*i));
}

void EditorMap::initBlank(int width_, int height_, const string& title_, const string& author_) throw () {
    title = title_;
    author = author_;
    width = width_;
    height = height_;
    rooms.assign(width, vector<EditorRoom>(height));
    for (int t = 0; t < 2; ++t) {
        team[t].flags.clear();
        team[t].spawns.clear();
        team[t].respawnAreas.clear();
    }
    wildFlags.clear();
}

// -- EditorMap::exportText --
//
// Always emits "S <plw> <plh>" as the map scale, then writes every coordinate as the raw internal
// engine-unit value with no conversion. This works because Map::parse_line converts a coordinate
// by "value * plw / scalex" (and the y equivalent with plh/scaley) when reading it off a line --
// setting scalex=plw and scaley=plh makes that multiplier exactly 1, i.e. the text value *is* the
// internal value, both on the way out here and on the way back in when re-parsed. Since this
// document already stores everything in that same internal unit space (see importFrom above),
// every write below is a direct, lossless (given enough printed precision) copy with no need to
// replicate the parser's unit-conversion math in reverse.
//
// Does not use the "X <label> ..." room-template-sharing mechanism (see doc/maps.html) that
// hand-authored maps often use for file compactness -- correctness doesn't depend on it, so it's
// left for a later phase if the exported file's readability/size becomes a concern.

void EditorMap::exportText(ostream& out) const throw () {
    out.precision(17); // exact round-trip for any double: see http://www.exploringbinary.com/number-of-digits-required-for-round-trip-conversions and 17 = digits10+2 for IEEE double

    out << "P width " << width << '\n';
    out << "P height " << height << '\n';
    out << "P title " << title << '\n';
    if (!author.empty())
        out << "P author " << author << '\n';
    out << "S " << plw << ' ' << plh << '\n';

    for (int x = 0; x < width; ++x)
        for (int y = 0; y < height; ++y) {
            out << "R " << x << ' ' << y << '\n';
            const EditorRoom& r = rooms[x][y];
            for (vector<EditorWall*>::const_iterator i = r.readWalls().begin(); i != r.readWalls().end(); ++i)
                (*i)->exportLine(out, 'W');
            for (vector<EditorWall*>::const_iterator i = r.readGround().begin(); i != r.readGround().end(); ++i)
                (*i)->exportLine(out, 'G');
        }

    for (int t = 0; t < 2; ++t)
        for (vector<EditorMapArea>::const_iterator i = team[t].flags.begin(); i != team[t].flags.end(); ++i)
            out << "flag " << t << ' ' << i->roomX << ' ' << i->roomY << ' ' << i->x1 << ' ' << i->y1 << '\n';
    for (vector<EditorMapArea>::const_iterator i = wildFlags.begin(); i != wildFlags.end(); ++i)
        out << "flag 2 " << i->roomX << ' ' << i->roomY << ' ' << i->x1 << ' ' << i->y1 << '\n';

    for (int t = 0; t < 2; ++t)
        for (vector<EditorMapArea>::const_iterator i = team[t].spawns.begin(); i != team[t].spawns.end(); ++i)
            out << "spawn " << t << ' ' << i->roomX << ' ' << i->roomY << ' ' << i->x1 << ' ' << i->y1 << '\n';

    // Never uses the "V respawn 2 ..." (both teams) form -- always writes each team's areas
    // separately. That form is only a text-file convenience (world.cpp's parser expands it into
    // separate tinfo[0]/tinfo[1] entries immediately); writing it out per-team instead produces
    // an identical parsed result and avoids having to detect whether two areas that happen to
    // match were originally one shared line or two coincidentally identical ones.
    for (int t = 0; t < 2; ++t)
        for (vector<EditorMapArea>::const_iterator i = team[t].respawnAreas.begin(); i != team[t].respawnAreas.end(); ++i) {
            out << "V respawn " << t << ' ' << i->roomX << ' ' << i->roomY << ' ' << i->x1 << ' ' << i->y1;
            if (!i->isPoint)
                out << ' ' << i->x2 << ' ' << i->y2;
            out << '\n';
        }
}
