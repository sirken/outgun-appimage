/*
 *  bot_support.h
 *
 *  Copyright (C) 2008, 2009, 2010 - Niko Ritari
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

#ifndef BOT_SUPPORT_H_INC
#define BOT_SUPPORT_H_INC

#include "map_with_helpers.h"
#include "pointervector.h"
#include "utility.h"
#include "world.h"

enum DistanceTableId {
    Table_Main, // always built around me
    Table_Def,  // used for calculating defense needs
    Table_Destination, // always built around chosen destination
    Table_Max
};

class AreaMap {
    class RoomAreaMap;
    ControlledPtr<RoomAreaMap> splitRoom(const Map& map, const RoomCoords& room) throw ();

public:
    AreaMap() throw() { }
    explicit AreaMap(const Map& map) throw () { initialize(map); }
    AreaMap(const AreaMap& o) throw () { *this = o; }
    AreaMap& operator=(const AreaMap& o) throw ();

    void initialize(const Map& map) throw ();

    class Area {
    public:
        RoomCoords room;

        int distance[Table_Max];

        double respawnFrequency[2]; // if specific respawn areas are set, respawnFrequencies for each team sum to 1; else all are 0
        double respawnValue[2]; // respawn frequency smoothed out by bleeding to neighboring areas

        struct Neighbor {
            enum Direction { Up, Down, Left, Right };

            Area* area;
            Direction direction;
            std::vector< std::pair<double, double> > doors; // each door a pair of min-coord, max-coord

            Neighbor(Direction dir, double door0, double door1) throw () : area(0), direction(dir) { doors.push_back(std::make_pair(door0, door1)); }

            int dx() const throw () { return direction == Left ? -1 : direction == Right ? +1 : 0; }
            int dy() const throw () { return direction == Up   ? -1 : direction == Down  ? +1 : 0; }
        };

        bool operator==(const Area& o) const throw () { return this == &o; } // the Areas are created so that there is exactly one instance per "physical" area
        bool operator!=(const Area& o) const throw () { return this != &o; }

        const std::vector<Neighbor>& neighbors() const throw () { return n; }
        const std::vector<Area*>& reverseNeighbors() const throw () { return rn; }

    private:
        std::vector<Neighbor> n; // all neighbors in the same direction must be stored in sequence
        std::vector<Area*> rn; /// all areas that have this as a neighbor

        Area(const RoomCoords& room_) throw ();

        friend ControlledPtr<RoomAreaMap> AreaMap::splitRoom(const Map& map, const RoomCoords& room) throw ();
        friend void AreaMap::initialize(const Map& map) throw ();
        friend AreaMap& AreaMap::operator=(const AreaMap& o) throw ();
    };

          Area* identifyArea(const WorldCoords& pos)       throw ();
    const Area* identifyArea(const WorldCoords& pos) const throw ();

    void clearDistanceTable(DistanceTableId num) throw ();

private:
    class RoomAreaMap : private NoCopying {
        class AreaMapLevel : private NoCopying {
        public:
            virtual ~AreaMapLevel() throw () { }
            virtual AreaMapLevel* clone(const std::map<const Area*, Area*>& areaTranslation) const throw () = 0;

            virtual Area* find(const Coords& pos) const throw () = 0;
        };

        class SingleArea : public AreaMapLevel {
            Area* a;

        public:
            SingleArea(Area* area) throw () : a(area) { nAssert(area); }
            SingleArea* clone(const std::map<const Area*, Area*>& areaTranslation) const throw () { return new SingleArea(map_get_assert(areaTranslation, a)); }

            Area* find(const Coords&) const throw () { return a; }
        };

        class SplitByX : public AreaMapLevel {
            AreaMapLevel* l, * r;
            double borderX;

        public:
            SplitByX(AreaMapLevel* left, AreaMapLevel* right, double borderX_) throw () : l(left), r(right), borderX(borderX_) { }
            ~SplitByX() throw () { delete l; delete r; }
            SplitByX* clone(const std::map<const Area*, Area*>& areaTranslation) const throw () { return new SplitByX(l->clone(areaTranslation), r->clone(areaTranslation), borderX); }

            Area* find(const Coords& pos) const throw () { return (pos.x < borderX ? l : r)->find(pos); }
        };

        class SplitByY : public AreaMapLevel {
            AreaMapLevel* t, * b;
            double borderY;

        public:
            SplitByY(AreaMapLevel* top, AreaMapLevel* bottom, double borderY_) throw () : t(top), b(bottom), borderY(borderY_) { }
            ~SplitByY() throw () { delete t; delete b; }
            SplitByY* clone(const std::map<const Area*, Area*>& areaTranslation) const throw () { return new SplitByY(t->clone(areaTranslation), b->clone(areaTranslation), borderY); }

            Area* find(const Coords& pos) const throw () { return (pos.y < borderY ? t : b)->find(pos); }
        };

        class AreaSplitter {
            unsigned bestValue;
            bool bestIsX;
            int bestPoint;

            std::vector<int> count; // counts of cells of each area within the box
            unsigned nActualAreas; // how many non-zero counts

            AreaMapLevel* result;

            void testPoint(bool axisIsX, int point, unsigned value) throw ();

            void testAxisPoints(const std::vector<int>& minimums, const std::vector<int>& maximums, int& minValue, int& maxValue, bool axisIsX) throw (); // fills in minValue and maxValue as well as tests the points

        public:
            AreaSplitter(const std::vector< std::vector<int> >& roomMap, const std::vector<Area*>& roomAreas, int x0, int y0, int x1, int y1) throw ();

            AreaMapLevel* operator()() const throw () { return result; }
        };

        AreaMapLevel* topLevel;

        explicit RoomAreaMap(AreaMapLevel* tl) throw () : topLevel(tl) { }

    public:
        RoomAreaMap(const std::vector< std::vector<int> >& roomMap, const std::vector<Area*>& roomAreas) throw ();
        ~RoomAreaMap() throw () { delete topLevel; }
        RoomAreaMap* clone(const std::map<const Area*, Area*>& areaTranslation) const throw () { return new RoomAreaMap(topLevel->clone(areaTranslation)); }

        Area* identifyArea(const Coords& pos) const throw () { return topLevel->find(pos); }
    };

    PointerVector<Area> areas;
    PointerVector< PointerVector<RoomAreaMap> > roomMaps;
};

class BotSharedDataStorage {
public:
    class MapIdentifier {
        int crc;
        std::string name;

    public:
        MapIdentifier() throw () { }
        explicit MapIdentifier(const Map& m) throw () : crc(m.crc), name(m.title) { }
        bool operator<(const MapIdentifier& o) const { return crc < o.crc || crc == o.crc && name < o.name; }
    };

    BotSharedDataStorage() : mutex("BotSharedDataStorage::mutex") { }
    ~BotSharedDataStorage() throw () { nAssert(maps.empty()); }
    const AreaMap& acquire(const Map& m) throw ();
    void release(const MapIdentifier& mid) throw ();

private:
    struct MapData {
        AreaMap* areas;
        int nUsers; // number of bots having an active copy of this data; the data is kept as long as there are any, as there might come new users as well
    };
    std::map<MapIdentifier, MapData> maps;
    Mutex mutex;
};

class BotSharedDataHandle : private NoCopying {
    BotSharedDataStorage& storage;

    BotSharedDataStorage::MapIdentifier mid;
    bool acquired;

public:
    BotSharedDataHandle(BotSharedDataStorage& s) throw () : storage(s), acquired(false) { }
    ~BotSharedDataHandle() throw () { release(); }

    const AreaMap& acquire(const Map& m) throw () { release(); acquired = true; mid = BotSharedDataStorage::MapIdentifier(m); return storage.acquire(m); }
    void release() throw () { if (acquired) { storage.release(mid); acquired = false; } }
};

#endif
