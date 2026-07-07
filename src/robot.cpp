/*
 *  robot.cpp
 *
 *  Copyright (C) 2006, 2008 - Peter Kosyh
 *  Copyright (C) 2006, 2008, 2009, 2010, 2011 - Niko Ritari
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

//#define BOTDEBUG
//#define DEBUGSTRATEGY

#include <iomanip>
#include <queue>
#include <set>

#include "binaryaccess.h"
#include "leetnet/client.h"
#include "localconnection.h"
#include "nassert.h"
#include "network.h"
#include "protocol.h"
#include "timer.h"

#include "robot.h"

#ifdef BOT_TEST_COMPARE_TWO

#ifdef ALTERNATE_BOT

typedef Robot Robot1;
#undef ROBOT_H_INC
#define Robot Robot2 // both for include, and the rest of this file
#include "robot2.h"

BotInterface* BotInterface::newBot(const ClientExternalSettings& config, Log& clientLog, MemoryLog& externalErrorLog_, ControlledPtr<LocalConnection> conn) throw () {
    static bool turn = false;
    turn = !turn;
    if (turn)
        return new Robot2(config, clientLog, externalErrorLog_, conn);
    else
        return new Robot1(config, clientLog, externalErrorLog_, conn);
}

#endif

#else

BotInterface* BotInterface::newBot(const ClientExternalSettings& config, Log& clientLog, MemoryLog& externalErrorLog_, ControlledPtr<LocalConnection> conn) throw () {
    return new Robot(config, clientLog, externalErrorLog_, conn);
}

#endif

using std::list;
using std::make_pair;
using std::max;
using std::min;
using std::pair;
using std::set;
using std::string;
using std::priority_queue;
using std::vector;

const int S_W = plw;
const int S_H = plh;
const int FADEOUT = 50;

inline GunDirection inv_dir(GunDirection dir) throw () { return dir.adjust(4); }
inline int inv_dir(int dir) throw () { return dir ^ 4; }

int Robot::xDelta(Area::Neighbor::Direction dir) throw () { return (dir == Area::Neighbor::Right) ? +1 : (dir == Area::Neighbor::Left) ? -1 : 0; }
int Robot::yDelta(Area::Neighbor::Direction dir) throw () { return (dir == Area::Neighbor::Down ) ? +1 : (dir == Area::Neighbor::Up  ) ? -1 : 0; }

const DeathbringerExplosion* Robot::explosionInRoom(const RoomCoords& room) const throw () {
    for (list<DeathbringerExplosion>::const_iterator dbi = fx.deathbringerExplosions().begin(); dbi != fx.deathbringerExplosions().end(); ++dbi) {
        const WorldCoords& pos = dbi->position();
        if (pos.room == room && (dbi->team() != myTeam() || fx.physics.friendly_db))
            return &*dbi;
    }
    return 0;
}

bool Robot::imminentExplosionHere() const throw () {
    for (int i = 0; i < maxplayers; ++i) {
        const ClientPlayer& pl = fx.player[i];
        if (!pl.used || pl.dead || !here(pl, true))
            continue;
        if (myTeam(pl) && !fx.physics.friendly_db)
            continue;
        if (pl.item_deathbringer && pl.under_deathbringer_effect(get_time()))
            return true;
    }
    return false;
}

double Robot::distanceFromDoor(const Area::Neighbor& n, const Coords& pos) const throw () {
    try {
        return (nearestDoor(n, pos) - pos).mag();
    } catch (AlreadyInRoom) {
        return 0;
    }
}

bool Robot::dangerousExplosionInNeighbor(const Area::Neighbor& neighbor) const throw () {
    const DeathbringerExplosion* const dbe = explosionInRoom(neighbor.area->room);
    if (!dbe)
        return false;
    // see when we can be there at the earliest, don't worry if the explosion has expired then
    const double minMoveTime = distanceFromDoor(neighbor) / fx.physics.max_run_speed; // ignores that we're faster with turbo
    return !dbe->expired(fx.frame + averageLag + minMoveTime);
}

bool Robot::moreDefensive(const ClientPlayer& player) const throw () {
    if (HaveFlag(player.pid))
        return true;
    if ((here(player, true) || see_minimap_player_properties >= 1) && player.item_turbo != fx.player[me].item_turbo)
        return !player.item_turbo;
    if (player.name.substr(0, 4) == "BOT ")
        return player.pid < me;
    else
        return player.defending;
}

bool Robot::IsBehindWall(const Vec& delta, double radius, double maxDistanceFromTarget) const throw () {
    return IsBehindWall(myPos, delta, radius, maxDistanceFromTarget);
}

bool Robot::IsBehindWall(const WorldCoords& startPos, const Vec& delta, double radius, double maxDistanceFromTarget) const throw () {
    const Room& room = fx.map[startPos.room];
    const double dist = delta.mag();
    if (dist == 0)
        return false;
    const double nearEnoughFraction = (dist - maxDistanceFromTarget) / dist;
    if (nearEnoughFraction <= 0.)
        return false;
    return room.genGetTimeTillWall(startPos.local(), delta, radius, nearEnoughFraction).first < nearEnoughFraction;
}

double Robot::ScanDir(GunDirection dir) const throw () {
    return WallHitPosition(dir, PLAYER_RADIUS).first;
}

pair<double, Coords> Robot::WallHitPosition(GunDirection dir, double radius) const throw () {
    const double deg = dir.toRad();
    const Vec d(cos(deg), sin(deg));
    const Room& room = fx.map[myPos.room];
    double maxDist = 1e99;
    if (d.x != 0)
        maxDist = min(maxDist, (d.x > 0 ? S_W - myPos.x : -myPos.x) / d.x);
    if (d.y != 0)
        maxDist = min(maxDist, (d.y > 0 ? S_H - myPos.y : -myPos.y) / d.y);
    const double dist = min(maxDist, room.genGetTimeTillWall(myPos.local(), d, radius, maxDist).first);
    return make_pair(dist, Coords(myPos.local() + d * dist));
}

pair<Robot::AimLevel, int> Robot::TryAimTradTurning(int target) const throw () {
    nAssert(!fx.physics.allowFreeTurning);

    const Vec tt = predictPos(fx.player[target]);
    Vec d = tt - myPos.local();
    const double dist = d.mag();

    if (dist <= PLAYER_RADIUS)
        return make_pair(AL_Full, myGundir);

    d += fx.player[target].vel * (dist / fx.physics.rocket_speed);

    const int dir = GetDir(d).to8way();

    if (IsBehindWall(d, ROCKET_RADIUS, PLAYER_RADIUS + ROCKET_RADIUS))
        return make_pair(AL_None, dir);

    static const int rocketsPerWeaponLevel[9] = { 1, 2, 3, 2, 3, 2, 3, 2, 3 };
    const int w = fx.player[me].weapon;
    const int rockets = w >= 1 && w <= 9 ? rocketsPerWeaponLevel[w - 1] : 1;
    static const double treshold = 1.3 * PLAYER_RADIUS + .7 * WorldBase::shot_deltax * (rockets - 1); // both 1.3 and .7 are semi-arbitrary
    bool belowTreshold;
    if (dir == 0 || dir == 4) // left or right
        belowTreshold = fabs(d.y) < treshold;
    else if (dir == 2 || dir == 6) // up or down
        belowTreshold = fabs(d.x) < treshold;
    else // diagonal
        belowTreshold = fabs(fabs(d.y) - fabs(d.x)) <= treshold * sqrt(2);
    return make_pair(belowTreshold ? AL_Full : AL_Near, dir);
}

GunDirection Robot::GetDir(const Vec& delta) const throw () {
    return GunDirection().fromRad(atan2(delta.y, delta.x));
}

int Robot::GetDangerousRocket() const throw () {
    static const int thinkAheadFrames = 10;
    static const double safetyRoomMul = 4., framesAfterBounce = .5;

    int nearRocket = -1;
    double firstTime = 1e99;

    ClientPlayer player = fx.player[me];
    player.pos = myPos;

    const Room& room = fx.map[player.room()];

    const double bounceTime = fx.getTimeTillBounce(room, player, PLAYER_RADIUS, thinkAheadFrames).first;

    for (int i = 0; i < MAX_ROCKETS; ++i) {
        const Rocket& rocket = fx.rock[i];

        if (rocket.owner == -1 || rocket.team == player.team() && !fx.physics.friendly_fire || rocket.room() != player.room())
            continue;

        Rocket rFuture = rocket;
        rFuture.move(averageLag); // same as the player start position
        const double collisionTime = fx.getTimeTillCollision(player, rFuture, (PLAYER_RADIUS + ROCKET_RADIUS) * safetyRoomMul);
        if (collisionTime > bounceTime + framesAfterBounce || collisionTime >= firstTime)
            continue;

        const double rocketWallTime = fx.getTimeTillWall(room, rFuture, thinkAheadFrames);
        if (collisionTime > rocketWallTime)
            continue;

        firstTime = collisionTime;
        nearRocket = i;
    }

    return nearRocket;
}

int Robot::GetEasyEnemy() const throw () {
    int target = -1;
    double nearestDist = 1e10;

    for (int i = 0; i < maxplayers; ++i) {
        const ClientPlayer& enemy = fx.player[i];
        if (!enemy.used || myTeam(enemy) || enemy.dead || !here(enemy, true))
            continue;

        const Vec tt = predictPos(enemy);
        const Vec d = tt - myPos.local();

        const double playerDist = d.mag();
        const double escapeDist = (playerDist / fx.physics.rocket_speed) * fx.physics.max_run_speed / 2;

        double rocketPlaneDist;
        if (fx.physics.allowFreeTurning)
            rocketPlaneDist = playerDist;
        else {
            const int dir = GetDir(d).to8way();
            if (dir == 0 || dir == 4)
                rocketPlaneDist = fabs(d.y);
            else if (dir == 2 || dir == 6)
                rocketPlaneDist = fabs(d.x);
            else if (dir == 1 || dir == 5)
                rocketPlaneDist = fabs(d.y - d.x) / sqrt(2.);
            else
                rocketPlaneDist = fabs(d.y + d.x) / sqrt(2.);
        }

        if (rocketPlaneDist < 2 * PLAYER_RADIUS && rocketPlaneDist + escapeDist < nearestDist && !IsBehindWall(d, ROCKET_RADIUS, PLAYER_RADIUS + ROCKET_RADIUS)) {
            target = i;
            nearestDist = rocketPlaneDist + escapeDist;
        }
    }
    #ifdef BOTDEBUG
    if (target != -1)
        fprintf(stderr,"Looking on easy. enemy %d\n", target);
    #endif
    return target;
}

int Robot::GetDangerousEnemy() const throw () {
    int target = -1;
    double nearestDist = 1e10;

    for (int i = 0; i < maxplayers; ++i) {
        const ClientPlayer& enemy = fx.player[i];
        if (!enemy.used || myTeam(enemy) || enemy.dead || !here(enemy, true))
            continue;

        const Vec tt = predictPos(enemy);
        const Vec d = tt - myPos.local();

        const double playerDist = d.mag();
        const GunDirection aimTowardsMe = inv_dir(GetDir(d));

        bool aimed;
        if (fx.physics.allowFreeTurning) {
            static const double tolerance = N_PI_4; // this in both directions -> angles within a 90° range are considered dangerous
            const double diff = positiveFmod(enemy.gundir.toRad() - aimTowardsMe.toRad(), 2 * N_PI);
            aimed = diff < tolerance || diff > 2 * N_PI - tolerance;
        }
        else
            aimed = enemy.gundir.to8way() == aimTowardsMe.to8way();

        if (!aimed && playerDist > 2 * PLAYER_RADIUS)
            continue;

        const double escapeDist = (playerDist / fx.physics.rocket_speed) * fx.physics.max_run_speed / 2;

        double rocketPlaneDist;
        if (fx.physics.allowFreeTurning)
            rocketPlaneDist = playerDist;
        else {
            const int dir = aimTowardsMe.to8way();
            if (dir == 0 || dir == 4)
                rocketPlaneDist = fabs(d.y);
            else if (dir == 2 || dir == 6)
                rocketPlaneDist = fabs(d.x);
            else if (dir == 1 || dir == 5)
                rocketPlaneDist = fabs(d.y - d.x) / sqrt(2.);
            else
                rocketPlaneDist = fabs(d.y + d.x) / sqrt(2.);
        }

        if (rocketPlaneDist < 2 * PLAYER_RADIUS && rocketPlaneDist + escapeDist < nearestDist && !IsBehindWall(d, ROCKET_RADIUS, PLAYER_RADIUS + ROCKET_RADIUS)) {
            target = i;
            nearestDist = rocketPlaneDist + escapeDist;
        }
    }
    #ifdef BOTDEBUG
    if (target != -1)
        fprintf(stderr,"Looking on dang. enemy %d\n", target);
    #endif
    return target;
}

int Robot::GetNearestEnemy() const throw () {
    int target = -1;
    double mdist = 0;

    for (int i = 0; i < maxplayers; ++i) {
        const ClientPlayer& enemy = fx.player[i];
        if (!enemy.used || myTeam(enemy) || enemy.dead || !here(enemy, true))
            continue;

        const Vec tt = predictPos(enemy);
        const Vec d = tt - myPos.local();
        const double dist = d.mag();

        if (dist < mdist || mdist == 0) {
            //if (IsBehindWall(d)) // XXX ?
            //    continue;
            mdist = dist;
            target = i;
        }
    }
    return target;
}

pair<bool, GunDirection> Robot::TryAimFreeTurning(int target) const throw () {
    nAssert(fx.physics.allowFreeTurning);

    const Vec tt = predictPos(fx.player[target]);
    Vec d = tt - myPos.local();
    const double dist = d.mag();

    if (dist < 1)
        return make_pair(true, gunDir);

    const double tm = dist / fx.physics.rocket_speed;
    d += tm * fx.player[target].vel;

    return make_pair(!IsBehindWall(d, ROCKET_RADIUS, PLAYER_RADIUS + ROCKET_RADIUS), GetDir(d));
}

double Robot::GetHitTime(const GunDirection& dir, int iTarget) const throw () {
    const ClientPlayer& target = fx.player[iTarget];

    if (target.dead || !here(target, true))
        return 1e100;

    const Vec tt = predictPos(target);
    const Vec d = tt - myPos.local();
    const double dist = d.mag();

    if (dist <= PLAYER_RADIUS)
        return 0;

    const Vec rs = Vec(cos(dir.toRad()), sin(dir.toRad())) * fx.physics.rocket_speed;
    const Vec ts = rs - target.vel;

    // divide D=d to components parallel with, and perpendicular to T=ts

    // D_par = T / |T| * (T dot D) / |T| = T * (T dot D) / |T|²
    // D_perp = D - D_par

    const double ts2 = ts.mag2();

    if (ts2 == 0)
        return 1e100;

    const double mul = dot(d, ts) / ts2;
    const Vec par = ts * mul;
    const Vec perp = d - par;

    const double hitTime = sqrt(par.mag2() / ts2); // |par| / |ts|, combined under one square root op
    return perp.mag() < 3 * PLAYER_RADIUS ? hitTime : 1e100;
}

double Robot::GetHitTeammateTime(const GunDirection& dir) const throw () {
    double hitTime = 1e100;
    if (fx.physics.friendly_fire == 0)
        return hitTime;
    for (int i = 0; i < maxplayers; ++i)
        if (fx.player[i].used && myTeam(fx.player[i]) && i != me)
            hitTime = min(hitTime, GetHitTime(dir, i));
    return hitTime;
}

pair<bool, GunDirection> Robot::NeedShootFreeTurning(const GunDirection& defaultDir) throw () {
    vector<int> tryOrder;
    for (int i = 0; i < maxplayers; ++i) {
        const ClientPlayer& pl = fx.player[i];
        if (!pl.used || myTeam(pl) || pl.dead || !here(pl, true))
            continue;
        if (i != last_seen)
            tryOrder.push_back(i);
    }

    pair<bool, GunDirection> aimLastSeen = last_seen == -1 ? make_pair(false, defaultDir) : TryAimFreeTurning(last_seen);
    if (aimLastSeen.first) {
        if (GetHitTime(aimLastSeen.second, last_seen) <= GetHitTeammateTime(aimLastSeen.second))
            return aimLastSeen;
        aimLastSeen.first = false;
    }
    random_shuffle(tryOrder.begin(), tryOrder.end());
    for (vector<int>::const_iterator ti = tryOrder.begin(); ti != tryOrder.end(); ++ti) {
        const pair<bool, GunDirection> aim = TryAimFreeTurning(*ti);
        if (aim.first && GetHitTime(aim.second, *ti) <= GetHitTeammateTime(aim.second)) {
            last_seen = *ti;
            return aim;
        }
    }
    return aimLastSeen; // aim at last_seen if no one is actually shootable
}

pair<bool, int> Robot::ShootAtDoorTradTurning() throw () {
    // if we've just arrived at a new room and don't yet know if there are enemies, don't shoot at doors
    if (myPos.room != fx.player[me].room())
        return make_pair(false, -1);

    // if we're about to go to a new room, don't shoot at doors
    const Coords futurePos = Coords(myPos.local() + 5 * fx.player[me].vel);
    if (futurePos.x < 0 || futurePos.x > S_W || futurePos.y < 0 || futurePos.y > S_H)
        return make_pair(false, -1);

    set<const Area*> enemyAreas;
    for (int i = 0; i < maxplayers; ++i) {
        const ClientPlayer& pl = fx.player[i];
        if (!pl.used || myTeam(pl) || pl.dead || pl.posUpdated < fx.frame - FADEOUT)
            continue;
        enemyAreas.insert(area(pl));
    }

    const Area* const here = myArea();

    // if an enemy can appear near to us, don't shoot at any doors
    for (vector<Area::Neighbor>::const_iterator ni = here->neighbors().begin(); ni != here->neighbors().end(); ++ni) {
        if (fx.map[ni->area->room].enemies_seen_frame >= fx.frame - 5 && !enemyAreas.count(ni->area)) // nothing to fear from there
            continue;
        const double dist = distanceFromDoor(*ni, myPos.local());
        if (dist < S_H / 2)
            return make_pair(false, -1);
    }

    pair<double, Coords> hitPos[8];
    for (int dir = 0; dir < 8; ++dir)
        hitPos[dir] = WallHitPosition(GunDirection().from8way(dir), ROCKET_RADIUS);

    double bestDist = 1e99;
    int bestDir = -1;
    bool knownEnemyTargeted = false;
    for (vector<Area::Neighbor>::const_iterator ni = here->neighbors().begin(); ni != here->neighbors().end(); ++ni) {
        const bool roomSeen = fx.map[ni->area->room].enemies_seen_frame >= fx.frame - 5;
        const bool knownEnemy = enemyAreas.count(ni->area);
        const bool nearRespawn = ni->area->respawnValue[!myTeam()] >= .25;
        if (!knownEnemy && (roomSeen || !nearRespawn))
            continue;
        for (int dir = 0; dir < 8; ++dir) {
            if (hitPos[dir].first < S_H / 2) // only bother shooting speculatively if there's a long lifetime for the rocket
                continue;
            const double dist = distanceFromDoor(*ni, hitPos[dir].second);
            if (dist < bestDist && knownEnemy == knownEnemyTargeted || knownEnemy && !knownEnemyTargeted) {
                bestDist = dist;
                bestDir = dir;
                knownEnemyTargeted = knownEnemy;
                if (dist < 1. && knownEnemy)
                    return make_pair(true, dir);
            }
        }
    }

    if (bestDist > 4 * PLAYER_RADIUS)
        return make_pair(false, -1);
    else
        return make_pair(true, bestDir);
}

pair<bool, int> Robot::NeedShootTradTurning() throw () {
    vector< pair<bool, double> > dirDistances(8, make_pair(false, 0)); // if there's someone to shoot in the direction, first = true, second = time to hit
    bool anyInRoom = false;
    for (int i = 0; i < maxplayers; ++i) {
        const ClientPlayer& pl = fx.player[i];
        if (!pl.used || myTeam(pl) || pl.dead || !here(pl, true))
            continue;
        anyInRoom = true;

        const pair<AimLevel, int> aim = TryAimTradTurning(i);
        if (aim.first != AL_Full)
            continue;
        const double hitTime = GetHitTime(GunDirection().from8way(aim.second), i);
        if (hitTime > 1e10) // GetHitTime signals "no hit" with a huge time, and uses a better calculation than TryAim
            continue;
        if (!dirDistances[aim.second].first || hitTime < dirDistances[aim.second].second)
            dirDistances[aim.second] = make_pair(true, hitTime);
    }
    if (!anyInRoom)
        return ShootAtDoorTradTurning();

    int bestDir = -1;
    double bestValue = 1e99;
    for (int dir = 0; dir < 8; ++dir) {
        if (!dirDistances[dir].first || dirDistances[dir].second > GetHitTeammateTime(GunDirection().from8way(dir)))
            continue;
        int dirDiff = abs(dir - myGundir);
        nAssert(dirDiff < 8);
        if (dirDiff > 4)
            dirDiff = 8 - dirDiff;
        const double value = dirDistances[dir].second * dirDiff; // at or near the current aim is heavily favored (at gives always the best value)
        if (value < bestValue) {
            bestValue = value;
            bestDir = dir;
        }
    }
    return make_pair(bestDir != -1, bestDir);
}

double Robot::predictDistanceFromRocket(Rocket rocket, const ClientControls& ctrl) const throw () {
    const int maxPredictionFrames = 10;
    rocket.move(averageLag);
    ClientPlayer player = futureMe;
    int frames;
    for (frames = 0; frames < maxPredictionFrames; ++frames) {
        if (player.room() != rocket.room())
            return plw + plh - frames; // subtract frames as an easy way to prefer a faster exit
        const Vec diff = player.pos.local() - rocket.pos.local();
        const Vec dVel = rocket.vel - player.vel;
        const double timeToPlane = dot(dVel, diff) / dVel.mag2();
        if (timeToPlane <= 1.) {
            if (timeToPlane > 0.) {
                rocket.move(timeToPlane);
                fx.extrapolateSinglePlayerPosition(player, &ctrl, 0, 0, timeToPlane);
            }
            break;
        }
        rocket.move(1.);
        fx.extrapolateSinglePlayerPosition(player, &ctrl, 0, 0, 1.);
    }
    return player.room() == rocket.room() ? mag(player.pos.local() - rocket.pos.local()) : plw + plh - frames - 1;
}

ClientControls Robot::EscapeRocket(int mrock) const throw () {
    double bestDistance = 0;
    ClientControls bestControls;
    for (int dir = 0; dir < 8; ++dir) {
        const ClientControls ctrl = ClientControls().fromDirection(dir).setStrafe().setRun();
        const double dist = predictDistanceFromRocket(fx.rock[mrock], ctrl);
        if (dist > bestDistance) {
            bestDistance = dist;
            bestControls = ctrl;
        }
    }
    return bestControls;
}

ClientControls Robot::EscapeExplosion() const throw () {
    if (!explosionInRoom(myPos.room) && !imminentExplosionHere())
        return ClientControls();

    const Area::Neighbor* bestRoom = 0;
    double shortestDistance = 1e99; // always excape to the nearest room regardless of the explosion location (if that's a bad direction, we probably couldn't escape in any)
    const Area* const a = myArea();
    for (vector<Area::Neighbor>::const_iterator ni = a->neighbors().begin(); ni != a->neighbors().end(); ++ni) {
        if (explosionInRoom(ni->area->room))
            continue;
        const double dist = distanceFromDoor(*ni);
        if (dist < shortestDistance) {
            bestRoom = &*ni;
            shortestDistance = dist;
        }
    }
    if (bestRoom)
        return MoveToDoor(*bestRoom);
    else
        return ClientControls();
}

ClientControls Robot::Aim(int i) const throw () {
    nAssert(!fx.physics.allowFreeTurning);

    const Vec tt = predictPos(fx.player[i]);
    const Vec d = tt - myPos.local();

    const pair<AimLevel, int> aim = TryAimTradTurning(i);
    if (aim.first == AL_Full && aim.second == myGundir)
        return ClientControls();
    else if (aim.first == AL_None || aim.second != myGundir)
        return MoveTo(d, PLAYER_RADIUS + PLAYER_RADIUS);
    nAssert(aim.first == AL_Near && aim.second == myGundir);

    // almost aimed
    ClientControls ctrl;
    ctrl.setRun(); //always run!
    ctrl.setStrafe();
    if (myGundir == 0 || myGundir == 2 || myGundir == 4 || myGundir == 6) {
        if (d.x > 0)
            ctrl.setRight();
        else if (d.x < 0)
            ctrl.setLeft();
        if (d.y > 0)
            ctrl.setDown();
        else if (d.y < 0)
            ctrl.setUp();
    }
    else if (fabs(d.y) > fabs(d.x)) {
        if (d.y < 0)
            ctrl.setUp();
        else
            ctrl.setDown();
    }
    else {
        if (d.x < 0)
            ctrl.setLeft();
        else
            ctrl.setRight();
    }
    return ctrl;
}

int Robot::FreeDir() const throw () {
    int mdir = 0;
    double mdist = 0;

    for (int i = myGundir - 1; i <= myGundir + 1; ++i) {
        const int d = (i + 8) % 8;
        const double dist = ScanDir(GunDirection().from8way(d));

        if (dist > mdist || mdist == 0 || dist >= mdist - ROCKET_RADIUS && i == myGundir) {
            mdist = dist;
            mdir = d;
        }
    }
    if (mdist < 1.5 * PLAYER_RADIUS)
        mdir = inv_dir(mdir);
    return mdir;
}

ClientControls Robot::MoveDirNoAggregate(int dir) const throw () {
    Vec sd(0, 0);
    int n = 0;

    for (int i = 0; i < maxplayers; ++i) {
        const ClientPlayer& pl = fx.player[i];
        if (!pl.used || !myTeam(pl) || pl.dead || !pl.onscreen || i == me)
            continue;

        const Vec d = predictPos(pl) - predictPos(fx.player[me]); // don't use myPos, so that all players have the same view
        if (fabs(d.x) > PLAYER_RADIUS || fabs(d.y) > PLAYER_RADIUS)
            continue;
        sd += d;
        n++;
    }

    if (!n)
        return MoveDir(dir);

    if (!sd.x && !sd.y)
        dir = rand() % 8;

    sd /= n;

    ClientControls ctrl = ClientControls().fromDirection(dir).setRun().setStrafe();

    switch (dir) {
        case 0:
        case 4:
            if (sd.y > 0)
                ctrl.setUp();
            else
                ctrl.setDown();
            break;
        case 2:
        case 6:
            if (sd.x > 0)
                ctrl.setLeft();
            else
                ctrl.setRight();
            break;
        case 1:
            if (sd.y < sd.x)
                ctrl.clearRight();
            else
                ctrl.clearDown();
            break;
        case 5:
            if (sd.y < sd.x)
                ctrl.clearUp();
            else
                ctrl.clearLeft();
            break;
        case 3:
            if (sd.y > -sd.x)
                ctrl.clearDown();
            else
                ctrl.clearLeft();
            break;
        case 7:
            if (sd.y > -sd.x)
                ctrl.clearRight();
            else
                ctrl.clearUp();
            break;
    }
    return ctrl;
}

ClientControls Robot::MoveDir(int dir) const throw () {
    return ClientControls().fromDirection(dir).setRun();
}

ClientControls Robot::FreeWalk() throw () {
    for (int tries = 0; tries < 20; ++tries) {
        if (freeWalkTarget.x >= 0 && !IsBehindWall(freeWalkTarget - myPos.local(), PLAYER_RADIUS, PLAYER_RADIUS) && mag(freeWalkTarget - myPos.local()) > 3 * PLAYER_RADIUS)
            return MoveToNoAggregate(freeWalkTarget - myPos.local(), PLAYER_RADIUS);
        freeWalkTarget = Coords(rand() % S_W, rand() % S_H);
    }
    freeWalkTarget.x = -1;
    return MoveDirNoAggregate(FreeDir());
}

ClientControls Robot::MoveIndirectlyTowards(const Vec& delta, double maxDistanceFromTarget) const throw () {
    const Coords target(myPos.local() + delta);

    int bestDir = -1;
    double minDist = 1e99;

    for (int dirOffset = -3; dirOffset <= 3; ++dirOffset) {
        const int dir = positiveModulo(myGundir + dirOffset, 8);
        const pair<double, Coords> wallPos = WallHitPosition(GunDirection().from8way(dir), PLAYER_RADIUS);
        if (wallPos.first <= PLAYER_RADIUS)
            continue;
        const double fract = (wallPos.first - PLAYER_RADIUS) / wallPos.first; // pick a turning point along the line but an additional player radius away from the wall
        const Coords turnPos(wallPos.second * fract + myPos.local() * (1. - fract));

        if (IsBehindWall(WorldCoords(myPos.room, turnPos), target - turnPos, PLAYER_RADIUS, maxDistanceFromTarget))
            continue;

        const double dist = wallPos.first - PLAYER_RADIUS + mag(target - turnPos) + 2 * PLAYER_RADIUS * abs(dirOffset);
        if (dist < minDist) {
            minDist = dist;
            bestDir = dir;
        }
    }
    return MoveDir(bestDir != -1 ? bestDir : FreeDir());
}

ClientControls Robot::MoveToNoAggregate(const Vec& delta, double maxDistanceFromTarget) const throw () {
    if (IsBehindWall(delta, PLAYER_RADIUS, maxDistanceFromTarget))
        return MoveIndirectlyTowards(delta, maxDistanceFromTarget);
    else
        return MoveDirNoAggregate(GetDir(delta).to8way());
}


ClientControls Robot::MoveTo(const Vec& delta, double maxDistanceFromTarget) const throw () {
    if (IsBehindWall(delta, PLAYER_RADIUS, maxDistanceFromTarget))
        return MoveIndirectlyTowards(delta, maxDistanceFromTarget);
    else
        return MoveDir(GetDir(delta).to8way());
}

ClientControls Robot::GetPowerup(bool onImportantMission) const throw () {
    for (int i = 0; i < MAX_POWERUPS; ++i) {
        if (!fx.item[i].real() || area(fx.item[i].position()) != myArea())
            continue;
        const Vec d = fx.item[i].pos.local() - myPos.local();
        if (onImportantMission) {
            if (IsBehindWall(d, PLAYER_RADIUS, PLAYER_RADIUS + POWERUP_RADIUS))
                continue;
            if (dot(d, fx.player[me].vel) < 0 && d.mag2() > sqr(5 * PLAYER_RADIUS)) // don't bother if the powerup is behind
                continue;
        }
        return MoveTo(d, PLAYER_RADIUS + POWERUP_RADIUS);
    }
    return ClientControls();
}

ClientControls Robot::captureOnFlag(bool carried) const throw () {
    const int myFlag = HaveFlag(me);
    nAssert(myFlag);
    for (int type = 0; type <= 2; ++type) {
        const int team = type == 0 ? myTeam() : type == 1 ? 2 : !myTeam();
        const bool captureOnOtherFlag =   type == 0 && capture_on_team_flags_in_effect && myFlag != 3 ||   type == 1 && capture_on_wild_flags_in_effect;
        const bool captureOnMyFlag    = myFlag == 3 && capture_on_team_flags_in_effect &&   type != 0 || myFlag == 2 && capture_on_wild_flags_in_effect;
        if (!captureOnOtherFlag && !captureOnMyFlag)
            continue;
        const vector<Flag>& flags = team == 2 ? fx.wild_flags : fx.teams[team].flags();
        for (vector<Flag>::const_iterator fi = flags.begin(); fi != flags.end(); ++fi) {
            if (fi->carried() != carried || fi->carried() && (fi->carrier() == me || !myTeam(fx.player[fi->carrier()])))
                continue;
            const WorldCoords pos = fi->carried() ? fx.player[fi->carrier()].pos : fi->position();
            if (area(pos) != myArea())
                continue;
            if (!carry_own_team_flag && type == 0 || capture_away_from_base || IsFlagAtBase(*fi, team, true)) { // try to capture, or return own flag so that capture is possible; can't return wild flags
                const Coords lPos = fi->carried() ? predictPos(fx.player[fi->carrier()]) : pos.local();
                return MoveTo(lPos - myPos.local(), PLAYER_RADIUS + FLAG_RADIUS);
            }
            if (!capture_away_from_base && carried)
                for (int baseType = 0; baseType <= 1; ++baseType) {
                    const int baseTeam = baseType == 0 ? myTeam() : 2;
                    if (baseType == 0 && !(type == 0 && captureOnOtherFlag || myFlag == 3 && captureOnMyFlag) ||
                        baseType == 1 && !(type == 1 && captureOnOtherFlag || myFlag == 2 && captureOnMyFlag))
                    {
                        continue;
                    }
                    const vector<WorldCoords>& bases = baseTeam == 2 ? fx.map.wild_flags : fx.map.tinfo[baseTeam].flags;
                    for (vector<WorldCoords>::const_iterator bi = bases.begin(); bi != bases.end(); ++bi)
                        if (area(*bi) == myArea())
                            return MoveTo(bi->local() - myPos.local(), PLAYER_RADIUS + FLAG_RADIUS);
                }
        }
    }
    return ClientControls();
}

ClientControls Robot::pickUpFlag() const throw () {
    nAssert(!HaveFlag(me));
    for (int type = 0; type < 3; ++type) { // 0 for own flags, 1 for enemy, 2 for wild
        if (type == 2 ? lock_wild_flags_in_effect : lock_team_flags_in_effect)
            continue;
        const int team = type == 0 ? myTeam() : type == 1 ? !myTeam() : 2;
        const vector<Flag>& flags = team == 2 ? fx.wild_flags : fx.teams[team].flags();
        for (vector<Flag>::const_iterator fi = flags.begin(); fi != flags.end(); ++fi) {
            if (area(fi->position()) != myArea() || fi->carried())
                continue;
            if (!(type == 0 && !carry_own_team_flag && IsFlagAtBase(*fi, team, false))) // try to pick up a flag or return own flag
                return MoveTo(fi->position().local() - myPos.local(), PLAYER_RADIUS + FLAG_RADIUS);
        }
    }

    return ClientControls();
}

ClientControls Robot::GetFlag() const throw () {
    if (HaveFlag(me)) {
        const ClientControls ctrl = captureOnFlag(false);
        return !ctrl.idle() ? ctrl : captureOnFlag(true);
    }
    return pickUpFlag();
}

Robot::TeamCounts Robot::Teams(const Area* const a, bool countMe) const throw () {
    TeamCounts c;
    c.enemies = c.friends = 0;
    bool meFound = false;
    for (int i = 0; i < maxplayers; ++i) {
        const ClientPlayer& pl = fx.player[i];
        if (!pl.used || area(pl) != a || pl.dead)
            continue;
        if (myTeam(pl)) {
            if (i == me)
                meFound = true;
            c.friends++;
        }
    }

    for (int i = 0; i < maxplayers; ++i) {
        const ClientPlayer& pl = fx.player[i];
        if (!pl.used || area(pl) != a || pl.dead)
            continue;
        if (!myTeam(pl)) {
            if (fx.frame - pl.posUpdated > FADEOUT)
                continue;
            if (fx.map[pl.room()].enemies_seen_frame > pl.posUpdated)
                continue;
            c.enemies++;
        }
    }

    if (meFound && !countMe)
        --c.friends;
    return c;
}

bool Robot::AmILast() const throw () {
    for (int i = 0; i < maxplayers; ++i) {
        const ClientPlayer& pl = fx.player[i];
        if (!pl.used || pl.dead || !here(pl))
            continue;
        if (myTeam(pl) && i > me)
            return false; // i am not last one
    }
    return true;
}

ClientControls Robot::Escape() const throw () {
    if (!HaveFlag(me))
        return ClientControls();

    const Area* const a = myArea();

    const TeamCounts tc = Teams(a, true);
    if (tc.enemies <= tc.friends)
        return ClientControls();

    // looking for friends
    for (vector<Area::Neighbor>::const_iterator ni = a->neighbors().begin(); ni != a->neighbors().end(); ++ni) {
        const TeamCounts tc = Teams(ni->area, false);
        if (tc.friends + 1 > tc.enemies && tc.friends > 0) {
            if (dangerousExplosionInNeighbor(*ni))
                return ClientControls(); // don't check other neighbors, because this is the one we'll tend to go to when the explosion is over (soon)
            else
                return MoveToDoor(*ni);
        }
    }
    return ClientControls();
}

ClientControls Robot::FollowFlag() const throw () {
    Vec d(0, 0);
    Vec s(0, 0);
    int num = 0;
    for (int i = 0; i < maxplayers; ++i) {
        const ClientPlayer& pl = fx.player[i];
        if (!pl.used || !myTeam(pl) || pl.dead || i == me || !HaveFlag(i) || !here(pl))
            continue;

        d += pl.pos.local();
        s += pl.vel;
        num++;
    }
    if (!num || IsHome(myArea()))
        return ClientControls();
    d = (d + averageLag * s) / num - myPos.local();

    Vec target = d;
    const double sMag = s.mag();
    if (sMag) {
        const double timeAdvance = 4.; // the time of velocities added to the position difference to estimate inertia
        target += timeAdvance * (s - fx.player[me].vel);
        target += s * (5 * PLAYER_RADIUS / sMag); // take only the direction, try to lead the carrier by a constant distance
    }
    if (target.mag() < 3 * PLAYER_RADIUS)
        return ClientControls().setStrafe(); // signal to caller 'do nothing' instead of 'don't care'
    if (!IsBehindWall(target, PLAYER_RADIUS, 0))
        return MoveToNoAggregate(target, PLAYER_RADIUS);
    else
        return MoveToNoAggregate(d, PLAYER_RADIUS);
}

void Robot::BuildMap() throw () {
    enemiesInRoom = false;
    last_seen = -1;
    myGundir = -1;

    areaMap = sharedDataHandle.acquire(fx.map);

    for (int x = 0; x < fx.map.w; ++x)
        for (int y = 0; y < fx.map.h; ++y)
            fx.map[RoomCoords(x, y)].enemies_seen_frame = 0;

    for (int i = 0; i < Table_Max; i++)
        distanceTable[i].center = 0;

    destinationType = Dest_None;
    destination = 0;
    immediateDestination = 0;
    freeWalkTarget.x = -1;
}

void Robot::BuildDistanceTable(Area* startPoint, double respawnWeight, DistanceTableId num) throw () {
    return BuildDistanceTable(vector<Area*>(1, startPoint), respawnWeight, num);
}

void Robot::BuildDistanceTable(const vector<Area*>& startPoints, double respawnWeight, DistanceTableId num) throw () {
    if (startPoints.size() == 1) {
        const DistanceTableDescriptor desc(startPoints[0], respawnWeight);
        if (distanceTable[num] == desc)
            return;
        distanceTable[num] = desc;
    }
    else
        distanceTable[num] = DistanceTableDescriptor();

    areaMap.clearDistanceTable(num);

    priority_queue<const Area*, vector<const Area*>, DistanceComparison> workQueue((DistanceComparison(num)));
    for (vector<Area*>::const_iterator ai = startPoints.begin(); ai != startPoints.end(); ++ai) {
        (*ai)->distance[num] = 0;
        workQueue.push(*ai);
    }
    while (!workQueue.empty()) {
        const Area* const a = workQueue.top();
        workQueue.pop();
        for (vector<Area::Neighbor>::const_iterator ni = a->neighbors().begin(); ni != a->neighbors().end(); ++ni) {
            if (ni->area->distance[num] != -1) // already labeled
                continue;
            double dist = 1.;
            dist -= ni->area->respawnValue[ myTeam()] * respawnWeight * .5;
            dist += ni->area->respawnValue[!myTeam()] * respawnWeight;
            nAssert(dist >= 0.);
            ni->area->distance[num] = a->distance[num] + static_cast<int>(dist * roomToRoomBaseDistance);
            workQueue.push(ni->area);
        }
    }
}

void Robot::setDestination(Area* const target) throw () {
    nAssert(target);
    if (target == destination)
        return;
    destination = target;
    #ifdef BOTDEBUG
    fprintf(stderr, "%d: Set destination: %d %d\n", me, target->room.x, target->room.y);
    #endif

    BuildDistanceTable(target, myRespawnWeight(), Table_Destination);
}

ClientControls Robot::MoveToDestination() const throw () {
    if (destinationType == Dest_None)
        return ClientControls();

    const Area* const here = myArea();

    if (here->distance[Table_Destination] == -1 || destination == here)
        return ClientControls();

    int bestDistance = here->distance[Table_Destination];
    vector<const Area::Neighbor*> goodNeighbors;
    bool oldDestinationFound = false;
    for (vector<Area::Neighbor>::const_iterator ni = here->neighbors().begin(); ni != here->neighbors().end(); ++ni) {
        const Area* const na = ni->area;
        if (na->distance[Table_Destination] <= bestDistance) {
            if (HaveFlag(me)) {
                const TeamCounts tc = Teams(na, false);
                if (tc.enemies > tc.friends + 1)
                    continue;
            }
            if (na->distance[Table_Destination] < bestDistance) {
                bestDistance = na->distance[Table_Destination];
                goodNeighbors.clear();
                oldDestinationFound = false;
            }
            if (&*ni == immediateDestination)
                oldDestinationFound = true;
            if (!dangerousExplosionInNeighbor(*ni))
                goodNeighbors.push_back(&*ni);
        }
    }

    if (oldDestinationFound && dangerousExplosionInNeighbor(*immediateDestination)) // we keep the same destination and wait for the explosion to settle because it won't take long
        return ClientControls();

    if (!oldDestinationFound)
        immediateDestination = goodNeighbors.empty() ? 0 : goodNeighbors[rand() % goodNeighbors.size()];

    if (immediateDestination)
        return MoveToDoor(*immediateDestination);
    else
        return ClientControls();
}

Coords Robot::nearestDoor(const Area::Neighbor& neighbor, const Coords& pos) const throw (AlreadyInRoom) {
    int fixedTarget;
    bool xFixed;
    switch (neighbor.direction) {
    /*break;*/ case Area::Neighbor::Up:
            fixedTarget = 0;
            xFixed = false;
            if (pos.y < 0) // if predicted correctly, we're already in the target room (by the time our controls reach the server)
                throw AlreadyInRoom();
        break; case Area::Neighbor::Down:
            fixedTarget = S_H;
            xFixed = false;
            if (pos.y > S_H)
                throw AlreadyInRoom();
        break; case Area::Neighbor::Left:
            fixedTarget = 0;
            xFixed = true;
            if (pos.x < 0)
                throw AlreadyInRoom();
        break; case Area::Neighbor::Right:
            fixedTarget = S_W;
            xFixed = true;
            if (pos.x > S_W)
                throw AlreadyInRoom();
        break; default:
            nAssert(0);
    }
    const double myFreeCoord = xFixed ? pos.y : pos.x;
    vector< pair<double, double> >::const_iterator nearestDoor = neighbor.doors.end();
    double minDist = 1e10;
    for (vector< pair<double, double> >::const_iterator di = neighbor.doors.begin(); di != neighbor.doors.end(); ++di) { // find the nearest door
        if (di->first <= myFreeCoord && di->second >= myFreeCoord) {
            nearestDoor = di;
            break;
        }
        const double dist = di->first <= myFreeCoord ? myFreeCoord - di->second : di->first - myFreeCoord;
        if (dist < minDist) {
            nearestDoor = di;
            minDist = dist;
        }
    }
    nAssert(nearestDoor != neighbor.doors.end());
    const double doorMiddle = (nearestDoor->first + nearestDoor->second) / 2;
    const double niceLow  = min(doorMiddle, nearestDoor->first  + 4 * PLAYER_RADIUS); // if possible, take some headroom, mainly to avoid bumping into walls
    const double niceHigh = max(doorMiddle, nearestDoor->second - 4 * PLAYER_RADIUS);
    const double freeTarget = bound(myFreeCoord, niceLow, niceHigh);
    if (xFixed)
        return Coords(fixedTarget, freeTarget);
    else
        return Coords(freeTarget, fixedTarget);
}

