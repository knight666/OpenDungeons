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

#include "rooms/RoomCrypt.h"

#include "entities/Creature.h"
#include "entities/SmallSpiderEntity.h"
#include "entities/Tile.h"
#include "game/Player.h"
#include "gamemap/GameMap.h"
#include "rooms/RoomManager.h"
#include "utils/ConfigManager.h"
#include "utils/LogManager.h"
#include "utils/Random.h"

static RoomManagerRegister<RoomCrypt> reg(RoomType::crypt, "Crypt", "Crypt room");

const int32_t OFFSET_TILE_X = 0;
const int32_t OFFSET_TILE_Y = -1;

RoomCrypt::RoomCrypt(GameMap* gameMap) :
    Room(gameMap),
    mRottenPoints(0)
{
    setMeshName("Crypt");
}

RenderedMovableEntity* RoomCrypt::notifyActiveSpotCreated(ActiveSpotPlace place, Tile* tile)
{
    switch(place)
    {
        case ActiveSpotPlace::activeSpotCenter:
        {
            mRottingCreatures[tile] = std::pair<Creature*,int32_t>(nullptr, -1);
            int rnd = Random::Int(0, 100);
            if (rnd < 33)
                return loadBuildingObject(getGameMap(), "KnightCoffin", tile, 0.0, false);
            else if (rnd < 66)
                return loadBuildingObject(getGameMap(), "CelticCross", tile, 0.0, false);
            else
                return loadBuildingObject(getGameMap(), "StoneCoffin", tile, 0.0, false);
        }
        case ActiveSpotPlace::activeSpotLeft:
        {
            return loadBuildingObject(getGameMap(), "KnightStatue", tile, 90.0, false);
        }
        case ActiveSpotPlace::activeSpotRight:
        {
            return loadBuildingObject(getGameMap(), "KnightStatue", tile, 270.0, false);
        }
        case ActiveSpotPlace::activeSpotTop:
        {
            return loadBuildingObject(getGameMap(), "KnightStatue2", tile, 0.0, false);
        }
        case ActiveSpotPlace::activeSpotBottom:
        {
            return loadBuildingObject(getGameMap(), "KnightStatue2", tile, 180.0, false);
        }
        default:
            break;
    }
    return nullptr;
}

void RoomCrypt::notifyActiveSpotRemoved(ActiveSpotPlace place, Tile* tile)
{
    Room::notifyActiveSpotRemoved(place, tile);
    if(place != ActiveSpotPlace::activeSpotCenter)
        return;

    std::pair<Creature*,int32_t> rottingCreature = mRottingCreatures[tile];
    mRottingCreatures.erase(tile);
    if(rottingCreature.first == nullptr)
        return;

    // If the dead creature is already rotting, we add it back to its tile so that it can continue
    // its normal dead creature life ^^
    if(rottingCreature.second == -1)
        return;

    rottingCreature.first->addEntityToPositionTile();
}

void RoomCrypt::absorbRoom(Room *r)
{
    if(r->getType() != getType())
    {
        OD_LOG_ERR("Trying to merge incompatible rooms: " + getName() + ", type=" + RoomManager::getRoomNameFromRoomType(getType()) + ", with " + r->getName() + ", type=" + RoomManager::getRoomNameFromRoomType(r->getType()));
        return;
    }
    RoomCrypt* rc = static_cast<RoomCrypt*>(r);
    mRottingCreatures.insert(rc->mRottingCreatures.begin(), rc->mRottingCreatures.end());
    rc->mRottingCreatures.clear();
    mRottenPoints += rc->mRottenPoints;

    Room::absorbRoom(r);
}

