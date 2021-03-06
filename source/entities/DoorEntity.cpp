/*
 *  Copyright (C) 2011-2015  OpenDungeons Team
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "entities/DoorEntity.h"

#include "network/ODPacket.h"
#include "game/Seat.h"
#include "gamemap/GameMap.h"
#include "traps/Trap.h"
#include "traps/TrapDoor.h"
#include "traps/TrapManager.h"
#include "utils/Helper.h"
#include "utils/LogManager.h"

#include <iostream>

DoorEntity::DoorEntity(GameMap* gameMap, bool isOnServerMap, Seat* seat, const std::string& buildingName, const std::string& meshName,
        Tile* tile, Ogre::Real rotationAngle, bool hideCoveredTile, float opacity,
        const std::string& initialAnimationState, bool initialAnimationLoop) :
    TrapEntity(gameMap, isOnServerMap, buildingName, meshName, tile, rotationAngle, hideCoveredTile, opacity)
{
    setSeat(seat);
    mPrevAnimationState = initialAnimationState;
    mPrevAnimationStateLoop = initialAnimationLoop;
}

DoorEntity::DoorEntity(GameMap* gameMap, bool isOnServerMap) :
    TrapEntity(gameMap, isOnServerMap)
{
}

void DoorEntity::exportToPacket(ODPacket& os, const Seat* seat) const
{
    TrapEntity::exportToPacket(os, seat);
    int seatId = getSeat()->getId();
    os << seatId;
}

void DoorEntity::importFromPacket(ODPacket& is)
{
    TrapEntity::importFromPacket(is);
    int seatId;
    OD_ASSERT_TRUE(is >> seatId);
    Seat* seat = getGameMap()->getSeatById(seatId);
    if(seat == nullptr)
    {
        OD_LOG_ERR("Couldn't get seat id=" + Helper::toString(seatId) + ", name=" + getName());
        return;
    }
    setSeat(seat);
}

bool DoorEntity::canSlap(Seat* seat)
{
    Tile* tile = getPositionTile();
    if(tile == nullptr)
    {
        OD_LOG_ERR("entityName=" + getName());
        return false;
    }

    if(getGameMap()->isInEditorMode())
        return true;

    // Only the owning player can slap a door
    if(getSeat() != seat)
        return false;

    return true;
}

void DoorEntity::slap()
{
    if(!getIsOnServerMap())
        return;

    // TODO : We notify the trap that it should change the floodfill for owning seat. It should be
    // handled in the trap because we want to remind if a door is open/closed when saving a game. And
    // saving it in the trap is the easiest
    Trap* trap = getGameMap()->getTrapByName(mBaseName);
    if(trap == nullptr)
    {
        OD_LOG_ERR("name=" + getName() + ", trapname=" + mBaseName);
        return;
    }

    switch(trap->getType())
    {
        case TrapType::doorWooden:
            break;

        default:
        {
            OD_LOG_ERR("name=" + getName() + ", wrong type=" + TrapManager::getTrapNameFromTrapType(trap->getType()));
            return;
        }
    }

    Tile* posTile = getPositionTile();
    if(posTile == nullptr)
    {
        OD_LOG_ERR("name=" + getName());
        return;
    }

    TrapDoor* trapDoor = static_cast<TrapDoor*>(trap);
    trapDoor->notifyDoorSlapped(this, posTile);
}

DoorEntity* DoorEntity::getDoorEntityFromPacket(GameMap* gameMap, ODPacket& is)
{
    DoorEntity* obj = new DoorEntity(gameMap, false);
    return obj;
}
