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

#include "entities/Creature.h"

#include "creatureeffect/CreatureEffect.h"
#include "creatureeffect/CreatureEffectSlap.h"
#include "creaturemood/CreatureMood.h"
#include "creatureskill/CreatureSkill.h"
#include "entities/ChickenEntity.h"
#include "entities/CreatureAction.h"
#include "entities/CreatureDefinition.h"
#include "entities/MissileOneHit.h"
#include "entities/Tile.h"
#include "entities/TreasuryObject.h"
#include "entities/Weapon.h"
#include "game/Player.h"
#include "game/Research.h"
#include "game/Seat.h"
#include "gamemap/GameMap.h"
#include "gamemap/Pathfinding.h"
#include "giftboxes/GiftBoxResearch.h"
#include "network/ODClient.h"
#include "network/ODServer.h"
#include "network/ServerNotification.h"
#include "render/CreatureOverlayStatus.h"
#include "render/RenderManager.h"
#include "rooms/RoomCrypt.h"
#include "rooms/RoomTreasury.h"
#include "rooms/RoomDormitory.h"
#include "sound/SoundEffectsManager.h"
#include "spells/Spell.h"
#include "spells/SpellType.h"
#include "traps/Trap.h"
#include "utils/ConfigManager.h"
#include "utils/Helper.h"
#include "utils/LogManager.h"
#include "utils/Random.h"

#include <CEGUI/System.h>
#include <CEGUI/WindowManager.h>
#include <CEGUI/Window.h>
#include <CEGUI/widgets/PushButton.h>
#include <CEGUI/Event.h>
#include <CEGUI/UDim.h>
#include <CEGUI/Vector.h>

#include <OgreQuaternion.h>
#include <OgreVector3.h>
#include <OgreVector2.h>

#include <cmath>
#include <algorithm>

#if OGRE_PLATFORM == OGRE_PLATFORM_WIN32
#define snprintf_is_banned_in_OD_code _snprintf
#endif

static const int NB_TURN_FLEE_MAX = 5;
const Ogre::Real CANNON_MISSILE_HEIGHT = 0.3;

const uint32_t Creature::NB_OVERLAY_HEALTH_VALUES = 8;

enum CreatureMoodEnum
{
    Angry = 0x0001,
    Furious = 0x0002,
    GetFee = 0x0004,
    LeaveDungeon = 0x0008,
    KoDeath = 0x0010,
    Hungry = 0x0020,
    Tired = 0x0040,
    KoTemp = 0x0080,
    InJail = 0x0100,
    // To know if a creature is KO
    KoDeathOrTemp = KoTemp | KoDeath,
    // Mood filters for creatures in prison that every player will see
    MoodPrisonFiltersAllPlayers = InJail,
    // Mood filters for creatures in prison that prison allied will see
    MoodPrisonFiltersPrisonAllies = KoTemp | InJail
};

//! \brief CreatureAction is contained in a deque in the Creature class. However, we don't
//! want to use this class directly in the handle action functions because the action can be
//! removed from the deque within the handle function. To avoid that, we use this wrapper class
class CreatureActionWrapper
{
public:
    CreatureActionWrapper(const CreatureAction& action) :
        mType(action.getType()),
        mAttackedEntity(action.getAttackedEntity()),
        mTile(action.getTile()),
        mCreatureSkillData(action.getCreatureSkillData()),
        mNbTurns(action.getNbTurns()),
        mNbTurnsActive(action.getNbTurnsActive())
    {
    }

    const CreatureActionType mType;
    GameEntity* const mAttackedEntity;
    Tile* const mTile;
    CreatureSkillData* const mCreatureSkillData;
    const int32_t mNbTurns;
    const int32_t mNbTurnsActive;
};

CreatureParticuleEffect::~CreatureParticuleEffect()
{
    if(mEffect != nullptr)
        delete mEffect;

    mEffect = nullptr;
}

Creature::Creature(GameMap* gameMap, bool isOnServerMap, const CreatureDefinition* definition, Seat* seat, Ogre::Vector3 position) :
    MovableGameEntity        (gameMap, isOnServerMap),
    mPhysicalDefense         (3.0),
    mMagicalDefense          (1.5),
    mElementDefense          (0.0),
    mWeaponL                 (nullptr),
    mWeaponR                 (nullptr),
    mHomeTile                (nullptr),
    mDefinition              (definition),
    mHasVisualDebuggingEntities (false),
    mAwakeness               (100.0),
    mHunger                  (0.0),
    mLevel                   (1),
    mHp                      (10.0),
    mMaxHP                   (10.0),
    mExp                     (0.0),
    mGroundSpeed             (1.0),
    mWaterSpeed              (0.0),
    mLavaSpeed               (0.0),
    mDigRate                 (0.0),
    mClaimRate               (0.0),
    mDeathCounter            (0),
    mJobCooldown             (0),
    mEatCooldown             (0),
    mGoldFee                 (0),
    mGoldCarried             (0),
    mResearchTypeDropDeath   (ResearchType::nullResearchType),
    mWeaponDropDeath         ("none"),
    mJobRoom                 (nullptr),
    mEatRoom                 (nullptr),
    mStatsWindow             (nullptr),
    mNbTurnsWithoutBattle    (0),
    mForceAction             (forcedActionNone),
    mCarriedEntity           (nullptr),
    mCarriedEntityDestType   (GameEntityType::unknown),
    mMoodCooldownTurns       (0),
    mMoodValue               (CreatureMoodLevel::Neutral),
    mMoodPoints              (0),
    mFirstTurnFurious        (-1),
    mOverlayHealthValue      (0),
    mOverlayMoodValue        (0),
    mOverlayStatus           (nullptr),
    mNeedFireRefresh         (false),
    mDropCooldown            (0),
    mSpeedModifier           (1.0),
    mKoTurnCounter           (0),
    mSeatPrison              (nullptr)

{
    //TODO: This should be set in initialiser list in parent classes
    setSeat(seat);
    mPosition = position;
    setMeshName(definition->getMeshName());
    setName(getGameMap()->nextUniqueNameCreature(definition->getClassName()));

    pushAction(CreatureActionType::idle, false, true);

    mMaxHP = mDefinition->getMinHp();
    setHP(mMaxHP);

    mGroundSpeed = mDefinition->getMoveSpeedGround();
    mWaterSpeed = mDefinition->getMoveSpeedWater();
    mLavaSpeed = mDefinition->getMoveSpeedLava();

    mDigRate = mDefinition->getDigRate();
    mClaimRate = mDefinition->getClaimRate();

    // Fighting stats
    mPhysicalDefense = mDefinition->getPhysicalDefense();
    mMagicalDefense = mDefinition->getMagicalDefense();
    mElementDefense = mDefinition->getElementDefense();

    if(mDefinition->getWeaponSpawnL().compare("none") != 0)
        mWeaponL = gameMap->getWeapon(mDefinition->getWeaponSpawnL());

    if(mDefinition->getWeaponSpawnR().compare("none") != 0)
        mWeaponR = gameMap->getWeapon(mDefinition->getWeaponSpawnR());

    setupDefinition(*gameMap, *ConfigManager::getSingleton().getCreatureDefinitionDefaultWorker());
}

Creature::Creature(GameMap* gameMap, bool isOnServerMap) :
    MovableGameEntity        (gameMap, isOnServerMap),
    mPhysicalDefense         (3.0),
    mMagicalDefense          (1.5),
    mElementDefense          (0.0),
    mWeaponL                 (nullptr),
    mWeaponR                 (nullptr),
    mHomeTile                (nullptr),
    mDefinition              (nullptr),
    mHasVisualDebuggingEntities (false),
    mAwakeness               (100.0),
    mHunger                  (0.0),
    mLevel                   (1),
    mHp                      (10.0),
    mMaxHP                   (10.0),
    mExp                     (0.0),
    mGroundSpeed             (1.0),
    mWaterSpeed              (0.0),
    mLavaSpeed               (0.0),
    mDigRate                 (0.0),
    mClaimRate               (0.0),
    mDeathCounter            (0),
    mJobCooldown             (0),
    mEatCooldown             (0),
    mGoldFee                 (0),
    mGoldCarried             (0),
    mResearchTypeDropDeath   (ResearchType::nullResearchType),
    mWeaponDropDeath         ("none"),
    mJobRoom                 (nullptr),
    mEatRoom                 (nullptr),
    mStatsWindow             (nullptr),
    mNbTurnsWithoutBattle    (0),
    mForceAction             (forcedActionNone),
    mCarriedEntity           (nullptr),
    mCarriedEntityDestType   (GameEntityType::unknown),
    mMoodCooldownTurns       (0),
    mMoodValue               (CreatureMoodLevel::Neutral),
    mMoodPoints              (0),
    mFirstTurnFurious        (-1),
    mOverlayHealthValue      (0),
    mOverlayMoodValue        (0),
    mOverlayStatus           (nullptr),
    mNeedFireRefresh         (false),
    mDropCooldown            (0),
    mSpeedModifier           (1.0),
    mKoTurnCounter           (0),
    mSeatPrison              (nullptr)
{
    pushAction(CreatureActionType::idle, false, true);
}

Creature::~Creature()
{
}

void Creature::createMeshLocal()
{
    MovableGameEntity::createMeshLocal();
    if(!getIsOnServerMap())
    {
        RenderManager::getSingleton().rrCreateCreature(this);

        // By default, we set the creature in idle state
        RenderManager::getSingleton().rrSetObjectAnimationState(this, EntityAnimation::idle_anim, true);
    }

    createMeshWeapons();
}

void Creature::destroyMeshLocal()
{
    destroyMeshWeapons();
    MovableGameEntity::destroyMeshLocal();
    if(getIsOnServerMap())
        return;

    destroyStatsWindow();
    RenderManager::getSingleton().rrDestroyCreature(this);
}

void Creature::createMeshWeapons()
{
    if(getIsOnServerMap())
        return;

    if(mWeaponL != nullptr)
        RenderManager::getSingleton().rrCreateWeapon(this, mWeaponL, "L");

    if(mWeaponR != nullptr)
        RenderManager::getSingleton().rrCreateWeapon(this, mWeaponR, "R");
}

void Creature::destroyMeshWeapons()
{
    if(getIsOnServerMap())
        return;

    if(mWeaponL != nullptr)
        RenderManager::getSingleton().rrDestroyWeapon(this, mWeaponL, "L");

    if(mWeaponR != nullptr)
        RenderManager::getSingleton().rrDestroyWeapon(this, mWeaponR, "R");
}

void Creature::addToGameMap()
{
    getGameMap()->addCreature(this);
    getGameMap()->addAnimatedObject(this);
    getGameMap()->addClientUpkeepEntity(this);

    if(!getIsOnServerMap())
        return;

    getGameMap()->addActiveObject(this);
}

void Creature::removeFromGameMap()
{
    fireEntityRemoveFromGameMap();
    removeEntityFromPositionTile();
    getGameMap()->removeCreature(this);
    getGameMap()->removeAnimatedObject(this);
    getGameMap()->removeClientUpkeepEntity(this);

    if(!getIsOnServerMap())
        return;

    // If the creature has a homeTile where it sleeps, its bed needs to be destroyed.
    if (getHomeTile() != nullptr)
    {
        RoomDormitory* home = static_cast<RoomDormitory*>(getHomeTile()->getCoveringBuilding());
        home->releaseTileForSleeping(getHomeTile(), this);
    }

    fireRemoveEntityToSeatsWithVision();
    getGameMap()->removeActiveObject(this);
}

std::string Creature::getCreatureStreamFormat()
{
    std::string format = MovableGameEntity::getMovableGameEntityStreamFormat();
    if(!format.empty())
        format += "\t";

    format += "ClassName\tLevel\tCurrentXP\tCurrentHP\tCurrentAwakeness"
            "\tCurrentHunger\tGoldToDeposit\tLeftWeapon\tRightWeapon\tCarriedResearch\tCarriedWeapon"
            "\tNbCreatureEffects\tN*CreatureEffects";

    return format;
}

void Creature::exportToStream(std::ostream& os) const
{
    MovableGameEntity::exportToStream(os);
    os << mDefinition->getClassName() << "\t";
    os << getLevel() << "\t" << mExp << "\t";
    if(getHP() < mMaxHP)
        os << getHP();
    else
        os << "max";
    os << "\t" << mAwakeness << "\t" << mHunger << "\t" << mGoldCarried;

    // Check creature weapons
    if(mWeaponL != nullptr)
        os << "\t" << mWeaponL->getName();
    else
        os << "\tnone";

    if(mWeaponR != nullptr)
        os << "\t" << mWeaponR->getName();
    else
        os << "\tnone";

    os << "\t" << mResearchTypeDropDeath;

    os << "\t" << mWeaponDropDeath;

    uint32_t nbEffects = mEntityParticleEffects.size();
    os << "\t" << nbEffects;
    for(EntityParticleEffect* effect : mEntityParticleEffects)
    {
        CreatureParticuleEffect* creatureParticuleEffect = static_cast<CreatureParticuleEffect*>(effect);
        os << "\t";
        CreatureEffect::write(*creatureParticuleEffect->mEffect, os);
    }
}

void Creature::importFromStream(std::istream& is)
{
    // Beware: A generic class name might be used here so we shouldn't use mDefinition
    // here as it is not set yet (for example, default worker will be available only after
    // seat lobby configuration)
    MovableGameEntity::importFromStream(is);
    std::string tempString;

    OD_ASSERT_TRUE(is >> mDefinitionString);

    OD_ASSERT_TRUE(is >> mLevel);

    OD_ASSERT_TRUE(is >> mExp);

    OD_ASSERT_TRUE(is >> mHpString);

    OD_ASSERT_TRUE(is >> mAwakeness);

    OD_ASSERT_TRUE(is >> mHunger);

    OD_ASSERT_TRUE(is >> mGoldCarried);

    OD_ASSERT_TRUE(is >> tempString);
    if(tempString != "none")
    {
        mWeaponL = getGameMap()->getWeapon(tempString);
        if(mWeaponL == nullptr)
        {
            OD_LOG_ERR("Unknown weapon name=" + tempString);
        }
    }

    OD_ASSERT_TRUE(is >> tempString);
    if(tempString != "none")
    {
        mWeaponR = getGameMap()->getWeapon(tempString);
        if(mWeaponR == nullptr)
        {
            OD_LOG_ERR("Unknown weapon name=" + tempString);
        }
    }

    OD_ASSERT_TRUE(is >> mResearchTypeDropDeath);

    OD_ASSERT_TRUE(is >> mWeaponDropDeath);

    mLevel = std::min(MAX_LEVEL, mLevel);

    uint32_t nbEffects;
    OD_ASSERT_TRUE(is >> nbEffects);
    while(nbEffects > 0)
    {
        --nbEffects;
        CreatureEffect* effect = CreatureEffect::load(is);
        if(effect == nullptr)
            continue;


        addCreatureEffect(effect);
    }
}

void Creature::buildStats()
{
    // Get the base value
    mMaxHP = mDefinition->getMinHp();
    mDigRate = mDefinition->getDigRate();
    mClaimRate = mDefinition->getClaimRate();
    mGroundSpeed = mDefinition->getMoveSpeedGround();
    mWaterSpeed = mDefinition->getMoveSpeedWater();
    mLavaSpeed  = mDefinition->getMoveSpeedLava();

    mPhysicalDefense = mDefinition->getPhysicalDefense();
    mMagicalDefense = mDefinition->getMagicalDefense();
    mElementDefense = mDefinition->getElementDefense();

    mScale = getDefinition()->getScale();
    Ogre::Real scaleFactor = static_cast<Ogre::Real>(1.0 + 0.02 * static_cast<double>(getLevel()));
    mScale *= scaleFactor;

    // Improve the stats to the current level
    double multiplier = mLevel - 1;
    if (multiplier == 0.0)
        return;

    mMaxHP += mDefinition->getHpPerLevel() * multiplier;
    mDigRate += mDefinition->getDigRatePerLevel() * multiplier;
    mClaimRate += mDefinition->getClaimRatePerLevel() * multiplier;
    mGroundSpeed += mDefinition->getGroundSpeedPerLevel() * multiplier;
    mWaterSpeed += mDefinition->getWaterSpeedPerLevel() * multiplier;
    mLavaSpeed += mDefinition->getLavaSpeedPerLevel() * multiplier;

    mPhysicalDefense += mDefinition->getPhysicalDefPerLevel() * multiplier;
    mMagicalDefense += mDefinition->getMagicalDefPerLevel() * multiplier;
    mElementDefense += mDefinition->getElementDefPerLevel() * multiplier;
}

Creature* Creature::getCreatureFromStream(GameMap* gameMap, std::istream& is)
{
    //TODO - Handle load errors
    Creature* creature = new Creature(gameMap, true);
    creature->importFromStream(is);
    return creature;
}

Creature* Creature::getCreatureFromPacket(GameMap* gameMap, ODPacket& is)
{
    Creature* creature = new Creature(gameMap, false);
    creature->importFromPacket(is);
    return creature;
}

void Creature::exportToPacket(ODPacket& os, const Seat* seat) const
{
    MovableGameEntity::exportToPacket(os, seat);
    const std::string& className = mDefinition->getClassName();
    os << className;
    os << mLevel;
    os << mExp;

    os << mHp;
    os << mMaxHP;

    os << mDigRate;
    os << mClaimRate;
    os << mAwakeness;
    os << mHunger;

    os << mGroundSpeed;
    os << mWaterSpeed;
    os << mLavaSpeed;

    os << mPhysicalDefense;
    os << mMagicalDefense;
    os << mElementDefense;
    os << mOverlayHealthValue;

    // Only allied players should see creature mood (except some states)
    uint32_t moodValue = 0;
    if(seat->isAlliedSeat(getSeat()))
        moodValue = mOverlayMoodValue;
    else if(mSeatPrison != nullptr)
    {
        if(mSeatPrison->isAlliedSeat(seat))
            moodValue = mOverlayMoodValue & CreatureMoodEnum::MoodPrisonFiltersPrisonAllies;
        else
            moodValue = mOverlayMoodValue & CreatureMoodEnum::MoodPrisonFiltersAllPlayers;
    }

    os << moodValue;
    os << mSpeedModifier;

    if(mWeaponL != nullptr)
        os << mWeaponL->getName();
    else
        os << "none";

    if(mWeaponR != nullptr)
        os << mWeaponR->getName();
    else
        os << "none";

    uint32_t nbEffects = mEntityParticleEffects.size();
    os << nbEffects;
    for(EntityParticleEffect* effect : mEntityParticleEffects)
    {
        os << effect->mName;
        os << effect->mScript;
        os << effect->mNbTurnsEffect;
    }
}

void Creature::importFromPacket(ODPacket& is)
{
    MovableGameEntity::importFromPacket(is);
    std::string tempString;

    OD_ASSERT_TRUE(is >> mDefinitionString);

    OD_ASSERT_TRUE(is >> mLevel);
    OD_ASSERT_TRUE(is >> mExp);

    OD_ASSERT_TRUE(is >> mHp);
    OD_ASSERT_TRUE(is >> mMaxHP);

    OD_ASSERT_TRUE(is >> mDigRate);
    OD_ASSERT_TRUE(is >> mClaimRate);
    OD_ASSERT_TRUE(is >> mAwakeness);
    OD_ASSERT_TRUE(is >> mHunger);

    OD_ASSERT_TRUE(is >> mGroundSpeed);
    OD_ASSERT_TRUE(is >> mWaterSpeed);
    OD_ASSERT_TRUE(is >> mLavaSpeed);

    OD_ASSERT_TRUE(is >> mPhysicalDefense);
    OD_ASSERT_TRUE(is >> mMagicalDefense);
    OD_ASSERT_TRUE(is >> mElementDefense);

    OD_ASSERT_TRUE(is >> mOverlayHealthValue);
    OD_ASSERT_TRUE(is >> mOverlayMoodValue);
    OD_ASSERT_TRUE(is >> mSpeedModifier);

    OD_ASSERT_TRUE(is >> tempString);
    if(tempString != "none")
    {
        mWeaponL = getGameMap()->getWeapon(tempString);
        if(mWeaponL == nullptr)
        {
            OD_LOG_ERR("Unknown weapon name=" + tempString);
        }
    }

    OD_ASSERT_TRUE(is >> tempString);
    if(tempString != "none")
    {
        mWeaponR = getGameMap()->getWeapon(tempString);
        if(mWeaponR == nullptr)
        {
            OD_LOG_ERR("Unknown weapon name=" + tempString);
        }
    }

    uint32_t nbEffects;
    OD_ASSERT_TRUE(is >> nbEffects);

    while(nbEffects > 0)
    {
        --nbEffects;

        std::string effectName;
        std::string effectScript;
        uint32_t nbTurns;
        OD_ASSERT_TRUE(is >> effectName >> effectScript >> nbTurns);
        CreatureParticuleEffectClient* effect = new CreatureParticuleEffectClient(effectName, effectScript, nbTurns);
        mEntityParticleEffects.push_back(effect);
    }
    setupDefinition(*getGameMap(), *ConfigManager::getSingleton().getCreatureDefinitionDefaultWorker());
}

