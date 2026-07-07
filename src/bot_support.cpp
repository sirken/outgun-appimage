/*
 *  bot_support.cpp
 *
 *  Copyright (C) 2008, 2009, 2010, 2011 - Niko Ritari
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
#include <numeric>
#include <queue>

#include "bot_support.h"

using std::map;
using std::min;
using std::queue;
using std::vector;

const AreaMap& BotSharedDataStorage::acquire(const Map& m) throw () {
    const MapIdentifier mid(m);
    Lock ml(mutex); // the return value is still usable outside the mutex lock because the data is constant and availability is ensured with nUsers
    const map<MapIdentifier, MapData>::iterator mi = maps.find(mid);
    if (mi == maps.end()) {
        MapData md;
        md.nUsers = 1;
        md.areas = new AreaMap(m);
        maps[mid] = md;
        return *md.areas;
    }
    else {
        ++mi->second.nUsers;
        return *mi->second.areas;
    }
}

void BotSharedDataStorage::release(const MapIdentifier& mid) throw () {
    Lock ml(mutex);
    const map<MapIdentifier, MapData>::iterator mi = maps.find(mid);
    nAssert(mi != maps.end());
    if (--mi->second.nUsers == 0) {
        delete mi->second.areas;
        maps.erase(mi);
    }
}

ControlledPtr<AreaMap::RoomAreaMap> AreaMap::splitRoom(const Map& map, const RoomCoords& roomPos) throw () {
    const Room& room = map[roomPos];

    static const unsigned xPoints = 65; // keep this at 4n+1 for some n, to satisfy the assertion below (since plh = plw × 3 / 4)
    static const unsigned yPoints = (xPoints - 1) * plh / plw + 1;
    static const double pointDistance = double(plw) / (xPoints - 1);
    nAssert(plh * (xPoints - 1) == plw * (yPoints - 1)); // to ensure that points on the bottom fall at the room edge, as well as the points on the right (equal to plh / (yPoints - 1) == pointDistance but workable in integer arithmetic)

    /* To deem a point wall-free, we want there to be a player-sized path between it and
     * all non-diagonal wall-free neighbors. Generally, a clear path of width w across the
     * distance d between points A and B, is ensured if around both A and B, a radius of
     * sqrt((½w)² + (½d)²) is clear.
     * Here, d = pointDistance and w = PLAYER_RADIUS * 2. Some extra breathing room is
     * gained by adding to PLAYER_RADIUS.
     */
    const double pointRadius = sqrt(sqr(.5 * pointDistance) + sqr(1.1 * PLAYER_RADIUS));

    static const int mapWall = -1, mapUnreached = -2;
    int mapReachedCurrent = 0; // increased when starting a new area, to separate points reached by current and previous areas; in the end all non-negative points have been reached by some area
    vector< vector<int> > roomMap(xPoints, vector<int>(yPoints));

    for (unsigned ix = 0; ix < xPoints; ++ix) {
        const double x = ix * pointDistance;
        for (unsigned iy = 0; iy < yPoints; ++iy) {
            const double y = iy * pointDistance;
            roomMap[ix][iy] = room.fall_on_wall(Coords(x, y), pointRadius) ? mapWall : mapUnreached;
        }
    }

    for (unsigned nextUncheckedX = 0; nextUncheckedX < xPoints; ++nextUncheckedX)
        for (unsigned nextUncheckedY = 0; nextUncheckedY < yPoints; ++nextUncheckedY) {
            if (roomMap[nextUncheckedX][nextUncheckedY] != mapUnreached)
                continue;
            roomMap[nextUncheckedX][nextUncheckedY] = mapReachedCurrent;

            typedef BasicCoords<unsigned> PointCoords;
            queue<PointCoords> workQueue;
            workQueue.push(PointCoords(nextUncheckedX, nextUncheckedY));

            #define checkExpand(x, y) if (roomMap[x][y] == mapUnreached) { roomMap[x][y] = mapReachedCurrent; workQueue.push(PointCoords(x, y)); }

            while (!workQueue.empty()) {
                const PointCoords pc = workQueue.front();
                workQueue.pop();
                if (pc.x > 0)
                    checkExpand(pc.x - 1, pc.y);
                if (pc.x < xPoints - 1)
                    checkExpand(pc.x + 1, pc.y);
                if (pc.y > 0)
                    checkExpand(pc.x, pc.y - 1);
                if (pc.y < yPoints - 1)
                    checkExpand(pc.x, pc.y + 1);
            }

            #undef checkExpand

            ++mapReachedCurrent;
        }

    vector<Area*> roomAreas;
    unsigned nCancelledAreas = 0;
    for (int areaIndex = 0; areaIndex < mapReachedCurrent; ++areaIndex) {
        Area* const a = new Area(roomPos);
        for (int iEdge = 0; iEdge < 2; ++iEdge) {
            const unsigned xEdge = iEdge ? xPoints - 1 : 0;
            const unsigned yEdge = iEdge ? yPoints - 1 : 0;
            const Area::Neighbor::Direction xDir = iEdge ? Area::Neighbor::Right : Area::Neighbor::Left;
            const Area::Neighbor::Direction yDir = iEdge ? Area::Neighbor::Down  : Area::Neighbor::Up;
            int door0 = -1;

            #define checkDoor(iPoint, pointIsDoor, direction)           \
                {                                                       \
                    if ((pointIsDoor) && door0 == -1)                   \
                        door0 = iPoint;                                 \
                    else if (!(pointIsDoor) && door0 != -1) {           \
                        a->n.push_back(Area::Neighbor(direction, door0 * pointDistance - pointRadius, (iPoint - 1) * pointDistance + pointRadius)); \
                        door0 = -1;                                     \
                    }                                                   \
                }

            for (unsigned x = 0; x < xPoints; ++x)
                checkDoor(x, roomMap[x][yEdge] == areaIndex, yDir);
            checkDoor(xPoints, false, yDir);
            for (unsigned y = 0; y < yPoints; ++y)
                checkDoor(y, roomMap[xEdge][y] == areaIndex, xDir);
            checkDoor(yPoints, false, xDir);

            #undef checkDoor
        }

        if (a->n.empty()) { // even if this area could be entered (from a small unnoticed opening or from another room "over a wall"), there are no known exits so it is not useful for navigation and would just needlessly complicate the room area map
            ++nCancelledAreas;
            roomAreas.push_back(0); // otherwise we'd need to renumber the higher numbered cells here to keep roomAreas in sync with roomMap
            delete a;
        }
        else {
            roomAreas.push_back(a);
            areas.push_back(give_control(a));
        }
    }

    if (roomAreas.size() == nCancelledAreas) {
        roomAreas.clear();
        nCancelledAreas = 0;
        // every room still needs an area
        Area* const a = new Area(roomPos);
        areas.push_back(give_control(a));
        roomAreas.push_back(a);
        // no need to touch roomMap because RoomAreaMap will see that there's only one area
    }
    else if (nCancelledAreas)
        for (unsigned x = 0; x < xPoints; ++x)
            for (unsigned y = 0; y < yPoints; ++y)
                if (roomMap[x][y] >= 0 && !roomAreas[roomMap[x][y]])
                    roomMap[x][y] = mapWall;

    // count respawn areas
    for (int team = 0; team < 2; ++team) {
        const vector<WorldRect>& respawns = map.tinfo[team].respawn;
        for (vector<WorldRect>::const_iterator ri = respawns.begin(); ri != respawns.end(); ++ri) {
            if (ri->room != roomPos)
                continue;
            if (roomAreas.size() - nCancelledAreas == 1) {
                for (vector<Area*>::const_iterator ai = roomAreas.begin(); ai != roomAreas.end(); ++ai)
                    if (*ai)
                        (*ai)->respawnFrequency[team] += 1. / respawns.size();
            }
            else {
                const int ix1 = static_cast<int>((ri->x1 + pointDistance / 2) / pointDistance);
                const int ix2 = static_cast<int>((ri->x2 + pointDistance / 2) / pointDistance);
                const int iy1 = static_cast<int>((ri->y1 + pointDistance / 2) / pointDistance);
                const int iy2 = static_cast<int>((ri->y2 + pointDistance / 2) / pointDistance);
                nAssert(ix1 >= 0 && unsigned(ix2) < xPoints && iy1 >= 0 && unsigned(iy2) < yPoints);
                vector<int> areaPoints(roomAreas.size(), 0);
                for (int x = ix1; x <= ix2; ++x)
                    for (int y = iy1; y <= iy2; ++y)
                        if (roomMap[x][y] != mapWall)
                            ++areaPoints[roomMap[x][y]];
                const int totalPoints = std::accumulate(areaPoints.begin(), areaPoints.end(), 0);
                for (unsigned ai = 0; ai < roomAreas.size(); ++ai)
                    if (areaPoints[ai])
                        roomAreas[ai]->respawnFrequency[team] += double(areaPoints[ai]) / totalPoints / respawns.size();
            }
        }
    }

    return give_control(new RoomAreaMap(roomMap, roomAreas));
}

