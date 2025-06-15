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

#include <orea/engine/directparsensitivityanalysis.hpp>
#include <orea/engine/directparsensitivityinstrumentbuilder.hpp>
#include <ored/portfolio/enginefactory.hpp>
#include <ored/portfolio/trade.hpp>
#include <ored/utilities/log.hpp>
#include <ored/utilities/parsers.hpp>
#include <ored/utilities/to_string.hpp>
#include <ql/errors.hpp>

namespace ore {
namespace analytics {

DirectParSensitivityAnalysis::DirectParSensitivityAnalysis(
    const QuantLib::ext::shared_ptr<ore::data::Portfolio>& portfolio,
    const QuantLib::ext::shared_ptr<ore::data::Market>& market,
    const QuantLib::ext::shared_ptr<ore::data::Loader>& loader,
    const QuantLib::ext::shared_ptr<ore::data::TodaysMarketParameters>& todaysMarketParams,
    const QuantLib::ext::shared_ptr<ore::analytics::SensitivityScenarioData>& sensiData)
    : portfolio_(portfolio), market_(market), loader_(loader), todaysMarketParams_(todaysMarketParams), sensiData_(sensiData) {}

void DirectParSensitivityAnalysis::run() {
    DLOG("DirectParSensitivityAnalysis started");
    results_.clear();

    // 1. Calculate base NPV
    auto engineData = QuantLib::ext::make_shared<ore::data::EngineData>();
    auto engineFactory = QuantLib::ext::make_shared<ore::data::EngineFactory>(engineData, market_);
    portfolio_->build(engineFactory);
    Real baseNpv = 0.0;
    for (const auto& [tradeId, trade] : portfolio_->trades()) {
        baseNpv += trade->instrument()->NPV();
    }
    DLOG("Base NPV calculated: " << baseNpv);

    // 2. Create instrument builder with our loader
    auto instrumentBuilder = QuantLib::ext::make_shared<DirectParSensitivityInstrumentBuilder>(loader_, todaysMarketParams_);

    auto calculateSensi = [&](const std::string& curveName, const QuantLib::Period& tenor, Real shift, const std::string& suffix) {
        std::string scenarioName = curveName + "_" + ore::data::to_string(tenor) + "_" + suffix;
        DLOG("Running scenario: " << scenarioName);

        auto shiftedMarket = instrumentBuilder->build(curveName, tenor, shift, market_->asofDate());

        auto shiftedPortfolio = QuantLib::ext::make_shared<ore::data::Portfolio>(*portfolio_);
        auto shiftedEngineFactory = QuantLib::ext::make_shared<ore::data::EngineFactory>(engineData, shiftedMarket);
        shiftedPortfolio->build(shiftedEngineFactory);

        Real shiftedNpv = 0.0;
        for (const auto& [tradeId, trade] : shiftedPortfolio->trades()) {
            shiftedNpv += trade->instrument()->NPV();
        }
        
        results_[scenarioName] = shiftedNpv - baseNpv;
        DLOG("Scenario " << scenarioName << " completed. ShiftedNPV=" << shiftedNpv << ", Delta=" << results_[scenarioName]);
    };

    // 3. Loop over all par shift scenarios
    for (const auto& shiftDataPair : sensiData_->yieldCurveShiftData()) {
        const auto& curveName = shiftDataPair.first;
        const auto& shiftData = shiftDataPair.second;

        for (const auto& tenor : shiftData->shiftTenors) {
            auto scheme = shiftData->shiftScheme;
            auto shiftSize = shiftData->shiftSize;

            if (scheme == ore::analytics::ShiftScheme::Forward || scheme == ore::analytics::ShiftScheme::Central) {
                calculateSensi(curveName, tenor, shiftSize, "up");
            }
            if (scheme == ore::analytics::ShiftScheme::Backward || scheme == ore::analytics::ShiftScheme::Central) {
                calculateSensi(curveName, tenor, -shiftSize, "down");
            }
        }
    }

    DLOG("DirectParSensitivityAnalysis finished");
}

} // namespace analytics
} // namespace ore 