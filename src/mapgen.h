/*
 *  mapgen.h
 *
 *  Copyright (C) 2008, 2010 - Jani Rivinoja
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

#include <iostream>
#include <string>
#include <vector>

#include "world.h"

class MapGenerator {
    enum Direction { up, down, left, right };
    enum Symmetry { asymmetric, rotational, horizontal, vertical };

    class SimpleRoom {
    public:
        SimpleRoom(bool walls = false) throw () : top(walls), bottom(walls), left(walls), right(walls), visited(false), checked_through(false), team_flag(false), green_flag(false), respawn(-1), mirror(false) { }

        void add_respawn(int team);

        bool top, bottom, left, right;  // walls
        bool visited;
        bool checked_through;
        bool team_flag;
        bool green_flag;
        int respawn; // -1 no, 0 red, 1 blue, 2 both teams
        bool mirror;
    };

    class Node {
    public:
        Node() throw () : cost(INT_MAX), score(INT_MAX) { }
        int cost, score;
    };

    struct DistRoom : public RoomCoords {
        DistRoom(): dist(0) { }
        DistRoom(int x, int y, int d = 0): RoomCoords(x, y), dist(d) { }

        static DistRoom invalid() { return DistRoom(0, 0, -1); }

        operator const void*() const throw() { return dist >= 0 ? this : 0; }
        bool operator !() const throw() { return dist < 0; }

        int dist;
    };

    typedef std::pair<DistRoom, DistRoom> BasePair;

public:
    /** Generate map. Return the distance between the bases.
     */
    int generate(int w, int h, bool allow_over_edge = false, bool respawn_area = false, float repetitive_respawn = 0, bool green_flag = false, bool create_asymmetric = false) throw ();

    void draw(std::ostream& out) const throw ();
    void save_map(std::ostream& out, const std::string& title, const std::string& author) const throw ();

private:
    bool remove_wall(int rx, int ry, int dx, int dy, int& visited_rooms, bool mirror = false) throw ();

    void shift_rooms_if_needed() throw ();
    void shift_rooms(int x_shift, int y_shift) throw ();

    DistRoom select_base() const throw ();
    DistRoom select_green_flag_base(int team_flag_x, int team_flag_y) const throw ();
    DistRoom select_base(bool team_base, int team_flag_x, int team_flag_y) const throw ();
    BasePair select_asymmetric_bases() const throw ();
    DistRoom select_asymmetric_green_base(const RoomCoords& red_base, const RoomCoords& blue_base) const throw ();

    int distance(int sx, int sy, int gx, int gy) const throw ();
    const RoomCoords& find_best(const std::vector<std::vector<Node> >& node, const std::vector<RoomCoords>& open) const throw ();

    int width() const throw () { return room.size(); }
    int height() const throw () { return room.front().size(); }

    std::vector<std::vector<SimpleRoom> > room; // accessible by room[x][y]
    Symmetry symmetry;
    bool over_edge;
    int flags;
};