void Creature::setPosition(const Ogre::Vector3& v)
{
    MovableGameEntity::setPosition(v);
    if(mCarriedEntity != nullptr)
        mCarriedEntity->notifyCarryMove(v);
}

void Creature::setHP(double nHP)
{
    if (nHP > mMaxHP)
        mHp = mMaxHP;
    else
        mHp = nHP;

    computeCreatureOverlayHealthValue();
}

void Creature::heal(double hp)
{
    mHp = std::min(mHp + hp, mMaxHP);

    computeCreatureOverlayHealthValue();
}

bool Creature::isAlive() const
{
    if(!getIsOnServerMap())
        return mOverlayHealthValue < (NB_OVERLAY_HEALTH_VALUES - 1);

    return mHp > 0.0;
}

void Creature::update(Ogre::Real timeSinceLastFrame)
{
    Tile* previousPositionTile = getPositionTile();
    // Update movements, direction, ...
    MovableGameEntity::update(timeSinceLastFrame);

    // Update the visual debugging entities
    //if we are standing in a different tile than we were last turn
    if (mHasVisualDebuggingEntities &&
        getIsOnServerMap() &&
        (getPositionTile() != previousPositionTile))
    {
        computeVisualDebugEntities();
    }

    if(getOverlayStatus() != nullptr)
    {
        getOverlayStatus()->update(timeSinceLastFrame);
    }
}

void Creature::computeVisibleTiles()
{
    // dead Creatures do not give vision
    if (getHP() <= 0.0)
        return;

    // KO Creatures do not give vision
    if (isKo())
        return;

    // creatures in jail do not give vision
    if (mSeatPrison != nullptr)
        return;

    if (!getIsOnMap())
        return;

    // Look at the surrounding area
    updateTilesInSight();
    for(Tile* tile : mVisibleTiles)
        tile->notifyVision(getSeat());
}

void Creature::setLevel(unsigned int level)
{
    // Reset XP once the level has been acquired.
    mLevel = std::min(MAX_LEVEL, level);
    mExp = 0.0;

    buildStats();

    mNeedFireRefresh = true;
}

void Creature::dropCarriedEquipment()
{
    fireCreatureSound(CreatureSound::Die);
    stopJob();
    stopEating();
    clearDestinations(EntityAnimation::die_anim, false);

    // We drop what we are carrying
    Tile* myTile = getPositionTile();
    if(myTile == nullptr)
    {
        OD_LOG_ERR("name=" + getName() + ", position=" + Helper::toString(getPosition()));
        return;
    }

    if(mGoldCarried > 0)
    {
        TreasuryObject* obj = new TreasuryObject(getGameMap(), true, mGoldCarried);
        obj->addToGameMap();
        Ogre::Vector3 spawnPosition(static_cast<Ogre::Real>(myTile->getX()),
                                    static_cast<Ogre::Real>(myTile->getY()), 0.0f);
        obj->createMesh();
        obj->setPosition(spawnPosition);
        mGoldCarried = 0;
    }

    if(mResearchTypeDropDeath != ResearchType::nullResearchType)
    {
        GiftBoxResearch* researchEntity = new GiftBoxResearch(getGameMap(), getIsOnServerMap(),
            "DroppedBy" + getName(), mResearchTypeDropDeath);
        researchEntity->addToGameMap();
        Ogre::Vector3 spawnPosition(static_cast<Ogre::Real>(myTile->getX()),
                                    static_cast<Ogre::Real>(myTile->getY()), 0.0f);
        researchEntity->createMesh();
        researchEntity->setPosition(spawnPosition);
        mResearchTypeDropDeath = ResearchType::nullResearchType;
    }

    // TODO: drop weapon when available
}

void Creature::doUpkeep()
{
    // If the creature is in jail, we check if it is still standing on it (if not picked up). If
    // not, it is free
    if((mSeatPrison != nullptr) &&
       getIsOnMap())
    {
        Tile* myTile = getPositionTile();
        if(myTile == nullptr)
        {
            OD_LOG_ERR("name=" + getName() + ", position=" + Helper::toString(getPosition()));
            return;
        }

        Room* roomPrison = myTile->getCoveringRoom();
        if((roomPrison == nullptr) ||
           (roomPrison->getType() != RoomType::prison) ||
           (roomPrison->getSeat() != mSeatPrison))
        {
            // it is not standing on a jail. It is free
            mSeatPrison = nullptr;
            mNeedFireRefresh = true;
        }
    }

    // The creature may be killed while temporary KO
    if((mKoTurnCounter != 0) && !isAlive())
        mKoTurnCounter = 0;

    // If the creature is KO to death or dead, we remove its particle effects
    if(!mEntityParticleEffects.empty() &&
       ((mKoTurnCounter < 0) || !isAlive()))
    {
        for(EntityParticleEffect* effect : mEntityParticleEffects)
        {
            delete effect;
        }
        mEntityParticleEffects.clear();
    }

    // We apply creature effects if any
    for(auto it = mEntityParticleEffects.begin(); it != mEntityParticleEffects.end();)
    {
        CreatureParticuleEffect* effect = static_cast<CreatureParticuleEffect*>(*it);
        if(effect->mEffect->upkeepEffect(*this))
        {
            ++it;
            continue;
        }

        delete effect;
        it = mEntityParticleEffects.erase(it);
    }

    // if creature is not on map (picked up or being carried), we do nothing
    if(!getIsOnMap())
        return;

    // If the creature is temporary KO, it should do nothing
    if(mKoTurnCounter > 0)
    {
        --mKoTurnCounter;
        if(mKoTurnCounter > 0)
            return;

        computeCreatureOverlayMoodValue();
    }

    if(mKoTurnCounter < 0)
    {
        // If the counter reaches 0, the creature is dead
        ++mKoTurnCounter;
        if(mKoTurnCounter < 0)
            return;

        mHp = 0;
        computeCreatureOverlayHealthValue();
        computeCreatureOverlayMoodValue();
    }

    // Handle creature death
    if (!isAlive())
    {
        // Let the creature lay dead on the ground for a few turns before removing it from the GameMap.
        if (mDeathCounter == 0)
        {
            OD_LOG_INF("Creature=" + getName() + " RIP");

            dropCarriedEquipment();
        }
        else if (mDeathCounter >= ConfigManager::getSingleton().getCreatureDeathCounter())
        {
            // Remove the creature from the game map and into the deletion queue, it will be deleted
            // when it is safe, i.e. all other pointers to it have been wiped from the program.
            removeFromGameMap();
            deleteYourself();
        }

        ++mDeathCounter;
        return;
    }

    // If the creature is in jail, it should not auto heal or do anything
    if(mSeatPrison != nullptr)
        return;

    // If we are not standing somewhere on the map, do nothing.
    if (getPositionTile() == nullptr)
    {
        OD_LOG_ERR("creature=" + getName() + " not on map position=" + Helper::toString(getPosition()));
        return;
    }

    // Check to see if we have earned enough experience to level up.
    checkLevelUp();


    // Heal.
    mHp += mDefinition->getHpHealPerTurn();
    if (mHp > getMaxHp())
        mHp = getMaxHp();

    computeCreatureOverlayHealthValue();

    //Rogue creatures are not affected by awakness/hunger
    if(!getSeat()->isRogueSeat())
    {
        decreaseAwakeness(mDefinition->getAwakenessLostPerTurn());

        increaseHunger(mDefinition->getHungerGrowthPerTurn());
    }

    mVisibleEnemyObjects         = getVisibleEnemyObjects();
    mVisibleAlliedObjects        = getVisibleAlliedObjects();
    mReachableAlliedObjects      = getReachableAttackableObjects(mVisibleAlliedObjects);

    if (mDigRate > 0.0)
        updateVisibleMarkedTiles();

    // Check if we should compute mood
    if(mMoodCooldownTurns > 0)
    {
        --mMoodCooldownTurns;
    }
    // Rogue creatures do not have mood
    else if(!getSeat()->isRogueSeat())
    {
        computeMood();
        computeCreatureOverlayMoodValue();
        mMoodCooldownTurns = Random::Int(0, 5);
    }

    ++mNbTurnsWithoutBattle;

    bool isWarmUp = false;
    // We use creature skills if we can
    for(CreatureSkillData& skillData : mSkillData)
    {
        if(skillData.mWarmup > 0)
        {
            --skillData.mWarmup;
            isWarmUp = true;
        }

        if(skillData.mCooldown > 0)
        {
            --skillData.mCooldown;
            continue;
        }

        if(!skillData.mSkill->canBeUsedBy(this))
            continue;

        if(!skillData.mSkill->tryUseSupport(*getGameMap(), this))
            continue;

        skillData.mCooldown = skillData.mSkill->getCooldownNbTurns();
        skillData.mWarmup = skillData.mSkill->getWarmupNbTurns();

        if(skillData.mWarmup > 0)
            isWarmUp = true;
    }

    // If a warmup is active, we do nothing
    if(isWarmUp)
        return;

    decidePrioritaryAction();

    // The loopback variable allows creatures to begin processing a new
    // action immediately after some other action happens.
    bool loopBack = false;
    unsigned int loops = 0;

    mActionTry.clear();

    if(!mActionQueue.empty())
        mActionQueue.front().increaseNbTurnActive();

    do
    {
        ++loops;
        loopBack = false;

        // Carry out the current task
        if (!mActionQueue.empty())
        {
            CreatureActionWrapper topActionItem(mActionQueue.front());
            switch (topActionItem.mType)
            {
                case CreatureActionType::idle:
                    loopBack = handleIdleAction(topActionItem);
                    break;

                case CreatureActionType::walkToTile:
                    loopBack = handleWalkToTileAction(topActionItem);
                    break;

                case CreatureActionType::claimTile:
                    loopBack = handleClaimTileAction(topActionItem);
                    break;

                case CreatureActionType::claimWallTile:
                    loopBack = handleClaimWallTileAction(topActionItem);
                    break;

                case CreatureActionType::digTile:
                    loopBack = handleDigTileAction(topActionItem);
                    break;

                case CreatureActionType::findHome:
                case CreatureActionType::findHomeForced:
                    loopBack = handleFindHomeAction(topActionItem);
                    break;

                case CreatureActionType::sleep:
                    loopBack = handleSleepAction(topActionItem);
                    break;

                case CreatureActionType::jobdecided:
                case CreatureActionType::jobforced:
                    loopBack = handleJobAction(topActionItem);
                    break;

                case CreatureActionType::eatdecided:
                case CreatureActionType::eatforced:
                    loopBack = handleEatingAction(topActionItem);
                    break;

                case CreatureActionType::attackObject:
                    loopBack = handleAttackAction(topActionItem);
                    break;

                case CreatureActionType::fight:
                    loopBack = handleFightAction(topActionItem);
                    break;

                case CreatureActionType::flee:
                    loopBack = handleFleeAction(topActionItem);
                    break;

                case CreatureActionType::carryEntity:
                case CreatureActionType::carryEntityForced:
                    loopBack = handleCarryableEntities(topActionItem);
                    break;

                case CreatureActionType::getFee:
                    loopBack = handleGetFee(topActionItem);
                    break;

                case CreatureActionType::leaveDungeon:
                    loopBack = handleLeaveDungeon(topActionItem);
                    break;

                case CreatureActionType::fightNaturalEnemy:
                    loopBack = handleFightAlliedNaturalEnemyAction(topActionItem);
                    break;

                default:
                    OD_LOG_ERR("Unhandled action type in Creature::doUpkeep():" + mActionQueue.front().toString()
                        + ", action=" + Helper::toString(static_cast<int>(topActionItem.mType)));
                    popAction();
                    loopBack = false;
                    break;
            }
        }
        else
        {
            OD_LOG_ERR("Creature has empty action queue in doUpkeep(), this should not happen.");
            loopBack = false;
        }
    } while (loopBack && loops < 20);

    for(CreatureAction& creatureAction : mActionQueue)
        creatureAction.increaseNbTurn();

    if(loops >= 20)
    {
        OD_LOG_INF("> 20 loops in Creature::doUpkeep name:" + getName() +
                " seat id: " + Helper::toString(getSeat()->getId()) + ". Breaking out..");
    }
}

void Creature::decidePrioritaryAction()
{
    // Here, we should decide prioritary actions only (like fighting when an enemy is
    // visible). And if we decide to do something, we should clear the action queue.

    // If a creature is weak and there are foes, it shall flee
    bool isWeak = (mHp < mMaxHP * mDefinition->getWeakCoef());
    if (!mVisibleEnemyObjects.empty() && isWeak)
    {
        if(isActionInList(CreatureActionType::flee))
            return;

        clearDestinations(EntityAnimation::idle_anim, true);
        clearActionQueue();
        pushAction(CreatureActionType::flee, false, true);
        return;
    }

    // If we are weak we do not attack
    if (isWeak)
        return;

    // If a creature can see enemies, it may attack
    if (!mVisibleEnemyObjects.empty())
    {
        // Check if we are already fighting
        if(isActionInList(CreatureActionType::fight) || isActionInList(CreatureActionType::flee))
            return;

        // Unhappy creatures might flee instead of engaging enemies
        switch(mMoodValue)
        {
            case CreatureMoodLevel::Angry:
            case CreatureMoodLevel::Furious:
            {
                if(Random::Int(0,100) > 80)
                {
                    clearDestinations(EntityAnimation::idle_anim, true);
                    clearActionQueue();
                    pushAction(CreatureActionType::flee, false, true);
                    return;
                }
                break;
            }
            default:
                break;
        }

        // If we are not already fighting with a creature then start doing so.
        clearDestinations(EntityAnimation::idle_anim, true);
        clearActionQueue();
        pushAction(CreatureActionType::fight, false, true);
        return;
    }

    // Unhappy creatures might engage allied natural enemies
    switch(mMoodValue)
    {
        case CreatureMoodLevel::Upset:
        case CreatureMoodLevel::Angry:
        case CreatureMoodLevel::Furious:
        {
            // We are fighting an allied enemy. We don't consider leaving yet
            if(isActionInList(CreatureActionType::fightNaturalEnemy))
                return;

            // We check if we can attack a natural enemy
            if(Random::Int(0, 100) > 80)
            {
                pushAction(CreatureActionType::fightNaturalEnemy, false, true);
                return;
            }

            // If we are furious, we consider leaving the dungeon
            if((mMoodValue != CreatureMoodLevel::Furious) ||
               (isActionInList(CreatureActionType::leaveDungeon)))
            {
                return;
            }

            clearDestinations(EntityAnimation::idle_anim, true);
            clearActionQueue();
            stopJob();
            stopEating();
            pushAction(CreatureActionType::leaveDungeon, false, true);
            break;
        }
        default:
            break;
    }
}

