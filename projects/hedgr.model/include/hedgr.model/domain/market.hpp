/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * Copyright (C) 2026 Marco Craveiro <marco.craveiro@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 3 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */
#ifndef HEDGR_MODEL_DOMAIN_MARKET_HPP
#define HEDGR_MODEL_DOMAIN_MARKET_HPP

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

namespace hedgr::model::domain {

/**
 * @brief The deployment's asset class, as the schema spells it.
 *
 * Render code branches on this rather than on the upper-cased URI authority:
 * the authority is the session's working vocabulary, not the server's, and a
 * change in its casing silently stopped the IR and FX views matching.
 */
enum class market_asset : std::uint8_t {
    unknown = 0,
    ir,
    fx,
    credit,
    equity,
    commodity,
    inflation,
    security,
    correlation,
    rating,
    shape_profile,
    generic,
};

/** @brief The asset @p name spells, folding case; unknown for anything else. */
[[nodiscard]] inline market_asset market_asset_of(
    std::string_view name) noexcept {
    const auto equals = [](std::string_view a, std::string_view b) {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            const auto fold = [](char c) {
                return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a')
                                            : c;
            };
            if (fold(a[i]) != fold(b[i]))
                return false;
        }
        return true;
    };
    if (equals(name, "ir"))
        return market_asset::ir;
    if (equals(name, "fx"))
        return market_asset::fx;
    if (equals(name, "credit"))
        return market_asset::credit;
    if (equals(name, "equity"))
        return market_asset::equity;
    if (equals(name, "commodity"))
        return market_asset::commodity;
    if (equals(name, "inflation"))
        return market_asset::inflation;
    if (equals(name, "security"))
        return market_asset::security;
    if (equals(name, "correlation"))
        return market_asset::correlation;
    if (equals(name, "rating"))
        return market_asset::rating;
    if (equals(name, "shape_profile"))
        return market_asset::shape_profile;
    if (equals(name, "generic"))
        return market_asset::generic;
    return market_asset::unknown;
}

/** @brief The schema's spelling of @p asset, or "unknown". */
[[nodiscard]] inline std::string_view to_string(market_asset asset) noexcept {
    switch (asset) {
    case market_asset::ir:
        return "ir";
    case market_asset::fx:
        return "fx";
    case market_asset::credit:
        return "credit";
    case market_asset::equity:
        return "equity";
    case market_asset::commodity:
        return "commodity";
    case market_asset::inflation:
        return "inflation";
    case market_asset::security:
        return "security";
    case market_asset::correlation:
        return "correlation";
    case market_asset::rating:
        return "rating";
    case market_asset::shape_profile:
        return "shape_profile";
    case market_asset::generic:
        return "generic";
    case market_asset::unknown:
        return "unknown";
    }
    return "unknown";
}

/** @brief One extra field of a typed series identity: a name and its text. */
struct market_series_identity_field final {
    std::string name;
    std::string text;
};

/**
 * @brief The typed identity of one market series.
 *
 * What a tick read names a series by: the asset, the scope (the URI's subject),
 * the instrument and quote types, the owning party and provider, and every
 * other identity field. It is parsed from the catalogue's oresmd URI once, when
 * the catalogue arrives, so a tick read does not list the deployment's
 * identities to find this series' own.
 */
struct market_series_identity final {
    /** @brief The asset class as the schema spells it, for example fx. */
    std::string asset;

    /** @brief The URI's subject, for example the base currency. */
    std::string scope;

    /** @brief The instrument type as the schema spells it, for example fx_spot. */
    std::string instrument_type;

    /** @brief The quote type as the schema spells it, for example rate. */
    std::string quote_type;

    /** @brief The party that owns the series. */
    std::string party_id;

    /** @brief The provider whose ids the series carries. */
    std::string provider_code;

    /** @brief The identity's remaining fields, in schema order. */
    std::vector<market_series_identity_field> fields;

    /** @brief Whether the identity was parsed; an empty one falls back to a list. */
    [[nodiscard]] bool empty() const noexcept {
        return instrument_type.empty();
    }
};

/**
 * @brief One market series, as the catalogue lists it.
 *
 * The URI is the series' identity. The label is the interface's reading of it,
 * made once when the catalogue arrives; the typed identity is the read's, made
 * once then too.
 */
struct market_series_row final {
    /** @brief The series' own key. */
    std::string id;

    /** @brief The canonical oresmd URI, the series' identity. */
    std::string uri;

    /** @brief A person's name for the series. */
    std::string label;

    /** @brief The asset class, upper-cased. */
    std::string asset_class;