ClientControls Robot::MoveToDoor(const Area::Neighbor& neighbor) const throw () {
    const Area::Neighbor::Direction dir = neighbor.direction;
    try {
        const Coords door = nearestDoor(neighbor, myPos.local());
        return MoveToNoAggregate(door + PLAYER_RADIUS * Vec(xDelta(dir), yDelta(dir)) - myPos.local(), 0);
    } catch (AlreadyInRoom) {
        ClientControls ctrl;
        ctrl.setRun();
        switch (dir) {
        /*break;*/ case Area::Neighbor::Up:    return ctrl.setUp();
            break; case Area::Neighbor::Down:  return ctrl.setDown();
            break; case Area::Neighbor::Left:  return ctrl.setLeft();
            break; case Area::Neighbor::Right: return ctrl.setRight();
        }
        nAssert(0);
        return ctrl;
    }
}

int Robot::GetPlayers(int team) const throw () {
    int npl = 0;
    for (int i = 0; i < maxplayers; ++i) {
        const ClientPlayer& player = fx.player[i];
        if (player.used && player.team() == team)
            ++npl;
    }
    return npl;
}

bool Robot::flagIgnored(const Flag& flag, const WorldCoords& base, int team) throw () {
    if (HaveFlag(me))
        return false;

    bool atBase;
    if (flag.carried() || flag.position().room != base.room)
        atBase = false;
    else {
        const Vec dist = base.local() - flag.position().local();
        atBase = fabs(dist.x) <= 5. && fabs(dist.y) <= 5.;
    }

    const bool limitInterest = atBase && team == myTeam() || flag.carried() && myTeam(fx.player[flag.carrier()]);
    const bool droppedEnemyFlag = !atBase && !flag.carried() && team == !myTeam() && !carry_own_team_flag; // flags in fear of being returned by the enemy
    if (!limitInterest && !droppedEnemyFlag || flag.carried() && fx.player[flag.carrier()].posUpdated < fx.frame - FADEOUT)
        return false;

    const WorldCoords pos = flag.carried() ? fx.player[flag.carrier()].pos : flag.position();
    BuildDistanceTable(area(pos), 0., Table_Def);
    const int myDistance = myArea()->distance[Table_Def];

    if (droppedEnemyFlag) {
        if (myDistance < 2 * roomToRoomBaseDistance)
            return false;
        int nearEnemyDistance = myDistance, nearFriendDistance = myDistance;
        for (int i = 0; i < maxplayers; ++i) {
            const ClientPlayer& player = fx.player[i];
            if (!player.used || player.dead || i == me || player.room().x >= fx.map.w || player.room().y >= fx.map.h || player.posUpdated < fx.frame - FADEOUT)
                continue;
            const int distance = area(player)->distance[Table_Def];
            if (myTeam(player))
                nearFriendDistance = min(nearFriendDistance, distance);
            else
                nearEnemyDistance = min(nearEnemyDistance, distance);
        }
        return nearFriendDistance >= 2 * roomToRoomBaseDistance && nearEnemyDistance < nearFriendDistance - roomToRoomBaseDistance * 3 / 2;
    }

    const int nAllFlags = fx.map.tinfo[0].flags.size() + fx.map.tinfo[1].flags.size() + fx.map.wild_flags.size();
    const int maxPlayers = flag.carried() ? GetPlayers(myTeam()) / 2
                                          : GetPlayers(myTeam()) / nAllFlags;
    if (maxPlayers == 0)
        return true;

    int nearNum = 0;
    for (int i = 0; i < maxplayers; ++i) {
        const ClientPlayer& player = fx.player[i];
        if (!player.used || !myTeam(player) || player.dead || i == me || player.room().x >= fx.map.w || player.room().y >= fx.map.h)
            continue;
        const int distance = area(player)->distance[Table_Def];
        if (distance < myDistance || distance == myDistance && moreDefensive(player))
            if (++nearNum >= maxPlayers)
                return true;
    }
    return false;
}

