//------------------------------------------------------------------------------
/*
    This file is part of clio: https://github.com/XRPLF/clio
    Copyright (c) 2025, the clio developers.

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

#include "util/LoggerFixtures.hpp"

#include <spdlog/async.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

struct ostream_sink_mt_fwd {
    // ostream_sink is marked as final, so we can't inherit from it
    std::shared_ptr<spdlog::sinks::ostream_sink_mt> impl;

    explicit ostream_sink_mt_fwd(std::ostream& stream) : impl{std::make_shared<spdlog::sinks::ostream_sink_mt>(stream)}
    {
    }
};

LoggerFixture::LoggerFixture()
{
    static std::once_flag kONCE;
    std::call_once(kONCE, [] { spdlog::init_thread_pool(1024, 1); });

    // Create ostream sink for testing
    ostream_sink_ = std::make_shared<ostream_sink_mt_fwd>(stream_);
    ostream_sink_->impl->set_pattern("%Y-%m-%d %H:%M:%S.%e (%s:%#) [%t] %n:%^%l%$ %v");

    // Clear any existing loggers
    spdlog::drop_all();

    // Create loggers for each channel
    std::ranges::for_each(util::Logger::kCHANNELS, [this](char const* channel) {
        auto logger = std::make_shared<spdlog::logger>(channel, ostream_sink_->impl);
        logger->set_level(spdlog::level::trace);
        spdlog::register_logger(logger);
    });

    // Set specific levels for some channels
    if (auto general = spdlog::get("General")) {
        general->set_level(spdlog::level::debug);
    }
    if (auto trace = spdlog::get("Trace")) {
        trace->set_level(spdlog::level::trace);
    }
}

NoLoggerFixture::NoLoggerFixture()
{
    spdlog::set_level(spdlog::level::off);
}
