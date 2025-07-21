//------------------------------------------------------------------------------
/*
    This file is part of clio: https://github.com/XRPLF/clio
    Copyright (c) 2022, the clio developers.

    Permission to use, copy, modify, and distribute this software for any
    purpose with or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL,  DIRECT,  INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include "util/log/Logger.hpp"

#include "util/Assert.hpp"
#include "util/BytesConverter.hpp"
#include "util/SourceLocation.hpp"
#include "util/config/ArrayView.hpp"
#include "util/config/ConfigDefinition.hpp"
#include "util/config/ObjectView.hpp"
#include "util/prometheus/Counter.hpp"
#include "util/prometheus/Label.hpp"
#include "util/prometheus/Prometheus.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <fmt/format.h>
#include <spdlog/async.h>
#include <spdlog/common.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <ios>
#include <iostream>
#include <memory>
#include <optional>
#include <ostream>
#include <regex>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace util {

namespace {

class LoggerExceptionHandler {
    std::reference_wrapper<util::prometheus::CounterInt> exceptionCounter_ =
        PrometheusService::counterInt("logger_exceptions_total_number", util::prometheus::Labels{});

public:
    using result_type = void;

    LoggerExceptionHandler()
    {
        ASSERT(PrometheusService::isInitialised(), "Prometheus should be initialised before Logger");
    }

    void
    operator()(std::exception const& e) const
    {
        std::cerr << fmt::format("Exception in logger: {}\n", e.what());
        ++exceptionCounter_.get();
    }
};

}  // namespace

Logger LogService::generalLog = Logger{"General"};

std::ostream&
operator<<(std::ostream& stream, Severity sev)
{
    static constexpr std::array<char const*, 6> kLABELS = {
        "TRC",
        "DBG",
        "NFO",
        "WRN",
        "ERR",
        "FTL",
    };

    return stream << kLABELS.at(static_cast<int>(sev));
}

spdlog::level::level_enum
toSpdlogLevel(Severity sev)
{
    switch (sev) {
        case Severity::TRC:
            return spdlog::level::trace;
        case Severity::DBG:
            return spdlog::level::debug;
        case Severity::NFO:
            return spdlog::level::info;
        case Severity::WRN:
            return spdlog::level::warn;
        case Severity::ERR:
            return spdlog::level::err;
        case Severity::FTL:
            return spdlog::level::critical;
    }
    return spdlog::level::info;
}

/**
 * @brief converts the loglevel to string to a corresponding Severity enum value.
 *
 * @param logLevel A string representing the log level
 * @return Severity The corresponding Severity enum value.
 */
static Severity
getSeverityLevel(std::string_view logLevel)
{
    if (boost::iequals(logLevel, "trace"))
        return Severity::TRC;
    if (boost::iequals(logLevel, "debug"))
        return Severity::DBG;
    if (boost::iequals(logLevel, "info"))
        return Severity::NFO;
    if (boost::iequals(logLevel, "warning") || boost::iequals(logLevel, "warn"))
        return Severity::WRN;
    if (boost::iequals(logLevel, "error"))
        return Severity::ERR;
    if (boost::iequals(logLevel, "fatal"))
        return Severity::FTL;

    // already checked during parsing of config that value must be valid
    ASSERT(false, "Parsing of log_level is incorrect");
    std::unreachable();
}