AreaMap::Area::Area(const RoomCoords& room_) throw () : room(room_) {
    for (int i = 0; i < Table_Max; i++)
        distance[i] = -1;
    for (int i = 0; i < 2; ++i)
        respawnFrequency[i] = respawnValue[i] = 0.;
}

void AreaMap::RoomAreaMap::AreaSplitter::testPoint(bool axisIsX, int point, unsigned value) throw () {
    nAssert(value > 0);
    if (value > bestValue) {
        bestValue = value;
        bestIsX = axisIsX;
        bestPoint = point;
    }
}

void AreaMap::RoomAreaMap::AreaSplitter::testAxisPoints(const vector<int>& minimums, const vector<int>& maximums, int& minValue, int& maxValue, bool axisIsX) throw () { // fills in minValue and maxValue as well as tests the points
    /* Example of overlapping ranges in one dimension:
     *
     * 1-1-1-1
     *     2-2-2-2-2
     *   3-3
     *             4
     *   5-5
     *  | | | |   |   <- sensible cut-points are at each non-border min/max
     *      | +   +   <- cut-points that leave an entire area on both sides are preferred
     *        |       <- ones near the middle are best
     */
    minValue = minimums[0]; maxValue = maximums[0]; // can be taken over all minimums/maximums even if count == 0, since those can't have more extreme values than relevant ones
    for (unsigned i = 1; i < minimums.size(); ++i) {
        if (minimums[i] < minValue)
            minValue = minimums[i];
        if (maximums[i] > maxValue)
            maxValue = maximums[i];
    }
    vector<int> points;
    for (unsigned i = 0; i < minimums.size(); ++i)
        if (count[i]) {
            if (minimums[i] != minValue)
                points.push_back(minimums[i]);
            if (maximums[i] != maxValue)
                points.push_back(maximums[i] + 1); // +1 because the "physical" point considered is "before" the stored value
        }
    if (minValue != maxValue)
        testPoint(axisIsX, minValue + (maxValue - minValue + 1) / 2, (maxValue - minValue) * 10);
    if (points.empty()) // all areas span an identical range
        return;
    sort(points.begin(), points.end());
    unsigned unbrokenAreasBefore = 0, unbrokenAreasAfter = nActualAreas;
    for (unsigned i = 0; i < minimums.size(); ++i)
        if (count[i] && minimums[i] == minValue) // the first point considered in the actual loop is later than minValue
            --unbrokenAreasAfter;
    int prev = -1;
    for (vector<int>::const_iterator pi = points.begin(); pi != points.end(); ++pi) {
        if (*pi == prev)
            continue;
        prev = *pi;

        // adjust to conditions at just before *pi
        for (unsigned i = 0; i < minimums.size(); ++i)
            if (count[i] && maximums[i] == *pi - 1) // these were stored as *pi, exactly because them closing just before *pi make *pi interesting
                ++unbrokenAreasBefore;

        // consider making a split just before *pi
        const int sizeBefore = *pi - minValue, sizeAfter = maxValue + 1 - *pi;
        testPoint(axisIsX, *pi,
                  unbrokenAreasBefore * unbrokenAreasAfter * 100000 + // if there are unbroken areas on both sides, that will totally dominate
                  ((unbrokenAreasBefore ? sizeAfter  : 0) +
                   (unbrokenAreasAfter  ? sizeBefore : 0)) * 40 + // try to minimize the side with unbroken areas, but fall back to cutting at the middle (with the value [maxValue - minValue] * 10 above) if the cut away broken part would be less than 1/4 of the range
                  min(sizeBefore, sizeAfter)); // given two equal choices, pick the one closer to the middle

        // adjust to conditions after *pi
        for (unsigned i = 0; i < minimums.size(); ++i)
            if (count[i] && minimums[i] == *pi)
                --unbrokenAreasAfter;
        if (unbrokenAreasBefore + unbrokenAreasAfter == nActualAreas) { // if there is a span with no broken areas, put the split point in the middle of the no man's land rather than either border
            nAssert(unbrokenAreasBefore && unbrokenAreasAfter);
            nAssert(pi + 1 != points.end());
            testPoint(axisIsX, (*pi + *(pi + 1)) / 2,
                      unbrokenAreasBefore * unbrokenAreasAfter * 100000 + 90000 + min(sizeBefore, sizeAfter));
        }
    }
    nAssert(unbrokenAreasAfter == 0);
    #ifndef NDEBUG
    for (unsigned i = 0; i < minimums.size(); ++i)
        if (count[i] && maximums[i] == maxValue) // the last point considered in the actual loop is before maxValue (*pi == maxValue is possible but that point is still before maxValue)
            ++unbrokenAreasBefore;
    nAssert(unbrokenAreasBefore == nActualAreas);
    #endif
}