    /** @brief The provider whose ids the series carries, for example ore. */
    std::string provider_code;

    /** @brief The party that owns the series. */
    std::string party_id;

    /** @brief The tenor or term the series carries, or empty. */
    std::string tenor;

    /** @brief The curve identity the series belongs to, or empty. */
    std::string curve_key;

    /** @brief The curve's name, from the URI without its term. */
    std::string curve_label;

    /** @brief The typed identity a tick read names the series by. */
    market_series_identity identity;

    /**
     * @brief The schema's asset class, for a branch that must not guess.
     *
     * Unknown for a row from before the parse; render code falls back to the
     * display string then.
     */
    market_asset asset = market_asset::unknown;
};

/**
 * @brief One market data provider, as the deployment lists it.
 *
 * The kind is the deployment's own word, not a mapping: `data_vendor`,
 * `exchange`, `broker` or `reference`. A reference provider publishes the
 * definition of an instrument and never ticks; a series of one is listed
 * beside the same pair's data-vendor series.
 */
struct market_provider_row final {
    /** @brief The provider's code, for example ore or bloomberg. */
    std::string code;

    /** @brief The provider's name as a person knows it. */
    std::string name;

    /** @brief The deployment's kind for it, for example data_vendor. */
    std::string kind;
};

/** @brief Whether @p row publishes data rather than a reference definition. */
[[nodiscard]] inline bool is_reference_provider(
    const market_provider_row& row) noexcept {
    return row.kind == "reference";
}

/**
 * @brief One axis of an instrument's shape, with its values in draw order.
 *
 * The field is an oresmd field code such as `tenor` or `strike_label`, never a
 * display name, because it names the coordinate rather than a column heading.
 * The values arrive in the server's order, so a plot draws them as they come
 * and never sorts them.
 */
struct instrument_axis final {
    /** @brief The oresmd field code the shape stores, for example tenor. */
    std::string field;

    /** @brief The values on this axis, in the order the server states them. */
    std::vector<std::string> values;
};

/**
 * @brief One source behind an instrument, and whether its value is current.
 *
 * The provider kind is the provider table's own word, so a definition can be
 * told from a quote without a second read. The staleness verdict is the
 * server's, measured against the threshold it holds for the instrument's kind:
 * this client draws it and never recomputes it.
 */
struct instrument_source final {
    /** @brief The provider's code, for example ore or bloomberg. */
    std::string provider_code;

    /** @brief The provider's own kind, for example reference or data_vendor. */
    std::string provider_kind;

    /** @brief The series this source quotes the instrument through. */
    std::string series_id;

    /** @brief When this source's newest observation was taken, epoch ns, or 0. */
    std::int64_t as_of_ns = 0;

    /** @brief How far behind the service clock that instant is. */
    std::int64_t age_seconds = 0;

    /** @brief Whether the server judges this source past its staleness threshold. */
    bool stale = false;
};

/**
 * @brief Whether @p source is a definition rather than a price.
 *
 * A reference provider publishes what an instrument is and never ticks, so a
 * screen that offers prices can leave these out and one that explains an
 * instrument keeps them.
 */
[[nodiscard]] inline bool is_reference_source(
    const instrument_source& source) noexcept {
    return source.provider_kind == "reference";
}

/**
 * @brief One instrument: the series a person reads as one thing.
 *
 * The deployment's definition, not this client's grouping. A curve is one entry
 * and never one entry per pillar; a pair is one entry with no axes; a surface is
 * one entry whose two axes are its expiries and its strikes. The name is the
 * server's, unique within the party and the kind, and is what a person types
 * into the search.
 *
 * The kind is the server's word - `pair`, `ladder`, `curve` or `surface` - and
 * a screen draws it as a badge rather than deriving a category of its own. Any
 * grouping this client would be tempted to do is already done here.
 */
struct market_instrument final {
    /** @brief The instrument's identifier, stable within the party. */
    std::string instrument_id;

    /** @brief The server's kind for it: pair, ladder, curve or surface. */
    std::string kind;

    /** @brief The name a person searches for and a list draws. */
    std::string name;

    /** @brief The currency it is about, for a single-currency instrument. */
    std::string currency;

    /** @brief The pair's base currency, or empty when it is not a pair. */
    std::string base_currency;

    /** @brief The pair's quote currency, or empty when it is not a pair. */
    std::string quote_currency;

    /** @brief The schema's instrument type, for example ir_swap or fx_spot. */
    std::string instrument_type;

    /** @brief The schema's quote type, for example rate or price. */
    std::string quote_type;