bool Creature::handleIdleAction(const CreatureActionWrapper& actionItem)
{
    double diceRoll = Random::Double(0.0, 1.0);

    setAnimationState(EntityAnimation::idle_anim);

    if(mDefinition->isWorker() && mForceAction == forcedActionSearchAction)
    {
        // If a worker is dropped, he will search in the tile he is and in the 4 neighboor tiles.
        // 1 - If the tile he is in is treasury and he is carrying gold, he should deposit it
        // 2 - if one of the 4 neighboor tiles is marked, he will dig
        // 3 - if the the tile he is in is not claimed and one of the neigbboor tiles is claimed, he will claim
        // 4 - if the the tile he is in is claimed and one of the neigbboor tiles is not claimed, he will claim
        // 5 - If the tile he is in is claimed and one of the neigbboor tiles is a not claimed wall, he will claim
        Tile* position = getPositionTile();
        Seat* seat = getSeat();
        Tile* tileMarkedDig = nullptr;
        Tile* tileToClaim = nullptr;
        Tile* tileWallNotClaimed = nullptr;
        for (Tile* tile : position->getAllNeighbors())
        {
            if(tileMarkedDig == nullptr &&
                tile->getMarkedForDigging(getGameMap()->getPlayerBySeat(seat))
                )
            {
                tileMarkedDig = tile;
            }
            else if(tileToClaim == nullptr &&
                tile->isClaimedForSeat(seat) &&
                position->isGroundClaimable(seat)
                )
            {
                tileToClaim = position;
            }
            else if(tileToClaim == nullptr &&
                position->isClaimedForSeat(seat) &&
                tile->isGroundClaimable(seat)
                )
            {
                tileToClaim = tile;
            }
            else if(tileWallNotClaimed == nullptr &&
                position->isClaimedForSeat(seat) &&
                tile->isWallClaimable(seat)
                )
            {
                tileWallNotClaimed = tile;
            }
        }

        if((mGoldCarried > 0) && (mDigRate > 0.0) &&
           (position->getCoveringRoom() != nullptr) &&
           (position->getCoveringRoom()->getType() == RoomType::treasury))
        {
            RoomTreasury* treasury = static_cast<RoomTreasury*>(position->getCoveringRoom());
            int deposited = treasury->depositGold(mGoldCarried, position);
            if(deposited > 0)
            {
                mGoldCarried -= deposited;
                return true;
            }
        }

        std::vector<GameEntity*> carryable;
        position->fillWithCarryableEntities(this, carryable);
        bool forceCarryObject = false;
        if(!carryable.empty())
            forceCarryObject = true;

        // Now, we can decide
        if((tileMarkedDig != nullptr) && (mDigRate > 0.0))
        {
            mForceAction = forcedActionDigTile;
        }
        else if((tileToClaim != nullptr) && (mClaimRate > 0.0))
        {
            mForceAction = forcedActionClaimTile;
        }
        else if((tileWallNotClaimed != nullptr) && (mClaimRate > 0.0))
        {
            mForceAction = forcedActionClaimWallTile;
        }
        else if(forceCarryObject)
        {
            mForceAction = forcedActionNone;
            pushAction(CreatureActionType::carryEntity, false, true);
            return true;
        }
        else
        {
            // We couldn't find why we were dropped here. Let's behave as usual
            mForceAction = forcedActionNone;
        }
    }

    // Handle if worker was dropped
    if(mDefinition->isWorker() && mForceAction != forcedActionNone)
    {
        switch(mForceAction)
        {
            case forcedActionDigTile:
            {
                pushAction(CreatureActionType::digTile, false, true);
                return true;
            }
            case forcedActionClaimTile:
            {
                pushAction(CreatureActionType::claimTile, false, true);
                return true;
            }
            case forcedActionClaimWallTile:
            {
                pushAction(CreatureActionType::claimWallTile, false, true);
                return true;
            }
            default:
                break;
        }
    }

    // Decide to check for diggable tiles
    if (mDigRate > 0.0 && !mVisibleMarkedTiles.empty())
    {
        if(pushAction(CreatureActionType::digTile, false, false))
            return true;
    }
    // Decide to check for dead creature to carry to the crypt
    if (mDefinition->isWorker() && diceRoll < 0.3)
    {
        if(pushAction(CreatureActionType::carryEntity, false, false))
            return true;
    }
    // Decide to check for claimable tiles
    if (mClaimRate > 0.0 && diceRoll < 0.9)
    {
        if(pushAction(CreatureActionType::claimTile, false, false))
            return true;
    }

    // Fighters
    bool isWeak = (mHp < mMaxHP * mDefinition->getWeakCoef());
    // If a fighter is weak, he should try to sleep
    if (isWeak && !mDefinition->isWorker())
    {
        // If a fighter is weak, it should not care about forced action
        mForceAction = forcedActionNone;
        if((mHomeTile != nullptr) && (getGameMap()->pathExists(this, getPositionTile(), mHomeTile)))
        {
            if(pushAction(CreatureActionType::sleep, false, false))
                return true;
        }

        // If we have no home tile, we try to find one
        if(mHomeTile == nullptr)
        {
            std::vector<Room*> tempRooms = getGameMap()->getRoomsByTypeAndSeat(RoomType::dormitory, getSeat());
            tempRooms = getGameMap()->getReachableRooms(tempRooms, getPositionTile(), this);
            if (!tempRooms.empty())
            {
                clearDestinations(EntityAnimation::idle_anim, true);
                clearActionQueue();
                if(pushAction(CreatureActionType::sleep, false, false) &&
                   pushAction(CreatureActionType::findHome, false, false))
                {
                    return true;
                }
            }
        }

        // If the home tile is not accessible, we wander
        wanderRandomly(EntityAnimation::flee_anim);
        return false;
    }

    if(!mDefinition->isWorker() && mForceAction == forcedActionSearchAction)
    {
        mForceAction = forcedActionNone;
        Tile* tile = getPositionTile();
        if((tile != nullptr) &&
           (tile->getCoveringRoom() != nullptr))
        {
            Room* room = tile->getCoveringRoom();
            // we see if we are in an hatchery
            if(room->getType() == RoomType::hatchery)
            {
                pushAction(CreatureActionType::eatforced, false, true);
                return true;
            }
            else if(room->getType() == RoomType::dormitory)
            {
                pushAction(CreatureActionType::sleep, false, true);
                pushAction(CreatureActionType::findHomeForced, false, true);
                return true;
            }
            // If not, can we work in this room ?
            else if(room->getType() != RoomType::hatchery)
            {
                pushAction(CreatureActionType::jobforced, false, true);
                return true;
            }
        }
    }

    // We check if we are looking for our fee
    if(!mDefinition->isWorker() && Random::Double(0.0, 1.0) < 0.5 && mGoldFee > 0)
    {
        if(pushAction(CreatureActionType::getFee, false, false))
            return true;
    }

    // We check if there is a go to war spell reachable
    std::vector<Spell*> callToWars = getGameMap()->getSpellsBySeatAndType(getSeat(), SpellType::callToWar);
    if(!callToWars.empty())
    {
        std::vector<Spell*> reachableCallToWars;
        for(Spell* callToWar : callToWars)
        {
            if(!callToWar->getIsOnMap())
                continue;

            Tile* callToWarTile = callToWar->getPositionTile();
            if(callToWarTile == nullptr)
                continue;

            if (!getGameMap()->pathExists(this, getPositionTile(), callToWarTile))
                continue;

            reachableCallToWars.push_back(callToWar);
        }

        if(!reachableCallToWars.empty())
        {
            // We go there
            uint32_t index = Random::Uint(0,reachableCallToWars.size()-1);
            Spell* callToWar = reachableCallToWars[index];
            Tile* callToWarTile = callToWar->getPositionTile();
            std::list<Tile*> tempPath = getGameMap()->path(this, callToWarTile);
            // If we are 5 tiles from the call to war, we don't go there
            if(tempPath.size() >= 5)
            {
                std::vector<Ogre::Vector3> path;
                tileToVector3(tempPath, path, true, 0.0);
                setWalkPath(EntityAnimation::walk_anim, EntityAnimation::idle_anim, true, path);
                pushAction(CreatureActionType::walkToTile, false, true);
                return false;
            }
        }
    }

    // Check to see if we have found a "home" tile where we can sleep. Even if we are not sleepy,
    // we want to have a bed
    if (!mDefinition->isWorker() && mHomeTile == nullptr && Random::Double(0.0, 1.0) < 0.5)
    {
        // Check to see if there are any dormitory owned by our color that we can reach.
        std::vector<Room*> tempRooms = getGameMap()->getRoomsByTypeAndSeat(RoomType::dormitory, getSeat());
        tempRooms = getGameMap()->getReachableRooms(tempRooms, getPositionTile(), this);
        if (!tempRooms.empty())
        {
            if(pushAction(CreatureActionType::findHome, false, false))
                return true;
        }
    }

    // If we are sleepy, we go to sleep
    if (!mDefinition->isWorker() &&
        (mHomeTile != nullptr) &&
        (Random::Double(20.0, 30.0) > mAwakeness))
    {
        // Check to see if we can work
        if(pushAction(CreatureActionType::sleep, false, false))
            return true;
    }

    // If we are hungry, we go to eat
    if (!mDefinition->isWorker() &&
        (Random::Double(70.0, 80.0) < mHunger))
    {
        // Check to see if we can work
        if(pushAction(CreatureActionType::eatdecided, false, false))
            return true;
    }

    // Otherwise, we try to work
    if (!mDefinition->isWorker() &&
        (Random::Double(0.0, 1.0) < 0.4))
    {
        // Check to see if we can work
        if(pushAction(CreatureActionType::jobdecided, false, false))
            return true;
    }

    // Any creature.

    // Workers should move around randomly at large jumps.  Non-workers either wander short distances or follow workers.
    Tile* tileDest = nullptr;
    // Define reachable tiles from the tiles within radius
    std::vector<Tile*> reachableTiles;
    for (Tile* tile: mTilesWithinSightRadius)
    {
        if (getGameMap()->pathExists(this, getPositionTile(), tile))
            reachableTiles.push_back(tile);
    }

    if (!mDefinition->isWorker())
    {
        // Non-workers only.

        // Check to see if we want to try to follow a worker around or if we want to try to explore.
        double r = Random::Double(0.0, 1.0);
        //if(creatureJob == weakFighter) r -= 0.2;
        if (r < 0.7)
        {
            bool workerFound = false;
            // Try to find a worker to follow around.
            for (unsigned int i = 0; !workerFound && i < mReachableAlliedObjects.size(); ++i)
            {
                // Check to see if we found a worker.
                if (mReachableAlliedObjects[i]->getObjectType() == GameEntityType::creature
                    && static_cast<Creature*>(mReachableAlliedObjects[i])->mDefinition->isWorker())
                {
                    // We found a worker so find a tile near the worker to walk to.  See if the worker is digging.
                    Tile* tempTile = mReachableAlliedObjects[i]->getCoveredTile(0);
                    if (static_cast<Creature*>(mReachableAlliedObjects[i])->isActionInList(CreatureActionType::digTile))
                    {
                        // Worker is digging, get near it since it could expose enemies.
                        int x = static_cast<int>(static_cast<double>(tempTile->getX()) + 3.0
                                * Random::gaussianRandomDouble());
                        int y = static_cast<int>(static_cast<double>(tempTile->getY()) + 3.0
                                * Random::gaussianRandomDouble());
                        tileDest = getGameMap()->getTile(x, y);
                    }
                    else
                    {
                        // Worker is not digging, wander a bit farther around the worker.
                        int x = static_cast<int>(static_cast<double>(tempTile->getX()) + 8.0
                                * Random::gaussianRandomDouble());
                        int y = static_cast<int>(static_cast<double>(tempTile->getY()) + 8.0
                                * Random::gaussianRandomDouble());
                        tileDest = getGameMap()->getTile(x, y);
                    }
                    workerFound = true;
                }

                // If there are no workers around, choose tiles far away to "roam" the dungeon.
                if (!workerFound)
                {
                    if (!reachableTiles.empty())
                    {
                        tileDest = reachableTiles[static_cast<unsigned int>(Random::Double(0.6, 0.8)
                                                                           * (reachableTiles.size() - 1))];
                    }
                }
            }
        }
        else
        {
            // Randomly choose a tile near where we are standing to walk to.
            if (!reachableTiles.empty())
            {
                unsigned int tileIndex = static_cast<unsigned int>(reachableTiles.size()
                                                                   * Random::Double(0.1, 0.3));
                tileDest = reachableTiles[tileIndex];
            }
        }
    }
    else
    {
        // Workers only.

        // Choose a tile far away from our current position to wander to.
        if (!reachableTiles.empty())
        {
            tileDest = reachableTiles[Random::Uint(reachableTiles.size() / 2,
                                                   reachableTiles.size() - 1)];
        }
    }

    if(setDestination(tileDest))
        return false;

    return true;
}

bool Creature::handleWalkToTileAction(const CreatureActionWrapper& actionItem)
{
    if (mWalkQueue.empty())
    {
        popAction();
        return true;
    }

    // If we are moving during a fight, we do not wait to reach destination to force to compute again what to do
    // Because enemies may have moved, closest creatures could be near...
    if(isActionInList(CreatureActionType::fight) && actionItem.mNbTurns > 1)
    {
        clearDestinations(EntityAnimation::idle_anim, true);
        popAction();
        return true;
    }

    return false;
}

bool Creature::handleClaimTileAction(const CreatureActionWrapper& actionItem)
{
    Tile* myTile = getPositionTile();
    if (myTile == nullptr)
    {
        popAction();
        return false;
    }

    if(mForceAction != forcedActionClaimTile)
    {
        // Randomly decide to stop claiming with a small probability
        if (Random::Double(0.0, 1.0) < 0.1 + 0.2 * mVisibleMarkedTiles.size())
        {
            // If there are any visible tiles marked for digging start working on that.
            if (!mVisibleMarkedTiles.empty())
            {
                if(pushAction(CreatureActionType::digTile, true, false))
                    return true;
            }
        }
        // Randomly try to carry entities if any around
        if (Random::Int(0, 10) < 5)
        {
            // We don't pop action to continue claiming after carrying entity
            if(pushAction(CreatureActionType::carryEntity, false, false))
                return true;
        }
    }

    // See if the tile we are standing on can be claimed
    if (myTile->isGroundClaimable(getSeat()))
    {
        //cout << "\nTrying to claim the tile I am standing on.";
        // Check to see if one of the tile's neighbors is claimed for our color
        for (Tile* tempTile : myTile->getAllNeighbors())
        {
            // Check to see if the current neighbor is a claimed ground tile
            if (tempTile->isClaimedForSeat(getSeat()) && (tempTile->getFullness() == 0.0) && tempTile->getClaimedPercentage() >= 1.0)
            {
                //cout << "\t\tFound a neighbor that is claimed.";
                // If we found a neighbor that is claimed for our side than we can start
                // dancing on this tile.  If there is "left over" claiming that can be done
                // it will spill over into neighboring tiles until it is gone.
                setAnimationState(EntityAnimation::claim_anim);
                myTile->claimForSeat(getSeat(), mClaimRate);
                receiveExp(1.5 * (mClaimRate / (0.35 + 0.05 * getLevel())));

                // Since we danced on a tile we are done for this turn
                return false;
            }
        }
    }

    // The tile we are standing on is already claimed or is not currently
    // claimable, find candidates for claiming.
    // Start by checking the neighbor tiles of the one we are already in
    std::vector<Tile*> neighbors = myTile->getAllNeighbors();
    std::random_shuffle(neighbors.begin(), neighbors.end());
    while (!neighbors.empty())
    {
        // If the current neighbor is claimable, walk into it and skip to the end of this turn
        Tile* tempTile = neighbors.back();
        neighbors.pop_back();
        if(tempTile == nullptr)
            continue;
        if(tempTile->isFullTile())
            continue;
        if(!tempTile->isGroundClaimable(getSeat()))
            continue;

        // The neighbor tile is a potential candidate for claiming, to be an actual candidate
        // though it must have a neighbor of its own that is already claimed for our side.
        for(Tile* tempTile2 : tempTile->getAllNeighbors())
        {
            if(tempTile2->isFullTile())
                continue;
            if(!tempTile2->isClaimedForSeat(getSeat()))
                continue;
            if(tempTile2->getClaimedPercentage() < 1.0)
                continue;

            Ogre::Vector3 dest(static_cast<Ogre::Real>(tempTile->getX()), static_cast<Ogre::Real>(tempTile->getY()), 0.0);
            std::vector<Ogre::Vector3> path;
            path.push_back(dest);
            setWalkPath(EntityAnimation::walk_anim, EntityAnimation::idle_anim, true, path);
            pushAction(CreatureActionType::walkToTile, false, true);
            return false;
        }
    }

    // If we still haven't found a tile to claim, check the rest of the accessible tiles in radius
    std::vector<Tile*> claimableTiles;
    for (Tile* tempTile : mTilesWithinSightRadius)
    {
        // if this tile is not fully claimed yet or the tile is of another player's color
        if(tempTile == nullptr)
            continue;
        if(tempTile->isFullTile())
            continue;
        if(!tempTile->isGroundClaimable(getSeat()))
            continue;

        // Check to see if one of the tile's neighbors is claimed for our color
        for (Tile* t : tempTile->getAllNeighbors())
        {
            if(t->isFullTile())
                continue;
            if(!t->isClaimedForSeat(getSeat()))
                continue;
            if(t->getClaimedPercentage() < 1.0)
                continue;
            if(!getGameMap()->pathExists(this, myTile, t))
                continue;

            claimableTiles.push_back(t);
        }
    }

    // Randomly pick a claimable tile, plot a path to it and walk to it
    unsigned int tempUnsigned = 0;
    Tile* tempTile = nullptr;
    while (!claimableTiles.empty())
    {
        // Randomly find a "good" tile to claim.  A good tile is one that has many neighbors
        // already claimed, this makes the claimed are more "round" and less jagged.
        do
        {
            int numNeighborsClaimed = 0;

            // Start by randomly picking a candidate tile.
            tempTile = claimableTiles[Random::Uint(0, claimableTiles.size() - 1)];

            // Count how many of the candidate tile's neighbors are already claimed.
            for (Tile* t : tempTile->getAllNeighbors())
            {
                if(t->isFullTile())
                    continue;
                if(!t->isClaimedForSeat(getSeat()))
                    continue;
                if(t->getClaimedPercentage() < 1.0)
                    continue;

                ++numNeighborsClaimed;
            }

            // Pick a random number in [0:1], if this number is high enough, than use this tile to claim.  The
            // bar for success approaches 0 as numTiles approaches N so this will be guaranteed to succeed at,
            // or before the time we get to the last unclaimed tile.  The bar for success is also lowered
            // according to how many neighbors are already claimed.
            //NOTE: The bar can be negative, when this happens we are guaranteed to use this candidate tile.
            double bar = 1.0 - (numNeighborsClaimed / 4.0) - (tempUnsigned / static_cast<double>(claimableTiles.size() - 1));
            if (Random::Double(0.0, 1.0) >= bar)
                break;

            // Safety catch to prevent infinite loop in case the bar for success is too high and is never met.
            if (tempUnsigned >= claimableTiles.size() - 1)
                break;

            // Increment the counter indicating how many candidate tiles we have rejected so far.
            ++tempUnsigned;
        } while (true);

        if (tempTile != nullptr)
        {
            // If we find a valid path to the tile start walking to it and break
            if (setDestination(tempTile))
                return false;
        }

        // If we got to this point, the tile we randomly picked cannot be gotten to via a
        // valid path.  Delete it from the claimable tiles vector and repeat the outer
        // loop to try to find another valid tile.
        for (unsigned int i = 0; i < claimableTiles.size(); ++i)
        {
            if (claimableTiles[i] == tempTile)
            {
                claimableTiles.erase(claimableTiles.begin() + i);
                break; // Break out of this for loop.
            }
        }
    }

    // We couldn't find a tile to try to claim so we start searching for claimable walls
    mForceAction = forcedActionNone;
    popAction();
    pushAction(CreatureActionType::claimWallTile, false, false);
    return true;
}

bool Creature::handleClaimWallTileAction(const CreatureActionWrapper& actionItem)
{
    Tile* myTile = getPositionTile();
    if (myTile == nullptr)
    {
        popAction();
        return false;
    }

    // Randomly decide to stop claiming with a small probability
    if(mForceAction != forcedActionClaimWallTile)
    {
        if (Random::Double(0.0, 1.0) < 0.1 + 0.2 * mVisibleMarkedTiles.size())
        {
            // If there are any visible tiles marked for digging start working on that.
            if (!mVisibleMarkedTiles.empty())
            {

                if(pushAction(CreatureActionType::digTile, true, false))
                    return true;
            }
        }
        // Randomly try to carry entities if any around
        if (Random::Int(0, 10) < 5)
        {
            // We don't pop action to continue claiming after carrying entity
            if(pushAction(CreatureActionType::carryEntity, true, false))
                return true;
        }
    }

    // See if any of the tiles is one of our neighbors
    bool wasANeighbor = false;
    Player* tempPlayer = getGameMap()->getPlayerBySeat(getSeat());
    for (Tile* tempTile : myTile->getAllNeighbors())
    {
        if(wasANeighbor)
            break;

        if (tempPlayer == nullptr)
            break;

        if (!tempTile->isWallClaimable(getSeat()))
            continue;

        // Dig out the tile by decreasing the tile's fullness.
        Ogre::Vector3 walkDirection(tempTile->getX() - getPosition().x, tempTile->getY() - getPosition().y, 0);
        walkDirection.normalise();
        setAnimationState(EntityAnimation::claim_anim, true, walkDirection);
        tempTile->claimForSeat(getSeat(), mClaimRate);
        receiveExp(1.5 * mClaimRate / 20.0);

        wasANeighbor = true;
        //std::cout << "Claiming wall" << std::endl;
        break;
    }

    // If we successfully found a wall tile to claim then we are done for this turn.
    if (wasANeighbor)
        return false;

    // Find paths to all of the neighbor tiles for all of the visible wall tiles.
    std::vector<Tile*> wallTiles = getVisibleClaimableWallTiles();
    if(wallTiles.empty())
    {
        mForceAction = forcedActionNone;
        popAction();
        return true;
    }

    // Take the shortest path.
    unsigned int shortestPath = 10000;
    std::list<Tile*> chosenPath;
    for (unsigned int i = 0; i < wallTiles.size(); ++i)
    {
        for (Tile* neighborTile : wallTiles[i]->getAllNeighbors())
        {
            // We have the shortest path already, so don't bother testing anything else.
            if (shortestPath == 2)
                break;

            if (!getGameMap()->pathExists(this, getPositionTile(), neighborTile))
                continue;

            std::list<Tile*> currentPath = getGameMap()->path(this, neighborTile);
            unsigned int pathLength = currentPath.size();
            if (pathLength < shortestPath)
            {
                shortestPath = pathLength;
                chosenPath = currentPath;
            }
        }

        // We have the shortest path already, so don't bother testing anything else.
        if (shortestPath == 2)
            break;
    }

    // If the path is a legitimate path, walk down it to the tile to be dug out
    std::vector<Ogre::Vector3> path;
    tileToVector3(chosenPath, path, true, 0.0);
    setWalkPath(EntityAnimation::walk_anim, EntityAnimation::idle_anim, true, path);
    pushAction(CreatureActionType::walkToTile, false, true);
    return false;
}