bool Robot::EnemyHasUnseenFlags(bool wild) const throw () {
    const vector<Flag>& flags = wild ? fx.wild_flags : fx.teams[myTeam()].flags();

    for (vector<Flag>::const_iterator fi = flags.begin(); fi != flags.end(); ++fi) {
        if (!fi->carried())
            continue;
        const ClientPlayer& pl = fx.player[fi->carrier()];

        if (!pl.used || pl.room().x >= fx.map.w || pl.room().y >= fx.map.h)
            return true;
        if (myTeam(pl))
            continue;
        if (pl.posUpdated < max(fx.frame - FADEOUT, fx.map[pl.room()].enemies_seen_frame))
            return true;
    }
    return false;
}

void Robot::ChooseDestination() throw () { // NEED rewrite
    const int flag = HaveFlag(me);
    destinationType = Dest_None;

    if (!flag) {
        const bool sef = !lock_team_flags_in_effect; // try to steal enemy flags?
        const bool swf = !lock_wild_flags_in_effect; // try to steal wild flags?

        const bool deb = (EnemyHasUnseenFlags(true) || EnemyHasUnseenFlags(false)) && capture_on_team_flags_in_effect;
        TargetNearest(sef, sef,   1,   1,
                      sef,   1,   1,   1,
                      swf, swf,   1,   1,
                        0,   0,
                      deb,   0,   0); // any flag that makes sense, or enemy base if they have an unseen flag
        if (destinationType == Dest_None) {
            for (int team = 0; team <= 2; ++team) // for lack of better things to do, stop ignoring already crowded targets
                for (vector<bool>::iterator fii = flagsIgnored[team].begin(); fii != flagsIgnored[team].end(); ++fii)
                    *fii = false;
            // only defend already defended flags if the team can make a capture with the currently controlled flags
            const bool controlOwn  = TeamHasFlags(myTeam(),  myTeam()) || IsAnyFlagAtBase(myTeam());
            const bool controlOpp  = TeamHasFlags(myTeam(), !myTeam());
            const bool controlWild = TeamHasFlags(myTeam(), 2);
            bool def = false;
            if (capture_on_team_flags_in_effect && controlOwn && (controlOpp || controlWild))
                def = true;
            if (capture_on_wild_flags_in_effect && controlOpp && (controlWild || lock_wild_flags_in_effect))
                def = true;
            if (!capture_on_wild_flags_in_effect && !capture_on_team_flags_in_effect)
                def = true;
            TargetNearest(sef, sef,   1, def,
                   sef && def,   1,   1, def,
                          swf, swf,   1, def,
                            0,   0,
                          sef,   0, swf); // any flag that makes sense (with ignores relaxed), or an empty base (hopefully getting its flag returned when captured soon)
        }
        if (destinationType == Dest_None) {
            TargetNearest(  0,   0,   0,   0,
                            0,   0,   0,   0,
                            0,   0,   0,   0,
                            1,   0,
                            0,   0,   0);  // if nothing else, target an enemy
        }
        if (destinationType == Dest_None || destinationType == Dest_Base) {
            if (destinationType == Dest_Base) {
                const TeamCounts tc = Teams(destination, true);
                if (tc.friends > tc.enemies) { // if we are going to base where is already our forces, forget it
                    if (destination != myArea() || tc.enemies == 0 && AmILast())
                        TargetFog();
                }
            }
            else
                TargetFog();
        }
    }
    else { // i am flagman ;)
        const bool ctf = capture_on_team_flags_in_effect;
        const bool cwfe = capture_on_wild_flags_in_effect;

        const bool cwf = flag != 3 && cwfe || flag == 3 && ctf && capture_away_from_base; // (normal or reverse) capture on wild flag?
        const bool cof = flag != 3 && ctf; // capture on own flag?
        const bool cmwf = capture_away_from_base && cwf; // (normal or reverse) capture on moved wild flag?
        const bool cmof = capture_away_from_base && cof; // capture on moved own flag?
        const bool cef  = capture_away_from_base && (flag == 3 && ctf || flag == 2 && cwfe); // (reverse) capture on enemy flag?
        //#fix: when to follow carriers when !capture_away_from_base

        if (GetPlayers(myTeam()) > 1) {
            TargetNearest(cef,  cef,   0,  cef,
                          cof, cmof,   0, cmof,
                          cwf, cmwf,   0, cmwf,
                            0,    0,
                            0,    0,   0); // available capture point
        }
        else {
            TargetNearest(cef,  cef,   0,   0,
                          cof,    1,   0,   0,
                          cwf, cmwf,   0,   0,
                            0,    0,
                            0,    0,   0); // available capture point, or dropped own team flag
        }
        if (destinationType == Dest_None) {
            TargetNearest(  0,   0,   0,   0,
                            0,   0,   0,   0,
                            0,   0,   0,   0,
                            0,   0,
                            0, ctf,   0);  // ok, to capture point, even if unavailable
        }
        if (destinationType == Dest_None) {
            TargetNearest(  0,   0,   0,   0,
                            0,   0,   0,   0,
                            0,   0,   0,   0,
                            0,   0,
                            0,   0, cwf);  // only go wait in an empty wild flag base as a last resort
        }
    }
}