    /** @brief The forward start the instrument is struck at, or empty. */
    std::string forward_start;

    /** @brief The shape's axes, in the order the server states them. */
    std::vector<instrument_axis> axes;

    /** @brief The providers behind it, each with its own verdict. */
    std::vector<instrument_source> sources;

    /** @brief The party that owns it. */
    std::string party_id;

    /** @brief Whether every source is stale, in the server's judgement. */
    bool stale = false;

    /** @brief The newest instant any source was observed, epoch ns, or 0. */
    std::int64_t as_of_ns = 0;
};

/**
 * @brief What a caller narrows the instrument catalogue to.
 *
 * The deployment applies all three and returns what remains, so a search is one
 * read rather than a filter this client runs over a cached list: a query it
 * matches on more than a name, and a kind it knows the full set of.
 */
struct instrument_filter final {
    /** @brief Free text, matched against the facts the server holds. */
    std::string query;

    /** @brief The kinds wanted, empty for all of them. */
    std::vector<std::string> kinds;

    /** @brief The provider whose instruments are wanted, empty for all. */
    std::string provider_code;
};

/**
 * @brief One curve the deployment publishes, as a chooser lists it.
 *
 * A curve is an object the deployment calibrates and publishes rather than a
 * set of series a client assembles, so it is named and opened on its own. The
 * series it is published under is what a window reads.
 */
struct published_curve final {
    /** @brief The curve's name, as the deployment spells it ("EUR-ESTR"). */
    std::string label;

    /** @brief The series the curve is published under. */
    std::string series_id;

    /** @brief How many pillars the curve holds. */
    std::uint32_t points = 0;
};

/**
 * @brief One volatility surface blueprint a deployment publishes.
 *
 * A blueprint is what a surface is calibrated and published under: the pair,
 * the model, and the two axes the output grid runs on, as the deployment spells
 * them. The axes travel as the deployment wrote them - `1W,1M,2M` and
 * `DEL/Spot/Put/25,ATM,...` - because they name a grid the surface module
 * publishes, not a value this client scales.
 */
struct surface_blueprint final {
    /** @brief The blueprint's identifier. */
    std::string id;

    /** @brief The name a person wrote for it, such as EURUSD_SSVI. */
    std::string name;

    /** @brief The pair's base and quote currencies. */
    std::string base_currency;
    std::string quote_currency;

    /** @brief The model fitted at each expiry: ssvi, sabr or spline. */
    std::string smile_model;

    /** @brief The output grid's expiries as tenor codes, comma separated. */
    std::string expiries;

    /** @brief The output grid's strikes as the deployment spells them. */
    std::string strikes;
};

/** @brief The three resolutions a live series keeps. */
enum class market_level : std::uint8_t {
    fine = 0,
    medium = 1,
    coarse = 2,
};

/** @brief The interval one fine bucket covers. */
inline constexpr std::int64_t market_fine_ns = 1'000'000'000;

/** @brief The interval one medium bucket covers. */
inline constexpr std::int64_t market_medium_ns = 60 * market_fine_ns;

/** @brief The interval one coarse bucket covers. */
inline constexpr std::int64_t market_coarse_ns = 60 * market_medium_ns;

/** @brief How many fine buckets a series keeps, two hours at one second. */
inline constexpr std::size_t market_fine_capacity = 7200;

/** @brief How many medium buckets a series keeps, two days at one minute. */
inline constexpr std::size_t market_medium_capacity = 2880;

/** @brief How many coarse buckets a series keeps, thirty days at one hour. */
inline constexpr std::size_t market_coarse_capacity = 720;

/** @brief The interval a level aggregates over. */
[[nodiscard]] inline constexpr std::int64_t
market_level_ns(market_level level) noexcept {
    switch (level) {
    case market_level::fine:
        return market_fine_ns;
    case market_level::medium:
        return market_medium_ns;
    case market_level::coarse:
        return market_coarse_ns;
    }
    return market_fine_ns;
}

/** @brief The number of buckets a level keeps. */
[[nodiscard]] inline constexpr std::size_t
market_level_capacity(market_level level) noexcept {
    switch (level) {
    case market_level::fine:
        return market_fine_capacity;
    case market_level::medium:
        return market_medium_capacity;
    case market_level::coarse:
        return market_coarse_capacity;
    }
    return market_fine_capacity;
}

/**
 * @brief The level a chart span reads from.
 *
 * Up to two hours reads the fine level, up to two days the medium level, and
 * anything longer, or the whole series (zero), the coarse level.
 */
