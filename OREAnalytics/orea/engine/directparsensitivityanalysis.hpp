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

#pragma once

#include <orea/scenario/sensitivityscenariodata.hpp>
#include <ored/marketdata/market.hpp>
#include <ored/marketdata/loader.hpp>
#include <ored/marketdata/todaysmarketparameters.hpp>
#include <ored/portfolio/portfolio.hpp>
#include <map>
#include <string>

namespace ore {
namespace analytics {

class DirectParSensitivityAnalysis {
public:
    DirectParSensitivityAnalysis(
        const QuantLib::ext::shared_ptr<ore::data::Portfolio>& portfolio,
        const QuantLib::ext::shared_ptr<ore::data::Market>& market,
        const QuantLib::ext::shared_ptr<ore::data::Loader>& loader,
        const QuantLib::ext::shared_ptr<ore::data::TodaysMarketParameters>& todaysMarketParams,
        const QuantLib::ext::shared_ptr<ore::analytics::SensitivityScenarioData>& sensiData);

    void run();

    const std::map<std::string, QuantLib::Real>& results() const { return results_; }

private:
    QuantLib::ext::shared_ptr<ore::data::Portfolio> portfolio_;
    QuantLib::ext::shared_ptr<ore::data::Market> market_;
    QuantLib::ext::shared_ptr<ore::data::Loader> loader_;
    QuantLib::ext::shared_ptr<ore::data::TodaysMarketParameters> todaysMarketParams_;
    QuantLib::ext::shared_ptr<ore::analytics::SensitivityScenarioData> sensiData_;
    std::map<std::string, QuantLib::Real> results_;
};

} // namespace analytics
} // namespace ore 