double Robot::myRespawnWeight() const throw () {
    return HaveFlag(me) ? 1. : 0.;
}

bool Robot::IsMassive() const throw () {
    Vec d(0, 0);
    int n = 0;
    for (int i = 0; i < maxplayers; ++i) {
        const ClientPlayer& pl = fx.player[i];
        if (!pl.used || !myTeam(pl) || pl.dead || !here(pl))
            continue;

        const Vec diff = predictPos(pl) - myPos.local();
        d += Vec(fabs(diff.x), fabs(diff.y));
        ++n;
    }

    if (n > 1)
        return (d / n).mag() <= 2 * PLAYER_RADIUS;
    else
        return false;
}

int Robot::HaveFlag(int n) const throw () {
    for (int team = 0; team <= 2; ++team) {
        const vector<Flag>& flags = team == 2 ? fx.wild_flags : fx.teams[team].flags();
        for (vector<Flag>::const_iterator fi = flags.begin(); fi != flags.end(); ++fi)
            if (fi->carried() && fi->carrier() == n)
                return team == fx.player[n].team() ? 3 : team == 2 ? 2 : 1;
    }
    return 0;
}

bool Robot::TeamHasFlags(int carrierTeam, int flagTeam) const throw () {
    const vector<Flag>& flags = flagTeam == 2 ? fx.wild_flags : fx.teams[flagTeam].flags();
    for (vector<Flag>::const_iterator fi = flags.begin(); fi != flags.end(); ++fi)
        if (fi->carried() && fx.player[fi->carrier()].team() == carrierTeam)
            return true;
    return false;
}

