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

#include "entities/MissileOneHit.h"

#include "entities/Building.h"
#include "gamemap/GameMap.h"
#include "network/ODPacket.h"
#include "utils/LogManager.h"
#include "utils/Random.h"

#include <iostream>

MissileOneHit::MissileOneHit(GameMap* gameMap, bool isOnServerMap, Seat* seat, const std::string& senderName, const std::string& meshName,
        const std::string& particleScript, const Ogre::Vector3& direction, double speed, double physicalDamage, double magicalDamage,
        double elementDamage, Tile* tileBuildingTarget, bool damageAllies) :
    MissileObject(gameMap, isOnServerMap, seat, senderName, meshName, direction, speed, tileBuildingTarget, damageAllies),
    mPhysicalDamage(physicalDamage),
    mMagicalDamage(magicalDamage),
    mElementDamage(elementDamage)
{
    if(!particleScript.empty())
    {
        MissileParticuleEffectClient* effect = new MissileParticuleEffectClient(nextParticleSystemsName(), particleScript, -1);
        mEntityParticleEffects.push_back(effect);
    }
}

MissileOneHit::MissileOneHit(GameMap* gameMap, bool isOnServerMap) :
        MissileObject(gameMap, isOnServerMap),
    mPhysicalDamage(0.0),
    mMagicalDamage(0.0),
    mElementDamage(0.0)
{
}

bool MissileOneHit::hitCreature(GameEntity* entity)
{
    std::vector<Tile*> tiles = entity->getCoveredTiles();
    if(tiles.empty())
        return true;

    Tile* hitTile = tiles[0];
    entity->takeDamage(this, mPhysicalDamage, mMagicalDamage, mElementDamage, hitTile, false, false, false);
    return false;
}

void MissileOneHit::hitTargetBuilding(Tile* tile, Building* target)
{
    target->takeDamage(this, mPhysicalDamage, mMagicalDamage, mElementDamage, tile, false, false, false);
}

MissileOneHit* MissileOneHit::getMissileOneHitFromStream(GameMap* gameMap, std::istream& is)
{
    MissileOneHit* obj = new MissileOneHit(gameMap, true);
    obj->importFromStream(is);
    return obj;
}

MissileOneHit* MissileOneHit::getMissileOneHitFromPacket(GameMap* gameMap, ODPacket& is)
{
    MissileOneHit* obj = new MissileOneHit(gameMap, false);
    obj->importFromPacket(is);
    return obj;
}

void MissileOneHit::exportToStream(std::ostream& os) const
{
    MissileObject::exportToStream(os);
    os << mPhysicalDamage << "\t";
    os << mMagicalDamage << "\t";
    os << mElementDamage << "\t";

    uint32_t nbEffects = mEntityParticleEffects.size();
    os << "\t" << nbEffects;
    for(EntityParticleEffect* effect : mEntityParticleEffects)
    {
        os << "\t" << effect->mScript;
    }
}

void MissileOneHit::importFromStream(std::istream& is)
{
    MissileObject::importFromStream(is);
    OD_ASSERT_TRUE(is >> mPhysicalDamage);
    OD_ASSERT_TRUE(is >> mMagicalDamage);
    OD_ASSERT_TRUE(is >> mElementDamage);
    uint32_t nbEffects;
    OD_ASSERT_TRUE(is >> nbEffects);
    while(nbEffects > 0)
    {
        --nbEffects;
        std::string effectScript;
        OD_ASSERT_TRUE(is >> effectScript);
        MissileParticuleEffectClient* effect = new MissileParticuleEffectClient(nextParticleSystemsName(), effectScript, -1);
        mEntityParticleEffects.push_back(effect);
    }
}

void MissileOneHit::exportToPacket(ODPacket& os, const Seat* seat) const
{
    MissileObject::exportToPacket(os, seat);
    uint32_t nbEffects = mEntityParticleEffects.size();
    os << nbEffects;
    for(EntityParticleEffect* effect : mEntityParticleEffects)
    {
        os << effect->mName;
        os << effect->mScript;
        os << effect->mNbTurnsEffect;
    }
}

void MissileOneHit::importFromPacket(ODPacket& is)
{
    MissileObject::importFromPacket(is);
    uint32_t nbEffects;
    OD_ASSERT_TRUE(is >> nbEffects);

    while(nbEffects > 0)
    {
        --nbEffects;

        std::string effectName;
        std::string effectScript;
        uint32_t nbTurns;
        OD_ASSERT_TRUE(is >> effectName >> effectScript >> nbTurns);
        MissileParticuleEffectClient* effect = new MissileParticuleEffectClient(effectName, effectScript, nbTurns);
        mEntityParticleEffects.push_back(effect);
    }
}
