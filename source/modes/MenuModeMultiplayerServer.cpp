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

#include "modes/MenuModeMultiplayerServer.h"

#include "utils/Helper.h"
#include "render/Gui.h"
#include "modes/ModeManager.h"
#include "sound/MusicPlayer.h"
#include "gamemap/GameMap.h"
#include "render/ODFrameListener.h"
#include "network/ODServer.h"
#include "network/ODClient.h"
#include "utils/LogManager.h"
#include "gamemap/MapLoader.h"
#include "utils/ConfigManager.h"
#include "utils/ResourceManager.h"

#include <CEGUI/CEGUI.h>
#include <boost/filesystem.hpp>
#include <boost/locale.hpp>

const std::string LEVEL_EXTENSION = ".level";

MenuModeMultiplayerServer::MenuModeMultiplayerServer(ModeManager *modeManager):
    AbstractApplicationMode(modeManager, ModeManager::MENU_MULTIPLAYER_SERVER)
{
    CEGUI::Window* window = getModeManager().getGui().getGuiSheet(Gui::guiSheet::multiplayerServerMenu);

    addEventConnection(
        window->getChild(Gui::MPM_BUTTON_SERVER)->subscribeEvent(
            CEGUI::PushButton::EventClicked,
            CEGUI::Event::Subscriber(&MenuModeMultiplayerServer::serverButtonPressed,
                                     this)
        )
    );
    addEventConnection(
        window->getChild(Gui::MPM_LIST_LEVELS)->subscribeEvent(
            CEGUI::Listbox::EventMouseClick,
            CEGUI::Event::Subscriber(&MenuModeMultiplayerServer::updateDescription,
                                     this)
        )
    );
    addEventConnection(
        window->getChild(Gui::MPM_LIST_LEVELS)->subscribeEvent(
            CEGUI::Listbox::EventMouseDoubleClick,
            CEGUI::Event::Subscriber(&MenuModeMultiplayerServer::serverButtonPressed,
                                     this)
        )
    );

    addEventConnection(
        window->getChild(Gui::MPM_BUTTON_BACK)->subscribeEvent(
            CEGUI::PushButton::EventClicked,
            CEGUI::Event::Subscriber(&AbstractApplicationMode::goBack,
                                     static_cast<AbstractApplicationMode*>(this))
        )
    );
    addEventConnection(
        window->getChild("LevelWindowFrame/__auto_closebutton__")->subscribeEvent(
            CEGUI::PushButton::EventClicked,
            CEGUI::Event::Subscriber(&AbstractApplicationMode::goBack,
                                     static_cast<AbstractApplicationMode*>(this))
        )
    );
}

void MenuModeMultiplayerServer::activate()
{
    // Loads the corresponding Gui sheet.
    Gui& gui = getModeManager().getGui();
    gui.loadGuiSheet(Gui::multiplayerServerMenu);

    giveFocus();

    // Play the main menu music
    MusicPlayer::getSingleton().play(ConfigManager::getSingleton().getMainMenuMusic());

    GameMap* gameMap = ODFrameListener::getSingleton().getClientGameMap();
    gameMap->clearAll();
    gameMap->setGamePaused(true);

    ConfigManager& config = ConfigManager::getSingleton();

    CEGUI::Window* mainWin = gui.getGuiSheet(Gui::guiSheet::multiplayerServerMenu);

    CEGUI::Editbox* editNick = static_cast<CEGUI::Editbox*>(mainWin->getChild(Gui::MPM_EDIT_NICK));
    std::string nickname = config.getGameValue(Config::NICKNAME, std::string(), false);
    if (!nickname.empty())
        editNick->setText(reinterpret_cast<const CEGUI::utf8*>(nickname.c_str()));

    CEGUI::Listbox* levelSelectList = static_cast<CEGUI::Listbox*>(mainWin->getChild(Gui::MPM_LIST_LEVELS));

    mainWin->getChild(Gui::MPM_TEXT_LOADING)->setText("");
    mFilesList.clear();
    mDescriptionList.clear();
    levelSelectList->resetList();

    std::string levelPath = ResourceManager::getSingleton().getLevelPathMultiplayer();
    if(Helper::fillFilesList(levelPath, mFilesList, LEVEL_EXTENSION))
    {
        for (uint32_t n = 0; n < mFilesList.size(); ++n)
        {
            std::string filename = mFilesList[n];

            LevelInfo levelInfo;
            std::string mapName;
            std::string mapDescription;
            if(MapLoader::getMapInfo(filename, levelInfo))
            {
                mapName = levelInfo.mLevelName;
                mapDescription = levelInfo.mLevelDescription;
            }
            else
            {
                mapName = "invalid map";
                mapDescription = "invalid map";
            }

            mDescriptionList.push_back(mapDescription);
            CEGUI::ListboxTextItem* item = new CEGUI::ListboxTextItem(mapName);
            item->setID(n);
            item->setSelectionBrushImage("OpenDungeonsSkin/SelectionBrush");
            levelSelectList->addItem(item);
        }
    }
}

