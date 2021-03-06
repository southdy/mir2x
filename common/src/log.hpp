/*
 * =====================================================================================
 *
 *       Filename: log.hpp
 *        Created: 03/16/2016 16:05:17
 *    Description: log functionality enabled by g3Log
 *
 *        Version: 1.0
 *       Revision: none
 *       Compiler: gcc
 *
 *         Author: ANHONG
 *          Email: anhonghe@gmail.com
 *   Organization: USTC
 *
 * =====================================================================================
 */

#pragma once
#include <array>
#include <thread>
#include <string>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <g3log/g3log.hpp>
#include <g3log/logworker.hpp>

#include "strf.hpp"
#include "logprof.hpp"

#if (defined(WIN32) || defined(_WIN32) || defined(__WIN32__))
#define LOG_PATH "./"
#else
#define LOG_PATH "/tmp/"
#endif


#define MIR2X_VERSION_STRING "mir2x-v0.1"

#ifdef MIR2X_VERSION_STRING
#define LOG_ARGV0 MIR2X_VERSION_STRING
#else
#define LOG_ARGV0 "mir2x"
#endif

class Log final
{
    public:
        enum LogTypeIntValue: int
        {
            LOGTYPEV_INFO    = 0,
            LOGTYPEV_WARNING = 1,
            LOGTYPEV_FATAL   = 2,
            LOGTYPEV_DEBUG   = 3,
        };

    private:
        std::unique_ptr<g3::LogWorker>      m_worker;
        std::unique_ptr<g3::FileSinkHandle> m_handler;
        std::string                         m_logFileName;

    public:
        Log(const char *szLogArg0 = LOG_ARGV0, const char *szLogPath = LOG_PATH)
            : m_worker(g3::LogWorker::createLogWorker())
            , m_handler(m_worker->addDefaultLogger(szLogArg0, szLogPath))
        {
            g3::initializeLogging(m_worker.get());

            std::future<std::string> szLogFileName = m_handler->call(&g3::FileSink::fileName);

            std::cout << "* This is the initialization of Log functionality"           << std::endl;
            std::cout << "* For info/debug/warning/fatal messages."                    << std::endl;

            m_logFileName = szLogFileName.get();

            std::cout << "* Log file: [" << m_logFileName << "]"                       << std::endl;
            std::cout << "* Log functionality established!"                            << std::endl;
            std::cout << "* All messges will be redirected to the log after this line" << std::endl;
        }

    public:
        ~Log() = default;

    public:
        const char *logPath() const
        {
            return m_logFileName.c_str();
        }

    private:
        decltype(INFO) getLevel(const char *szLevel)
        {
            if(!std::strcmp(szLevel, "0")){ return INFO;    }
            if(!std::strcmp(szLevel, "1")){ return WARNING; }
            if(!std::strcmp(szLevel, "2")){ return FATAL;   }

            return DEBUG;
        }

    public:
        void addLog(const std::array<std::string, 4> &stLoc, const char *szLogFormat, ...)
        {
            std::string szLog;
            bool bError = false;
            {
                va_list ap;
                va_start(ap, szLogFormat);

                try{
                    szLog = str_vprintf(szLogFormat, ap);
                }catch(const std::exception &e){
                    bError = true;
                    szLog = str_printf("Exception caught in Log::addLog(\"%s\", ...): %s", szLogFormat, e.what());
                }

                va_end(ap);
            }

            const int nLine = std::atoi(stLoc[2].c_str());
            const auto stLevel = bError ? WARNING : getLevel(stLoc[0].c_str());
            LogCapture(stLoc[1].c_str(), nLine, stLoc[3].c_str(), stLevel).capturef("%s", szLog.c_str());
        }
};

#define LOGTYPE_INFO    {std::to_string(Log::LOGTYPEV_INFO   ), std::string(__FILE__), std::to_string(__LINE__), std::string(__PRETTY_FUNCTION__)}
#define LOGTYPE_WARNING {std::to_string(Log::LOGTYPEV_WARNING), std::string(__FILE__), std::to_string(__LINE__), std::string(__PRETTY_FUNCTION__)}
#define LOGTYPE_FATAL   {std::to_string(Log::LOGTYPEV_FATAL  ), std::string(__FILE__), std::to_string(__LINE__), std::string(__PRETTY_FUNCTION__)}
#define LOGTYPE_DEBUG   {std::to_string(Log::LOGTYPEV_DEBUG  ), std::string(__FILE__), std::to_string(__LINE__), std::string(__PRETTY_FUNCTION__)}
