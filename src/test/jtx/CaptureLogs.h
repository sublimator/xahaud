//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/basics/Log.h>

namespace ripple {
namespace test {

/**
 * @brief Log manager for CaptureSinks. This class holds the stream
 * instance that is written to by the sinks. Upon destruction, all
 * contents of the stream are assigned to the string specified in the
 * ctor
 */
class CaptureLogs : public Logs
{
    std::mutex strmMutex_;
    std::stringstream strm_;
    std::string* pResult_;

    /**
     * @brief sink for writing all log messages to a stringstream
     */
    class CaptureSink : public beast::Journal::Sink
    {
        std::mutex& strmMutex_;
        std::stringstream& strm_;

    public:
        CaptureSink(
            beast::severities::Severity threshold,
            std::mutex& mutex,
            std::stringstream& strm)
            : beast::Journal::Sink(threshold, false)
            , strmMutex_(mutex)
            , strm_(strm)
        {
        }

        void
        write(beast::severities::Severity level, std::string const& text)
            override
        {
            std::lock_guard lock(strmMutex_);
            strm_ << text;
        }
    };

public:
    explicit CaptureLogs(std::string* pResult)
        : Logs(beast::severities::kInfo), pResult_(pResult)
    {
    }

    ~CaptureLogs() override
    {
        *pResult_ = strm_.str();
    }

    std::unique_ptr<beast::Journal::Sink>
    makeSink(
        std::string const& partition,
        beast::severities::Severity threshold) override
    {
        return std::make_unique<CaptureSink>(threshold, strmMutex_, strm_);
    }
};
/**
 * @brief Log manager for FileSinks. This class writes log messages
 * to a file specified during construction. The file remains open
 * until the FileLogs object is destroyed.
 */
class FileLogs : public Logs
{
    std::mutex fileMutex_;
    std::ofstream fileStream_;
    std::string filename_;
    bool flushImmediately_;

    /**
     * @brief sink for writing all log messages to a file
     */
    class FileSink : public beast::Journal::Sink
    {
        std::mutex& fileMutex_;
        std::ofstream& fileStream_;
        bool flushImmediately_;
        std::string partition_;

    public:
        FileSink(
            beast::severities::Severity threshold,
            std::mutex& mutex,
            std::ofstream& fileStream,
            bool flushImmediately,
            std::string const& partition)
            : beast::Journal::Sink(threshold, false)
            , fileMutex_(mutex)
            , fileStream_(fileStream)
            , flushImmediately_(flushImmediately)
            , partition_(partition)
        {
        }

        void
        write(beast::severities::Severity level, std::string const& text)
            override
        {
            using namespace beast::severities;

            char const* const s = [level]() {
                switch (level)
                {
                    case kTrace:
                        return "TRC:";
                    case kDebug:
                        return "DBG:";
                    case kInfo:
                        return "INF:";
                    case kWarning:
                        return "WRN:";
                    case kError:
                        return "ERR:";
                    default:
                        break;
                    case kFatal:
                        break;
                }
                return "FTL:";
            }();

            // Only write the string if the level at least equals the threshold.
            if (level >= threshold())
            {
                std::lock_guard lock(fileMutex_);
                fileStream_ << s << ":" << partition_  << " " << text << std::endl;

                if (flushImmediately_)
                    fileStream_.flush();
            }
        }
    };

public:
    explicit FileLogs(
        std::string const& filename,
        beast::severities::Severity threshold = beast::severities::kInfo,
        bool flushImmediately = true)
        : Logs(threshold)
        , filename_(filename)
        , flushImmediately_(flushImmediately)
    {
        // Open file in truncation mode (std::ios::trunc) instead of append (std::ios::app)
        fileStream_.open(filename_, std::ios::out | std::ios::trunc);
        if (!fileStream_.is_open())
        {
            throw std::runtime_error("Failed to open log file: " + filename_);
        }
    }

    ~FileLogs() override
    {
        if (fileStream_.is_open())
        {
            fileStream_.close();
        }
    }

    std::unique_ptr<beast::Journal::Sink>
    makeSink(
        std::string const& partition,
        beast::severities::Severity threshold) override
    {
        return std::make_unique<FileSink>(
            threshold, fileMutex_, fileStream_, flushImmediately_, partition);
    }

    // Get the filename this log is writing to
    std::string const&
    filename() const
    {
        return filename_;
    }
};

}  // namespace test
}  // namespace ripple