bool MenuModeMultiplayerServer::serverButtonPressed(const CEGUI::EventArgs&)
{
    CEGUI::Window* mainWin = getModeManager().getGui().getGuiSheet(Gui::guiSheet::multiplayerServerMenu);
    CEGUI::Listbox* levelSelectList = static_cast<CEGUI::Listbox*>(mainWin->getChild(Gui::MPM_LIST_LEVELS));

    if(levelSelectList->getSelectedCount() == 0)
    {
        mainWin->getChild(Gui::MPM_TEXT_LOADING)->setText("Please select a level first.");
        return true;
    }

    CEGUI::Editbox* editNick = static_cast<CEGUI::Editbox*>(mainWin->getChild(Gui::MPM_EDIT_NICK));
    std::string nick = editNick->getText().c_str();
    CEGUI::String nickCeguiStr = reinterpret_cast<const CEGUI::utf8*>(nick.c_str());
    if (nickCeguiStr.empty())
    {
        mainWin->getChild(Gui::MPM_TEXT_LOADING)->setText("Please enter a nickname.");
        return true;
    }
    else if (nickCeguiStr.length() > 20)
    {
        mainWin->getChild(Gui::MPM_TEXT_LOADING)->setText("Please enter a shorter nickname. (20 letters max.)");
        return true;
    }

    ODFrameListener::getSingleton().getClientGameMap()->setLocalPlayerNick(nick);

    mainWin->getChild(Gui::MPM_TEXT_LOADING)->setText("Loading...");

    CEGUI::ListboxItem* selItem = levelSelectList->getFirstSelectedItem();
    int id = selItem->getID();

    const std::string& level = mFilesList[id];

    // We are a server
    if(!ODServer::getSingleton().startServer(level, ServerMode::ModeGameMultiPlayer))
    {
        OD_LOG_ERR("Could not start server for multi player game !!!");
        mainWin->getChild(Gui::MPM_TEXT_LOADING)->setText("ERROR: Could not start server for multi player game !!!");
        return true;
    }

    // We connect ourself
    if(!ODClient::getSingleton().connect("localhost", ConfigManager::getSingleton().getNetworkPort()))
    {
        OD_LOG_ERR("Could not connect to server for multi player game !!!");
        mainWin->getChild(Gui::MPM_TEXT_LOADING)->setText("Error: Couldn't connect to local server!");
        return true;
    }
    return true;
}

bool MenuModeMultiplayerServer::updateDescription(const CEGUI::EventArgs&)
{
    // Get the level corresponding id
    CEGUI::Window* mainWin = getModeManager().getGui().getGuiSheet(Gui::multiplayerServerMenu);
    CEGUI::Listbox* levelSelectList = static_cast<CEGUI::Listbox*>(mainWin->getChild(Gui::SKM_LIST_LEVELS));

    CEGUI::Window* descTxt = mainWin->getChild("LevelWindowFrame/MapDescriptionText");

    if(levelSelectList->getSelectedCount() == 0)
    {
        descTxt->setText("");
        return true;
    }

    getModeManager().getGui().playButtonClickSound();

    CEGUI::ListboxItem* selItem = levelSelectList->getFirstSelectedItem();
    int id = selItem->getID();

    std::string description = mDescriptionList[id];
    descTxt->setText(reinterpret_cast<const CEGUI::utf8*>(description.c_str()));
    return true;
}