bool Creature::handleDigTileAction(const CreatureActionWrapper& actionItem)
{
    Tile* myTile = getPositionTile();
    if (myTile == nullptr)
        return false;

    // See if any of the tiles is one of our neighbors
    bool wasANeighbor = false;
    std::vector<Tile*> creatureNeighbors = myTile->getAllNeighbors();
    Player* tempPlayer = getGameMap()->getPlayerBySeat(getSeat());
    for (Tile* tempTile : creatureNeighbors)
    {
        if(wasANeighbor)
            break;

        if (tempPlayer == nullptr)
            break;

        if (!tempTile->getMarkedForDigging(tempPlayer))
            continue;

        // We found a tile marked by our controlling seat, dig out the tile.

        // Dig out the tile by decreasing the tile's fullness.
        Ogre::Vector3 walkDirection(tempTile->getX() - getPosition().x, tempTile->getY() - getPosition().y, 0);
        walkDirection.normalise();
        setAnimationState(EntityAnimation::dig_anim, true, walkDirection);
        double amountDug = tempTile->digOut(mDigRate);
        if(amountDug > 0.0)
        {
            receiveExp(1.5 * mDigRate / 20.0);

            // If the tile is a gold tile accumulate gold for this creature.
            switch(tempTile->getType())
            {
                case TileType::gold:
                {
                    static const double digCoefGold = ConfigManager::getSingleton().getDigCoefGold();
                    double tempDouble = digCoefGold * amountDug;
                    mGoldCarried += static_cast<int>(tempDouble);
                    getSeat()->addGoldMined(static_cast<int>(tempDouble));
                    // Receive extra experience for digging gold
                    receiveExp(digCoefGold * mDigRate / 20.0);
                    break;
                }
                case TileType::gem:
                {
                    static const double digCoefGem = ConfigManager::getSingleton().getDigCoefGold();
                    double tempDouble = digCoefGem * amountDug;
                    mGoldCarried += static_cast<int>(tempDouble);
                    getSeat()->addGoldMined(static_cast<int>(tempDouble));
                    // Receive extra experience for digging gold
                    receiveExp(digCoefGem * mDigRate / 20.0);
                    break;
                }
                default:
                    break;
            }

            // If the tile has been dug out, move into that tile and try to continue digging.
            if (tempTile->getFullness() == 0.0)
            {
                receiveExp(2.5);
                Ogre::Vector3 v (static_cast<Ogre::Real>(tempTile->getX()), static_cast<Ogre::Real>(tempTile->getY()), 0.0);
                std::vector<Ogre::Vector3> path;
                path.push_back(v);
                setWalkPath(EntityAnimation::walk_anim, EntityAnimation::idle_anim, true, path);
                pushAction(CreatureActionType::walkToTile, false, true);
            }
            //Set sound position and play dig sound.
            fireCreatureSound(CreatureSound::Dig);
        }
        else
        {
            //We tried to dig a tile we are not able to
            //Completely bail out if this happens.
            clearActionQueue();
        }

        wasANeighbor = true;
        break;
    }

    // Check to see if we are carrying the maximum amount of gold we can carry, and if so, try to take it to a treasury.
    if (mGoldCarried >= mDefinition->getMaxGoldCarryable())
    {
        // We create the treasury object and push action to deposit it
        TreasuryObject* obj = new TreasuryObject(getGameMap(), true, mGoldCarried);
        mGoldCarried = 0;
        Ogre::Vector3 pos(static_cast<Ogre::Real>(myTile->getX()), static_cast<Ogre::Real>(myTile->getY()), 0.0f);
        obj->addToGameMap();
        obj->createMesh();
        obj->setPosition(pos);

        std::vector<Room*> treasuries = getGameMap()->getRoomsByTypeAndSeat(RoomType::treasury, getSeat());
        treasuries = getGameMap()->getReachableRooms(treasuries, myTile, this);
        bool isTreasuryAvailable = false;
        for(Room* room : treasuries)
        {
            RoomTreasury* treasury = static_cast<RoomTreasury*>(room);
            if(treasury->emptyStorageSpace() <= 0)
                continue;

            isTreasuryAvailable = true;
            break;
        }
        if(isTreasuryAvailable)
        {
            pushAction(CreatureActionType::carryEntity, false, true);
            return true;
        }

        if((getSeat() != nullptr) &&
            (getSeat()->getPlayer() != nullptr) &&
            (getSeat()->getPlayer()->getIsHuman()))
        {
            getSeat()->getPlayer()->notifyNoTreasuryAvailable();
        }
    }

    // If we successfully dug a tile then we are done for this turn.
    if (wasANeighbor)
        return false;

    // Find paths to all of the neighbor tiles for all of the marked visible tiles.
    std::vector<std::list<Tile*> > possiblePaths;
    for (unsigned int i = 0; i < mVisibleMarkedTiles.size(); ++i)
    {
        for (Tile* neighborTile : mVisibleMarkedTiles[i]->getAllNeighbors())
        {
            if (getGameMap()->pathExists(this, getPositionTile(), neighborTile))
                possiblePaths.push_back(getGameMap()->path(this, neighborTile));
        }
    }

    // Find the shortest path and start walking toward the tile to be dug out
    if (!possiblePaths.empty())
    {
        // Find the N shortest valid paths, see if there are any valid paths shorter than this first guess
        std::vector<std::list<Tile*> > shortPaths;
        for (unsigned int i = 0; i < possiblePaths.size(); ++i)
        {
            // If the current path is long enough to be valid
            unsigned int currentLength = possiblePaths[i].size();
            if (currentLength >= 2)
            {
                shortPaths.push_back(possiblePaths[i]);

                // If we already have enough short paths
                if (shortPaths.size() > 5)
                {
                    unsigned int longestLength, longestIndex;

                    // Kick out the longest
                    longestLength = shortPaths[0].size();
                    longestIndex = 0;
                    for (unsigned int j = 1; j < shortPaths.size(); ++j)
                    {
                        if (shortPaths[j].size() > longestLength)
                        {
                            longestLength = shortPaths.size();
                            longestIndex = j;
                        }
                    }

                    shortPaths.erase(shortPaths.begin() + longestIndex);
                }
            }
        }

        // Randomly pick a short path to take
        unsigned int numShortPaths = shortPaths.size();
        if (numShortPaths > 0)
        {
            unsigned int shortestIndex;
            shortestIndex = Random::Uint(0, numShortPaths - 1);
            std::list<Tile*> walkPath = shortPaths[shortestIndex];

            // If the path is a legitimate path, walk down it to the tile to be dug out
            std::vector<Ogre::Vector3> path;
            tileToVector3(walkPath, path, true, 0.0);
            setWalkPath(EntityAnimation::walk_anim, EntityAnimation::idle_anim, true, path);
            pushAction(CreatureActionType::walkToTile, false, true);
            return false;
        }
    }

    // If none of our neighbors are marked for digging we got here too late.
    // Finish digging
    mForceAction = forcedActionNone;
    bool isDigging = isActionInList(CreatureActionType::digTile);
    if (isDigging)
    {
        popAction();
        if(mGoldCarried > 0)
        {
            TreasuryObject* obj = new TreasuryObject(getGameMap(), true, mGoldCarried);
            mGoldCarried = 0;
            Ogre::Vector3 pos(static_cast<Ogre::Real>(myTile->getX()), static_cast<Ogre::Real>(myTile->getY()), 0.0f);
            obj->addToGameMap();
            obj->createMesh();
            obj->setPosition(pos);

            std::vector<Room*> treasuries = getGameMap()->getRoomsByTypeAndSeat(RoomType::treasury, getSeat());
            treasuries = getGameMap()->getReachableRooms(treasuries, myTile, this);
            bool isTreasuryAvailable = false;
            for(Room* room : treasuries)
            {
                RoomTreasury* treasury = static_cast<RoomTreasury*>(room);
                if(treasury->emptyStorageSpace() <= 0)
                    continue;

                isTreasuryAvailable = true;
                break;
            }
            if(isTreasuryAvailable)
            {
                pushAction(CreatureActionType::carryEntity, false, true);
            }
            else if((getSeat() != nullptr) &&
                    (getSeat()->getPlayer() != nullptr) &&
                    (getSeat()->getPlayer()->getIsHuman()))
            {
                getSeat()->getPlayer()->notifyNoTreasuryAvailable();
            }
        }

        return true;
    }
    return false;
}

bool Creature::handleFindHomeAction(const CreatureActionWrapper& actionItem)
{
    // Check to see if we are standing in an open dormitory tile that we can claim as our home.
    Tile* myTile = getPositionTile();
    if (myTile == nullptr)
    {
        popAction();
        return false;
    }

    if((mHomeTile != nullptr) && (actionItem.mType != CreatureActionType::findHomeForced))
    {
        popAction();
        return false;
    }

    if((myTile->getCoveringRoom() != nullptr) &&
       (getSeat()->canOwnedCreatureUseRoomFrom(myTile->getCoveringRoom()->getSeat())) &&
       (myTile->getCoveringRoom()->getType() == RoomType::dormitory))
    {
        Room* roomHomeTile = nullptr;
        if(mHomeTile != nullptr)
        {
            roomHomeTile = mHomeTile->getCoveringRoom();
            // Same dormitory nothing to do
            if(roomHomeTile == myTile->getCoveringRoom())
            {
                popAction();
                return true;
            }
        }

        Tile* homeTile = static_cast<RoomDormitory*>(myTile->getCoveringBuilding())->claimTileForSleeping(myTile, this);
        if(homeTile != nullptr)
        {
            // We could install the bed in the dormitory. If we already had one, we remove it
            if(roomHomeTile != nullptr)
                static_cast<RoomDormitory*>(roomHomeTile)->releaseTileForSleeping(mHomeTile, this);

            mHomeTile = homeTile;
            popAction();
            return true;
        }

        // The tile where we are is not claimable. We search if there is another in this dormitory
        Tile* tempTile = static_cast<RoomDormitory*>(myTile->getCoveringBuilding())->getLocationForBed(this);
        if(tempTile != nullptr)
        {
            std::list<Tile*> tempPath = getGameMap()->path(this, tempTile);
            std::vector<Ogre::Vector3> path;
            tileToVector3(tempPath, path, true, 0.0);
            setWalkPath(EntityAnimation::walk_anim, EntityAnimation::idle_anim, true, path);
            pushAction(CreatureActionType::walkToTile, false, true);
            return false;
        }
    }

    // If we found a tile to claim as our home in the above block
    // If we have been forced, we do not search in another dormitory
    if ((mHomeTile != nullptr) || (actionItem.mType == CreatureActionType::findHomeForced))
    {
        popAction();
        return true;
    }

    // Check to see if we can walk to a dormitory that does have an open tile.
    std::vector<Room*> tempRooms = getGameMap()->getRoomsByTypeAndSeat(RoomType::dormitory, getSeat());
    std::random_shuffle(tempRooms.begin(), tempRooms.end());
    unsigned int nearestDormitoryDistance = 0;
    bool validPathFound = false;
    std::list<Tile*> tempPath;
    for (unsigned int i = 0; i < tempRooms.size(); ++i)
    {
        // Get the list of open rooms at the current dormitory and check to see if
        // there is a place where we could put a bed big enough to sleep in.
        Tile* tempTile = static_cast<RoomDormitory*>(tempRooms[i])->getLocationForBed(this);

        // Check to see if either of the two possible bed orientations tried above resulted in a successful placement.
        if (tempTile != nullptr)
        {
            std::list<Tile*> tempPath2 = getGameMap()->path(this, tempTile);

            // Find out the minimum valid path length of the paths determined in the above block.
            if (!validPathFound)
            {
                // If the current path is long enough to be valid then record the path and the distance.
                if (tempPath2.size() >= 2)
                {
                    tempPath = tempPath2;
                    nearestDormitoryDistance = tempPath.size();
                    validPathFound = true;
                }
            }
            else
            {
                // If the current path is long enough to be valid but shorter than the
                // shortest path seen so far, then record the path and the distance.
                if (tempPath2.size() >= 2 && tempPath2.size()
                        < nearestDormitoryDistance)
                {
                    tempPath = tempPath2;
                    nearestDormitoryDistance = tempPath.size();
                }
            }
        }
    }

    // If we found a valid path to an open room in a dormitory, then start walking along it.
    if (validPathFound)
    {
        std::vector<Ogre::Vector3> path;
        tileToVector3(tempPath, path, true, 0.0);
        setWalkPath(EntityAnimation::walk_anim, EntityAnimation::idle_anim, true, path);
        pushAction(CreatureActionType::walkToTile, false, true);
        return false;
    }

    // If we got here there are no reachable dormitory that are unclaimed so we quit trying to find one.
    popAction();
    return true;
}

bool Creature::handleJobAction(const CreatureActionWrapper& actionItem)
{
    // Current creature tile position
    Tile* myTile = getPositionTile();
    if (myTile == nullptr)
    {
        popAction();
        return false;
    }

    // If we are unhappy, we stop working
    switch(mMoodValue)
    {
        case CreatureMoodLevel::Upset:
        {
            // 20% chances of not working
            if(Random::Int(0, 100) < 20)
            {
                popAction();
                stopJob();
                return true;
            }
            break;
        }
        case CreatureMoodLevel::Angry:
        case CreatureMoodLevel::Furious:
        {
            // We don't work
            popAction();
            stopJob();
            return true;
        }
        default:
            // We can work
            break;
    }

    // If we are tired, we go to bed unless we have been slapped
    if (!hasCreatureEffect(CreatureEffectType::slap) &&
        (Random::Double(20.0, 30.0) > mAwakeness))
    {
        popAction();
        stopJob();
        pushAction(CreatureActionType::sleep, false, false);
        return true;
    }

    // If we are hungry, we go to bed unless we have been slapped
    if (!hasCreatureEffect(CreatureEffectType::slap) &&
        (Random::Double(70.0, 80.0) < mHunger))
    {
        popAction();
        stopJob();
        pushAction(CreatureActionType::eatdecided, false, false);
        return true;
    }

    // If we are already working no need to search for a room
    if(mJobRoom != nullptr)
        return false;

    if(actionItem.mType == CreatureActionType::jobforced)
    {
        // We check if we can work in the given room
        Room* room = myTile->getCoveringRoom();
        const std::vector<CreatureRoomAffinity>& roomAffinity = mDefinition->getRoomAffinity();
        for(const CreatureRoomAffinity& affinity : roomAffinity)
        {
            if(room == nullptr)
                continue;
            if(room->getType() != affinity.getRoomType())
                continue;
            if(affinity.getEfficiency() <= 0)
                continue;
            if(!getSeat()->canOwnedCreatureUseRoomFrom(room->getSeat()))
                continue;

            // It is the room responsibility to test if the creature is suited for working in it
            if(room->hasOpenCreatureSpot(this) && room->addCreatureUsingRoom(this))
            {
                mJobRoom = room;
                return false;
            }
            break;
        }

        // If we couldn't work on the room we were forced to, we stop trying
        popAction();
        return true;
    }

    // We get the room we like the most. If we are on such a room, we start working if we can
    const std::vector<CreatureRoomAffinity>& roomAffinity = mDefinition->getRoomAffinity();
    for(const CreatureRoomAffinity& affinity : roomAffinity)
    {
        // If likeness = 0, we don't consider working here
        if(affinity.getLikeness() <= 0)
            continue;

        // See if we are in the room we like the most. If yes and we can work, we stay. If no,
        // We check if there is such a room somewhere else where we can go
        if((myTile->getCoveringRoom() != nullptr) &&
           (myTile->getCoveringRoom()->getType() == affinity.getRoomType()) &&
           (getSeat()->canOwnedCreatureUseRoomFrom(myTile->getCoveringRoom()->getSeat())))
        {
            Room* room = myTile->getCoveringRoom();
            // If the efficiency is 0 or the room is a hatchery, we only wander in the room
            if((affinity.getEfficiency() <= 0) ||
               (room->getType() == RoomType::hatchery))
            {
                int index = Random::Int(0, room->numCoveredTiles() - 1);
                Tile* tileDest = room->getCoveredTile(index);
                std::list<Tile*> tempPath = getGameMap()->path(this, tileDest);
                std::vector<Ogre::Vector3> path;
                tileToVector3(tempPath, path, true, 0.0);
                setWalkPath(EntityAnimation::walk_anim, EntityAnimation::idle_anim, true, path);
                pushAction(CreatureActionType::walkToTile, false, true);
                return false;
            }

            // It is the room responsibility to test if the creature is suited for working in it
            if(room->hasOpenCreatureSpot(this) && room->addCreatureUsingRoom(this))
            {
                mJobRoom = room;
                return false;
            }
        }

        // We are not in a room of the good type or we couldn't use it. We check if there is a reachable room
        // of the good type
        std::vector<Room*> rooms = getGameMap()->getRoomsByTypeAndSeat(affinity.getRoomType(), getSeat());
        rooms = getGameMap()->getReachableRooms(rooms, myTile, this);
        std::random_shuffle(rooms.begin(), rooms.end());
        for(Room* room : rooms)
        {
            // If efficiency is 0, we just want to wander so no need to check if the room
            // is available
            if((affinity.getEfficiency() <= 0) ||
               room->hasOpenCreatureSpot(this))
            {
                int index = Random::Int(0, room->numCoveredTiles() - 1);
                Tile* tileDest = room->getCoveredTile(index);
                std::list<Tile*> tempPath = getGameMap()->path(this, tileDest);
                std::vector<Ogre::Vector3> path;
                tileToVector3(tempPath, path, true, 0.0);
                setWalkPath(EntityAnimation::walk_anim, EntityAnimation::idle_anim, true, path);
                pushAction(CreatureActionType::walkToTile, false, true);
                return false;
            }
        }
    }

    // Default action
    popAction();
    stopJob();
    return true;
}

bool Creature::handleEatingAction(const CreatureActionWrapper& actionItem)
{
    // Current creature tile position
    Tile* myTile = getPositionTile();
    if(myTile == nullptr)
    {
        OD_LOG_ERR("creature=" + getName());
        popAction();
        stopEating();
        return true;
    }

    if(mEatCooldown > 0)
    {
        --mEatCooldown;
        // We do nothing
        setAnimationState(EntityAnimation::idle_anim);
        return false;
    }

    if (((actionItem.mType == CreatureActionType::eatforced) && mHunger < 5.0) ||
        ((actionItem.mType != CreatureActionType::eatforced) && mHunger <= Random::Double(0.0, 15.0)))
    {
        popAction();

        stopEating();
        return true;
    }

    // If we are in a hatchery, we go to the closest chicken in it. If we are not
    // in a hatchery, we check if there is a free chicken and eat it if we see it
    Tile* closestChickenTile = nullptr;
    double closestChickenDist = 0.0;
    for(Tile* tile : mTilesWithinSightRadius)
    {
        std::vector<GameEntity*> chickens;
        tile->fillWithChickenEntities(chickens);
        if(chickens.empty())
            continue;

        if((mEatRoom == nullptr) &&
           (tile->getCoveringRoom() != nullptr) &&
           (tile->getCoveringRoom()->getType() == RoomType::hatchery))
        {
            // We are not in a hatchery and the currently processed tile is a hatchery. We
            // cannot eat any chicken there
            continue;
        }

        if((mEatRoom != nullptr) && (tile->getCoveringBuilding() != mEatRoom))
            continue;

        double dist = std::pow(static_cast<double>(std::abs(tile->getX() - myTile->getX())), 2);
        dist += std::pow(static_cast<double>(std::abs(tile->getY() - myTile->getY())), 2);
        if((closestChickenTile == nullptr) ||
           (closestChickenDist > dist))
        {
            closestChickenTile = tile;
            closestChickenDist = dist;
        }
    }

    if(closestChickenTile != nullptr)
    {
        if(closestChickenDist <= 1.0)
        {
            // We eat the chicken
            std::vector<GameEntity*> chickens;
            closestChickenTile->fillWithChickenEntities(chickens);
            if(chickens.empty())
            {
                OD_LOG_ERR("name=" + getName());
                return false;
            }
            ChickenEntity* chicken = static_cast<ChickenEntity*>(chickens.at(0));
            chicken->eatChicken(this);
            foodEaten(ConfigManager::getSingleton().getRoomConfigDouble("HatcheryHungerPerChicken"));
            mEatCooldown = Random::Int(ConfigManager::getSingleton().getRoomConfigUInt32("HatcheryCooldownChickenMin"),
                ConfigManager::getSingleton().getRoomConfigUInt32("HatcheryCooldownChickenMax"));
            mHp += ConfigManager::getSingleton().getRoomConfigDouble("HatcheryHpRecoveredPerChicken");
            computeCreatureOverlayHealthValue();
            Ogre::Vector3 walkDirection = Ogre::Vector3(closestChickenTile->getX(), closestChickenTile->getY(), 0) - getPosition();
            walkDirection.normalise();
            setAnimationState(EntityAnimation::attack_anim, false, walkDirection);
            return false;
        }

        // We walk to the chicken
        std::list<Tile*> pathToChicken = getGameMap()->path(this, closestChickenTile);
        if(pathToChicken.empty())
        {
            OD_LOG_ERR("creature=" + getName() + " empty path to chicken");
            popAction();
            stopEating();
            return true;
        }

        // We make sure we don't go too far as the chicken is also moving
        if(pathToChicken.size() > 2)
        {
            // We only keep 80% of the path
            int nbTiles = 8 * pathToChicken.size() / 10;
            pathToChicken.resize(nbTiles);
        }

        std::vector<Ogre::Vector3> path;
        tileToVector3(pathToChicken, path, true, 0.0);
        setWalkPath(EntityAnimation::walk_anim, EntityAnimation::idle_anim, true, path);
        pushAction(CreatureActionType::walkToTile, false, true);
        return false;
    }

    if(mEatRoom != nullptr)
        return false;

    // See if we are in a hatchery. If so, we try to add the creature. If it is ok, the room
    // will handle the creature from here to make it go where it should
    if((myTile->getCoveringRoom() != nullptr) &&
       (getSeat()->canOwnedCreatureUseRoomFrom(myTile->getCoveringRoom()->getSeat())) &&
       (myTile->getCoveringRoom()->getType() == RoomType::hatchery) &&
       (myTile->getCoveringRoom()->hasOpenCreatureSpot(this)))
    {
        Room* tempRoom = myTile->getCoveringRoom();
        if(tempRoom->addCreatureUsingRoom(this))
        {
            mEatRoom = tempRoom;
            return false;
        }
    }

    // Get the list of hatchery controlled by our seat and make sure there is at least one.
    std::vector<Room*> tempRooms = getGameMap()->getRoomsByTypeAndSeat(RoomType::hatchery, getSeat());

    if (tempRooms.empty())
    {
        popAction();

        stopEating();
        return true;
    }

    // Pick a hatchery and try to walk to it.
    double maxDistance = 40.0;
    Room* tempRoom = nullptr;
    int nbTry = 5;
    do
    {
        int tempInt = Random::Uint(0, tempRooms.size() - 1);
        tempRoom = tempRooms[tempInt];
        tempRooms.erase(tempRooms.begin() + tempInt);
        double tempDouble = 1.0 / (maxDistance - Pathfinding::distanceTile(*myTile, *tempRoom->getCoveredTile(0)));
        if (Random::Double(0.0, 1.0) < tempDouble)
            break;
        --nbTry;
    } while (nbTry > 0 && !tempRoom->hasOpenCreatureSpot(this) && !tempRooms.empty());

    if (!tempRoom || !tempRoom->hasOpenCreatureSpot(this))
    {
        // The room is already being used, stop trying to eat
        popAction();
        stopEating();
        return true;
    }

    Tile* tempTile = tempRoom->getCoveredTile(Random::Uint(0, tempRoom->numCoveredTiles() - 1));
    std::list<Tile*> tempPath = getGameMap()->path(this, tempTile);
    if (tempPath.size() < maxDistance)
    {
        std::vector<Ogre::Vector3> path;
        tileToVector3(tempPath, path, true, 0.0);
        setWalkPath(EntityAnimation::walk_anim, EntityAnimation::idle_anim, true, path);
        pushAction(CreatureActionType::walkToTile, false, true);
        return false;
    }
    else
    {
        // We could not find a room where we can eat so stop trying to find one.
        popAction();
        stopEating();
        return true;
    }
}