void RoomCrypt::doUpkeep()
{
    Room::doUpkeep();

    if(mCoveredTiles.empty())
        return;

    // Each central active spot has a probability to spawn a spider
    for(Tile* tile : mCentralActiveSpotTiles)
    {
        if(Random::Int(1, 10) > 1)
            continue;

        SmallSpiderEntity* spider = new SmallSpiderEntity(getGameMap(), true, getName(), 10);
        Ogre::Vector3 pos(static_cast<Ogre::Real>(tile->getX()), static_cast<Ogre::Real>(tile->getY()), 0.0f);
        spider->addToGameMap();
        spider->createMesh();
        spider->setPosition(pos);
    }

    // We increment rotting creatures counter
    for(std::pair<Tile* const, std::pair<Creature*, int32_t> >& p : mRottingCreatures)
    {
        if((p.second.first == nullptr) || (p.second.second == -1))
            continue;

        ConfigManager& configManager = ConfigManager::getSingleton();

        ++p.second.second;
        if(p.second.second < configManager.getRoomConfigInt32("CryptRotNbTurns"))
            continue;

        // We add the rotten creature points to the room and release the active spot
        double coef = 1.0 + static_cast<double>(mNumActiveSpots - mCentralActiveSpotTiles.size()) * configManager.getRoomConfigDouble("CryptBonusWallActiveSpot");
        Creature* c = p.second.first;
        mRottenPoints += static_cast<int32_t>(c->getMaxHp() * coef);

        c->removeFromGameMap();
        c->deleteYourself();
        p.second.first = nullptr;
        p.second.second = -1;

        int32_t maxCreatures = configManager.getMaxCreaturesPerSeatAbsolute();
        int32_t numCreatures = getGameMap()->getCreaturesBySeat(getSeat()).size();
        int32_t cryptPointsForSpawn = configManager.getRoomConfigInt32("CryptPointsForSpawn");
        if((numCreatures < maxCreatures) &&
           (mRottenPoints >= cryptPointsForSpawn))
        {
            Tile* tileSpawn = p.first;
            mRottenPoints -= cryptPointsForSpawn;
            const std::string& className = configManager.getRoomConfigString("CryptSpawnClass");
            const CreatureDefinition* classToSpawn = getGameMap()->getClassDescription(className);
            if(classToSpawn == nullptr)
            {
                OD_LOG_ERR("className=" + className);
                continue;
            }
            // Create a new creature and copy over the class-based creature parameters.
            Creature* newCreature = new Creature(getGameMap(), true, classToSpawn, getSeat());

            // Add the creature to the gameMap and create meshes so it is visible.
            newCreature->addToGameMap();
            newCreature->setPosition(Ogre::Vector3(tileSpawn->getX(), tileSpawn->getY(), 0.0f));
            newCreature->createMesh();
        }
    }
}

bool RoomCrypt::hasCarryEntitySpot(GameEntity* carriedEntity)
{
    if(carriedEntity->getObjectType() != GameEntityType::creature)
        return false;

    Creature* creature = static_cast<Creature*>(carriedEntity);
    if(!creature->canBeCarriedToBuilding(this))
        return false;

    for(std::pair<Tile* const, std::pair<Creature*, int32_t> >& p : mRottingCreatures)
    {
        if(p.second.first == nullptr)
            return true;
    }
    return false;
}

Tile* RoomCrypt::askSpotForCarriedEntity(GameEntity* carriedEntity)
{
    if(carriedEntity->getObjectType() != GameEntityType::creature)
    {
        OD_LOG_ERR("room=" + getName() + ", entity=" + carriedEntity->getName());
        return nullptr;
    }

    Creature* creature = static_cast<Creature*>(carriedEntity);
    for(std::pair<Tile* const, std::pair<Creature*, int32_t> >& p : mRottingCreatures)
    {
        if(p.second.first == nullptr)
        {
            p.second.first = creature;
            p.second.second = -1;
            Tile* spot = p.first;
            Tile* t = getGameMap()->getTile(spot->getX() + OFFSET_TILE_X,
                spot->getY() + OFFSET_TILE_Y);
            OD_ASSERT_TRUE_MSG(t != nullptr, "room=" + getName() + ", spot=" + Tile::displayAsString(spot));
            return t;
        }
    }
    return nullptr;
}