[[nodiscard]] inline constexpr market_level
market_level_for_span(std::int64_t span_ns) noexcept {
    if (span_ns == 0)
        return market_level::coarse;
    if (span_ns <= 2 * market_coarse_ns)
        return market_level::fine;
    if (span_ns <= 2 * 24 * market_coarse_ns)
        return market_level::medium;
    return market_level::coarse;
}

/**
 * @brief The most ticks one reply of the history call carries.
 *
 * The service refuses a span that holds more, so a read sized to a span
 * retries once on a shorter one rather than paging.
 */
inline constexpr std::size_t market_tick_ceiling = 25'000;

/**
 * @brief How many ticks a first load tries to bring back.
 *
 * A series that ticks slower than the first span covers would give a chart a
 * few points, so the first load widens its span until it holds this many or
 * runs out of spans.
 */
inline constexpr std::size_t market_first_load_min_points = 200;

/**
 * @brief The spans a first load tries, narrowest first.
 *
 * Each is what one level keeps: two hours at the fine level, two days at the
 * medium and thirty days at the coarse one.
 */
inline constexpr std::array<std::int64_t, 3> market_first_load_spans{
    static_cast<std::int64_t>(market_fine_capacity) * market_fine_ns,
    static_cast<std::int64_t>(market_medium_capacity) * market_medium_ns,
    static_cast<std::int64_t>(market_coarse_capacity) * market_coarse_ns};

/**
 * @brief The spans a read of a series' few newest ticks tries, narrowest first.
 *
 * A term structure keeps twenty points per pillar and refreshes on a cadence,
 * so it starts at a minute rather than at what a chart needs.
 */
inline constexpr std::array<std::int64_t, 4> market_tail_spans{
    market_medium_ns, market_coarse_ns, 24 * market_coarse_ns,
    static_cast<std::int64_t>(market_coarse_capacity) * market_coarse_ns};

/** @brief What a deeper read of a series would ask the deployment for. */
struct history_plan final {
    /** @brief The level the span reads from. */
    market_level level = market_level::fine;

    /** @brief The earliest instant the span needs, in epoch nanoseconds. */
    std::int64_t from_ns = 0;

    /** @brief The instant the read stops at, the level's oldest held bucket. */
    std::int64_t to_ns = 0;
};

/**
 * @brief The read a chart span needs beyond what the series holds, if any.
 *
 * A span is measured back from the newest point of its level, as the plot
 * crops it, and never reaches further than the level's ring keeps. Nothing is
 * needed when the level already holds a bucket at or before the start of the
 * span, when an earlier read already asked at least that far back (so a series
 * the deployment holds little of is not asked again), or when the level holds
 * nothing to extend.
 *
 * @param span_ns the chart span, or zero for the whole series.
 * @param level_oldest_ns the oldest held bucket of the span's level, or zero.
 * @param level_newest_ns the newest held bucket of that level, or zero.
 * @param floor_ns how far back an earlier read asked, or zero for none.
 */
[[nodiscard]] inline constexpr std::optional<history_plan>
plan_history_read(std::int64_t span_ns, std::int64_t level_oldest_ns,
    std::int64_t level_newest_ns, std::int64_t floor_ns) noexcept {
    if (level_oldest_ns == 0 || level_newest_ns == 0)
        return std::nullopt;
    const auto level = market_level_for_span(span_ns);
    const auto bucket = market_level_ns(level);
    const auto kept = static_cast<std::int64_t>(market_level_capacity(level))
        * bucket;
    const auto wanted = span_ns == 0 ? kept : std::min(span_ns, kept);
    const auto from_ns = level_newest_ns - wanted;
    if (level_oldest_ns <= from_ns + bucket)
        return std::nullopt;
    if (floor_ns != 0 && floor_ns <= from_ns)
        return std::nullopt;
    return history_plan{.level = level, .from_ns = from_ns,
        .to_ns = level_oldest_ns};
}

/** @brief One observation, in the deployment's own time and value. */
struct market_point final {
    /** @brief The observation time, in nanoseconds since the Unix epoch. */
    std::int64_t at_ns = 0;

    /** @brief The observed value, and the close of a bucket. */
    double value = 0.0;

    /**
     * @brief The bucket's first value.
     *
     * Zero on a point that was not built from a bucket, which a candle reads
     * as "use the value for all four prices". Live aggregation and backfill
     * fill all of open, high and low.
     */
    double open = 0.0;

    /** @brief The highest value in the bucket. */
    double high = 0.0;

    /** @brief The lowest value in the bucket. */
    double low = 0.0;
};

}

#endif