AreaMap::RoomAreaMap::AreaSplitter::AreaSplitter(const vector< vector<int> >& roomMap, const vector<Area*>& roomAreas, int x0, int y0, int x1, int y1) throw () :
    bestValue(0),
    count(roomAreas.size(), 0)
{
    vector<int> minX(roomAreas.size(), x1),
                minY(roomAreas.size(), y1),
                maxX(roomAreas.size(), x0),
                maxY(roomAreas.size(), y0);

    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const int idx = roomMap[x][y];
            if (idx < 0)
                continue;
            nAssert(unsigned(idx) < roomAreas.size());
            if (x < minX[idx])
                minX[idx] = x;
            if (x > maxX[idx])
                maxX[idx] = x;
            if (y < minY[idx])
                minY[idx] = y;
            if (y > maxY[idx])
                maxY[idx] = y;
            ++count[idx];
        }

    unsigned last = 0; // initialized to please GCC
    nActualAreas = 0;
    for (unsigned i = 0; i < roomAreas.size(); ++i)
        if (count[i]) {
            ++nActualAreas;
            last = i;
        }
    nAssert(nActualAreas != 0);
    if (nActualAreas == 1) {
        result = new SingleArea(roomAreas[last]);
        return;
    }

    int minXinUse, maxXinUse, minYinUse, maxYinUse;
    testAxisPoints(minX, maxX, minXinUse, maxXinUse, true);
    testAxisPoints(minY, maxY, minYinUse, maxYinUse, false);

    #if 0
    std::cerr << "AreaSplitter(" << x0 << ", " << y0 << ", " << x1 << ", " << y1 << "):\nArea:\n";
    for (int y = y0; y <= y1; ++y) {
        std::cerr << std::setw(2) << y << ' ';
        for (int x = x0; x <= x1; ++x) {
            const int idx = roomMap[x][y];
            std::cerr << ' ' << (idx < 0 ? '.' : idx > 9 ? 'X' : char('0' + idx));
        }
        std::cerr << '\n';
    }
    std::cerr << "   ";
    for (int x = x0; x <= x1; ++x)
        std::cerr << std::setw(2) << ((x == x0 || x % 10 == 0) ? x : x % 10);
    std::cerr << "\nSplit in " << (bestIsX ? 'X' : 'Y') << " before " << bestPoint << "\n\n";
    #endif

    nAssert(bestValue != 0);
    if (bestIsX)
        result = new SplitByX(AreaSplitter(roomMap, roomAreas, minXinUse, minYinUse, bestPoint - 1, maxYinUse)(),
                              AreaSplitter(roomMap, roomAreas, bestPoint, minYinUse, maxXinUse    , maxYinUse)(),
                              (bestPoint - .5) / (roomMap.size() - 1) * plw);
    else
        result = new SplitByY(AreaSplitter(roomMap, roomAreas, minXinUse, minYinUse, maxXinUse, bestPoint - 1)(),
                              AreaSplitter(roomMap, roomAreas, minXinUse, bestPoint, maxXinUse, maxYinUse    )(),
                              (bestPoint - .5) / (roomMap[0].size() - 1) * plh);

    #if 0
    std::cerr << "AreaSplitter(" << x0 << ", " << y0 << ", " << x1 << ", " << y1 << ") completed recursion\n\n";
    #endif
}