bool Robot::IsFlagAtBase(const Flag& f, int team, bool captureableEnough) const throw () {
    if (f.carried())
        return false;
    const vector<WorldCoords>& bases = fx.map.tinfo[team].flags;
    for (vector<WorldCoords>::const_iterator bi = bases.begin(); bi != bases.end(); ++bi) {
        const int treshold = captureableEnough ? 2 * FLAG_RADIUS + PLAYER_RADIUS : 5; // flag_r+player_r for the player to touch the flag, and another flag_r for the player to touch the capture-point as well
        if (bi->room == f.position().room && (bi->local() - f.position().local()).mag() <= treshold)
            return true;
    }
    return false;
}

bool Robot::IsAnyFlagAtBase(int team) const throw () {
    const vector<Flag>& flags = team == 2 ? fx.wild_flags : fx.teams[team].flags();
    for (vector<Flag>::const_iterator fi = flags.begin(); fi != flags.end(); ++fi)
        if (IsFlagAtBase(*fi, team, true))
            return true;
    return false;
}

void Robot::TargetNearestBase(int& m_distance, Area*& targetArea, int team) throw () {
    const vector<WorldCoords>& tflags = fx.map.tinfo[team].flags;
    int distance = 0;

    for (vector<WorldCoords>::const_iterator pi = tflags.begin(); pi != tflags.end(); ++pi) {
        Area* const a = area(*pi);
        distance = a->distance[Table_Main];
        if (distance == -1)
            continue;
        if (distance < m_distance || m_distance == -1) {
            m_distance = distance;
            targetArea = a;
            destinationType = Dest_Base;
        }
    }
}

