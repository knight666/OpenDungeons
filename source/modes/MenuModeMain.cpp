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

#include "MenuModeMain.h"

#include "modes/ModeManager.h"

#include "ODApplication.h"
#include "gamemap/GameMap.h"
#include "render/Gui.h"
#include "render/ODFrameListener.h"
#include "render/TextRenderer.h"
#include "sound/MusicPlayer.h"
#include "utils/ConfigManager.h"
#include "utils/LogManager.h"

#include <CEGUI/widgets/PushButton.h>

MenuModeMain::MenuModeMain(ModeManager *modeManager):
    AbstractApplicationMode(modeManager, ModeManager::MENU_MAIN),
    mRoot(getModeManager().getGui().getGuiSheet(Gui::mainMenu)),
    mSettings(SettingsWindow(getModeManager().getGui().getGuiSheet(Gui::mainMenu)))
{
    CEGUI::Window* pMenuItems = mRoot->getChild("MenuItems");
    OD_ASSERT_TRUE(pMenuItems != nullptr);

    connectModeChangeEvent(pMenuItems, "MapEditorButton", AbstractModeManager::ModeType::MENU_EDITOR);
    connectModeChangeEvent(pMenuItems, "StartReplayButton", AbstractModeManager::ModeType::MENU_REPLAY);
    connectModeChangeEvent(pMenuItems, "LoadGameButton", AbstractModeManager::ModeType::MENU_LOAD_SAVEDGAME);

    mMultiplayerItems = pMenuItems->getChild("MultiplayerItems");
    OD_ASSERT_TRUE(mMultiplayerItems != nullptr);
    connectModeChangeEvent(mMultiplayerItems, "StartSkirmishButton", AbstractModeManager::ModeType::MENU_SKIRMISH);
    connectModeChangeEvent(mMultiplayerItems, "StartMultiplayerClientButton", AbstractModeManager::ModeType::MENU_MULTIPLAYER_CLIENT);
    connectModeChangeEvent(mMultiplayerItems, "StartMultiplayerServerButton", AbstractModeManager::ModeType::MENU_MULTIPLAYER_SERVER);

    CEGUI::Window* pSettingsButton = pMenuItems->getChild("SettingsButton");
    OD_ASSERT_TRUE(pSettingsButton != nullptr);
    addEventConnection(
        pSettingsButton->subscribeEvent(
            CEGUI::PushButton::EventClicked,
            CEGUI::Event::Subscriber(&MenuModeMain::toggleSettings, this)
        )
    );

    CEGUI::Window* pQuitButton = pMenuItems->getChild("QuitButton");
    OD_ASSERT_TRUE(pQuitButton != nullptr);
    addEventConnection(
        pQuitButton->subscribeEvent(
            CEGUI::PushButton::EventClicked,
            CEGUI::Event::Subscriber([] (const CEGUI::EventArgs& e) {
                ODFrameListener::getSingletonPtr()->requestExit();
                return true;
            })
        )
    );
}

void MenuModeMain::activate()
{
    // Loads the corresponding Gui sheet.
    getModeManager().getGui().loadGuiSheet(Gui::mainMenu);

    giveFocus();

    TextRenderer::getSingleton().setText(ODApplication::POINTER_INFO_STRING, "");

    // Play the main menu music
    MusicPlayer::getSingleton().play(ConfigManager::getSingleton().getMainMenuMusic());

    GameMap* gameMap = ODFrameListener::getSingletonPtr()->getClientGameMap();
    gameMap->clearAll();
    gameMap->setGamePaused(true);
}

void MenuModeMain::connectModeChangeEvent(CEGUI::Window* parent, const std::string& buttonName, AbstractModeManager::ModeType mode)
{
    CEGUI::Window* childButton = parent->getChild(buttonName.c_str());
    OD_ASSERT_TRUE(childButton != nullptr);

    addEventConnection(
        childButton->subscribeEvent(
            CEGUI::PushButton::EventClicked,
            CEGUI::Event::Subscriber([this, mode] (const CEGUI::EventArgs& e) {
                changeModeEvent(mode, e);
                return true;
            })
        )
    );
}

bool MenuModeMain::toggleSettings(const CEGUI::EventArgs&)
{
    if (mSettings.isVisible())
        mSettings.onCancelSettings();
    else
        mSettings.show();
    return true;
}