void Creature::stopJob()
{
    if (mJobRoom == nullptr)
        return;

    mJobRoom->removeCreatureUsingRoom(this);
    mJobRoom = nullptr;
}

void Creature::stopEating()
{
    if (mEatRoom == nullptr)
        return;

    mEatRoom->removeCreatureUsingRoom(this);
    mEatRoom = nullptr;
}

bool Creature::isJobRoom(Room* room)
{
    return mJobRoom == room;
}

bool Creature::isEatRoom(Room* room)
{
    return mEatRoom == room;
}

bool Creature::handleAttackAction(const CreatureActionWrapper& actionItem)
{
    // We always pop action to make sure next time we will try to find if a closest foe is there
    // or if we need to hit and run
    popAction();

    if (actionItem.mTile == nullptr)
        return true;

    // actionItem.mAttackedEntity can be nullptr if the entity died between the time we started to chase it and the time we strike
    if(actionItem.mAttackedEntity == nullptr)
        return true;

    // We check what we are attacking.

    // Turn to face the creature we are attacking and set the animation state to Attack.
    Ogre::Vector3 walkDirection(actionItem.mTile->getX() - getPosition().x, actionItem.mTile->getY() - getPosition().y, 0);
    walkDirection.normalise();
    setAnimationState(EntityAnimation::attack_anim, false, walkDirection);

    fireCreatureSound(CreatureSound::Attack);

    mNbTurnsWithoutBattle = 0;

    // Calculate how much damage we do.
    Tile* myTile = getPositionTile();
    float range = Pathfinding::distanceTile(*myTile, *actionItem.mTile);
    if(actionItem.mCreatureSkillData == nullptr)
    {
        OD_LOG_ERR("creature=" + getName() + ", attackedObject=" + actionItem.mAttackedEntity->getName()
            + ", myTile=" + Tile::displayAsString(myTile) + ", attackedTile=" + Tile::displayAsString(actionItem.mTile));
        return false;
    }

    // We use the skill
    actionItem.mCreatureSkillData->mSkill->tryUseFight(*getGameMap(), this, range,
        actionItem.mAttackedEntity, actionItem.mTile);
    actionItem.mCreatureSkillData->mWarmup = actionItem.mCreatureSkillData->mSkill->getWarmupNbTurns();
    actionItem.mCreatureSkillData->mCooldown = actionItem.mCreatureSkillData->mSkill->getCooldownNbTurns();

    // Fighting is tiring
    decreaseAwakeness(0.5);
    // but gives experience
    receiveExp(1.5);

    return false;
}

bool Creature::handleFightAction(const CreatureActionWrapper& actionItem)
{
    // If there are no more enemies, stop fighting.
    if (mVisibleEnemyObjects.empty())
    {
        popAction();
        return true;
    }

    Tile* myTile = getPositionTile();
    if(myTile == nullptr)
    {
        OD_LOG_ERR("name=" + getName() + ", position=" + Helper::toString(getPosition()));
        popAction();
        return false;
    }

    // We try to attack creatures first
    std::vector<GameEntity*> enemyPrioritaryTargets;
    std::vector<GameEntity*> enemySecondaryTargets;
    for(GameEntity* entity : mVisibleEnemyObjects)
    {
        switch(entity->getObjectType())
        {
            case GameEntityType::creature:
            {
                Creature* creature = static_cast<Creature*>(entity);
                if(!creature->isAlive())
                    continue;

                // Workers should attack workers only
                if(getDefinition()->isWorker() && !creature->getDefinition()->isWorker())
                    continue;

                enemyPrioritaryTargets.push_back(creature);
                break;
            }
            default:
            {
                // Workers can attack workers only
                if(getDefinition()->isWorker())
                    continue;

                enemySecondaryTargets.push_back(entity);
            }
        }
    }

    if(!enemyPrioritaryTargets.empty())
    {
        GameEntity* entityAttack = nullptr;
        Tile* tileAttack = nullptr;
        Tile* tilePosition = nullptr;
        CreatureSkillData* skillData = nullptr;
        if(searchBestTargetInList(enemyPrioritaryTargets, entityAttack, tileAttack, tilePosition, skillData))
        {
            if((myTile == tilePosition) &&
               (entityAttack != nullptr) &&
               (tileAttack != nullptr) &&
               (skillData != nullptr))
            {
                // We can attack
                pushAction(CreatureActionType::attackObject, false, true, entityAttack, tileAttack, skillData);
                return true;
            }

            // We need to move
            std::list<Tile*> result = getGameMap()->path(this, tilePosition);
            if(result.empty())
            {
                OD_LOG_ERR("name=" + getName() + ", myTile=" + Tile::displayAsString(myTile) + ", dest=" + Tile::displayAsString(tilePosition));
                return false;
            }

            if(result.size() > 3)
                result.resize(3);

            std::vector<Ogre::Vector3> path;
            tileToVector3(result, path, true, 0.0);
            setWalkPath(EntityAnimation::walk_anim, EntityAnimation::idle_anim, true, path);
            pushAction(CreatureActionType::walkToTile, false, true);
            return false;
        }
    }

    if(!enemySecondaryTargets.empty())
    {
        GameEntity* entityAttack = nullptr;
        Tile* tileAttack = nullptr;
        Tile* tilePosition = nullptr;
        CreatureSkillData* skillData = nullptr;
        if(searchBestTargetInList(enemySecondaryTargets, entityAttack, tileAttack, tilePosition, skillData))
        {
            if((myTile == tilePosition) &&
               (entityAttack != nullptr) &&
               (tileAttack != nullptr) &&
               (skillData != nullptr))
            {
                // We can attack
                pushAction(CreatureActionType::attackObject, false, true, entityAttack, tileAttack, skillData);
                return true;
            }

            // We need to move to the entity
            std::list<Tile*> result = getGameMap()->path(this, tilePosition);
            if(result.empty())
            {
                OD_LOG_ERR("name" + getName() + ", myTile=" + Tile::displayAsString(myTile) + ", dest=" + Tile::displayAsString(tilePosition));
                return false;
            }

            if(result.size() > 3)
                result.resize(3);

            std::vector<Ogre::Vector3> path;
            tileToVector3(result, path, true, 0.0);
            setWalkPath(EntityAnimation::walk_anim, EntityAnimation::idle_anim, true, path);
            pushAction(CreatureActionType::walkToTile, false, true);
            return false;
        }
    }

    // No suitable target
    popAction();
    return true;
}

bool Creature::searchBestTargetInList(const std::vector<GameEntity*>& listObjects, GameEntity*& attackedEntity, Tile*& attackedTile, Tile*& positionTile, CreatureSkillData*& creatureSkillData)
{
    Tile* myTile = getPositionTile();
    if(myTile == nullptr)
    {
        OD_LOG_ERR("name=" + getName() + ", position=" + Helper::toString(getPosition()));
        return false;
    }

    GameEntity* entityFlee = nullptr;
    // Closest creature
    GameEntity* entityAttack = nullptr;
    Tile* tileAttack = nullptr;
    CreatureSkillData* skillData = nullptr;
    Tile* tilePosition = nullptr;
    int closestDist = -1;
    // We try to attack creatures first
    for(GameEntity* entity : listObjects)
    {
        GameEntity* entityAttackCheck = nullptr;
        Tile* tileAttackCheck = nullptr;
        CreatureSkillData* skillDataCheck = nullptr;
        int closestDistCheck = closestDist;
        // We check if this creature is closer than the other one (if any)
        std::vector<Tile*> coveredTiles = entity->getCoveredTiles();
        for(Tile* tile : coveredTiles)
        {
            int dist = Pathfinding::squaredDistanceTile(*tile, *myTile);
            if((closestDistCheck != -1) && (dist >= closestDistCheck))
                continue;

            // We found a tile closer
            // Note that we don't break because if this entity is on more than 1 tile,
            // we want to attack the closest tile
            closestDistCheck = dist;
            entityAttackCheck = entity;
            tileAttackCheck = tile;
        }

        if((entityAttackCheck == nullptr) || (tileAttackCheck == nullptr))
            continue;

        // We check if we are supposed to flee from this entity
        if((entityFlee == nullptr) && entityAttackCheck->isDangerous(this, closestDistCheck))
            entityFlee = entityAttackCheck;

        // If we found a suitable enemy, we check if we can attack it
        double skillRangeMax = 0.0;
        for(CreatureSkillData& skillDataTmp : mSkillData)
        {
            if(skillDataTmp.mCooldown > 0)
                continue;

            if(!skillDataTmp.mSkill->canBeUsedBy(this))
                continue;

            double skillRange = skillDataTmp.mSkill->getRangeMax(this, entityAttackCheck);
            if(skillRange <= 0)
                continue;
            if(skillRange < skillRangeMax)
                continue;

            skillRangeMax = skillRange;
            skillDataCheck = &skillDataTmp;
        }
        if(skillRangeMax <= 0)
            continue;

        // Check if we can attack from our position
        int rangeTarget = Pathfinding::squaredDistanceTile(*tileAttackCheck, *myTile);
        if(rangeTarget <= (skillRangeMax * skillRangeMax))
        {
             // We can attack
             if((closestDist == -1) || (rangeTarget < closestDist))
             {
                tilePosition = myTile;
                entityAttack = entityAttackCheck;
                tileAttack = tileAttackCheck;
                skillData = skillDataCheck;
                closestDist = rangeTarget;
             }
             continue;
        }

        // We check if we can attack from somewhere. To do that, we check
        // from the target point of view if there is a tile with visibility within range
        int skillRangeMaxInt = static_cast<int>(skillRangeMax);
        int skillRangeMaxIntSquared = skillRangeMaxInt * skillRangeMaxInt;
        int bestScoreAttack = -1;
        std::vector<Tile*> tiles = getGameMap()->visibleTiles(tileAttackCheck->getX(), tileAttackCheck->getY(), skillRangeMaxInt);
        for(Tile* tile : tiles)
        {
            if(tile->isFullTile())
                continue;

            if(!getGameMap()->pathExists(this, myTile, tile))
                continue;

            int distFoeTmp = Pathfinding::squaredDistanceTile(*tile, *tileAttackCheck);
            int distAttackTmp = Pathfinding::squaredDistanceTile(*tile, *myTile);
            // We compute a score for each tile. We will choose the best one. Note that we try to be as close as possible
            // from the fightIdleDist but by walking the less possible. We need to find a compromise
            int scoreAttack = std::abs(skillRangeMaxIntSquared - distFoeTmp) * 2 + distAttackTmp;
            if((bestScoreAttack != -1) && (bestScoreAttack <= scoreAttack))
                continue;

            // We found a better target
            bestScoreAttack = scoreAttack;
            tilePosition = tile;
            entityAttack = entityAttackCheck;
            tileAttack = tileAttackCheck;
            skillData = skillDataCheck;
            closestDist = closestDistCheck;
            // We don't break because there might be a better spot
        }
    }

    // If there is a dangerous entity and we cannot attack, we should try to get away
    if((entityFlee != nullptr) && (skillData == nullptr))
    {
        // Let's try to run to the closest spot at the distance closest to the fight idle distance
        Tile* tileEntityFlee = entityFlee->getPositionTile();
        if(tileEntityFlee == nullptr)
        {
            OD_LOG_ERR("entity=" + entityFlee->getName() + ", position=" + Helper::toString(entityFlee->getPosition()));
            return false;
        }

        int bestScoreFlee = -1;
        int32_t fightIdleDist = getDefinition()->getFightIdleDist();
        Tile* fleeTile = nullptr;
        std::vector<Tile*> tiles = getGameMap()->visibleTiles(tileEntityFlee->getX(), tileEntityFlee->getY(), fightIdleDist);
        int32_t fightIdleDistSquared = fightIdleDist * fightIdleDist;
        for(Tile* tile : tiles)
        {
            if(tile->isFullTile())
                continue;

            if(!getGameMap()->pathExists(this, myTile, tile))
                continue;

            int distFoeTmp = Pathfinding::squaredDistanceTile(*tile, *tileEntityFlee);
            int fleeDistTmp = Pathfinding::squaredDistanceTile(*tile, *myTile);
            // We compute a score for each tile. We will choose the best one. Note that we try to be as close as possible
            // from the fightIdleDist but by walking the less possible. We need to find a compromise
            int scoreFlee = std::abs(fightIdleDistSquared - distFoeTmp) * 2 + fleeDistTmp;
            if((bestScoreFlee != -1) && (bestScoreFlee <= scoreFlee))
                continue;

            bestScoreFlee = scoreFlee;
            fleeTile = tile;
        }

        if(fleeTile == nullptr)
            return false;

        attackedEntity = nullptr;
        attackedTile = nullptr;
        positionTile = fleeTile;
    }
    else if ((entityAttack == nullptr) ||
        (tileAttack == nullptr) ||
        (tilePosition == nullptr))
    {
        // We couldn't find an entity to attack
        return false;
    }
    else
    {
        attackedEntity = entityAttack;
        attackedTile = tileAttack;
        creatureSkillData = skillData;
        positionTile = tilePosition;
    }

    return true;
}

bool Creature::handleSleepAction(const CreatureActionWrapper& actionItem)
{
    if (mHomeTile == nullptr)
    {
        if(pushAction(CreatureActionType::findHome, false, false))
            return true;

        popAction();
        return false;
    }

    Tile* myTile = getPositionTile();
    if (myTile != mHomeTile)
    {
        // Walk to the the home tile.
        if (setDestination(mHomeTile))
            return false;
    }
    else
    {
        // We are at the home tile so sleep. If it is the first time we are sleeping,
        // we send the animation
        if(actionItem.mNbTurnsActive == 0)
        {
            Room* roomHomeTile = mHomeTile->getCoveringRoom();
            if((roomHomeTile == nullptr) || (roomHomeTile->getType() != RoomType::dormitory))
            {
                OD_LOG_ERR("creature=" + getName() + ", wrong sleep tile on " + Tile::displayAsString(mHomeTile));
                popAction();
                return false;
            }
            RoomDormitory* dormitory = static_cast<RoomDormitory*>(roomHomeTile);
            setAnimationState(EntityAnimation::sleep_anim, false, dormitory->getSleepDirection(this));
        }

        // Improve awakeness
        mAwakeness += 1.5;
        if (mAwakeness > 100.0)
            mAwakeness = 100.0;

        mHp += mDefinition->getSleepHeal();
        if (mHp > mMaxHP)
            mHp = mMaxHP;

        computeCreatureOverlayHealthValue();

        if (mAwakeness >= 100.0 && mHp >= mMaxHP)
        {
            popAction();
            return false;
        }
    }
    return false;
}

bool Creature::handleFleeAction(const CreatureActionWrapper& actionItem)
{
    // We try to go as far as possible from the enemies within visible tiles. We will quit flee mode when there will be no more
    // enemy objects nearby or if we have already flee for too much time
    if ((mVisibleEnemyObjects.empty()) || (actionItem.mNbTurns > NB_TURN_FLEE_MAX))
    {
        popAction();
        return true;
    }

    // We try to go closer to the dungeon temple. If we are too near or if we cannot go there, we will flee randomly
    std::vector<Room*> tempRooms = getGameMap()->getRoomsByTypeAndSeat(RoomType::dungeonTemple, getSeat());
    tempRooms = getGameMap()->getReachableRooms(tempRooms, getPositionTile(), this);
    if(!tempRooms.empty())
    {
        // We can go to one dungeon temple
        Room* room = tempRooms[Random::Int(0, tempRooms.size() - 1)];
        Tile* tile = room->getCoveredTile(0);
        std::list<Tile*> result = getGameMap()->path(this, tile);
        // If we are not too near from the dungeon temple, we go there
        if(result.size() > 5)
        {
            result.resize(5);
            std::vector<Ogre::Vector3> path;
            tileToVector3(result, path, true, 0.0);
            setWalkPath(EntityAnimation::flee_anim, EntityAnimation::idle_anim, true, path);
            pushAction(CreatureActionType::walkToTile, false, true);
            return false;
        }
    }

    // No dungeon temple is acessible or we are too near. We will wander randomly
    wanderRandomly(EntityAnimation::flee_anim);
    return false;
}