AreaMap::RoomAreaMap::RoomAreaMap(const vector< vector<int> >& roomMap, const vector<Area*>& roomAreas) throw () {
    nAssert(!roomAreas.empty());
    if (roomAreas.size() == 1)
        topLevel = new SingleArea(roomAreas.back());
    else
        topLevel = AreaSplitter(roomMap, roomAreas, 0, 0, roomMap.size() - 1, roomMap[0].size() - 1)();
}

AreaMap::Area* AreaMap::identifyArea(const WorldCoords& pos) throw () {
    return const_cast<Area*>(static_cast<const AreaMap*>(this)->identifyArea(pos));
}

const AreaMap::Area* AreaMap::identifyArea(const WorldCoords& pos) const throw () {
    if (pos.room.x >= 0 && pos.room.y >= 0 && (unsigned)pos.room.x < roomMaps.size() && (unsigned)pos.room.y < roomMaps[0].size())
        return roomMaps[pos.room.x][pos.room.y].identifyArea(pos.local());
    else
        return &areas.front(); // just return some area even for invalid coordinates; they are temporary and won't cause much harm
}

AreaMap& AreaMap::operator=(const AreaMap& o) throw () {
    areas.clear();
    roomMaps.clear();
    map<const Area*, Area*> newAreas;
    for (PointerVector<Area>::const_iterator ai = o.areas.begin(); ai != o.areas.end(); ++ai) {
        Area* na = new Area(*ai);
        areas.push_back(give_control(na));
        newAreas[&*ai] = na;
    }
    for (PointerVector<Area>::iterator ai = areas.begin(); ai != areas.end(); ++ai) {
        for (vector<Area::Neighbor>::iterator ni = ai->n.begin(); ni != ai->n.end(); ++ni)
            ni->area = newAreas[ni->area];
        for (vector<Area*>::iterator rni = ai->rn.begin(); rni != ai->rn.end(); ++rni)
            *rni = newAreas[*rni];
    }
    roomMaps.reserve(o.roomMaps.size());
    for (PointerVector< PointerVector<RoomAreaMap> >::const_iterator rmxi = o.roomMaps.begin(); rmxi != o.roomMaps.end(); ++rmxi) {
        roomMaps.push_back(give_control(new PointerVector<RoomAreaMap>()));
        roomMaps.back().reserve(rmxi->size());
        for (PointerVector<RoomAreaMap>::const_iterator rmi = rmxi->begin(); rmi != rmxi->end(); ++rmi)
            roomMaps.back().push_back(give_control(rmi->clone(newAreas)));
    }
    return *this;
}