void Robot::TargetNearestTeam(int& m_distance, Area*& targetArea, int team) throw () {
    // looking for soldiers
    const bool enemy = myTeam() != team;

    for (int i = 0; i < maxplayers; ++i) {
        const ClientPlayer& pl = fx.player[i];
        if (i == me || !pl.used || pl.team() != team || pl.dead || pl.room().x >= fx.map.w || pl.room().y >= fx.map.h)
            continue;

        if (enemy) {
            if (fx.frame - pl.posUpdated > FADEOUT)
                continue;
            if (fx.map[pl.room()].enemies_seen_frame > pl.posUpdated)
                continue;
        }

        Area* const a = area(pl);
        const int distance = a->distance[Table_Main];
        if (distance == -1)
            continue;

        if (distance < m_distance || m_distance == -1) {
            m_distance = distance;
            targetArea = a;
            destinationType = Dest_Team;
        }
    }
}

bool Robot::IsHome(const Area* a) const throw () {
    const vector<WorldCoords>& tflags = fx.map.tinfo[myTeam()].flags;
    // our bases
    for (vector<WorldCoords>::const_iterator pi = tflags.begin(); pi != tflags.end(); ++pi)
        if (area(*pi) == a)
            return true;
    return false;
}

bool Robot::IsFlagsAtBases(int team) const throw () {
    const vector<Flag>& flags = (team != 2) ? fx.teams[team].flags() : fx.wild_flags;
    for (vector<Flag>::const_iterator fi = flags.begin(); fi != flags.end(); ++fi)
        if (fi->carried() || !IsFlagAtBase(*fi, team, true))
            return false;
    return true;
}

void Robot::TargetNearestFlag(int& m_distance, Area*& targetArea, int team, int state) throw () {
    // state - 0 - at base, 1 - dropped off base, 2 - carried by friends, 3 - carried by enemy

    const bool wantCarried = state == 2 || state == 3;
    const int carrierTeam = state == 2 ? myTeam() : !myTeam();

    const vector<Flag>& flags = (team != 2) ? fx.teams[team].flags() : fx.wild_flags;

    int index = 0;
    for (vector<Flag>::const_iterator fi = flags.begin(); fi != flags.end(); ++fi, ++index) {
        if (fi->carried() != wantCarried || flagsIgnored[team][index])
            continue;

        WorldCoords pos;
        if (fi->carried()) {
            const ClientPlayer& pl = fx.player[fi->carrier()];
            if (pl.team() != carrierTeam || !pl.used || pl.pid == me || pl.room().x >= fx.map.w || pl.room().y >= fx.map.h)
                continue;
            if (state == 3 && !pl.onscreen) { // check if the position is current enough
                if (fx.frame - pl.posUpdated > FADEOUT) // TODO fadeout
                    continue;
                if (fx.map[pl.room()].enemies_seen_frame > pl.posUpdated)
                    continue;
            }
            pos = pl.position();
        }
        else {
            if (IsFlagAtBase(*fi, team, true) != (state == 0))
                continue;
            pos = fi->position();
        }

        Area* const a = area(pos);
        int distance = a->distance[Table_Main];
        if (distance == -1)
            continue;

        if (state == 1 && team != 2 && distance <= roomToRoomBaseDistance * 3 / 2 && !carry_own_team_flag)
            distance = 0; // prioritize nearby dropped team flags over other targets if they can be returned

        if (distance < m_distance || m_distance == -1) {
            m_distance = distance;
            targetArea = a;
            destinationType = Dest_Flag;
        }
    }
}

void Robot::TargetFog() throw () {
    const Area* const here = myArea();
    double max_delta = 0;
    Area* target = 0;
    for (vector<Area::Neighbor>::const_iterator ni = here->neighbors().begin(); ni != here->neighbors().end(); ++ni) {
        Area* const na = ni->area;
        if (destinationType == Dest_Base && na->distance[Table_Destination] > 3 * roomToRoomBaseDistance)
            continue;
        const TeamCounts tc = Teams(na, false);
        if (tc.friends && !tc.enemies) // our sector
            continue;
        const double delta = fabs(fx.frame - fx.map[na->room].enemies_seen_frame);
        if (delta >= max_delta) {
            max_delta = delta;
            target = na;
        }
    }
    if (!target)
        return;

    destinationType = Dest_Fog;
    setDestination(target);
}

/* TargetNearest(enemy flag {at base, dropped off base, carried by enemy, carried by friend},
 *                  my flag {at base, dropped off base, carried by enemy, carried by friend},
 *                wild flag {at base, dropped off base, carried by enemy, carried by friend},
 *         {enemy, my} team,
 *   {enemy, my, wild} base)
 *
 * targets first one of the enabled options that yields the minimal distance among enabled options.
 *
 * flag: a flag with desired status and probably known location
 * team: a living non-me player with probably known location
 * base: a base, regardless of where its flag is
 */