std::expected<void, std::string>
LogService::init(config::ClioConfigDefinition const& config)
{
    try {
        // Initialize spdlog
        spdlog::init_thread_pool(8192, 1);

        std::vector<spdlog::sink_ptr> sinks;

        auto defaultSeverity = getSeverityLevel(config.get<std::string>("log_level"));

        // Setup console logging
        if (config.get<bool>("log_to_console")) {
            auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            sinks.push_back(consoleSink);
        }

        // Setup file logging
        auto const logDir = config.maybeValue<std::string>("log_directory");
        if (logDir) {
            std::filesystem::path dirPath{logDir.value()};
            if (not std::filesystem::exists(dirPath)) {
                if (std::error_code error; not std::filesystem::create_directories(dirPath, error)) {
                    return std::unexpected{
                        fmt::format("Couldn't create logs directory '{}': {}", dirPath.string(), error.message())
                    };
                }
            }

            auto const rotationSize = mbToBytes(config.get<uint32_t>("log_rotation_size"));
            auto const maxFiles =
                config.get<uint32_t>("log_directory_max_size") / config.get<uint32_t>("log_rotation_size");

            auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                (dirPath / "clio.log").string(), rotationSize, maxFiles > 0 ? maxFiles : 10
            );
            sinks.push_back(fileSink);
        }

        // Always add stderr sink for fatal errors
        auto stderrSink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
        stderrSink->set_level(spdlog::level::critical);
        sinks.push_back(stderrSink);

        // Get channel severity settings
        std::unordered_map<std::string, Severity> minSeverity;
        for (auto const& channel : Logger::kCHANNELS)
            minSeverity[channel] = defaultSeverity;
        minSeverity["Alert"] = Severity::WRN;

        auto const overrides = config.getArray("log_channels");
        for (auto it = overrides.begin<util::config::ObjectView>(); it != overrides.end<util::config::ObjectView>();
             ++it) {
            auto const& channelConfig = *it;
            auto const name = channelConfig.get<std::string>("channel");
            if (std::ranges::count(Logger::kCHANNELS, name) == 0) {
                return std::unexpected{
                    fmt::format("Can't override settings for log channel {}: invalid channel", name)
                };
            }
            minSeverity[name] = getSeverityLevel(channelConfig.get<std::string>("log_level"));
        }

        // Convert boost log format to spdlog format (simplified)
        std::string format = config.get<std::string>("log_format");
        std::string spdlogFormat = "%Y-%m-%d %H:%M:%S.%e (%s:%#) [%t] %n:%^%l%$ %v";

        // Create loggers for each channel
        for (auto const& [channel, severity] : minSeverity) {
            auto logger = std::make_shared<spdlog::async_logger>(
                channel, sinks.begin(), sinks.end(), spdlog::thread_pool(), spdlog::async_overflow_policy::block
            );
            logger->set_level(toSpdlogLevel(severity));
            logger->set_pattern(spdlogFormat);
            spdlog::register_logger(std::move(logger));
        }

        LOG(LogService::info()) << "Default log level = " << defaultSeverity;
        return {};
    } catch (spdlog::spdlog_ex const& ex) {
        return std::unexpected{fmt::format("Log initialization failed: {}", ex.what())};
    }
}

bool
LogService::enabled()
{
    return spdlog::default_logger() != nullptr && spdlog::default_logger()->level() != spdlog::level::off;
}

// Logger constructor
Logger::Logger(std::string channel)
{
    logger_ = spdlog::get(channel);
    if (!logger_) {
        // Create a basic logger if not found
        logger_ = spdlog::stdout_color_mt(channel);
    }
}

// Logger::Pump constructor
Logger::Pump::Pump(std::shared_ptr<spdlog::logger> logger, Severity sev, SourceLocationType const& loc)
    : logger_(std::move(logger))
    , severity_(sev)
    , sourceLocation_(prettyPath(loc))
    , enabled_(logger_ && logger_->should_log(toSpdlogLevel(sev)))
{
}

// Logger::Pump destructor
Logger::Pump::~Pump()
{
    if (enabled_ && logger_) {
        auto message = stream_.str();
        switch (severity_) {
            case Severity::TRC:
                logger_->trace("{} {}", sourceLocation_, message);
                break;
            case Severity::DBG:
                logger_->debug("{} {}", sourceLocation_, message);
                break;
            case Severity::NFO:
                logger_->info("{} {}", sourceLocation_, message);
                break;
            case Severity::WRN:
                logger_->warn("{} {}", sourceLocation_, message);
                break;
            case Severity::ERR:
                logger_->error("{} {}", sourceLocation_, message);
                break;
            case Severity::FTL:
                logger_->critical("{} {}", sourceLocation_, message);
                break;
        }
    }
}

Logger::Pump
Logger::trace(SourceLocationType const& loc) const
{
    return {logger_, Severity::TRC, loc};
}
Logger::Pump
Logger::debug(SourceLocationType const& loc) const
{
    return {logger_, Severity::DBG, loc};
}
Logger::Pump
Logger::info(SourceLocationType const& loc) const
{
    return {logger_, Severity::NFO, loc};
}
Logger::Pump
Logger::warn(SourceLocationType const& loc) const
{
    return {logger_, Severity::WRN, loc};
}
Logger::Pump
Logger::error(SourceLocationType const& loc) const
{
    return {logger_, Severity::ERR, loc};
}
Logger::Pump
Logger::fatal(SourceLocationType const& loc) const
{
    return {logger_, Severity::FTL, loc};
}

std::string
Logger::Pump::prettyPath(SourceLocationType const& loc, size_t maxDepth)
{
    auto const filePath = std::string{loc.file_name()};
    auto idx = filePath.size();
    while (maxDepth-- > 0) {
        idx = filePath.rfind('/', idx - 1);
        if (idx == std::string::npos || idx == 0)
            break;
    }
    return filePath.substr(idx == std::string::npos ? 0 : idx + 1) + ':' + std::to_string(loc.line());
}

}  // namespace util
