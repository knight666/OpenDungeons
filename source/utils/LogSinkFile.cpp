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

#include "utils/LogSinkFile.h"

#include "utils/Helper.h"

LogSinkFile::LogSinkFile(const std::string& filepath)
    : mFilePath(filepath)
{
    mFile.open(mFilePath.c_str(), std::ios::out | std::ios::trunc);
    if (mFile.is_open())
    {
        mFile.close();
    }
}

LogSinkFile::~LogSinkFile()
{

}

void LogSinkFile::write(LogMessageLevel level, const char* module, const char* timestamp, const char* filename, int line, const char* message)
{
    mFile.open(mFilePath.c_str(), std::ios::out | std::ios::app);
    if (!mFile.is_open())
    {
        return;
    }

    if (level >= LogMessageLevel::WARNING)
    {
        mFile << "(" << filename << ":" << line << ") ";
    }

    mFile
        << "(" << timestamp << ") "
        << "(" << module << ") "
        << "[" << LogMessageLevelToString(level) << "] "
        << message
        << std::endl;

    mFile.flush();

    mFile.close();
}
