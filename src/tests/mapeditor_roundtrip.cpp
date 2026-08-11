/*
 *  tests/mapeditor_roundtrip.cpp
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

/* Round-trip test for the map editor's Phase-1 document model (../mapeditor_doc.h): for every
 * shipped map plus a couple of hand-authored fixtures exercising format corners no shipped map
 * touches, load it with the real engine parser, import it into an EditorMap, export it back to
 * text, re-parse *that* with the same real engine parser, and check the two resulting Map objects
 * are structurally identical. Deliberately does not compare Map::crc -- it's computed over the
 * raw file bytes, and the re-emitted text is not expected to be byte-identical to hand-authored
 * source (different whitespace/line order/no "X" grouping), only semantically equivalent.
 */

#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "../mapeditor_doc.h"
#include "../platform.h"
#include "../world.h"

#include "tests.h"

using namespace std;

static bool nearlyEqual(double a, double b) throw () { return fabs(a - b) < 1e-6; }

static bool wallEqual(const WallBase* a, const WallBase* b, ostream& diag) throw () {
    if (a->texture() != b->texture()) {
        diag << "texture mismatch: " << a->texture() << " vs " << b->texture() << '\n';
        return false;
    }
    if (a->alpha() != b->alpha()) {
        diag << "alpha mismatch: " << a->alpha() << " vs " << b->alpha() << '\n';
        return false;
    }
    if (const RectWall* ra = dynamic_cast<const RectWall*>(a)) {
        const RectWall* rb = dynamic_cast<const RectWall*>(b);
        if (!rb) {
            diag << "type mismatch: rect vs non-rect\n";
            return false;
        }
        if (!nearlyEqual(ra->x1(), rb->x1()) || !nearlyEqual(ra->y1(), rb->y1()) ||
            !nearlyEqual(ra->x2(), rb->x2()) || !nearlyEqual(ra->y2(), rb->y2())) {
            diag << "rect coords mismatch\n";
            return false;
        }
        return true;
    }
    if (const TriWall* ta = dynamic_cast<const TriWall*>(a)) {
        const TriWall* tb = dynamic_cast<const TriWall*>(b);
        if (!tb) {
            diag << "type mismatch: tri vs non-tri\n";
            return false;
        }
        if (!nearlyEqual(ta->point1().x, tb->point1().x) || !nearlyEqual(ta->point1().y, tb->point1().y) ||
            !nearlyEqual(ta->point2().x, tb->point2().x) || !nearlyEqual(ta->point2().y, tb->point2().y) ||
            !nearlyEqual(ta->point3().x, tb->point3().x) || !nearlyEqual(ta->point3().y, tb->point3().y)) {
            diag << "tri coords mismatch\n";
            return false;
        }
        return true;
    }
    if (const CircWall* ca = dynamic_cast<const CircWall*>(a)) {
        const CircWall* cb = dynamic_cast<const CircWall*>(b);
        if (!cb) {
            diag << "type mismatch: circ vs non-circ\n";
            return false;
        }
        if (!nearlyEqual(ca->center().x, cb->center().x) || !nearlyEqual(ca->center().y, cb->center().y) ||
            !nearlyEqual(ca->radius(), cb->radius()) || !nearlyEqual(ca->radius_in(), cb->radius_in()) ||
            !nearlyEqual(ca->angles()[0], cb->angles()[0]) || !nearlyEqual(ca->angles()[1], cb->angles()[1])) {
            diag << "circ coords mismatch\n";
            return false;
        }
        return true;
    }
    nAssert(0);
    return false;
}

static bool wallListEqual(const vector<WallBase*>& a, const vector<WallBase*>& b, ostream& diag) throw () {
    if (a.size() != b.size()) {
        diag << "wall/ground list size mismatch: " << a.size() << " vs " << b.size() << '\n';
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i)
        if (!wallEqual(a[i], b[i], diag)) {
            diag << "  (at index " << i << ")\n";
            return false;
        }
    return true;
}

static bool pointsEqual(const vector<WorldCoords>& a, const vector<WorldCoords>& b, ostream& diag) throw () {
    if (a.size() != b.size()) {
        diag << "point list size mismatch: " << a.size() << " vs " << b.size() << '\n';
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].room != b[i].room || !nearlyEqual(a[i].x, b[i].x) || !nearlyEqual(a[i].y, b[i].y)) {
            diag << "point mismatch at index " << i << '\n';
            return false;
        }
    return true;
}