void Robot::TargetNearest(int efb, int efd, int efce, int efcf,
                          int mfb, int mfd, int mfce, int mfcf,
                          int wfb, int wfd, int wfce, int wfcf,
                          int en,  int fr,
                          int eb,  int fb, int wb) throw () {
    int m_distance = -1;
    Area* targetArea = 0;
    const int t = myTeam();
    const int et = 1 - t;

    BuildDistanceTable(myArea(), myRespawnWeight(), Table_Main);

    destinationType = Dest_None;

    if (efb)  TargetNearestFlag(m_distance, targetArea, et, 0);
    if (efd)  TargetNearestFlag(m_distance, targetArea, et, 1);
    if (efce) TargetNearestFlag(m_distance, targetArea, et, 3);
    if (efcf) TargetNearestFlag(m_distance, targetArea, et, 2);
    if (mfb)  TargetNearestFlag(m_distance, targetArea,  t, 0);
    if (mfd)  TargetNearestFlag(m_distance, targetArea,  t, 1);
    if (mfce) TargetNearestFlag(m_distance, targetArea,  t, 3);
    if (mfcf) TargetNearestFlag(m_distance, targetArea,  t, 2);
    if (wfb)  TargetNearestFlag(m_distance, targetArea,  2, 0);
    if (wfd)  TargetNearestFlag(m_distance, targetArea,  2, 1);
    if (wfce) TargetNearestFlag(m_distance, targetArea,  2, 3);
    if (wfcf) TargetNearestFlag(m_distance, targetArea,  2, 2);
    if (en)   TargetNearestTeam(m_distance, targetArea, et);
    if (fr)   TargetNearestTeam(m_distance, targetArea,  t);
    if (eb)   TargetNearestBase(m_distance, targetArea, et);
    if (fb)   TargetNearestBase(m_distance, targetArea,  t);
    if (wb)   TargetNearestBase(m_distance, targetArea,  2);

    #ifdef DEBUGSTRATEGY
    fprintf(stderr, "%d %s: ", static_cast<int>(fx.frame / 10) - map_start_time, fx.player[me].name.c_str());
    fprintf(stderr, "TargetNearest(%d, %d, %d, %d,   %d, %d, %d, %d,   %d, %d, %d, %d,   %d, %d,   %d, %d, %d) -> %d\n",
            efb, efd, efce, efcf,
            mfb, mfd, mfce, mfcf,
            wfb, wfd, wfce, wfcf,
            en,  fr,
            eb,  fb, wb,
            destinationType);
    #endif

    if (destinationType == Dest_None) // nothing todo
        return;

    nAssert(targetArea);

    setDestination(targetArea);
}

bool Robot::IsMission() const throw () {
    const int to_home = IsHome(destination);
    // if we are looking for flag or going to our base for something
    if (destination == myArea())
        return false;
    return HaveFlag(me) || destinationType == Dest_Flag || to_home || !to_home && destinationType == Dest_Base;
}

void Robot::updateUnknownPosition(ClientPlayer& pl) throw () {
    if (pl.posUpdated < fx.frame - FADEOUT) // we don't care about them anymore
        return;
    if (pl.posUpdated >= fx.map[pl.room()].enemies_seen_frame) // they could still be there
        return;

    // see where they could have gone
    const Area* a = area(pl);
    WorldCoords posGuess;
    double timeGuess = 0; // initialized to please GCC
    bool haveGuess = false;
    for (vector<Area::Neighbor>::const_iterator ni = a->neighbors().begin(); ni != a->neighbors().end(); ++ni) {
        Coords doorPos;
        try {
            doorPos = nearestDoor(*ni, pl.pos.local());
        } catch (AlreadyInRoom) {
            nAssert(0);
        }
        const double doorDist = (doorPos - pl.pos.local()).mag();
        const double earliestTimeThere = pl.posUpdated + ceil(doorDist / fx.physics.max_run_speed);
        if (earliestTimeThere > fx.frame)
            continue;

        const Area* n = ni->area;
        const double nSeen = fx.map[n->room].enemies_seen_frame;
        if (nSeen >= fx.frame - 10)
            continue; // actually, they could have went in and out before nSeen if there was a period of the room not being seen, but such is hard to keep track of

        if (haveGuess) // many possible neighbors, don't try to guess
            return; //#improve: ideally, save the info about rooms that were found impossible, to work better when some current choices are proven impossible

        const double lx = doorPos.x - xDelta(ni->direction) * S_W,
                     ly = doorPos.y - yDelta(ni->direction) * S_H; // move from this-room-coords to relative to the neighbor
        posGuess = WorldCoords(n->room, lx, ly);
        timeGuess = max(nSeen + 10, earliestTimeThere);
        haveGuess = true;
    }
    if (haveGuess) {
        #ifdef BOTDEBUG
        fprintf(stderr, "%d %s: ", static_cast<int>(fx.frame / 10) - map_start_time, fx.player[me].name.c_str());
        fprintf(stderr, "Guessing %s moved to %d,%d (%d,%d) %.1f s ago - last seen at %d,%d/%d,%d %.1f s ago, verified away %.1f s ago\n", pl.name.c_str(),
                posGuess.room.x, posGuess.room.y, (int)posGuess.x, (int)posGuess.y,
                (fx.frame - timeGuess) / 10.,
                pl.room().x, pl.room().y, (int)pl.pos.x, (int)pl.pos.y,
                (fx.frame - pl.posUpdated) / 10.,
                (fx.frame - fx.map[pl.room()].enemies_seen_frame) / 10.);
        #endif
        pl.setPosition(posGuess, timeGuess); // leaves no mark about the position being a guess, but that isn't terrible
    }
    else
        pl.posUpdated = fx.frame - FADEOUT; // "forget" the position
}

ClientControls Robot::getRobotControls() throw () {
    fx.map[fx.player[me].room()].enemies_seen_frame = fx.frame;

    if (fx.player[me].item_shadow_time > fx.frame / 10.) {
        for (int x = 0; x < fx.map.w; ++x)
            for (int y = 0; y < fx.map.h; ++y) {
                double& esf = fx.map[RoomCoords(x, y)].enemies_seen_frame;
                esf = max(esf, fx.frame - 10); // estimate at most 10 frames to send everyone's position (assumes that we already had shadow 10 frames ago)
            }
    }

    for (int pi = 0; pi < maxplayers; ++pi) {
        const ClientPlayer& p = fx.player[pi];
        if (!p.used || p.dead || !myTeam(p))
            continue;
        if (p.posUpdated == fx.frame && p.fromMinimapUpdate && p.prevMapPosUpdateFrame >= p.posUpdated - 20 && p.prevMapUpdateRoom == p.room()) {
            double& esf = fx.map[p.room()].enemies_seen_frame;
            esf = max(esf, p.prevMapPosUpdateFrame); // we'd expect to have received information of any enemies in the room between the two updates of the friend
        }
    }

    bool enemiesInRoomNow = false;
    for (int pi = 0; pi < maxplayers; ++pi) {
        ClientPlayer& p = fx.player[pi];
        if (p.used && !p.dead && !myTeam(p)) {
            if (here(p, true))
                enemiesInRoomNow = true;
            else
                updateUnknownPosition(p);
        }
    }

    if (!enemiesInRoomNow && enemiesInRoom) { // randomize movement when the last enemy observer is lost
        freeWalkTarget.x = -1;
        immediateDestination = 0;
    }
    enemiesInRoom = enemiesInRoomNow;

    for (int team = 0; team <= 2; ++team) {
        flagsIgnored[team].clear();
        const vector<WorldCoords>& bases = team == 2 ? fx.map.wild_flags : fx.map.tinfo[team].flags;
        const vector<Flag>& flags = team == 2 ? fx.wild_flags : fx.teams[team].flags();
        for (int fi = 0; fi < (int)flags.size(); ++fi)
            flagsIgnored[team].push_back(flagIgnored(flags[fi], bases[fi], team));
    }

    if (last_seen != -1) {
        const ClientPlayer& lsp = fx.player[last_seen];
        if (!lsp.used || myTeam(lsp) || lsp.dead || !here(lsp, true)) // lost target
            last_seen = -1;
    }

    if (!IsMassive()) {
        const int dangerousRocket = GetDangerousRocket();
        if (dangerousRocket != -1) {
            if (last_seen == -1)
                last_seen = GetDangerousEnemy();
            #ifdef DEBUGSTRATEGY
            fprintf(stderr, "%d %s: ", static_cast<int>(fx.frame / 10) - map_start_time, fx.player[me].name.c_str());
            fprintf(stderr, "Escaping rocket.\n");
            #endif
            return EscapeRocket(dangerousRocket);
        }
    }

    ClientControls ctrl = EscapeExplosion();
    if (!ctrl.idle()) {
        #ifdef DEBUGSTRATEGY
        fprintf(stderr, "%d %s: ", static_cast<int>(fx.frame / 10) - map_start_time, fx.player[me].name.c_str());
        fprintf(stderr, "Escaping explosion.\n");
        #endif
        return ctrl;
    }

    ctrl = GetFlag();
    if (!ctrl.idle()) { // if any
        #ifdef DEBUGSTRATEGY
        fprintf(stderr, "%d %s: ", static_cast<int>(fx.frame / 10) - map_start_time, fx.player[me].name.c_str());
        fprintf(stderr, "Targetting flag.\n");
        #endif
        return ctrl;
    }

    ChooseDestination();

    if (IsMission()) // get only dangerous enemy
        last_seen = GetDangerousEnemy();
    else if (last_seen != -1) { // already locked on someone
        const int easy = GetEasyEnemy(); // more easy???
        if (easy != -1)
            last_seen = easy;
    }
    else // get someone
        last_seen = GetNearestEnemy();

    const bool importantMission = HaveFlag(me) && IsMission();

    if (!importantMission && last_seen != -1 && !fx.physics.allowFreeTurning) {
        #ifdef DEBUGSTRATEGY
        fprintf(stderr, "%d %s: ", static_cast<int>(fx.frame / 10) - map_start_time, fx.player[me].name.c_str());
        fprintf(stderr, "Targetting enemy.\n");
        #endif
        return Aim(last_seen);
    }

    // ok, free tour ;)
    ctrl = GetPowerup(importantMission);
    if (!ctrl.idle()) {
        #ifdef DEBUGSTRATEGY
        fprintf(stderr, "%d %s: ", static_cast<int>(fx.frame / 10) - map_start_time, fx.player[me].name.c_str());
        fprintf(stderr, "Targetting powerup.\n");
        #endif
        return ctrl;
    }

    ctrl = Escape();
    if (!ctrl.idle()) {
        #ifdef DEBUGSTRATEGY
        fprintf(stderr, "%d %s: ", static_cast<int>(fx.frame / 10) - map_start_time, fx.player[me].name.c_str());
        fprintf(stderr, "Escaping from room.\n");
        #endif
        return ctrl;
    }

    ctrl = MoveToDestination();
    if (!ctrl.idle()) {
        #ifdef DEBUGSTRATEGY
        fprintf(stderr, "%d %s: ", static_cast<int>(fx.frame / 10) - map_start_time, fx.player[me].name.c_str());
        fprintf(stderr, "Going to %d,%d (target type %d).\n", destination->room.x, destination->room.y, destinationType);
        #endif
        return ctrl;
    }

    ctrl = FollowFlag();
    if (!ctrl.idle()) {
        #ifdef DEBUGSTRATEGY
        fprintf(stderr, "%d %s: ", static_cast<int>(fx.frame / 10) - map_start_time, fx.player[me].name.c_str());
        fprintf(stderr, "Following a carrier.\n");
        #endif
        return ctrl;
    }

    ctrl = FreeWalk();
    if (last_seen == -1)
        ctrl.clearRun();
    #ifdef DEBUGSTRATEGY
    fprintf(stderr, "%d %s: ", static_cast<int>(fx.frame / 10) - map_start_time, fx.player[me].name.c_str());
    if (destinationType == Dest_None)
        fprintf(stderr, "Nothing to do.\n");
    else
        fprintf(stderr, "Nothing to do, already at target [type %d].\n", destinationType);
    #endif
    return ctrl;
}

