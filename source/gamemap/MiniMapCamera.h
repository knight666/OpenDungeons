/*!
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

#ifndef MINIMAPCAMERA_H
#define MINIMAPCAMERA_H

#include <OgreRenderTargetListener.h>
#include <OgreTexture.h>
#include <OgreVector2.h>
#include <OgreVector3.h>

#include <vector>

namespace CEGUI
{
    class Window;
}

class CameraManager;
class GameMap;

//! \brief The class handling the minimap seen bottom-right of the in-game screen.
class MiniMapCamera : public Ogre::RenderTargetListener
{
public:
    MiniMapCamera(CEGUI::Window* miniMapWindow);
    ~MiniMapCamera();

    Ogre::Vector2 camera_2dPositionFromClick(int xx, int yy);

    void update(Ogre::Real timeSinceLastFrame);

    //! This functions allow to hook minimap rendering to adjust nodes we want to display or not
    virtual void preRenderTargetUpdate(const Ogre::RenderTargetEvent& rte) override;
    virtual void postRenderTargetUpdate(const Ogre::RenderTargetEvent& rte) override;

private:
    CEGUI::Window* mMiniMapWindow;

    GameMap& mGameMap;
    CameraManager& mCameraManager;

    int mTopLeftCornerX;
    int mTopLeftCornerY;

    Ogre::Real mWidth;
    Ogre::Real mHeight;

    Ogre::Real mMapX;
    Ogre::Real mMapY;

    Ogre::Real mElapsedTime;

    Ogre::TexturePtr mMiniMapOgreTexture;

    int mCurCamPosX;
    int mCurCamPosY;
    Ogre::Camera* mMiniMapCam;

    void updateMinimapCamera();
};

#endif // MINIMAPCAMERA_H
