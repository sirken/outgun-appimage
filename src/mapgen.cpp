/*
 *  mapgen.cpp
 *
 *  Copyright (C) 2008, 2010 - Jani Rivinoja
 *  Copyright (C) 2010 - Niko Ritari
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
#include <iostream>
#include <string>
#include <vector>

#include <cstdlib>

#include "platform.h"
#include "commont.h"
#include "nassert.h"
#include "utility.h"
#include "version.h"

#include "mapgen.h"

using std::ifstream;
using std::ios;
using std::max;
using std::min;
using std::ostream;
using std::string;
using std::vector;

void MapGenerator::SimpleRoom::add_respawn(int team) {
    nAssert(team >= 0 && team <= 2);
    if (respawn == -1)
        respawn = team;
    else if (respawn == 1 - team)
        respawn = 2;
}

int MapGenerator::generate(int w, int h, bool allow_over_edge, bool respawn_area, float repetitive_respawn, bool green_flag, bool create_asymmetric) throw () {
    over_edge = allow_over_edge;
    room.clear();
    room.resize(w);
    for (vector<vector<SimpleRoom> >::iterator vi = room.begin(); vi != room.end(); vi++)
        *vi = vector<SimpleRoom>(h, true);

    if (green_flag && !create_asymmetric && w % 2 == 0 && h % 2 == 0) // no green flag if it can't be symmetrical
        green_flag = false;

    if (create_asymmetric)
        symmetry = asymmetric;
    else if (green_flag) {
        if (w % 2 == 0)
            symmetry = vertical;
        else if (h % 2 == 0)
            symmetry = horizontal;
        else {
            if (rand() % 5 == 0) // rotational maps only have the center room available for the green flag, which can be boring
                symmetry = rotational;
            else
                symmetry = rand() % 2 ? horizontal : vertical;
        }
    }
    else {
        if (rand() % 4)
            symmetry = rotational;
        else
            do {
                symmetry = Symmetry(rand() % 3 + 1);
            } while (symmetry == vertical && h == 1 && w > 1 || symmetry == horizontal && w == 1 && h > 1);
    }

    int rx = rand() % width(), ry = rand() % height();
    room[rx][ry].visited = true;
    int visited_rooms = 1;
    while (visited_rooms < width() * height()) {
        SimpleRoom& current_room = room[rx][ry];
        if (  (!over_edge && ry == 0 || room[rx][(ry + height() - 1) % height()].visited) &&
              (!over_edge && ry == height() - 1 || room[rx][(ry + 1) % height()].visited) &&
              (!over_edge && rx == 0 || room[(rx + width() - 1) % width()][ry].visited) &&
              (!over_edge && rx == width() - 1 || room[(rx + 1) % width()][ry].visited)) {
            current_room.checked_through = true;
            while (1) {
                rx = rand() % width();
                ry = rand() % height();
                if (room[rx][ry].visited && !room[rx][ry].checked_through)
                    break;
            }
            continue;
        }
        while (1) {
            const int dir = rand() % 4;
            int dx = 0, dy = 0;
            switch (dir) {
            /*break;*/ case up:    dy = -1;
                break; case down:  dy = +1;
                break; case left:  dx = -1;
                break; case right: dx = +1;
            }
            if (remove_wall(rx, ry, dx, dy, visited_rooms))
                break;
        }
    }

    shift_rooms_if_needed();

    // flags
    int x1, y1, x2, y2;
    int dist;
    if (symmetry == asymmetric) {
        const BasePair bases = select_asymmetric_bases();
        x1 = bases.first.x;
        y1 = bases.first.y;
        x2 = bases.second.x;
        y2 = bases.second.y;
        dist = bases.first.dist;
    }
    else {
        const DistRoom base = select_base();
        x1 = base.x;
        y1 = base.y;
        x2 = symmetry == vertical   ? x1 : width()  - 1 - x1;
        y2 = symmetry == horizontal ? y1 : height() - 1 - y1;
        dist = base.dist;
    }
    room[x1][y1].team_flag = true;
    room[x2][y2].team_flag = true;
    if (x1 == x2 && y1 == y2)
        flags = 1;
    else
        flags = 2;

    if (green_flag && flags == 2) { // Try to add a green flag.
        DistRoom green;
        if (symmetry == asymmetric)
            green = select_asymmetric_green_base(RoomCoords(x1, y1), RoomCoords(x2, y2));
        else
            green = select_green_flag_base(x1, y1);
        if (green) {
            SimpleRoom& green_base = room[green.x][green.y];
            green_base.green_flag = true;
            flags++;
        }
    }

    // respawn areas
    if (respawn_area)
        do {
            const RoomCoords red(rand() % w, rand() % h);
            RoomCoords blue;
            if (symmetry == asymmetric)
                blue = RoomCoords(rand() % w, rand() % h);
            else {
                blue.x = symmetry == vertical   ? red.x : width()  - 1 - red.x;
                blue.y = symmetry == horizontal ? red.y : height() - 1 - red.y;
            }
            room[red.x][red.y].add_respawn(0);
            room[blue.x][blue.y].add_respawn(1);
        } while (rand() % 1000 < 1000 * repetitive_respawn);

    return dist;
}

