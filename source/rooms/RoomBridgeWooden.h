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

#ifndef ROOMBRIDGEWOODEN_H
#define ROOMBRIDGEWOODEN_H

#include "rooms/RoomBridge.h"
#include "rooms/RoomType.h"

class Tile;

class RoomBridgeWooden: public RoomBridge
{
public:
    RoomBridgeWooden(GameMap* gameMap);

    virtual RoomType getType() const override
    { return RoomType::bridgeWooden; }

    static bool buildRoom(GameMap* gameMap, Player* player, ODPacket& packet);
    static bool buildRoomOnTiles(GameMap* gameMap, Player* player, const std::vector<Tile*>& tiles);
    static bool buildRoomEditor(GameMap* gameMap, ODPacket& packet);
    static Room* getRoomFromStream(GameMap* gameMap, std::istream& is);

    static void checkBuildRoom(GameMap* gameMap, const InputManager& inputManager, InputCommand& inputCommand);
    static void checkBuildRoomEditor(GameMap* gameMap, const InputManager& inputManager, InputCommand& inputCommand);

protected:
    void updateFloodFillPathCreated(Seat* seat, const std::vector<Tile*>& tiles) override;
    void updateFloodFillTileRemoved(Seat* seat, Tile* tile) override;
};

#endif // ROOMBRIDGEWOODEN_H