bool Creature::handleCarryableEntities(const CreatureActionWrapper& actionItem)
{
    Tile* myTile = getPositionTile();
    if(myTile == nullptr)
    {
        OD_LOG_ERR("name=" + getName());
        popAction();
        return true;
    }

    // If we are not carrying anything, we check if there is something carryable around
    if(mCarriedEntity == nullptr)
    {
        std::vector<Building*> buildings = getGameMap()->getReachableBuildingsPerSeat(getSeat(), myTile, this);
        std::vector<GameEntity*> carryableEntities = getGameMap()->getVisibleCarryableEntities(this, mVisibleTiles);
        std::vector<Tile*> carryableEntityInMyTileClients;
        std::vector<GameEntity*> availableEntities;
        EntityCarryType highestPriority = EntityCarryType::notCarryable;
        // If a carryable entity of highest priority is in my tile, I proceed it
        GameEntity* carryableEntityInMyTile = nullptr;
        for(GameEntity* entity : carryableEntities)
        {
            // We start by checking that the carryable entity is reachable
            Tile* carryableEntTile = entity->getPositionTile();
            if(!getGameMap()->pathExists(this, myTile, carryableEntTile))
                continue;

            // If we are forced to carry something, we consider only entities on our tile
            if((actionItem.mType == CreatureActionType::carryEntityForced) &&
               (myTile != carryableEntTile))
            {
                continue;
            }

            // We check if the current entity is highest or equal to the older one (if any)
            if(entity->getEntityCarryType(this) > highestPriority)
            {
                // We check if a buildings wants this entity
                std::vector<Tile*> tilesDest;
                for(Building* building : buildings)
                {
                    if(building->hasCarryEntitySpot(entity))
                    {
                        Tile* tile = building->getCoveredTile(0);
                        tilesDest.push_back(tile);
                    }
                }

                if(!tilesDest.empty())
                {
                    // We found a reachable building for a higher priority entity. We use this from now on
                    carryableEntityInMyTile = nullptr;
                    availableEntities.clear();
                    availableEntities.push_back(entity);
                    highestPriority = entity->getEntityCarryType(this);
                    if(myTile == carryableEntTile)
                    {
                        carryableEntityInMyTile = entity;
                        carryableEntityInMyTileClients = tilesDest;
                    }
                }
            }
            else if(entity->getEntityCarryType(this) == highestPriority)
            {
                // We check if a buildings wants this entity
                std::vector<Tile*> tilesDest;
                for(Building* building : buildings)
                {
                    if(building->hasCarryEntitySpot(entity))
                    {
                        Tile* tile = building->getCoveredTile(0);
                        tilesDest.push_back(tile);
                    }
                }

                if(!tilesDest.empty())
                {
                    // We found a reachable building for a higher priority entity. We use this from now on
                    availableEntities.push_back(entity);
                    if((myTile == carryableEntTile) &&
                       (carryableEntityInMyTile == nullptr))
                    {
                        carryableEntityInMyTile = entity;
                        carryableEntityInMyTileClients = tilesDest;
                    }
                }
            }
            else
            {
                // Entity with lower priority. We don't proceed
            }
        }

        if(availableEntities.empty())
        {
            // No entity to carry. We can do something else
            popAction();
            return true;
        }

        // If a carryable entity is in my tile, I take it
        if(carryableEntityInMyTile != nullptr)
        {
            // We search for the closest building
            Tile* tileBuilding = nullptr;
            getGameMap()->findBestPath(this, myTile, carryableEntityInMyTileClients, tileBuilding);
            if(tileBuilding == nullptr)
            {
                // We couldn't find a way to the building
                popAction();
                return true;
            }

            Building* buildingWants = tileBuilding->getCoveringBuilding();
            Tile* tileDest = buildingWants->askSpotForCarriedEntity(carryableEntityInMyTile);
            if(tileDest == nullptr)
            {
                // The building doesn't want the entity after all
                popAction();
                return true;
            }

            if(!setDestination(tileDest))
            {
                popAction();
                return true;
            }

            mCarriedEntityDestType = buildingWants->getObjectType();
            mCarriedEntityDestName = buildingWants->getName();
            carryEntity(carryableEntityInMyTile);
            return false;
        }

        // We randomly choose one of the visible carryable entities
        uint32_t index = Random::Uint(0,availableEntities.size()-1);
        GameEntity* entity = availableEntities[index];
        Tile* t = entity->getPositionTile();
        if(t == nullptr)
        {
            OD_LOG_ERR("entity=" + entity->getName());
            popAction();
            return true;
        }

        if(!setDestination(t))
        {
            popAction();
            return true;
        }

        return false;
    }

    // If we are in this state while carrying something, we should be at the destination
    releaseCarriedEntity();
    popAction();
    return true;
}

bool Creature::handleGetFee(const CreatureActionWrapper& actionItem)
{
    Tile* myTile = getPositionTile();
    if(myTile == nullptr)
    {
        popAction();
        return true;
    }
    // We check if we are on a treasury. If yes, we try to take our fee
    if((myTile->getCoveringRoom() != nullptr) &&
       (myTile->getCoveringRoom()->getType() == RoomType::treasury) &&
       (getSeat()->canOwnedCreatureUseRoomFrom(myTile->getCoveringRoom()->getSeat())))
    {
        RoomTreasury* treasury = static_cast<RoomTreasury*>(myTile->getCoveringRoom());
        int gold = treasury->getTotalGold();
        if(gold > 0)
        {
            int goldTaken = treasury->withdrawGold(std::min(gold, mGoldFee));
            mGoldFee -= goldTaken;
            if((getSeat()->getPlayer() != nullptr) &&
               (getSeat()->getPlayer()->getIsHuman()))
            {
                ServerNotification *serverNotification = new ServerNotification(
                    ServerNotificationType::chatServer, getSeat()->getPlayer());
                std::string msg;
                // We don't display the same message if we have taken all our fee or only a part of it
                if(mGoldFee <= 0)
                    msg = getName() + " took its fee: " + Helper::toString(goldTaken);
                else
                    msg = getName() + " took " + Helper::toString(goldTaken) + " from its fee";

                serverNotification->mPacket << msg << EventShortNoticeType::aboutCreatures;
                ODServer::getSingleton().queueServerNotification(serverNotification);
            }

            mGoldCarried += goldTaken;
            if(mGoldFee <= 0)
            {
                // We were able to take all the gold. We can do something else
                mGoldFee = 0;
                popAction();
                return true;
            }
        }
    }

    // We try to go to some treasury were there is still some gold
    std::vector<Room*> tempRooms = getGameMap()->getRoomsByTypeAndSeat(RoomType::treasury, getSeat());
    tempRooms = getGameMap()->getReachableRooms(tempRooms, getPositionTile(), this);
    while(!tempRooms.empty())
    {
        // We can go to one treasury
        int index = Random::Int(0, tempRooms.size() - 1);
        Room* room = tempRooms[index];
        tempRooms.erase(tempRooms.begin() + index);
        RoomTreasury* treasury = static_cast<RoomTreasury*>(room);
        if(treasury->getTotalGold() > 0)
        {
            Tile* tile = room->getCoveredTile(0);
            std::list<Tile*> result = getGameMap()->path(this, tile);
            std::vector<Ogre::Vector3> path;
            tileToVector3(result, path, true, 0.0);
            setWalkPath(EntityAnimation::walk_anim, EntityAnimation::idle_anim, true, path);
            pushAction(CreatureActionType::walkToTile, false, true);
            return false;
        }
    }

    // No available treasury
    popAction();
    return true;
}

bool Creature::handleLeaveDungeon(const CreatureActionWrapper& actionItem)
{
    Tile* myTile = getPositionTile();
    if(myTile == nullptr)
    {
        popAction();
        return true;
    }

    // Check if we are on the central tile of a portal
    if((myTile->getCoveringRoom() != nullptr) &&
       (myTile->getCoveringRoom()->getType() == RoomType::portal) &&
       (getSeat()->canOwnedCreatureUseRoomFrom(myTile->getCoveringRoom()->getSeat())))
    {
        if(myTile == myTile->getCoveringRoom()->getCentralTile())
        {
            // We are on the central tile. We can leave the dungeon
            // If the creature has a homeTile where it sleeps, its bed needs to be destroyed.
            stopJob();
            stopEating();
            clearDestinations(EntityAnimation::idle_anim, true);

            // Remove the creature from the game map and into the deletion queue, it will be deleted
            // when it is safe, i.e. all other pointers to it have been wiped from the program.
            removeFromGameMap();
            deleteYourself();
            return false;
        }
    }

    // We try to go to the portal
    std::vector<Room*> tempRooms = getGameMap()->getRoomsByTypeAndSeat(RoomType::portal, getSeat());
    tempRooms = getGameMap()->getReachableRooms(tempRooms, myTile, this);
    if(tempRooms.empty())
    {
        popAction();
        return true;
    }

    int index = Random::Int(0, tempRooms.size() - 1);
    Room* room = tempRooms[index];
    Tile* tile = room->getCentralTile();
    std::list<Tile*> result = getGameMap()->path(this, tile);
    std::vector<Ogre::Vector3> path;
    tileToVector3(result, path, true, 0.0);
    setWalkPath(EntityAnimation::walk_anim, EntityAnimation::idle_anim, true, path);
    pushAction(CreatureActionType::walkToTile, false, true);
    return false;
}

bool Creature::handleFightAlliedNaturalEnemyAction(const CreatureActionWrapper& actionItem)
{
    Tile* myTile = getPositionTile();
    if(myTile == nullptr)
    {
        popAction();
        return true;
    }

    // We look for a reachable allied natural enemy
    GameEntity* entityAttack = nullptr;
    Tile* tileAttack = nullptr;
    Tile* tilePosition = nullptr;
    CreatureSkillData* skillData = nullptr;
    // If we are being attacked by a natural enemy, we attack it. Otherwise, we start looking
    // for a natural enemy to attack
    std::vector<GameEntity*> alliedNaturalEnemies;
    if(actionItem.mAttackedEntity != nullptr)
        alliedNaturalEnemies.push_back(actionItem.mAttackedEntity);
    else
        alliedNaturalEnemies = getGameMap()->getNaturalEnemiesInList(this, mReachableAlliedObjects);

    if(!alliedNaturalEnemies.empty() && searchBestTargetInList(alliedNaturalEnemies, entityAttack, tileAttack, tilePosition, skillData))
    {
        // We found a natural enemy. We stop what we were doing
        clearDestinations(EntityAnimation::idle_anim, true);
        clearActionQueue();
        stopJob();
        stopEating();

        // And we attack
        if(entityAttack->getObjectType() != GameEntityType::creature)
        {
            OD_LOG_ERR("attacker=" + getName() + ", attacked=" + entityAttack->getName() + ", type=" + Helper::toString(static_cast<uint32_t>(entityAttack->getObjectType())));
            return false;
        }
        Creature* attackedCreature = static_cast<Creature*>(entityAttack);
        attackedCreature->engageAlliedNaturalEnemy(this);

        if((myTile == tilePosition) &&
           (entityAttack != nullptr) &&
           (tileAttack != nullptr) &&
           (skillData != nullptr))
        {
            // We can attack
            pushAction(CreatureActionType::attackObject, false, true, entityAttack, tileAttack, skillData);
            return true;
        }

        // We need to move to the entity
        std::list<Tile*> result = getGameMap()->path(this, tilePosition);
        if(result.empty())
        {
            OD_LOG_ERR("name" + getName() + ", myTile=" + Tile::displayAsString(myTile) + ", dest=" + Tile::displayAsString(tilePosition));
            return false;
        }

        if(result.size() > 2)
            result.resize(2);

        std::vector<Ogre::Vector3> path;
        tileToVector3(result, path, true, 0.0);
        setWalkPath(EntityAnimation::walk_anim, EntityAnimation::idle_anim, true, path);
        pushAction(CreatureActionType::walkToTile, false, true);
        return false;
    }

    // No reachable allied natural enemy. We pop action
    popAction();
    return true;
}

void Creature::engageAlliedNaturalEnemy(Creature* attackerCreature)
{
    Tile* myTile = getPositionTile();
    if(myTile == nullptr)
    {
        OD_LOG_ERR("name=" + getName() + ", pos=" + Helper::toString(getPosition()));
        return;
    }

    // If we are already fighting a natural enemy, do nothing
    if(isActionInList(CreatureActionType::fightNaturalEnemy))
        return;

    clearActionQueue();
    clearDestinations(EntityAnimation::idle_anim, true);
    stopJob();
    stopEating();
    pushAction(CreatureActionType::fightNaturalEnemy, false, true, attackerCreature);
}

double Creature::getMoveSpeed() const
{
    return getMoveSpeed(getPositionTile());
}

double Creature::getMoveSpeed(Tile* tile) const
{
    if(tile == nullptr)
    {
        OD_LOG_ERR("creature=" + getName());
        return 1.0;
    }

    if(getIsOnServerMap())
    {
        // Check if the covering building allows this creature to go through
        if(tile->getCoveringBuilding() != nullptr)
            return tile->getCoveringBuilding()->getCreatureSpeed(this, tile);
        else
            return tile->getCreatureSpeedDefault(this);
    }
    else
    {
        if(tile->getHasBridge())
            return getMoveSpeedGround();
        else
            return tile->getCreatureSpeedDefault(this);
    }
}

double Creature::getPhysicalDefense() const
{
    double defense = mPhysicalDefense;
    if (mWeaponL != nullptr)
        defense += mWeaponL->getPhysicalDefense();
    if (mWeaponR != nullptr)
        defense += mWeaponR->getPhysicalDefense();

    return defense;
}

double Creature::getMagicalDefense() const
{
    double defense = mMagicalDefense;
    if (mWeaponL != nullptr)
        defense += mWeaponL->getMagicalDefense();
    if (mWeaponR != nullptr)
        defense += mWeaponR->getMagicalDefense();

    return defense;
}

double Creature::getElementDefense() const
{
    double defense = mElementDefense;
    if (mWeaponL != nullptr)
        defense += mWeaponL->getElementDefense();
    if (mWeaponR != nullptr)
        defense += mWeaponR->getElementDefense();

    return defense;
}

void Creature::checkLevelUp()
{
    if (getLevel() >= MAX_LEVEL)
        return;

    // Check the returned value.
    double newXP = mDefinition->getXPNeededWhenLevel(getLevel());

    // An error occurred
    if (newXP <= 0.0)
    {
        OD_LOG_ERR("creature=" + getName() + ", newXP=" + Helper::toString(newXP));
        return;
    }

    if (mExp < newXP)
        return;

    setLevel(mLevel + 1);
}

void Creature::exportToPacketForUpdate(ODPacket& os, const Seat* seat) const
{
    int seatId = getSeat()->getId();
    os << mLevel;
    os << seatId;
    os << mOverlayHealthValue;

    // Only allied players should see creature mood (except some states)
    uint32_t moodValue = 0;
    if(seat->isAlliedSeat(getSeat()))
        moodValue = mOverlayMoodValue;
    else if(mSeatPrison != nullptr)
    {
        if(mSeatPrison->isAlliedSeat(seat))
            moodValue = mOverlayMoodValue & CreatureMoodEnum::MoodPrisonFiltersPrisonAllies;
        else
            moodValue = mOverlayMoodValue & CreatureMoodEnum::MoodPrisonFiltersAllPlayers;
    }

    os << moodValue;
    os << mGroundSpeed;
    os << mWaterSpeed;
    os << mLavaSpeed;
    os << mSpeedModifier;

    uint32_t nbCreatureEffect = mEntityParticleEffects.size();
    os << nbCreatureEffect;
    for(EntityParticleEffect* effect : mEntityParticleEffects)
    {
        os << effect->mName;
        os << effect->mScript;
        os << effect->mNbTurnsEffect;
    }

    int seatPrisonId = -1;
    if(mSeatPrison != nullptr)
        seatPrisonId = mSeatPrison->getId();

    os << seatPrisonId;
}

void Creature::updateFromPacket(ODPacket& is)
{
    int seatId;
    OD_ASSERT_TRUE(is >> mLevel);
    OD_ASSERT_TRUE(is >> seatId);
    OD_ASSERT_TRUE(is >> mOverlayHealthValue);
    OD_ASSERT_TRUE(is >> mOverlayMoodValue);
    OD_ASSERT_TRUE(is >> mGroundSpeed);
    OD_ASSERT_TRUE(is >> mWaterSpeed);
    OD_ASSERT_TRUE(is >> mLavaSpeed);
    OD_ASSERT_TRUE(is >> mSpeedModifier);

    // We do not scale the creature if it is picked up (because it is already not at its normal size). It will be
    // resized anyway when dropped
    if(getIsOnMap())
        RenderManager::getSingleton().rrScaleEntity(this);

    if(getSeat()->getId() != seatId)
    {
        Seat* seat = getGameMap()->getSeatById(seatId);
        if(seat == nullptr)
        {
            OD_LOG_ERR("Creature " + getName() + ", wrong seatId=" + Helper::toString(seatId));
        }
        else
        {
            setSeat(seat);
        }
    }

    uint32_t nbEffects;
    OD_ASSERT_TRUE(is >> nbEffects);

    // We copy the list of effects currently on this creature. That will allow to
    // check if the effect is already known and only display the effect if it is not
    std::vector<EntityParticleEffect*> currentEffects = mEntityParticleEffects;
    while(nbEffects > 0)
    {
        --nbEffects;

        std::string effectName;
        std::string effectScript;
        uint32_t nbTurns;
        OD_ASSERT_TRUE(is >> effectName >> effectScript >> nbTurns);
        bool isEffectAlreadyDisplayed = false;
        for(EntityParticleEffect* effect : currentEffects)
        {
            if(effect->mName.compare(effectName) != 0)
                continue;

            isEffectAlreadyDisplayed = true;
            break;
        }

        if(isEffectAlreadyDisplayed)
            continue;

        CreatureParticuleEffectClient* effect = new CreatureParticuleEffectClient(effectName, effectScript, nbTurns);
        effect->mParticleSystem = RenderManager::getSingleton().rrEntityAddParticleEffect(this,
            effect->mName, effect->mScript);
        mEntityParticleEffects.push_back(effect);
    }

    OD_ASSERT_TRUE(is >> seatId);
    if(seatId == -1)
        mSeatPrison = nullptr;
    else
    {
        mSeatPrison = getGameMap()->getSeatById(seatId);
        if(mSeatPrison == nullptr)
        {
            OD_LOG_ERR("Creature " + getName() + ", wrong seatId=" + Helper::toString(seatId));
        }
    }
}

void Creature::updateTilesInSight()
{
    Tile* posTile = getPositionTile();
    if (posTile == nullptr)
        return;

    // The tiles with sight radius without constraints
    mTilesWithinSightRadius = getGameMap()->circularRegion(posTile->getX(), posTile->getY(), mDefinition->getSightRadius());

    // Only the tiles the creature can "see".
    mVisibleTiles = getGameMap()->visibleTiles(posTile->getX(), posTile->getY(), mDefinition->getSightRadius());
}

std::vector<GameEntity*> Creature::getVisibleEnemyObjects()
{
    return getVisibleForce(getSeat(), true);
}

std::vector<GameEntity*> Creature::getReachableAttackableObjects(const std::vector<GameEntity*>& objectsToCheck)
{
    std::vector<GameEntity*> tempVector;
    Tile* myTile = getPositionTile();

    // Loop over the vector of objects we are supposed to check.
    for (unsigned int i = 0; i < objectsToCheck.size(); ++i)
    {
        // Try to find a valid path from the tile this creature is in to the nearest tile where the current target object is.
        GameEntity* entity = objectsToCheck[i];
        // We only consider alive objects
        if(entity->getHP(nullptr) <= 0)
            continue;

        Tile* objectTile = entity->getCoveredTile(0);
        if (getGameMap()->pathExists(this, myTile, objectTile))
            tempVector.push_back(objectsToCheck[i]);
    }

    return tempVector;
}

std::vector<GameEntity*> Creature::getCreaturesFromList(const std::vector<GameEntity*> &objectsToCheck, bool workersOnly)
{
    std::vector<GameEntity*> tempVector;

    // Loop over the vector of objects we are supposed to check.
    for (std::vector<GameEntity*>::const_iterator it = objectsToCheck.begin(); it != objectsToCheck.end(); ++it)
    {
        // Try to find a valid path from the tile this creature is in to the nearest tile where the current target object is.
        GameEntity* entity = *it;
        // We only consider alive objects
        if(entity->getObjectType() != GameEntityType::creature)
            continue;

        if(workersOnly && !static_cast<Creature*>(entity)->getDefinition()->isWorker())
            continue;

        tempVector.push_back(entity);
    }

    return tempVector;
}

std::vector<GameEntity*> Creature::getVisibleAlliedObjects()
{
    return getVisibleForce(getSeat(), false);
}

void Creature::updateVisibleMarkedTiles()
{
    mVisibleMarkedTiles.clear();
    Player *player = getGameMap()->getPlayerBySeat(getSeat());
    if (player == nullptr)
        return;

    // Loop over all the visible tiles.
    for (Tile* tile : mTilesWithinSightRadius)
    {
        // Check to see whether the tile is marked for digging
        if (tile == nullptr || !tile->getMarkedForDigging(player))
            continue;

        // and can be reached by the creature
        for (Tile* neighborTile : tile->getAllNeighbors())
        {
            if (neighborTile == nullptr)
                continue;

            if (getGameMap()->pathExists(this, getPositionTile(), neighborTile))
            {
                mVisibleMarkedTiles.push_back(tile);
                break;
            }
        }
    }
}

