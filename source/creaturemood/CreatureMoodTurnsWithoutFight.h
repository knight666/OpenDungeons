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

#ifndef CREATUREMOODTURNSWITHOUTFIGHT_H
#define CREATUREMOODTURNSWITHOUTFIGHT_H

#include "creaturemood/CreatureMood.h"

class CreatureMoodTurnsWithoutFight : public CreatureMood
{
public:
    CreatureMoodTurnsWithoutFight() :
        mTurnsWithoutFightMin(0),
        mTurnsWithoutFightMax(0),
        mMoodModifier(0)
    {}

    virtual ~CreatureMoodTurnsWithoutFight() {}

    virtual int32_t computeMood(const Creature* creature) const override;

    inline CreatureMood* clone() const override
    {  return new CreatureMoodTurnsWithoutFight(mTurnsWithoutFightMin, mTurnsWithoutFightMax, mMoodModifier); }

    virtual bool importFromStream(std::istream& is) override;

private:
    CreatureMoodTurnsWithoutFight(int32_t turnsWithoutFightMin, int32_t turnsWithoutFightMax,
            int32_t moodModifier) :
        mTurnsWithoutFightMin(turnsWithoutFightMin),
        mTurnsWithoutFightMax(turnsWithoutFightMax),
        mMoodModifier(moodModifier)
    {}

    int32_t mTurnsWithoutFightMin;
    int32_t mTurnsWithoutFightMax;
    int32_t mMoodModifier;
};

#endif // CREATUREMOODTURNSWITHOUTFIGHT_H