bool MapGenerator::remove_wall(int rx, int ry, int dx, int dy, int& visited_rooms, bool mirror) throw () {
    if (dx == dy)
        return false;
    const int nx = rx + dx, ny = ry + dy;
    if (!over_edge && (nx < 0 || nx >= width() || ny < 0 || ny >= height()))
        return false;
    SimpleRoom& next = room[(nx + width()) % width()][(ny + height()) % height()];
    SimpleRoom& current = room[rx][ry];
    if (!mirror && next.visited && symmetry != asymmetric)
        return false;
    if (!mirror && !next.visited) {
        visited_rooms++;
        next.visited = true;
    }
    if (dy < 0)
        current.top = next.bottom = false;
    else if (dy > 0)
        current.bottom = next.top = false;
    else if (dx < 0)
        current.left = next.right = false;
    else if (dx > 0)
        current.right = next.left = false;
    if (symmetry != asymmetric && !mirror) {
        const int mx1 = symmetry == vertical   ? rx : width()  - 1 - rx;
        const int my1 = symmetry == horizontal ? ry : height() - 1 - ry;
        const int mx2 = mx1 + (symmetry == vertical   ? +dx : -dx);
        const int my2 = my1 + (symmetry == horizontal ? +dy : -dy);
        remove_wall(mx1, my1, mx2 - mx1, my2 - my1, visited_rooms, true);
    }
    return true;
}

void MapGenerator::shift_rooms_if_needed() throw () {
    bool solid_top_wall = true;
    for (int x = 0; x < width(); x++)
        if (!room[x][0].top) {
            solid_top_wall = false;
            break;
        }
    bool solid_left_wall = true;
    for (int y = 0; y < height(); y++)
        if (!room[0][y].left) {
            solid_left_wall = false;
            break;
        }
    int x_shift = 0, y_shift = 0;
    if (!solid_top_wall) {
        for (int y = 0; y < height(); y++) {
            bool solid_wall = true;
            for (int x = 0; x < width(); x++)
                if (!room[x][y].top) {
                    solid_wall = false;
                    break;
                }
            if (solid_wall) {
                y_shift = y;
                break;
            }
        }
    }
    if (!solid_left_wall) {
        for (int x = 0; x < width(); x++) {
            bool solid_wall = true;
            for (int y = 0; y < height(); y++)
                if (!room[x][y].left) {
                    solid_wall = false;
                    break;
                }
            if (solid_wall) {
                x_shift = x;
                break;
            }
        }
    }
    //std::cout << "shift (" << x_shift << ", " << y_shift << ")\n";
    shift_rooms(x_shift, y_shift);
}

void MapGenerator::shift_rooms(int x_shift, int y_shift) throw () {
    if (x_shift == 0 && y_shift == 0)
        return;
    vector<vector<SimpleRoom> > temp_room;
    temp_room.resize(width());
    for (vector<vector<SimpleRoom> >::iterator vi = temp_room.begin(); vi != temp_room.end(); vi++)
        *vi = vector<SimpleRoom>(height());
    for (int y = 0; y < height(); y++)
        for (int x = 0; x < width(); x++)
            temp_room[x][y] = room[(x + x_shift) % width()][(y + y_shift) % height()];
    room = temp_room;
}

void MapGenerator::draw(ostream& out) const throw () {
    for (int y = 0; y < height(); y++)
        for (int i = 0; i < 3; i++) {
            if (i == 0 && y > 0)
                continue;
            for (int x = 0; x < width(); x++) {
                const SimpleRoom& current = room[x][y];
                if (i == 1) {
                    if (x == 0)
                        if (current.left)
                            out << '|';
                        else
                            out << ' ';
                    //out << (current.visited ? current.checked_through ? " x " : " o " : "   ");
                    if (current.team_flag)
                        out << " P ";
                    else if (current.green_flag)
                        out << " G ";
                    else
                        out << "   ";
                    out << (current.right ? '|' : ' ');
                }
                else {
                    if (x == 0)
                        out << '+';
                    if (i == 0 && current.top || i == 2 && current.bottom)
                        out << "---+";
                    else
                        out << "   +";
                }
            }
            out << '\n';
        }
}

MapGenerator::DistRoom MapGenerator::select_base() const throw () {
    return select_base(true, 0, 0);
}

