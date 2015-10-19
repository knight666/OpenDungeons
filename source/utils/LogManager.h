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

#ifndef LOGMANAGER_H
#define LOGMANAGER_H

#include <memory>
#include <mutex>
#include <string>

#include <OgreSingleton.h>

#include "utils/Helper.h"
#include "utils/LogMessageLevel.h"
#include "utils/LogSink.h"

#ifndef OD_LOG_ALLOW_DEPRECATED
    #define OD_LOG_ALLOW_DEPRECATED 1
#endif

#if OD_LOG_ALLOW_DEPRECATED
    // DEPRECATED: Use OD_TRACE_ERR instead.
    #define OD_LOG_ERR(message)                      LogManager::getSingleton().logMessage(LogMessageLevel::CRITICAL, "", __FILE__, __LINE__, (std::string("") + message).c_str(), 0)
    // DEPRECATED: Use OD_TRACE_WRN instead.
    #define OD_LOG_WRN(message)                      LogManager::getSingleton().logMessage(LogMessageLevel::WARNING, "", __FILE__, __LINE__, (std::string("") + message).c_str(), 0)
    // DEPRECATED: Use OD_TRACE_INF instead.
    #define OD_LOG_INF(message)                      LogManager::getSingleton().logMessage(LogMessageLevel::NORMAL, "", __FILE__, __LINE__, (std::string("") + message).c_str(), 0)
    // DEPRECATED: Use OD_TRACE_DBG instead.
    #define OD_LOG_DBG(message)                      LogManager::getSingleton().logMessage(LogMessageLevel::TRIVIAL, "", __FILE__, __LINE__, (std::string("") + message).c_str(), 0)

    // DEPRECATED: Use OD_ASSERT instead.
    #define OD_ASSERT_TRUE(condition)                if (!(condition)) LogManager::getSingleton().logMessage(LogMessageLevel::CRITICAL, "", __FILE__, __LINE__, #condition, 0)
    // DEPRECATED: Use OD_ASSERT_MSG instead.
    #define OD_ASSERT_TRUE_MSG(condition, message)   if (!(condition)) LogManager::getSingleton().logMessage(LogMessageLevel::CRITICAL, "", __FILE__, __LINE__, (std::string("") + message).c_str(), 0)
#endif

#define OD_TRACE_ERR(_module, _message, ...)         LogManager::getSingleton().logMessage(LogMessageLevel::CRITICAL, #_module, __FILE__, __LINE__, _message, __VA_ARGS__)
#define OD_TRACE_WRN(_module, _message, ...)         LogManager::getSingleton().logMessage(LogMessageLevel::WARNING, #_module, __FILE__, __LINE__, _message, __VA_ARGS__)
#define OD_TRACE_INF(_module, _message, ...)         LogManager::getSingleton().logMessage(LogMessageLevel::NORMAL, #_module, __FILE__, __LINE__, _message, __VA_ARGS__)
#define OD_TRACE_DBG(_module, _message, ...)         LogManager::getSingleton().logMessage(LogMessageLevel::TRIVIAL, #_module, __FILE__, __LINE__, _message, __VA_ARGS__)

#define OD_ASSERT(_condition)                        if (!(_condition)) LogManager::getSingleton().logMessage(LogMessageLevel::CRITICAL, "Assert", __FILE__, __LINE__, #_condition, 0)
#define OD_ASSERT_MSG(_condition, _message, ...)     if (!(_condition)) LogManager::getSingleton().logMessage(LogMessageLevel::CRITICAL, "Assert", __FILE__, __LINE__, _message, __VA_ARGS__)

//! \brief Helper/wrapper class to provide thread-safe logging when ogre is compiled without threads.
class LogManager : public Ogre::Singleton<LogManager>
{
public:
    LogManager();
    ~LogManager();

    //! \brief Add a sink for log messages.
    void addSink(const std::shared_ptr<LogSink>& sink);

    //! \brief Log a message to the sinks.
    void logMessage(LogMessageLevel level, const char* module, const char* filepath, int line, const char* format, ...);

    static const std::string GAMELOG_NAME;
private:
    LogManager(const LogManager&) = delete;
    LogManager& operator=(const LogManager&) = delete;

    std::mutex mLock;
    std::vector<std::shared_ptr<LogSink>> mSinks;
};

#endif // LOGMANAGER_H