std::vector<Tile*> Creature::getVisibleClaimableWallTiles()
{
    std::vector<Tile*> claimableWallTiles;

    // Loop over all the visible tiles.
    for (Tile* tile : mTilesWithinSightRadius)
    {
        // Check to see whether the tile is a claimable wall
        if (tile == nullptr || !tile->isWallClaimable(getSeat()))
            continue;

        // and can be reached by the creature
        for (Tile* neighborTile : tile->getAllNeighbors())
        {
            if (neighborTile == nullptr)
                continue;

            if (getGameMap()->pathExists(this, getPositionTile(), neighborTile))
            {
                claimableWallTiles.push_back(tile);
                break;
            }
        }
    }

    return claimableWallTiles;
}

std::vector<GameEntity*> Creature::getVisibleForce(Seat* seat, bool invert)
{
    return getGameMap()->getVisibleForce(mVisibleTiles, seat, invert);
}

void Creature::computeVisualDebugEntities()
{
    if(!getIsOnServerMap())
        return;

    mHasVisualDebuggingEntities = true;

    updateTilesInSight();

    ServerNotification *serverNotification = new ServerNotification(
        ServerNotificationType::refreshCreatureVisDebug, nullptr);

    const std::string& name = getName();
    serverNotification->mPacket << name;
    serverNotification->mPacket << true;
    if(getIsOnMap())
    {
        uint32_t nbTiles = mVisibleTiles.size();
        serverNotification->mPacket << nbTiles;

        for (Tile* tile : mVisibleTiles)
            getGameMap()->tileToPacket(serverNotification->mPacket, tile);
    }
    else
    {
        uint32_t nbTiles = 0;
        serverNotification->mPacket << nbTiles;
    }

    ODServer::getSingleton().queueServerNotification(serverNotification);
}

void Creature::refreshVisualDebugEntities(const std::vector<Tile*>& tiles)
{
    if(getIsOnServerMap())
        return;

    mHasVisualDebuggingEntities = true;

    for (Tile* tile : tiles)
    {
        // We check if the visual debug is already on this tile
        if(std::find(mVisualDebugEntityTiles.begin(), mVisualDebugEntityTiles.end(), tile) != mVisualDebugEntityTiles.end())
            continue;

        RenderManager::getSingleton().rrCreateCreatureVisualDebug(this, tile);

        mVisualDebugEntityTiles.push_back(tile);
    }

    // now, we check if visual debug should be removed from a tile
    for (std::vector<Tile*>::iterator it = mVisualDebugEntityTiles.begin(); it != mVisualDebugEntityTiles.end();)
    {
        Tile* tile = *it;
        if(std::find(tiles.begin(), tiles.end(), tile) != tiles.end())
        {
            ++it;
            continue;
        }

        it = mVisualDebugEntityTiles.erase(it);

        RenderManager::getSingleton().rrDestroyCreatureVisualDebug(this, tile);
    }
}

void Creature::stopComputeVisualDebugEntities()
{
    if(!getIsOnServerMap())
        return;

    mHasVisualDebuggingEntities = false;

    ServerNotification *serverNotification = new ServerNotification(
        ServerNotificationType::refreshCreatureVisDebug, nullptr);
    const std::string& name = getName();
    serverNotification->mPacket << name;
    serverNotification->mPacket << false;
    ODServer::getSingleton().queueServerNotification(serverNotification);
}

void Creature::destroyVisualDebugEntities()
{
    if(getIsOnServerMap())
        return;

    mHasVisualDebuggingEntities = false;

    for (Tile* tile : mVisualDebugEntityTiles)
    {
        if (tile == nullptr)
            continue;

        RenderManager::getSingleton().rrDestroyCreatureVisualDebug(this, tile);
    }
    mVisualDebugEntityTiles.clear();
}

std::vector<Tile*> Creature::getCoveredTiles()
{
    std::vector<Tile*> tempVector;
    tempVector.push_back(getPositionTile());
    return tempVector;
}

Tile* Creature::getCoveredTile(int index)
{
    if(index > 0)
    {
        OD_LOG_ERR("name=" + getName() + ", index=" + Helper::toString(index));
        return nullptr;
    }

    return getPositionTile();
}

uint32_t Creature::numCoveredTiles()
{
    if(getPositionTile() == nullptr)
        return 0;

    return 1;
}

bool Creature::CloseStatsWindow(const CEGUI::EventArgs& /*e*/)
{
    destroyStatsWindow();
    return true;
}

void Creature::createStatsWindow()
{
    if (mStatsWindow != nullptr)
        return;

    ClientNotification *clientNotification = new ClientNotification(
        ClientNotificationType::askCreatureInfos);
    std::string name = getName();
    clientNotification->mPacket << name << true;
    ODClient::getSingleton().queueClientNotification(clientNotification);

    CEGUI::WindowManager* wmgr = CEGUI::WindowManager::getSingletonPtr();
    CEGUI::Window* rootWindow = CEGUI::System::getSingleton().getDefaultGUIContext().getRootWindow();

    mStatsWindow = wmgr->createWindow("OD/FrameWindow", std::string("CreatureStatsWindows_") + getName());
    mStatsWindow->setPosition(CEGUI::UVector2(CEGUI::UDim(0.3, 0), CEGUI::UDim(0.3, 0)));
    mStatsWindow->setSize(CEGUI::USize(CEGUI::UDim(0, 380), CEGUI::UDim(0, 400)));

    CEGUI::Window* textWindow = wmgr->createWindow("OD/StaticText", "TextDisplay");
    textWindow->setPosition(CEGUI::UVector2(CEGUI::UDim(0.05, 0), CEGUI::UDim(0.15, 0)));
    textWindow->setSize(CEGUI::USize(CEGUI::UDim(0.9, 0), CEGUI::UDim(0.8, 0)));
    textWindow->setProperty("FrameEnabled", "False");
    textWindow->setProperty("BackgroundEnabled", "False");

    // Search for the autoclose button and make it work
    CEGUI::Window* childWindow = mStatsWindow->getChild("__auto_closebutton__");
    childWindow->subscribeEvent(CEGUI::PushButton::EventClicked,
                                        CEGUI::Event::Subscriber(&Creature::CloseStatsWindow, this));

    // Set the window title
    childWindow = mStatsWindow->getChild("__auto_titlebar__");
    childWindow->setText(getName() + " (" + getDefinition()->getClassName() + ")");

    mStatsWindow->addChild(textWindow);
    rootWindow->addChild(mStatsWindow);
    mStatsWindow->show();

    updateStatsWindow("Loading...");
}

void Creature::destroyStatsWindow()
{
    if (mStatsWindow != nullptr)
    {
        ClientNotification *clientNotification = new ClientNotification(
            ClientNotificationType::askCreatureInfos);
        std::string name = getName();
        clientNotification->mPacket << name << false;
        ODClient::getSingleton().queueClientNotification(clientNotification);

        mStatsWindow->destroy();
        mStatsWindow = nullptr;
    }
}

void Creature::updateStatsWindow(const std::string& txt)
{
    if (mStatsWindow == nullptr)
        return;

    CEGUI::Window* textWindow = mStatsWindow->getChild("TextDisplay");
    textWindow->setText(txt);
}

std::string Creature::getStatsText()
{
    // The creatures are not refreshed at each turn so this information is relevant in the server
    // GameMap only
    std::stringstream tempSS;
    tempSS << "Level: " << getLevel() << std::endl;
    tempSS << "Experience: " << mExp << std::endl;
    tempSS << "HP: " << getHP() << " / " << mMaxHP << std::endl;
    if (!getDefinition()->isWorker())
    {
        tempSS << "Awakeness: " << mAwakeness << std::endl;
        tempSS << "Hunger: " << mHunger << std::endl;
    }
    tempSS << "Move speed (Ground/Water/Lava): " << getMoveSpeedGround() << "/"
        << getMoveSpeedWater() << "/" << getMoveSpeedLava() << std::endl;
    tempSS << "Weapons:" << std::endl;
    if(mWeaponL == nullptr)
        tempSS << "Left hand: none" << std::endl;
    else
        tempSS << "Left hand: " << mWeaponL->getName() << "-Atk(Phy/Mag): " << mWeaponL->getPhysicalDamage() << "/" << mWeaponL->getMagicalDamage() << std::endl;
    if(mWeaponR == nullptr)
        tempSS << "Right hand: none" << std::endl;
    else
        tempSS << "Right hand: " << mWeaponR->getName() << "-Atk(Phy/Mag): " << mWeaponR->getPhysicalDamage() << "/" << mWeaponR->getMagicalDamage() << std::endl;
    tempSS << "Total Defense (Phys/Mag): " << getPhysicalDefense() << "/" << getMagicalDefense() << std::endl;
    if (getDefinition()->isWorker())
    {
        tempSS << "Dig Rate: : " << getDigRate() << std::endl;
        tempSS << "Dance Rate: : " << mClaimRate << std::endl;
    }
    tempSS << "Actions:";
    for(const CreatureAction& ca : mActionQueue)
    {
        tempSS << " " << ca.toString();
    }
    tempSS << std::endl;
    tempSS << "Destinations:";
    for(const Ogre::Vector3& dest : mWalkQueue)
    {
        tempSS << Helper::toString(dest) << "/";
    }
    tempSS << std::endl;
    tempSS << "Seat id: " << getSeat()->getId() << std::endl;
    tempSS << "Team id: " << getSeat()->getTeamId() << std::endl;
    tempSS << "Position: " << Helper::toString(getPosition()) << std::endl;
    tempSS << "Mood: " << CreatureMood::toString(mMoodValue) << std::endl;
    tempSS << "MoodPoints: " << Helper::toString(mMoodPoints) << std::endl;
    return tempSS.str();
}

double Creature::takeDamage(GameEntity* attacker, double physicalDamage, double magicalDamage, double elementDamage,
        Tile *tileTakingDamage, bool ignorePhysicalDefense, bool ignoreMagicalDefense, bool ignoreElementDefense)
{
    mNbTurnsWithoutBattle = 0;
    if(!ignorePhysicalDefense)
        physicalDamage = std::max(physicalDamage - getPhysicalDefense(), 0.0);
    if(!ignoreMagicalDefense)
        magicalDamage = std::max(magicalDamage - getMagicalDefense(), 0.0);
    if(!ignoreElementDefense)
        elementDamage = std::max(elementDamage - getElementDefense(), 0.0);
    double damageDone = std::min(mHp, physicalDamage + magicalDamage + elementDamage);
    mHp -= damageDone;
    if(mHp <= 0)
    {
        // If the attacking entity is a creature and its seat is configured to KO creatures
        // instead of killing, we KO
        if((attacker != nullptr) &&
           (attacker->getObjectType() == GameEntityType::creature) &&
           (attacker->getSeat()->getKoCreatures()))
        {
            mHp = 1.0;
            mKoTurnCounter = -ConfigManager::getSingleton().getNbTurnsKoCreatureAttacked();
            OD_LOG_INF("creature=" + getName() + " has been KO by " + attacker->getName());
            dropCarriedEquipment();
        }
    }

    computeCreatureOverlayHealthValue();
    computeCreatureOverlayMoodValue();

    if(!isAlive())
        fireEntityDead();

    if(!getIsOnServerMap())
        return damageDone;

    Player* player = getGameMap()->getPlayerBySeat(getSeat());
    if (player == nullptr)
        return damageDone;

    // Tells the server game map the player is under attack.
    getGameMap()->playerIsFighting(player, tileTakingDamage);

    // If we are a worker attacked by a worker, we fight. Otherwise, we flee (if it is a fighter, a trap,
    // or whatever)
    if(!getDefinition()->isWorker())
        return damageDone;

    bool flee = true;
    if((attacker != nullptr) &&
       (attacker->getObjectType() == GameEntityType::creature))
    {
        Creature* creatureAttacking = static_cast<Creature*>(attacker);
        if(creatureAttacking->getDefinition()->isWorker())
        {
            // We do not flee because of this attack
            flee = false;
        }
    }

    if(flee)
    {
        clearDestinations(EntityAnimation::idle_anim, true);
        clearActionQueue();
        pushAction(CreatureActionType::flee, false, true);
        return damageDone;
    }
    return damageDone;
}

void Creature::receiveExp(double experience)
{
    if (experience < 0)
        return;

    mExp += experience;
}

bool Creature::isActionInList(CreatureActionType action) const
{
    for (const CreatureAction& ca : mActionQueue)
    {
        if (ca.getType() == action)
            return true;
    }
    return false;
}

void Creature::clearActionQueue()
{
    mActionQueue.clear();
    stopJob();
    stopEating();
    if(mCarriedEntity != nullptr)
        releaseCarriedEntity();

    mActionQueue.emplace_front(this, CreatureActionType::idle, nullptr, nullptr, nullptr);
}

bool Creature::pushAction(CreatureActionType actionType, bool popCurrentIfPush, bool forcePush, GameEntity* attackedEntity, Tile* tile, CreatureSkillData* skillData)
{
    if(std::find(mActionTry.begin(), mActionTry.end(), actionType) == mActionTry.end())
    {
        mActionTry.push_back(actionType);
    }
    else
    {
        if(!forcePush)
            return false;
    }

    if(popCurrentIfPush && !mActionQueue.empty())
        mActionQueue.pop_front();

    mActionQueue.emplace_front(this, actionType, attackedEntity, tile, skillData);
    return true;
}

void Creature::popAction()
{
    if(mActionQueue.empty())
    {
        OD_LOG_ERR("Trying to popAction empty queue " + getName());
        return;
    }

    mActionQueue.pop_front();
    if(!mActionQueue.empty())
        mActionQueue.front().clearNbTurnsActive();
}

bool Creature::tryPickup(Seat* seat)
{
    if(!getIsOnMap())
        return false;

    // Cannot pick up dead creatures
    if (!getGameMap()->isInEditorMode() && !isAlive())
        return false;

    if(!getGameMap()->isInEditorMode() && (mSeatPrison == nullptr) && !getSeat()->canOwnedCreatureBePickedUpBy(seat))
        return false;

    if(!getGameMap()->isInEditorMode() && (mSeatPrison != nullptr) && !mSeatPrison->canOwnedCreatureBePickedUpBy(seat))
        return false;

    // KO creatures cannot be picked up
    if(isKo())
        return false;

    return true;
}

void Creature::pickup()
{
    // Stop the creature walking and set it off the map to prevent the AI from running on it.
    removeEntityFromPositionTile();
    clearDestinations(EntityAnimation::idle_anim, true);
    clearActionQueue();

    if(!getIsOnServerMap())
        return;

    if(getHasVisualDebuggingEntities())
        computeVisualDebugEntities();

    fireCreatureSound(CreatureSound::Pickup);
}

bool Creature::canGoThroughTile(Tile* tile) const
{
    if(tile == nullptr)
        return false;

    return getMoveSpeed(tile) > 0.0;
}

bool Creature::tryDrop(Seat* seat, Tile* tile)
{
    // check whether the tile is a ground tile ...
    if(tile->isFullTile())
        return false;

    // In editor mode, we allow creatures to be dropped anywhere they can walk
    if(getGameMap()->isInEditorMode() && canGoThroughTile(tile))
        return true;

    // we cannot drop a creature on a tile we don't see
    if(!seat->hasVisionOnTile(tile))
        return false;

    // If it is a worker, he can be dropped on dirt
    if (getDefinition()->isWorker() && (tile->getTileVisual() == TileVisual::dirtGround || tile->getTileVisual() == TileVisual::goldGround))
        return true;

    // Every creature can be dropped on allied claimed tiles
    if(tile->isClaimedForSeat(seat))
        return true;

    return false;
}

void Creature::drop(const Ogre::Vector3& v)
{
    setPosition(v);
    if(!getIsOnServerMap())
    {
        mDropCooldown = 2;
        return;
    }

    mForceAction = forcedActionSearchAction;
    if(getHasVisualDebuggingEntities())
        computeVisualDebugEntities();

    fireCreatureSound(CreatureSound::Drop);

    // The creature is temporary KO
    mKoTurnCounter = mDefinition->getTurnsStunDropped();
    computeCreatureOverlayMoodValue();
}

bool Creature::setDestination(Tile* tile)
{
    if(tile == nullptr)
        return false;

    Tile *posTile = getPositionTile();
    if(posTile == nullptr)
        return false;

    std::list<Tile*> result = getGameMap()->path(this, tile);

    std::vector<Ogre::Vector3> path;
    tileToVector3(result, path, true, 0.0);
    setWalkPath(EntityAnimation::walk_anim, EntityAnimation::idle_anim, true, path);
    pushAction(CreatureActionType::walkToTile, false, true);
    return true;
}

bool Creature::wanderRandomly(const std::string& animationState)
{
    // We pick randomly a visible tile far away (at the end of visible tiles)
    if(mTilesWithinSightRadius.empty())
        return false;

    // Add reachable tiles only before searching for one of them
    std::vector<Tile*> reachableTiles;
    for (Tile* tile: mTilesWithinSightRadius)
    {
        if (getGameMap()->pathExists(this, getPositionTile(), tile))
            reachableTiles.push_back(tile);
    }

    if (reachableTiles.empty())
        return false;

    Tile* tileDestination = reachableTiles[Random::Uint(0, reachableTiles.size() - 1)];

    std::list<Tile*> result = getGameMap()->path(this, tileDestination);
    std::vector<Ogre::Vector3> path;
    tileToVector3(result, path, true, 0.0);
    setWalkPath(animationState, EntityAnimation::idle_anim, true, path);
    pushAction(CreatureActionType::walkToTile, false, true);
    return false;
}

bool Creature::isAttackable(Tile* tile, Seat* seat) const
{
    if(mHp <= 0.0)
        return false;

    // KO Creature to death creatures are not a threat and cannot be attacked. However, temporary KO can be
    if(mKoTurnCounter < 0)
        return false;

    // Creatures in prison are not a treat and cannot be attacked
    if(mSeatPrison != nullptr)
        return false;

    return true;
}

EntityCarryType Creature::getEntityCarryType(Creature* carrier)
{
    // KO to death entities can be carried
    if(mKoTurnCounter < 0)
        return EntityCarryType::koCreature;

    // Dead creatures are carryable
    if(getHP() <= 0.0)
        return EntityCarryType::corpse;

    return EntityCarryType::notCarryable;
}

void Creature::notifyEntityCarryOn(Creature* carrier)
{
    removeEntityFromPositionTile();
}

void Creature::notifyEntityCarryOff(const Ogre::Vector3& position)
{
    mPosition = position;
    addEntityToPositionTile();
}

bool Creature::canBeCarriedToBuilding(const Building* building) const
{
    // If the creature is dead, it can be carried to any crypt
    if(!isAlive() &&
       (building->getObjectType() == GameEntityType::room) &&
       (static_cast<const Room*>(building)->getType() == RoomType::crypt))
    {
        return true;
    }

    // If the creature is ko to death, it can be carried to an enemy prison
    if((mKoTurnCounter < 0) &&
       (building->getObjectType() == GameEntityType::room) &&
       (!building->getSeat()->isAlliedSeat(getSeat())) &&
       (static_cast<const Room*>(building)->getType() == RoomType::prison))
    {
        return true;
    }

    // If the creature is ko to death, it can be carried to its bed
    if((mKoTurnCounter < 0) &&
       (mHomeTile != nullptr) &&
       (mHomeTile->getCoveringBuilding() == building))
    {
        return true;
    }

    return false;
}

void Creature::carryEntity(GameEntity* carriedEntity)
{
    if(!getIsOnServerMap())
        return;

    OD_ASSERT_TRUE(carriedEntity != nullptr);
    OD_ASSERT_TRUE(mCarriedEntity == nullptr);
    mCarriedEntity = nullptr;
    if(carriedEntity == nullptr)
        return;

    OD_LOG_INF("creature=" + getName() + " is carrying " + carriedEntity->getName());
    carriedEntity->notifyEntityCarryOn(this);

    // We remove the carried entity from the clients gamemaps as well as the carrier
    // and we send the carrier creation message (that will embed the carried)
    carriedEntity->fireRemoveEntityToSeatsWithVision();
    // We only notify seats that already had vision. We copy the seats with vision list
    // because fireRemoveEntityToSeatsWithVision will empty it.
    std::vector<Seat*> seatsWithVision = mSeatsWithVisionNotified;
    // We remove ourself and send the creation
    fireRemoveEntityToSeatsWithVision();
    mCarriedEntity = carriedEntity;
    notifySeatsWithVision(seatsWithVision);
}