MapGenerator::DistRoom MapGenerator::select_green_flag_base(int team_flag_x, int team_flag_y) const throw () {
    // Find a base for the green flag. It must be in the same distance from both 
    // the team bases, symmetrically. That means that the green flags can only 
    // be in the centre line of the map, unless the map is asymmetric.
    if ((symmetry == horizontal || symmetry == rotational) && width() % 2 == 0 ||
        (symmetry == vertical   || symmetry == rotational) && height() % 2 == 0)
            return DistRoom::invalid();
    return select_base(false, team_flag_x, team_flag_y);
}

MapGenerator::DistRoom MapGenerator::select_base(bool team_base, int team_flag_x, int team_flag_y) const throw () {
    vector<DistRoom> candidates;
    int max_dist = 0;
    for (int y = 0; y < height(); y++)
        for (int x = 0; x < width(); x++) {
            if (team_base) {
                team_flag_x = symmetry == vertical   ? x : width()  - 1 - x;
                team_flag_y = symmetry == horizontal ? y : height() - 1 - y;
            }
            else if (room[x][y].team_flag ||
                    (symmetry == horizontal || symmetry == rotational) && x != width() / 2 ||
                    (symmetry == vertical   || symmetry == rotational) && y != height() / 2)
                continue;
            const int dist = distance(x, y, team_flag_x, team_flag_y);
            DistRoom d(x, y, dist);
            candidates.push_back(d);
            if (dist > max_dist)
                max_dist = dist;
        }
    int min_dist;
    if (team_base)
        min_dist = min(max_dist, max(8 * max_dist / 10, 3));
    else
        min_dist = max_dist;
    for (vector<DistRoom>::iterator di = candidates.begin(); di != candidates.end(); )
        if (di->dist < min_dist)
            di = candidates.erase(di);
        else
            ++di;
    nAssert(!candidates.empty());
    return candidates[rand() % candidates.size()];
}

MapGenerator::BasePair MapGenerator::select_asymmetric_bases() const throw () {
    // Select the rooms that are farthest apart each other.
    vector<BasePair> candidates;
    int max_dist = 0;
    for (int start_y = 0; start_y < height(); start_y++)
        for (int start_x = 0; start_x < width(); start_x++)
            for (int end_y = start_y; end_y < height(); end_y++)
                for (int end_x = start_x; end_x < width(); end_x++) {
                    const int dist = distance(start_x, start_y, end_x, end_y);
                    if (dist >= max_dist) {
                        if (dist > max_dist) {
                            candidates.clear();
                            max_dist = dist;
                        }
                        BasePair bases;
                        bases.first = DistRoom(start_x, start_y, dist);
                        bases.second = DistRoom(end_x, end_y, dist);
                        candidates.push_back(bases);
                    }
                }
    nAssert(!candidates.empty());
    return candidates[rand() % candidates.size()];
}

MapGenerator::DistRoom MapGenerator::select_asymmetric_green_base(const RoomCoords& red_base, const RoomCoords& blue_base) const throw () {
    // Select a room that is as far as possible and at the same distance from
    // both the team bases. That is not always possible and then there won't be
    // a green flag.
    vector<DistRoom> candidates;
    int max_dist = 0;
    for (int y = 0; y < height(); y++)
        for (int x = 0; x < width(); x++) {
            if (room[x][y].team_flag)
                continue;
            const int red_dist = distance(x, y, red_base.x, red_base.y);
            if (red_dist < max_dist)
                continue;
            const int blue_dist = distance(x, y, blue_base.x, blue_base.y);
            if (blue_dist < max_dist || blue_dist != red_dist)
                continue;
            if (red_dist > max_dist) {
                candidates.clear();
                max_dist = red_dist;
            }
            DistRoom d(x, y, red_dist);
            candidates.push_back(d);
        }
    if (candidates.empty())
        return DistRoom::invalid();
    return candidates[rand() % candidates.size()];
}