void AreaMap::initialize(const Map& sourceMap) throw () {
    areas.clear();
    roomMaps.clear();

    // create all Areas before creating links between them
    for (int x = 0; x < sourceMap.w; ++x) {
        roomMaps.push_back(give_control(new PointerVector<RoomAreaMap>()));
        for (int y = 0; y < sourceMap.h; ++y)
            roomMaps[x].push_back(splitRoom(sourceMap, RoomCoords(x, y)));
    }

    // link doors to target areas
    for (PointerVector<Area>::iterator ai = areas.begin(); ai != areas.end(); ++ai) {
        map<Area*, unsigned> areaNeighborIndex; // stores the index to ai->n for each already seen neighboring area
        Area::Neighbor::Direction prevDir = Area::Neighbor::Up; // initialized to please GCC
        for (unsigned ni = 0; ni < ai->n.size(); ) {
            Area::Neighbor& n = ai->n[ni];
            nAssert(n.doors.size() == 1);
            if (n.direction != prevDir) {
                prevDir = n.direction;
                areaNeighborIndex.clear(); // this is required because the same Area can be reachable in multiple directions if the map is just one room high and/or wide; we need separate Neighbors for each direction
            }
            const int dx = n.dx(), dy = n.dy();
            n.area = identifyArea(WorldCoords(positiveModulo(ai->room.x + dx, sourceMap.w),
                                              positiveModulo(ai->room.y + dy, sourceMap.h),
                                              dx ? (dx < 0 ? plw : 0) : (n.doors.front().first + n.doors.front().second) / 2,
                                              dy ? (dy < 0 ? plh : 0) : (n.doors.front().first + n.doors.front().second) / 2));
            const map<Area*, unsigned>::const_iterator nii = areaNeighborIndex.find(n.area);
            if (nii == areaNeighborIndex.end()) {
                areaNeighborIndex[n.area] = ni++;
                n.area->rn.push_back(&*ai);
            }
            else {
                Area::Neighbor& nPrimary = ai->n[nii->second];
                nPrimary.doors.push_back(n.doors.back());
                ai->n.erase(ai->n.begin() + ni);
                // ni stays the same for next round, pointing to a new Neighbor now
            }
        }
    }

    // construct respawn values from respawn frequencies
    for (PointerVector<Area>::iterator ai = areas.begin(); ai != areas.end(); ++ai) {
        static const double bledPart = .5;
        for (int team = 0; team < 2; ++team) {
            ai->respawnValue[team] = ai->respawnFrequency[team] * (1. - bledPart);
            for (vector<Area*>::const_iterator ni = ai->reverseNeighbors().begin(); ni != ai->reverseNeighbors().end(); ++ni)
                ai->respawnValue[team] += (*ni)->respawnFrequency[team] * bledPart / (*ni)->neighbors().size();
        }
    }

    #if 0
    std::cerr << "\nArea dump:\n";
    map<pair<int, int>, int> roomAreaCounts;
    map<const Area*, int> areaInRoomIds;
    for (PointerVector<Area>::const_iterator ai = areas.begin(); ai != areas.end(); ++ai)
        areaInRoomIds[&*ai] = ++roomAreaCounts[make_pair(ai->roomx, ai->roomy)];
    for (PointerVector<Area>::const_iterator ai = areas.begin(); ai != areas.end(); ++ai) {
        std::cerr << ai->roomx << ',' << ai->roomy << '/' << areaInRoomIds[&*ai] << ':';
        for (vector<Area::Neighbor>::const_iterator ni = ai->neighbors().begin(); ni != ai->neighbors().end(); ++ni) {
            std::cerr << " ->" << (ni->direction == Area::Neighbor::Up ? 'U' : ni->direction == Area::Neighbor::Down ? 'D' : ni->direction == Area::Neighbor::Left ? 'L' : 'R')
                      << ':' << ni->area->roomx << ',' << ni->area->roomy << '/' << areaInRoomIds[ni->area];
            for (vector<pair<double, double> >::const_iterator di = ni->doors.begin(); di != ni->doors.end(); ++di)
                std::cerr << " @" << int(di->first) << ".." << int(di->second);
        }
        std::cerr << '\n';
    }
    #endif
}

void AreaMap::clearDistanceTable(DistanceTableId num) throw () {
    for (PointerVector<Area>::iterator ai = areas.begin(); ai != areas.end(); ++ai)
        ai->distance[num] = -1;
}