void Creature::releaseCarriedEntity()
{
    if(!getIsOnServerMap())
        return;

    GameEntity* carriedEntity = mCarriedEntity;
    GameEntityType carriedEntityDestType = mCarriedEntityDestType;
    std::string carriedEntityDestName = mCarriedEntityDestName;

    mCarriedEntity = nullptr;
    mCarriedEntityDestType = GameEntityType::unknown;
    mCarriedEntityDestName.clear();

    if(carriedEntity == nullptr)
    {
        OD_LOG_ERR("name=" + getName());
        return;
    }

    const Ogre::Vector3& pos = getPosition();
    OD_LOG_INF("creature=" + getName() + " is releasing carried " + carriedEntity->getName() + ", pos=" + Helper::toString(pos));

    carriedEntity->notifyEntityCarryOff(pos);

    for(Seat* seat : mSeatsWithVisionNotified)
    {
        if(seat->getPlayer() == nullptr)
            continue;
        if(!seat->getPlayer()->getIsHuman())
            continue;

        ServerNotification* serverNotification = new ServerNotification(
            ServerNotificationType::releaseCarriedEntity, seat->getPlayer());
        serverNotification->mPacket << getName() << carriedEntity->getObjectType();
        serverNotification->mPacket << carriedEntity->getName();
        serverNotification->mPacket << mPosition;
        ODServer::getSingleton().queueServerNotification(serverNotification);
    }

    Building* dest = nullptr;
    switch(carriedEntityDestType)
    {
        case GameEntityType::room:
        {
            dest = getGameMap()->getRoomByName(carriedEntityDestName);
            break;
        }
        case GameEntityType::trap:
        {
            dest = getGameMap()->getTrapByName(carriedEntityDestName);
            break;
        }
        default:
            break;
    }
    if(dest == nullptr)
    {
        // We couldn't find the destination. This can happen if it has been destroyed while
        // we were carrying the entity
        OD_LOG_INF("Couldn't carry entity=" + carriedEntity->getName()
            + " to entity name=" + carriedEntityDestName);
        return;
    }

    dest->notifyCarryingStateChanged(this, carriedEntity);
}

bool Creature::canSlap(Seat* seat)
{
    Tile* tile = getPositionTile();
    if(tile == nullptr)
    {
        OD_LOG_ERR("entityName=" + getName());
        return false;
    }

    if(mDropCooldown > 0)
        return false;

    if(getGameMap()->isInEditorMode())
        return true;

    if(getHP() <= 0.0)
        return false;

    // If the creature is in prison, it can be slapped by the jail owner only
    if(mSeatPrison != nullptr)
        return (mSeatPrison == seat);

    // Only the owning player can slap a creature
    if(getSeat() != seat)
        return false;

    return true;
}

void Creature::slap()
{
    if(!getIsOnServerMap())
        return;

    fireCreatureSound(CreatureSound::Slap);

    // In editor mode, we remove the creature
    if(getGameMap()->isInEditorMode())
    {
        removeFromGameMap();
        deleteYourself();
        return;
    }

    CreatureEffectSlap* effect = new CreatureEffectSlap;
    addCreatureEffect(effect);
    mHp -= mMaxHP * ConfigManager::getSingleton().getSlapDamagePercent() / 100.0;
    computeCreatureOverlayHealthValue();
}

void Creature::fireAddEntity(Seat* seat, bool async)
{
    if(async)
    {
        ServerNotification serverNotification(
            ServerNotificationType::addEntity, seat->getPlayer());
        exportHeadersToPacket(serverNotification.mPacket);
        exportToPacket(serverNotification.mPacket, seat);
        ODServer::getSingleton().sendAsyncMsg(serverNotification);

        if(mCarriedEntity != nullptr)
        {
            OD_LOG_ERR("Trying to fire add creature in async mode name=" + getName() + " while carrying " + mCarriedEntity->getName());
        }
        return;
    }

    ServerNotification* serverNotification = new ServerNotification(
        ServerNotificationType::addEntity, seat->getPlayer());
    exportHeadersToPacket(serverNotification->mPacket);
    exportToPacket(serverNotification->mPacket, seat);
    ODServer::getSingleton().queueServerNotification(serverNotification);

    if(mCarriedEntity != nullptr)
    {
        mCarriedEntity->addSeatWithVision(seat, false);

        serverNotification = new ServerNotification(
            ServerNotificationType::carryEntity, seat->getPlayer());
        serverNotification->mPacket << getName() << mCarriedEntity->getObjectType();
        serverNotification->mPacket << mCarriedEntity->getName();
        ODServer::getSingleton().queueServerNotification(serverNotification);
    }
}

void Creature::fireRemoveEntity(Seat* seat)
{
    // If we are carrying an entity, we release it first, then we can remove it and us
    if(mCarriedEntity != nullptr)
    {
        ServerNotification* serverNotification = new ServerNotification(
            ServerNotificationType::releaseCarriedEntity, seat->getPlayer());
        serverNotification->mPacket << getName() << mCarriedEntity->getObjectType();
        serverNotification->mPacket << mCarriedEntity->getName();
        serverNotification->mPacket << mPosition;
        ODServer::getSingleton().queueServerNotification(serverNotification);

        mCarriedEntity->removeSeatWithVision(seat);
    }

    const std::string& name = getName();
    ServerNotification *serverNotification = new ServerNotification(
        ServerNotificationType::removeEntity, seat->getPlayer());
    GameEntityType type = getObjectType();
    serverNotification->mPacket << type;
    serverNotification->mPacket << name;
    ODServer::getSingleton().queueServerNotification(serverNotification);
}

void Creature::fireCreatureRefreshIfNeeded()
{
    if(!mNeedFireRefresh)
        return;

    mNeedFireRefresh = false;
    for(Seat* seat : mSeatsWithVisionNotified)
    {
        if(seat->getPlayer() == nullptr)
            continue;
        if(!seat->getPlayer()->getIsHuman())
            continue;

        const std::string& name = getName();
        ServerNotification *serverNotification = new ServerNotification(
            ServerNotificationType::entitiesRefresh, seat->getPlayer());
        uint32_t nbCreature = 1;
        serverNotification->mPacket << nbCreature;
        serverNotification->mPacket << GameEntityType::creature;
        serverNotification->mPacket << name;
        exportToPacketForUpdate(serverNotification->mPacket, seat);
        ODServer::getSingleton().queueServerNotification(serverNotification);
    }
}

void Creature::setupDefinition(GameMap& gameMap, const CreatureDefinition& defaultWorkerCreatureDefinition)
{
    bool setHpToStrHp = false;
    if(mDefinition == nullptr)
    {
        // If the classname corresponds to the default worker CreatureDefinition, we use
        // the dedicated class. The correct one will be set after the seat is initialized
        if(!mDefinitionString.empty() &&  mDefinitionString.compare(ConfigManager::DefaultWorkerCreatureDefinition) != 0)
        {
            mDefinition = gameMap.getClassDescription(mDefinitionString);
        }
        else
        {
            // If we are in editor mode, we take the default worker class. Otherwise, we take
            // the default worker from the seat faction
            if(gameMap.isInEditorMode() || !mSeat)
                mDefinition = &defaultWorkerCreatureDefinition;
            else
                mDefinition = getSeat()->getWorkerClassToSpawn();
        }

        if(mDefinition == nullptr)
        {
            OD_LOG_ERR("Definition=" + mDefinitionString);
            return;
        }

        if(getIsOnServerMap())
        {
            setHpToStrHp = true;

            // name
            if (getName().compare("autoname") == 0)
            {
                std::string name = getGameMap()->nextUniqueNameCreature(mDefinition->getClassName());
                setName(name);
            }
        }
    }

    if(getIsOnServerMap())
    {
        for(const CreatureSkill* skill : mDefinition->getCreatureSkills())
        {
            CreatureSkillData skillData(skill, skill->getCooldownNbTurns(), 0);
            mSkillData.push_back(skillData);
        }
    }

    buildStats();

    // Now, the max hp is known. If needed, we set it
    if(setHpToStrHp)
    {
        if(mHpString.compare("max") == 0)
            mHp = mMaxHP;
        else
            mHp = Helper::toDouble(mHpString);

        computeCreatureOverlayHealthValue();
    }
}

void Creature::fireCreatureSound(CreatureSound sound)
{
    Tile* posTile = getPositionTile();
    if(posTile == nullptr)
        return;

    std::string soundFamily;
    switch(sound)
    {
        case CreatureSound::Pickup:
            soundFamily = getDefinition()->getSoundFamilyPickup();
            break;
        case CreatureSound::Drop:
            soundFamily = getDefinition()->getSoundFamilyDrop();
            break;
        case CreatureSound::Attack:
            soundFamily = getDefinition()->getSoundFamilyAttack();
            break;
        case CreatureSound::Die:
            soundFamily = getDefinition()->getSoundFamilyDie();
            break;
        case CreatureSound::Slap:
            soundFamily = getDefinition()->getSoundFamilySlap();
            break;
        case CreatureSound::Dig:
            soundFamily = "Default/Dig";
            break;
        default:
            OD_LOG_ERR("Wrong CreatureSound value=" + Helper::toString(static_cast<uint32_t>(sound)));
            return;
    }

    getGameMap()->fireSpatialSound(mSeatsWithVisionNotified, SpatialSoundType::Creatures,
        soundFamily, posTile);
}

void Creature::itsPayDay()
{
    // Rogue creatures do not have to be paid
    if(getSeat()->isRogueSeat())
        return;

    mGoldFee += mDefinition->getFee(getLevel());
}

void Creature::increaseHunger(double value)
{
    if(getSeat()->isRogueSeat())
        return;

    mHunger = std::min(100.0, mHunger + value);
}

void Creature::decreaseAwakeness(double value)
{
    if(getSeat()->isRogueSeat())
        return;

    mAwakeness = std::max(0.0, mAwakeness - value);
}

void Creature::computeMood()
{
    mMoodPoints = getGameMap()->computeCreatureMoodModifiers(this);

    CreatureMoodLevel newMoodValue = ConfigManager::getSingleton().getCreatureMoodLevel(mMoodPoints);
    if((newMoodValue > CreatureMoodLevel::Neutral) &&
       (mMoodValue <= CreatureMoodLevel::Neutral))
    {
        // We became unhappy
        if((getSeat()->getPlayer() != nullptr) &&
           (getSeat()->getPlayer()->getIsHuman()))
        {
            ServerNotification *serverNotification = new ServerNotification(
                ServerNotificationType::chatServer, getSeat()->getPlayer());
            std::string msg = getName() + " is unhappy";
            serverNotification->mPacket << msg << EventShortNoticeType::aboutCreatures;
            ODServer::getSingleton().queueServerNotification(serverNotification);
        }
    }
    if(newMoodValue != CreatureMoodLevel::Furious)
    {
        mMoodValue = newMoodValue;
        mFirstTurnFurious = -1;
        return;
    }

    if(mMoodValue != CreatureMoodLevel::Furious)
    {
        mMoodValue = newMoodValue;
        mFirstTurnFurious = getGameMap()->getTurnNumber();

        if((getSeat()->getPlayer() != nullptr) &&
           (getSeat()->getPlayer()->getIsHuman()))
        {
            ServerNotification *serverNotification = new ServerNotification(
                ServerNotificationType::chatServer, getSeat()->getPlayer());
            std::string msg = getName() + " wants to leave your dungeon";
            serverNotification->mPacket << msg << EventShortNoticeType::aboutCreatures;
            ODServer::getSingleton().queueServerNotification(serverNotification);
        }
    }
    else if(getGameMap()->getTurnNumber() > (mFirstTurnFurious + ConfigManager::getSingleton().getNbTurnsFuriousMax()))
    {
        // We couldn't leave the dungeon in time, we become rogue
        if((getSeat()->getPlayer() != nullptr) &&
           (getSeat()->getPlayer()->getIsHuman()))
        {
            ServerNotification *serverNotification = new ServerNotification(
                ServerNotificationType::chatServer, getSeat()->getPlayer());
            std::string msg = getName() + " is not under your control anymore !";
            serverNotification->mPacket << msg << EventShortNoticeType::aboutCreatures;
            ODServer::getSingleton().queueServerNotification(serverNotification);
        }

        Seat* rogueSeat = getGameMap()->getSeatRogue();
        setSeat(rogueSeat);
        mMoodValue = CreatureMoodLevel::Neutral;
        mMoodPoints = 0;
        mAwakeness = 100;
        mHunger = 0;
        clearDestinations(EntityAnimation::idle_anim, true);
        clearActionQueue();
        stopJob();
        stopEating();
        mNeedFireRefresh = true;
        if (getHomeTile() != nullptr)
        {
            RoomDormitory* home = static_cast<RoomDormitory*>(getHomeTile()->getCoveringBuilding());
            home->releaseTileForSleeping(getHomeTile(), this);
        }
    }
}

void Creature::computeCreatureOverlayHealthValue()
{
    if(!getIsOnServerMap())
        return;

    uint32_t value = 0;
    double hp = getHP();
    // Note that we make a special case for hp = 0 to avoid errors due to roundness
    if(hp <= 0)
    {
        value = NB_OVERLAY_HEALTH_VALUES - 1;
    }
    else
    {
        uint32_t nbSteps = NB_OVERLAY_HEALTH_VALUES - 2;
        double healthStep = getMaxHp() / static_cast<double>(nbSteps);
        double tmpHealth = getMaxHp();
        for(value = 0; value < nbSteps; ++value)
        {
            if(hp >= tmpHealth)
                break;

            tmpHealth -= healthStep;
        }
    }

    if(mOverlayHealthValue != value)
    {
        mOverlayHealthValue = value;
        mNeedFireRefresh = true;
    }
}

void Creature::computeCreatureOverlayMoodValue()
{
    if(!getIsOnServerMap())
        return;

    uint32_t value = 0;
    // The creature mood applies only if the creature is alive
    if(isAlive())
    {
        if(mMoodValue == CreatureMoodLevel::Angry)
            value |= CreatureMoodEnum::Angry;
        else if(mMoodValue == CreatureMoodLevel::Furious)
            value |= CreatureMoodEnum::Furious;

        if(isActionInList(CreatureActionType::getFee))
            value |= CreatureMoodEnum::GetFee;

        if(isActionInList(CreatureActionType::leaveDungeon))
            value |= CreatureMoodEnum::LeaveDungeon;

        if(mKoTurnCounter < 0)
            value |= CreatureMoodEnum::KoDeath;
        else if(mKoTurnCounter > 0)
            value |= CreatureMoodEnum::KoTemp;

        if(isHungry())
            value |= CreatureMoodEnum::Hungry;

        if(isTired())
            value |= CreatureMoodEnum::Tired;

        if(mSeatPrison != nullptr)
            value |= CreatureMoodEnum::InJail;
    }

    if(mOverlayMoodValue != value)
    {
        mOverlayMoodValue = value;
        mNeedFireRefresh = true;
    }
}

void Creature::addCreatureEffect(CreatureEffect* effect)
{
    std::string effectName = nextParticleSystemsName();

    OD_LOG_INF("Added CreatureEffect name=" + effectName + " on creature=" + getName());

    CreatureParticuleEffect* particleEffect = new CreatureParticuleEffect(effectName, effect->getParticleEffectScript(),
        effect->getNbTurnsEffect(), effect);
    mEntityParticleEffects.push_back(particleEffect);

    mNeedFireRefresh = true;
}

bool Creature::hasCreatureEffect(CreatureEffectType type) const
{
    for(EntityParticleEffect* effect : mEntityParticleEffects)
    {
        if(effect->getEntityParticleEffectType() != EntityParticleEffectType::creature)
        {
            static bool logMsg = false;
            if(!logMsg)
            {
                logMsg = true;
                OD_LOG_ERR("Wrong effect on creature name=" + getName() + ", effectName=" + effect->mName + ", effectScript=" + effect->mScript);
            }
            continue;
        }

        CreatureParticuleEffect* creatureEffect = static_cast<CreatureParticuleEffect*>(effect);
        if(creatureEffect->mEffect->getCreatureEffectType() == type)
            return true;
    }
    return false;
}

bool Creature::isHurt() const
{
    //On server side, we test HP
    if(getIsOnServerMap())
        return getHP() < getMaxHp();

    // On client side, we test overlay value. 0 represents full health
    return mOverlayHealthValue > 0;
}

bool Creature::isKo() const
{
    if(getIsOnServerMap())
        return mKoTurnCounter != 0;

    // On client side, we test mood overlay value
    return (mOverlayMoodValue & KoDeathOrTemp) != 0;
}

bool Creature::isKoDeath() const
{
    if(getIsOnServerMap())
        return mKoTurnCounter < 0;

    // On client side, we test mood overlay value
    return (mOverlayMoodValue & CreatureMoodEnum::KoDeath) != 0;
}

bool Creature::isKoTemp() const
{
    if(getIsOnServerMap())
        return mKoTurnCounter > 0;

    // On client side, we test mood overlay value
    return (mOverlayMoodValue & CreatureMoodEnum::KoTemp) != 0;
}

bool Creature::isInPrison() const
{
    return mSeatPrison != nullptr;
}

void Creature::correctEntityMovePosition(Ogre::Vector3& position)
{
    static const double offset = 0.3;
    if(position.x > 0)
        position.x += Random::Double(-offset, offset);

    if(position.y > 0)
        position.y += Random::Double(-offset, offset);

    if(position.z > 0)
        position.z += Random::Double(-offset, offset);
}

void Creature::checkWalkPathValid()
{
    bool stop = false;
    for(const Ogre::Vector3& dest : mWalkQueue)
    {
        Tile* tile = getGameMap()->getTile(Helper::round(dest.x), Helper::round(dest.y));
        if(tile == nullptr)
        {
            stop = true;
            break;
        }

        if(!canGoThroughTile(tile))
        {
            stop = true;
            break;
        }
    }

    if(!stop)
        return;

    // There is an unpassable tile in our way. We stop what we are doing
    clearDestinations(EntityAnimation::idle_anim, true);
}

void Creature::setJobCooldown(int val)
{
    // If the creature has been slapped, its cooldown is decreased
    if(hasCreatureEffect(CreatureEffectType::slap))
        val = Helper::round(static_cast<float>(val) * 0.8f);

    mJobCooldown = val;
}

bool Creature::isTired() const
{
    if(getIsOnServerMap())
        return mAwakeness <= 20.0;

    return (mOverlayMoodValue & CreatureMoodEnum::Tired) != 0;
}

bool Creature::isHungry() const
{
    if(getIsOnServerMap())
        return mHunger >= 80.0;

    return (mOverlayMoodValue & CreatureMoodEnum::Hungry) != 0;
}

void Creature::releasedInBed()
{
    // The creature was released in its bed. Let's set its KO state to 0
    mKoTurnCounter = 0;
    computeCreatureOverlayMoodValue();
}

void Creature::setSeatPrison(Seat* seat)
{
    if(mSeatPrison == seat)
        return;

    // We reset KO to death counter
    if(seat != nullptr)
        mKoTurnCounter = 0;

    mSeatPrison = seat;
    mNeedFireRefresh = true;
}

bool Creature::isDangerous(const Creature* creature, int distance) const
{
    if(getDefinition()->isWorker())
        return false;

    return true;
}

void Creature::clientUpkeep()
{
    MovableGameEntity::clientUpkeep();
    if(mDropCooldown > 0)
        --mDropCooldown;
}

void Creature::setMoveSpeedModifier(double modifier)
{
    mSpeedModifier = modifier;

    mGroundSpeed = mDefinition->getMoveSpeedGround();
    mWaterSpeed = mDefinition->getMoveSpeedWater();
    mLavaSpeed  = mDefinition->getMoveSpeedLava();

    double multiplier = mLevel - 1;
    if (multiplier > 0.0)
    {
        mGroundSpeed += mDefinition->getGroundSpeedPerLevel() * multiplier;
        mWaterSpeed += mDefinition->getWaterSpeedPerLevel() * multiplier;
        mLavaSpeed += mDefinition->getLavaSpeedPerLevel() * multiplier;
    }

    mGroundSpeed *= mSpeedModifier;
    mWaterSpeed *= mSpeedModifier;
    mLavaSpeed *= mSpeedModifier;
    mNeedFireRefresh = true;
}