int MapGenerator::distance(int sx, int sy, int gx, int gy) const throw () {
    if (gx == sx && gy == sy)
        return 0;

    vector<vector<Node> > node(width(), vector<Node>(height()));
    node[sx][sy].cost = 0;
    node[sx][sy].score = node[sx][sy].cost + abs(gx - sx) + abs(gy - sy);

    const RoomCoords target(gx, gy);
    vector<RoomCoords> open;
    open.push_back(RoomCoords(sx, sy));
    while (!open.empty()) {
        const RoomCoords current = find_best(node, open);
        open.erase(find(open.begin(), open.end(), current));
        const int rx = current.x, ry = current.y;
        if (current == target)
            return node[rx][ry].cost;
        const int cost = node[rx][ry].cost + 1;
        for (int i = 0; i < 4; i++) {
            int dx = 0, dy = 0;
            switch (i) {
            /*break;*/ case up:
                    if (room[rx][ry].top)
                        continue;
                    dy = -1;
                break; case down:
                    if (room[rx][ry].bottom)
                        continue;
                    dy = +1;
                break; case left:
                    if (room[rx][ry].left)
                        continue;
                    dx = -1;
                break; case right:
                    if (room[rx][ry].right)
                        continue;
                    dx = +1;
            }
            const int nx = (rx + width() + dx) % width();
            const int ny = (ry + height() + dy) % height();
            const RoomCoords next_room(nx, ny);
            Node& neighbour = node[nx][ny];
            if (cost >= neighbour.cost)     // worse route
                continue;
            neighbour.cost = cost;
            int min_dx = abs(gx - nx);
            int min_dy = abs(gy - ny);
            if (over_edge) {
                if (min_dx > width() / 2)
                    min_dx = width() - min_dx;
                if (min_dy > height() / 2)
                    min_dy = height() - min_dy;
            }
            neighbour.score = neighbour.cost + min_dx + min_dy;
            if (find(open.begin(), open.end(), next_room) == open.end())
                open.push_back(next_room);
        }
    }
    return 0;
}

const RoomCoords& MapGenerator::find_best(const vector<vector<Node> >& node, const vector<RoomCoords>& open) const throw () {
    int score = INT_MAX;
    const RoomCoords* best_node = 0;
    for (vector<RoomCoords>::const_iterator ni = open.begin(); ni != open.end(); ++ni)
        if (node[ni->x][ni->y].score < score) {
            score = node[ni->x][ni->y].score;
            best_node = &*ni;
        }
    nAssert(best_node);
    return *best_node;
}

void MapGenerator::save_map(ostream& out, const string& title, const string& author) const throw () {
    out << "; This map is generated by Outgun " << getVersionString() << ".\n";
    out << "; " << date_and_time() << '\n';
    out << "P width " << width() << '\n';
    out << "P height " << height() << '\n';
    out << "P title " << title << '\n';
    out << "P author " << author << '\n';
    out << "S 16 12" << '\n';
    out << "X room 0 0 " << width() - 1 << ' ' << height() - 1 << '\n';
    int flag_team = rand() % 2;
    // For clarity, use a single floor texture for all shared respawn rooms.
    // 1 and 2 alternative floors, 5 ice, 6 sand, 7 mud
    int common_respawn_texture = rand() % 5 + 1;
    if (common_respawn_texture > 2)
        common_respawn_texture += 2;
    for (int y = 0; y < height(); y++)
        for (int x = 0; x < width(); x++) {
            const SimpleRoom& current = room[x][y];
            if (current.team_flag) {
                out << "flag " << (flags == 1 ? 2 : flag_team) << ' ' << x << ' ' << y << " 8 6\n";
                flag_team = 1 - flag_team;
            }
            else if (current.green_flag)
                out << "flag 2 " << x << ' ' << y << " 8 6\n";
            vector<string> walls;
            if (current.top)
                walls.push_back("top");
            if (current.bottom)
                walls.push_back("bottom");
            if (current.left)
                walls.push_back("left");
            if (current.right)
                walls.push_back("right");
            for (vector<string>::const_iterator wi = walls.begin(); wi != walls.end(); ++wi)
                out << "X " << *wi << ' ' << x << ' ' << y << '\n';
            if (current.respawn != -1) {
                out << "V respawn " << current.respawn << ' ' << x << ' ' << y << "\n";
                const int floor = current.respawn == 2 ? common_respawn_texture : current.respawn + 3; // 3 red floor, 4 blue floor
                out << "R " << x << ' ' << y << "\nG 0 0 16 12 " << floor << "\n";
            }
        }
    vector<string> files;
    const string dir = wheregamedir + "mapgen" + directory_separator;
    FileFinder* finder = platMakeFileFinder(dir, ".txt", false);
    string filename;
    for (int files = 1; finder->hasNext(); files++)
        if (rand() % files == 0)
            filename = finder->next();
        else
            finder->next();
    delete finder;
    if (filename.empty()) {
        out << ":room\nW 0 0 5 1\nW 16 0 11 1\nW 0 1 1 4\nW 16 1 15 4\nW 0 12 5 11\nW 16 12 11 11\nW 0 11 1 8\nW 16 11 15 8\n";
        out << ":top\nW 5 0 11 1\n";
        out << ":bottom\nW 5 12 11 11\n";
        out << ":left\nW 0 4 1 8\n";
        out << ":right\nW 16 4 15 8\n";
    }
    else {
        ifstream in((dir + filename).c_str(), ios::binary);
        std::copy(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>(), std::ostreambuf_iterator<char>(out));
    }
}