static bool rectsEqual(const vector<WorldRect>& a, const vector<WorldRect>& b, ostream& diag) throw () {
    if (a.size() != b.size()) {
        diag << "rect list size mismatch: " << a.size() << " vs " << b.size() << '\n';
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].room != b[i].room || !nearlyEqual(a[i].x1, b[i].x1) || !nearlyEqual(a[i].y1, b[i].y1) ||
            !nearlyEqual(a[i].x2, b[i].x2) || !nearlyEqual(a[i].y2, b[i].y2)) {
            diag << "rect mismatch at index " << i << '\n';
            return false;
        }
    return true;
}

static bool mapsEqual(const Map& a, const Map& b, ostream& diag) throw () {
    bool ok = true;
    if (a.w != b.w || a.h != b.h) {
        diag << "map size mismatch: " << a.w << 'x' << a.h << " vs " << b.w << 'x' << b.h << '\n';
        return false; // can't safely index rooms below if this differs
    }
    if (a.title != b.title) {
        diag << "title mismatch: '" << a.title << "' vs '" << b.title << "'\n";
        ok = false;
    }
    if (a.author != b.author) {
        diag << "author mismatch: '" << a.author << "' vs '" << b.author << "'\n";
        ok = false;
    }
    for (int x = 0; x < a.w; ++x)
        for (int y = 0; y < a.h; ++y) {
            const Room& ra = a[RoomCoords(x, y)];
            const Room& rb = b[RoomCoords(x, y)];
            if (!wallListEqual(ra.readWalls(), rb.readWalls(), diag)) {
                diag << "  (room " << x << ',' << y << " walls)\n";
                ok = false;
            }
            if (!wallListEqual(ra.readGround(), rb.readGround(), diag)) {
                diag << "  (room " << x << ',' << y << " ground)\n";
                ok = false;
            }
        }
    for (int t = 0; t < 2; ++t) {
        if (!pointsEqual(a.tinfo[t].flags, b.tinfo[t].flags, diag)) {
            diag << "  (team " << t << " flags)\n";
            ok = false;
        }
        if (!pointsEqual(a.tinfo[t].spawn, b.tinfo[t].spawn, diag)) {
            diag << "  (team " << t << " spawn)\n";
            ok = false;
        }
        if (!rectsEqual(a.tinfo[t].respawn, b.tinfo[t].respawn, diag)) {
            diag << "  (team " << t << " respawn)\n";
            ok = false;
        }
    }
    if (!pointsEqual(a.wild_flags, b.wild_flags, diag)) {
        diag << "  (wild flags)\n";
        ok = false;
    }
    return ok;
}

static bool roundTripFile(const string& path) throw () {
    LogSet silentLog(0, 0, 0); // discard parse errors; a parse failure is reported explicitly below instead

    ifstream in(path.c_str());
    if (!in) {
        cerr << "mapeditor_roundtrip: can't open " << path << '\n';
        return false;
    }
    Map original;
    if (!original.parse_file(silentLog, in)) {
        cerr << "mapeditor_roundtrip: FAIL (couldn't parse original) " << path << '\n';
        return false;
    }

    EditorMap doc;
    doc.importFrom(original);
    ostringstream exported;
    doc.exportText(exported);

    istringstream reimport(exported.str());
    Map roundTripped;
    if (!roundTripped.parse_file(silentLog, reimport)) {
        cerr << "mapeditor_roundtrip: FAIL (re-exported text failed to parse) " << path << "\n--- exported text ---\n" << exported.str();
        return false;
    }

    ostringstream diag;
    if (!mapsEqual(original, roundTripped, diag)) {
        cerr << "mapeditor_roundtrip: FAIL (structural mismatch) " << path << '\n' << diag.str();
        return false;
    }
    return true;
}

int main() {
    // Needed for platMakeFileFinder below (its directory scan joins path + directory_separator +
    // entry name); normally set by platInit(), which this standalone test doesn't call.
    directory_separator = '/';

    vector<string> dirs;
    dirs.push_back("../maps");
    dirs.push_back("../cmaps");
    dirs.push_back("tests/mapeditor_fixtures");

    int total = 0, failed = 0;
    for (size_t d = 0; d < dirs.size(); ++d) {
        FileFinder* finder = platMakeFileFinder(dirs[d], ".txt", false);
        while (finder->hasNext()) {
            const string path = dirs[d] + "/" + finder->next();
            ++total;
            if (!roundTripFile(path))
                ++failed;
        }
        delete finder;
    }

    nAssert(total > 0); // sanity: make sure the directory scan actually found and ran something
    if (failed > 0) {
        cerr << "mapeditor_roundtrip: " << failed << " of " << total << " maps failed round-trip.\n";
        nAssert(0);
    }
    cout << "mapeditor_roundtrip: " << total << " maps passed.\n";
    return 0;
}
