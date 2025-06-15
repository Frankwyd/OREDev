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

/*! \file orea/engine/directparsensitivityinstrumentbuilder.hpp
    \brief A builder for par instruments for direct sensitivity analysis
    \ingroup engine
*/

#pragma once

#include <ored/marketdata/loader.hpp>
#include <ored/marketdata/market.hpp>
#include <ored/marketdata/todaysmarketparameters.hpp>
#include <orea/scenario/scenariogenerator.hpp> // For RiskFactorKey

namespace ore {
namespace analytics {

//! Direct Par Sensitivity Instrument Builder
/*!
  This class is responsible for creating a new Market object that is consistent
  with a shift applied to a single par-instrument's quote. It is used for the
  direct par sensitivity calculation method.
*/
class DirectParSensitivityInstrumentBuilder {
public:
    DirectParSensitivityInstrumentBuilder(
        const QuantLib::ext::shared_ptr<ore::data::Loader>& loader,
        const QuantLib::ext::shared_ptr<ore::data::TodaysMarketParameters>& todaysMarketParams);

    QuantLib::ext::shared_ptr<ore::data::Market> build(const std::string& curveName,
                                                     const QuantLib::Period& tenor,
                                                     QuantLib::Real shiftSize,
                                                     const QuantLib::Date& asofDate);

private:
    QuantLib::ext::shared_ptr<ore::data::Loader> loader_;
    QuantLib::ext::shared_ptr<ore::data::TodaysMarketParameters> todaysMarketParams_;
};

} // namespace analytics
} // namespace ore
