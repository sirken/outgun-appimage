/*
 *  robot.h
 *
 *  Copyright (C) 2006, 2008, 2009, 2010, 2011 - Niko Ritari
 *  Copyright (C) 2006, 2008 - Jani Rivinoja
 *  Copyright (C) 2006 - Peter Kosyh
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

#ifndef ROBOT_H_INC
#define ROBOT_H_INC

#include "bot_support.h"
#include "clientbase.h"
#include "client_interface.h"

class LocalConnection;
class ClientLocalConnection;

extern BotSharedDataStorage g_botSharedDataStorage;

class Robot : public ClientBase, public BotInterface {
    LocalConnection* connection; // owned
    ClientLocalConnection* connectionWrapper; // given to ClientBase to replace leetnet client
    bool connectQueued;

    enum DestinationType {
        Dest_None,
        Dest_Flag,
        Dest_Base,
        Dest_Team,
        Dest_Fog
    };

    BotSharedDataHandle sharedDataHandle;

    std::string bot_password;
    int botId;
    double botReactedFrame;
    bool finished;

    AreaMap areaMap;
    typedef AreaMap::Area Area;

    static const int roomToRoomBaseDistance = 1000; // distances are multiplies of this, barring modifiers

    struct DistanceTableDescriptor { // the actual table is distributed in all Areas
        const Area* center; // the Area that has distance=0, or 0 if there are multiple centers
        double respawnWeight;

        DistanceTableDescriptor() throw () : center(0) { }
        DistanceTableDescriptor(const Area* c, double rw) throw () : center(c), respawnWeight(rw) { }

        bool operator==(const DistanceTableDescriptor& o) const { return center == o.center && respawnWeight == o.respawnWeight; }
    };

    DistanceTableDescriptor distanceTable[Table_Max];
    DestinationType destinationType;
    const Area*     destination;
    mutable const Area::Neighbor* immediateDestination; // one of the neighboring rooms

    Coords      freeWalkTarget;

    bool        botPrevFire;
    bool        enemiesInRoom;
    int         last_seen;
    int         myGundir;
    ClientPlayer futureMe; // extrapolated by averageLag
    WorldCoords myPos; // shortcut to futureMe.pos

    std::vector<bool> flagsIgnored[3];

    struct TeamCounts {
        int enemies, friends;
    };

    class DistanceComparison { // helper to BuildDistanceTable
        DistanceTableId table;

    public:
        DistanceComparison(DistanceTableId tab) throw () : table(tab) { }
        bool operator()(const Area* a1, const Area* a2) const throw () { return a1->distance[table] > a2->distance[table]; }
    };


    void BuildMap() throw ();

          Area* area(const WorldCoords& c)       throw () { return areaMap.identifyArea(c); }
    const Area* area(const WorldCoords& c) const throw () { return areaMap.identifyArea(c); }
          Area* area(const ClientPlayer& p)       throw () { return area(p.position()); }
    const Area* area(const ClientPlayer& p) const throw () { return area(p.position()); }
          Area* myArea()       throw () { return area(myPos); }
    const Area* myArea() const throw () { return area(myPos); }

    bool        here(const ClientPlayer& p, bool roomEnough = false) const throw () { return p.posUpdated > fx.frame - 10 && p.room() == myPos.room && (roomEnough || area(p) == myArea()); }

    Coords      predictPos(const Coords& startPos, const Vec& vel) const throw () { return Coords(startPos + averageLag * vel); }
    Coords      predictPos(const ClientPlayer& p) const throw () { return predictPos(p.pos.local(), p.vel); }
    Coords      predictPos(const Rocket& r) const throw () { return predictPos(r.pos.local(), r.vel); }

    int         myTeam() const throw () { return fx.player[me].team(); }
    bool        myTeam(const ClientPlayer& p) const throw () { return p.team() == myTeam(); }

    static int  xDelta(Area::Neighbor::Direction dir) throw ();
    static int  yDelta(Area::Neighbor::Direction dir) throw ();

    double      predictDistanceFromRocket(Rocket rocket, const ClientControls& ctrl) const throw ();
    const DeathbringerExplosion* explosionInRoom(const RoomCoords& room) const throw (); // returns the dangerous deathbringer-explosion in the room, if any
    bool        imminentExplosionHere() const throw ();
    class AlreadyInRoom { }; // exception
    Coords      nearestDoor(const Area::Neighbor& neighbor, const Coords& pos) const throw (AlreadyInRoom);
    double      distanceFromDoor(const Area::Neighbor& n, const Coords& pos) const throw ();
    double      distanceFromDoor(const Area::Neighbor& n) const throw () { return distanceFromDoor(n, myPos.local()); }
    bool        dangerousExplosionInNeighbor(const Area::Neighbor& neighbor) const throw ();
    bool        moreDefensive(const ClientPlayer& player) const throw ();

    void        updateUnknownPosition(ClientPlayer& pl) throw ();

    bool        IsFlagsAtBases(int team) const throw (); // are flags of team at bases?
    bool        EnemyHasUnseenFlags(bool wild) const throw ();
    int         GetPlayers(int team) const throw (); // get num of players
    TeamCounts  Teams(const Area* a, bool countMe) const throw (); // get num of en and fr for sector
    bool        IsHome(const Area* a) const throw (); //is it base

    bool        AmILast() const throw ();
    bool        IsMission() const throw (); // have i mission? (No agression mode)
    int         GetEasyEnemy() const throw (); // get easy enemy to kill
    bool        IsMassive() const throw (); // am i berserker? (No rocket avoiding)
    int         HaveFlag(int n) const throw (); // 0 if n isn't carrying a flag, 1 if n carries an enemy flag, 2 if n carries a wild flag, 3 if n carries an own flag
    bool        TeamHasFlags(int team, int flagTeam) const throw ();
    bool        IsFlagAtBase(const Flag& f, int team, bool captureableEnough) const throw ();
    bool        IsAnyFlagAtBase(int team) const throw ();
    enum AimLevel { AL_None, AL_Near, AL_Full };
    std::pair<AimLevel, int> TryAimTradTurning(int target) const throw (); // returns how near the target is to the aim in the best direction (AL_None if behind a wall), and that direction
    std::pair<bool, GunDirection> TryAimFreeTurning(int target) const throw (); // returns (shoot?, direction)
    double      GetHitTime(const GunDirection& dir, int iTarget) const throw (); // approximate time until a rocket shot in dir from (mex,mey) would hit player iTarget assuming no walls ("big" if no hit)
    double      GetHitTeammateTime(const GunDirection& dir) const throw (); // approximate time until a rocket shot in dir from (mex,mey) would hit first teammate assuming no walls ("big" if no hit, including if friendly fire is off)

    bool        IsBehindWall(const Vec& delta, double radius, double maxDistanceFromTarget) const throw ();
    bool        IsBehindWall(const WorldCoords& startPos, const Vec& delta, double radius, double maxDistanceFromTarget) const throw ();
    double      ScanDir(GunDirection dir) const throw (); // return length to wall (or room border) in dir
    std::pair<double, Coords> WallHitPosition(GunDirection dir, double radius) const throw (); // return length to wall (or room border) in dir, and the hit position
    std::pair<bool, GunDirection> NeedShootFreeTurning(const GunDirection& defaultDir) throw (); // to shoot or not to shoot, and the gunDir to aim at (defaultDir if there's no target)
    std::pair<bool, int> ShootAtDoorTradTurning() throw (); // to shoot or not to shoot, and the direction if shooting
    std::pair<bool, int> NeedShootTradTurning() throw ();   // to shoot or not to shoot, and the direction if shooting
    GunDirection GetDir(const Vec& delta) const throw (); // 0 - 0, 2 - Pi/2, 3 - Pi...
    int         GetDangerousRocket() const throw (); // get danger rocket index
    int         GetDangerousEnemy() const throw (); // same for enemy
    int         GetNearestEnemy() const throw (); // get nearest enemy
    int         FreeDir() const throw (); // maximum free space at front

    ClientControls Aim(int i) const throw ();
    ClientControls EscapeRocket(int mrock) const throw ();
    ClientControls EscapeExplosion() const throw ();
    ClientControls captureOnFlag(bool carried) const throw ();
    ClientControls pickUpFlag() const throw ();
    ClientControls GetFlag() const throw ();
    ClientControls FollowFlag() const throw ();
    ClientControls GetPowerup(bool onImportantMission) const throw ();
    ClientControls MoveDirNoAggregate(int dir) const throw ();
    ClientControls MoveTo(const Vec& delta, double maxDistanceFromTarget) const throw ();
    ClientControls MoveToDoor(const Area::Neighbor& n) const throw ();
    ClientControls MoveToNoAggregate(const Vec& delta, double maxDistanceFromTarget) const throw ();
    ClientControls MoveIndirectlyTowards(const Vec& delta, double maxDistanceFromTarget) const throw ();
    ClientControls MoveDir(int dir) const throw ();
    ClientControls Escape() const throw ();
    ClientControls FreeWalk() throw ();
    ClientControls MoveToDestination() const throw ();

    void BuildDistanceTable(Area* startPoint, double respawnWeight, DistanceTableId num) throw (); // build distance table from single point
    void BuildDistanceTable(const std::vector<Area*>& startPoints, double respawnWeight, DistanceTableId num) throw (); // build distance table from multiple points
    void setDestination(Area* target) throw ();
    void ChooseDestination() throw ();

    double myRespawnWeight() const throw ();

    void TargetNearestBase(int& m_distance, Area*& nearestArea, int team) throw ();
    void TargetNearestTeam(int& m_distance, Area*& nearestArea, int team) throw ();
    void TargetNearestFlag(int& m_distance, Area*& nearestArea, int team, int state) throw ();
    void TargetFog() throw ();

    void TargetNearest(int efb, int efd, int efce, int efcf,
                       int mfb, int mfd, int mfce, int mfcf,
                       int wfb, int wfd, int wfce, int wfcf,
                       int en,  int fr,
                       int eb,  int fb, int wb) throw ();

    ClientControls getRobotControls() throw ();

    ClientControls RobotMain() throw ();

    bool flagIgnored(const Flag& flag, const WorldCoords& base, int team) throw ();

    bool firstBotInTeam() const throw ();

    void net_text_message(Message_type type, int sender_team, const std::string& text) throw ();

    void connect_command() throw ();    // call with frameMutex locked

    void client_connected(ConstDataBlockRef data) throw ();    // call with frameMutex locked
    void client_disconnected(ConstDataBlockRef data) throw ();

    void bot_send_frame(ClientControls controls) throw ();

    void pollConnection() throw ();

    std::string getPlayerPassword() const throw () { return std::string(); }

public:
    Robot(const ClientExternalSettings& config, Log& clientLog, MemoryLog& externalErrorLog_, ControlledPtr<LocalConnection> conn) throw ();
    ~Robot() throw ();

    void bot_start(const Network::Address& addr, int ping, const std::string& name, int botId) throw ();
    void stop() throw ();
    void bot_loop() throw ();
    void set_ping(int ping) throw ();
    bool is_connected() const throw () { return connected; }
    bool bot_finished() const throw () { return finished; }

    int bot_player_id() const throw () { return me; }
    double bot_reacted_frame() const throw () { return botReactedFrame; }
    uint8_t bot_sent_frame() const throw () { return clFrameSent; }

    void set_bot_password(const std::string& pass) throw () { bot_password = pass; }

    int team() const throw () { return me / TSIZE; }
};

#endif
