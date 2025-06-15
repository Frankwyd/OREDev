/*
 Copyright (C) 2024 Quaternion Risk Management Ltd
 All rights reserved.

 This file is part of ORE, a free-software/open-source library
 for transparent pricing and risk analysis - http://opensourcerisk.org

 ORE is free software: you can redistribute it and/or modify it
 under the terms of the Modified BSD License.  You should have received a
 copy of the license along with this program.
 The license is also available online at <http://opensourcerisk.org>

 This program is distributed on the basis that it will form a useful
 contribution to risk analytics and model standardisation, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 FITNESS FOR A PARTICULAR PURPOSE. See the license for more details.
*/

#include <orea/engine/directparsensitivityinstrumentbuilder.hpp>
#include <ored/marketdata/inmemoryloader.hpp>
#include <ored/marketdata/todaysmarket.hpp>
#include <ored/configuration/curveconfigurations.hpp>
#include <ored/utilities/log.hpp>
#include <ored/utilities/parsers.hpp>
#include <ored/utilities/to_string.hpp>
#include <ql/errors.hpp>

namespace ore {
namespace analytics {

DirectParSensitivityInstrumentBuilder::DirectParSensitivityInstrumentBuilder(
    const QuantLib::ext::shared_ptr<ore::data::Loader>& loader,
    const QuantLib::ext::shared_ptr<ore::data::TodaysMarketParameters>& todaysMarketParams)
    : loader_(loader), todaysMarketParams_(todaysMarketParams) {}

QuantLib::ext::shared_ptr<ore::data::Market> DirectParSensitivityInstrumentBuilder::build(
    const std::string& curveName, const QuantLib::Period& tenor, QuantLib::Real shiftSize, const QuantLib::Date& asofDate) {

    // 1. Get the original market data from the injected loader.
    auto marketData = loader_->loadQuotes(asofDate);

    // 2. Create a new InMemoryLoader with a copy of the original data.
    auto newLoader = QuantLib::ext::make_shared<ore::data::InMemoryLoader>();
    for(const auto& md : marketData) {
        newLoader->add(asofDate, md->name(), md->quote()->value());
    }

    // 3. Find the specific quote to shift and update it in the new loader.
    std::string tenorStr = ore::data::to_string(tenor);
    
    // We need to find the correct quote name associated with the curve and tenor.
    auto curveConfigs = QuantLib::ext::make_shared<ore::data::CurveConfigurations>(); // Assuming we can create this
    auto curveConfig = curveConfigs->yieldCurveConfig(curveName);
    QL_REQUIRE(curveConfig, "Curve configuration for " << curveName << " not found.");

    std::string quoteName;
    for (const auto& segment : curveConfig->curveSegments()) {
        for (const auto& q : segment->quotes()) {
            // This is a simplification. We assume the quote's description contains the tenor.
            if (q.first.find(tenorStr) != std::string::npos) {
                quoteName = q.first;
                break;
            }
        }
        if (!quoteName.empty()) break;
    }
    QL_REQUIRE(!quoteName.empty(), "Quote for " << curveName << " with tenor " << tenorStr << " not found.");

    // Find the original datum to get its current value.
    auto originalDatum = loader_->get(quoteName, asofDate);
    QL_REQUIRE(originalDatum, "MarketDatum for " << quoteName << " not found in original loader.");

    // Create the new shifted datum.
    // Add (overwrite) the shifted datum in our new loader.
    newLoader->add(asofDate, quoteName, originalDatum->quote()->value() + shiftSize);
    DLOG("Applied shift of " << shiftSize << " to " << quoteName);

    // 4. Build a new TodaysMarket with the shifted data.
    auto shiftedMarket = QuantLib::ext::make_shared<ore::data::TodaysMarket>(
        asofDate,
        todaysMarketParams_,
        newLoader,
        curveConfigs
    );

    return shiftedMarket;
}

} // namespace analytics
} // namespace ore 