void RoomCrypt::notifyCarryingStateChanged(Creature* carrier, GameEntity* carriedEntity)
{
    for(std::pair<Tile* const, std::pair<Creature*, int32_t> >& p : mRottingCreatures)
    {
        if(p.second.first == carriedEntity)
        {
            // We check if the carrier is at the expected destination
            Tile* carrierTile = carrier->getPositionTile();
            if(carrierTile == nullptr)
            {
                OD_LOG_ERR("carrier=" + carrier->getName());
                p.second.first = nullptr;
                p.second.second = -1;
                return;
            }

            Tile* spot = p.first;
            Tile* tileExpected = getGameMap()->getTile(spot->getX() + OFFSET_TILE_X,
                spot->getY() + OFFSET_TILE_Y);
            if(tileExpected != carrierTile)
            {
                p.second.first = nullptr;
                p.second.second = -1;
                return;
            }

            // The carrier has brought the dead creature
            if(carriedEntity->getObjectType() != GameEntityType::creature)
            {
                OD_LOG_ERR("room=" + getName() + ", entity=" + carriedEntity->getName());
                return;
            }

            Creature* deadCreature = static_cast<Creature*>(carriedEntity);
            Tile* tileDeadCreature = deadCreature->getPositionTile();
            if(tileDeadCreature == nullptr)
            {
                OD_LOG_ERR("deadCreature=" + deadCreature->getName());
                return;
            }
            // Start rotting
            deadCreature->removeEntityFromPositionTile();
            p.second.second = 0;
            return;
        }
    }

    // We couldn't find the entity in the list. That may happen if the active spot has
    // been erased between the time the carrier tried to come and the time it arrived.
    // In any case, nothing to do
}

void RoomCrypt::exportToStream(std::ostream& os) const
{
    Room::exportToStream(os);
    os << mRottenPoints << "\n";
    // We do not save rotten creatures. They will automatically be carried again by workers
}

void RoomCrypt::importFromStream(std::istream& is)
{
    Room::importFromStream(is);
    OD_ASSERT_TRUE(is >> mRottenPoints);
    // We do not save rotten creatures. They will automatically be carried again by workers
}

void RoomCrypt::checkBuildRoom(GameMap* gameMap, const InputManager& inputManager, InputCommand& inputCommand)
{
    checkBuildRoomDefault(gameMap, RoomType::crypt, inputManager, inputCommand);
}

bool RoomCrypt::buildRoom(GameMap* gameMap, Player* player, ODPacket& packet)
{
    std::vector<Tile*> tiles;
    if(!getRoomTilesDefault(tiles, gameMap, player, packet))
        return false;

    int32_t pricePerTarget = RoomManager::costPerTile(RoomType::crypt);
    int32_t price = static_cast<int32_t>(tiles.size()) * pricePerTarget;
    if(!gameMap->withdrawFromTreasuries(price, player->getSeat()))
        return false;

    RoomCrypt* room = new RoomCrypt(gameMap);
    return buildRoomDefault(gameMap, room, player->getSeat(), tiles);
}

void RoomCrypt::checkBuildRoomEditor(GameMap* gameMap, const InputManager& inputManager, InputCommand& inputCommand)
{
    checkBuildRoomDefaultEditor(gameMap, RoomType::crypt, inputManager, inputCommand);
}

bool RoomCrypt::buildRoomEditor(GameMap* gameMap, ODPacket& packet)
{
    RoomCrypt* room = new RoomCrypt(gameMap);
    return buildRoomDefaultEditor(gameMap, room, packet);
}

bool RoomCrypt::buildRoomOnTiles(GameMap* gameMap, Player* player, const std::vector<Tile*>& tiles)
{
    int32_t pricePerTarget = RoomManager::costPerTile(RoomType::crypt);
    int32_t price = static_cast<int32_t>(tiles.size()) * pricePerTarget;
    if(!gameMap->withdrawFromTreasuries(price, player->getSeat()))
        return false;

    RoomCrypt* room = new RoomCrypt(gameMap);
    return buildRoomDefault(gameMap, room, player->getSeat(), tiles);
}

Room* RoomCrypt::getRoomFromStream(GameMap* gameMap, std::istream& is)
{
    RoomCrypt* room = new RoomCrypt(gameMap);
    room->importFromStream(is);
    return room;
}