ClientControls Robot::RobotMain() throw () {
    const bool hide_map = !map_ready || gameover_plaque != NEXTMAP_NONE || fx.skipped || me < 0 || me >= maxplayers;

    for (int p = 0; p < maxplayers; ++p)
        if (fx.player[p].dead)
            fx.player[p].defending = fx.player[p].defendingAfterDeath;

    if (hide_map || !fx.player[me].used || fx.player[me].dead || fx.player[me].team() != 0 && fx.player[me].team() != 1 ||
                    fx.player[me].room().x >= fx.map.w || fx.player[me].room().y >= fx.map.h) {
        myGundir = -1;
        freeWalkTarget.x = -1;
        if (botPrevFire) {
            BinaryBuffer<16> msg;
            msg.U8(data_fire_off);
            client->send_message(msg);
            botPrevFire = false;
        }
        return ClientControls();
    }

    futureMe = fx.player[me];
    const uint8_t nControlFrames = clFrameSent - clFrameWorld;
    averageLag = nControlFrames + .5;
    if (nControlFrames)
        fx.extrapolateSinglePlayerPosition(futureMe, controlHistory, clFrameWorld + 1, clFrameSent, 1.5);
    else
        fx.extrapolateSinglePlayerPosition(futureMe, controlHistory, clFrameSent, clFrameSent, .5);
    myPos = futureMe.pos;

    if (myGundir == -1) // was dead, or something like that
        myGundir = fx.player[me].gundir.to8way();

    ClientControls ctrl = getRobotControls();

    const int currentGundir = myGundir;

    if (!ctrl.isStrafe() && ctrl.getDirection() != -1)
        myGundir = ctrl.getDirection();

    bool shoot;
    if (fx.physics.allowFreeTurning) {
        const pair<bool, GunDirection> shootDir = NeedShootFreeTurning(GunDirection().from8way(myGundir)); // if there's no player to target, aim where we're going
        shoot = shootDir.first;
        // adjust gunDir
        static const double turnCeilingPerFrame = N_PI_2;
        static const double displacementMul = .7;
        static const double shootTreshold = N_PI / 8.; // shoot if aim is within 22° of target
        const double targetDiff = positiveFmod(shootDir.second.toRad() - gunDir.toRad() + N_PI, 2 * N_PI) - N_PI;
        double actualDiff = targetDiff;
        if (fabs(actualDiff) > turnCeilingPerFrame)
            actualDiff = turnCeilingPerFrame * (actualDiff > 0 ? +1. : -1.);
        const double modifier = (rand() % 10001 - 5000) / 5000.;
        actualDiff *= 1. + displacementMul * modifier * modifier * modifier; // actually turn by something between actualDiff * (1 +/- displacementMul), weighted so that values in the middle are more likely than extremes
        gunDir.adjust(actualDiff);
        if (fabs(actualDiff - targetDiff) > shootTreshold)
            shoot = false;
    }
    else {
        const pair<bool, int> shootDir = NeedShootTradTurning();
        shoot = shootDir.first;
        if (shoot && shootDir.second != myGundir) {
            if (shootDir.second == currentGundir) {
                nAssert(!ctrl.isStrafe());
                ctrl.setStrafe();
                myGundir = currentGundir;
            }
            else {
                ctrl.fromDirection(shootDir.second);
                myGundir = shootDir.second;
            }
        }
    }
    if (shoot) {
        if (!botPrevFire) {
            BinaryBuffer<16> msg;
            msg.U8(data_fire_on);
            client->send_message(msg);
            botPrevFire = true;
        }
    }
    else if (botPrevFire) {
        BinaryBuffer<16> msg;
        msg.U8(data_fire_off);
        client->send_message(msg);
        botPrevFire = false;
    }

    return ctrl;
}

Robot::Robot(const ClientExternalSettings& config, Log& clientLog, MemoryLog& externalErrorLog_, ControlledPtr<LocalConnection> conn) throw () :
    ClientBase(config, clientLog, externalErrorLog_),
    connection(conn),
    connectionWrapper(0),
    connectQueued(false),
    sharedDataHandle(g_botSharedDataStorage),
    finished(false),
    botPrevFire(false)
{ }

Robot::~Robot() throw () {
    delete connection;
}

void Robot::bot_start(const Network::Address& addr, int ping, const string& name, int bot_id) throw () {
    Lock ml(frameMutex);
    #ifndef DEDICATED_SERVER_ONLY
    botmode = true;
    #endif
    botId = bot_id;
    serverIP = addr;

    connectionWrapper = new ClientLocalConnection(*connection);
    startBase(give_control(connectionWrapper));

    playername = name;

    botReactedFrame = -1;

    set_ping(ping);

    connectQueued = true;
}

void Robot::set_ping(int ping) throw () {
    connection->sc.setPing(ping);
}

void Robot::client_connected(ConstDataBlockRef data) throw () { // call with frameMutex locked
    ClientBase::client_connected(data);

    bot_send_frame(ClientControls());
}

void Robot::client_disconnected(ConstDataBlockRef data) throw () {
    try {
        BinaryDataBlockReader read(data);

        const uint8_t reason = read.U8();
        numAssert2(!read.hasMore() && (reason == server_c::disconnect_client_initiated || reason == server_c::disconnect_server_shutdown
                                       || reason == server_c::disconnect_timeout || reason == disconnect_kick),
                   data.size(), reason);
    } catch (BinaryReader::ReadError) {
        nAssert(0);
    }

    stop();
}

void Robot::connect_command() throw () {   // call with frameMutex locked
    prepareForConnect();
    connect(serverIP.toString(), bot_password, "");
}

bool Robot::firstBotInTeam() const throw () {
    for (int i = 0; i < maxplayers; ++i) {
        const ClientPlayer& p = fx.player[i];
        if (p.used && myTeam(p) && p.name.substr(0, 4) == "BOT ")
            return i == me;
    }
    return true; // possible if we've been renamed to something without "BOT "
}

void Robot::net_text_message(Message_type type, int sender_team, const string& text) throw () {
    if (type != msg_team || sender_team != myTeam())
        return;
    const string::size_type colon = text.find(": ");
    if (colon == string::npos || colon >= text.length() - 2)
        return;
    const string senderName = text.substr(0, colon);
    int senderPid = -1;
    for (int i = 0; i < maxplayers; ++i) {
        const ClientPlayer& pl = fx.player[i];
        if (pl.used && myTeam(pl) && pl.name == senderName) {
            senderPid = i;
            break;
        }
    }
    if (senderPid == -1)
        return;
    ClientPlayer& sender = fx.player[senderPid];
    const string msg = text.substr(colon + 2);
    string description;
    if (msg == "d" || msg == "D" || msg == "def" || msg == "defending") {
        sender.defending = true;
        sender.defendingAfterDeath = msg != "d";
        description = sender.name + " prefers defending";
        if (!sender.defendingAfterDeath)
            description += " until dead, then attacking";
    }
    else if (msg == "a" || msg == "A" || msg == "att" || msg == "attacking") {
        sender.defending = false;
        sender.defendingAfterDeath = msg == "a";
        description = sender.name + " prefers attacking";
        if (sender.defendingAfterDeath)
            description += " until dead, then defending";
    }
    else
        return;
    if (firstBotInTeam()) {
        BinaryBuffer<256> msg;
        msg.U8(data_text_message);
        msg.str(".Roger - " + description);
        client->send_message(msg);
    }
}

void Robot::bot_send_frame(ClientControls controls) throw () {
    ++clFrameSent;
    controlHistory[clFrameSent] = sentControls = controls;
    svFrameHistory[clFrameSent] = fx.frame + (get_time() - frameReceiveTime) * 10.;
    BinaryBuffer<256> msg;
    msg.U8(clFrameSent);
    msg.U8(sentControls.toNetwork(false));
    if (fx.physics.allowFreeTurning)
        msg.U16(gunDir.toNetworkLongForm());
    client->send_frame(msg);
}

void Robot::pollConnection() throw () {
    if (!connected) {
        nAssert(connectionWrapper);
        if (connectQueued) {
            connectQueued = false;
            Lock ml(frameMutex);
            connect_command();
        }
        if (connectionWrapper->readHelloReply().data()) {
            cfunc_connection_update(this, 0, connectionWrapper->readHelloReply());
            connectionWrapper->clearHelloReply();
        }
        return;
    }
    if (!connection->sc.connected()) {
        BinaryBuffer<1> data;
        data.U8(disconnect_kick);
        cfunc_connection_update(this, 1, data);
        return;
    }
    const ConstDataBlockRef frame = connectionWrapper->receive_frame();
    if (frame.data())
        cfunc_server_data(this, frame);
}

void Robot::bot_loop() throw () {
    pollConnection();

    Lock ml(frameMutex);

    handlePendingThreadMessages();

    if (!connected || fx.frame == botReactedFrame)
        return;

    #ifdef BOT_TEST_COMPARE_TWO // change team if necessary
    #ifdef ALTERNATE_BOT
    const int desiredTeam = 1;
    #else
    const int desiredTeam = 0;
    #endif
    nAssert(me >= 0 && me < maxplayers);
    if (me / TSIZE != desiredTeam) {
        if (clFrameSent >= 100) {
            disconnect_command();
            return;
        }
        if (botReactedFrame == -1) {
            BinaryBuffer<16> msg;
            msg.U8(data_change_team_on);
            client->send_message(msg);
        }
    }
    #endif

    botReactedFrame = fx.frame;

    while (clientReadiesWaiting > 0) {
        send_client_ready();
        --clientReadiesWaiting;
    }

    if (mapChanged) {
        mapChanged = false;
        BuildMap();
    }

    fx.cleanOldDeathbringerExplosions();

    ClientControls controls = RobotMain();
    controls.clearModifiersIfIdle();
    bot_send_frame(controls);
}

void Robot::stop() throw () {
    ClientBase::stop();
    finished = true;
}
