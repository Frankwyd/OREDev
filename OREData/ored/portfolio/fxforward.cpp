/*
 Copyright (C) 2016 Quaternion Risk Management Ltd
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

#include <boost/make_shared.hpp>
#include <ored/portfolio/builders/fxforward.hpp>
#include <ored/portfolio/enginefactory.hpp>
#include <ored/portfolio/fxforward.hpp>
#include <ored/utilities/log.hpp>
#include <ored/utilities/parsers.hpp>
#include <ored/utilities/marketdata.hpp>
#include <ored/utilities/xmlutils.hpp>
#include <ql/cashflows/simplecashflow.hpp>
#include <qle/instruments/fxforward.hpp>

namespace ore {
namespace data {

void FxForward::build(const QuantLib::ext::shared_ptr<EngineFactory>& engineFactory) {

    // ISDA taxonomy
    additionalData_["isdaAssetClass"] = string("Foreign Exchange");
    additionalData_["isdaBaseProduct"] = string(settlement_ == "Cash" ? "NDF" : "Forward");
    additionalData_["isdaSubProduct"] = string("");
    additionalData_["isdaTransaction"] = string("");  
        
    additionalData_["soldCurrency"] = soldCurrency_;
    additionalData_["boughtCurrency"] = boughtCurrency_;
    additionalData_["soldAmount"] = soldAmount_;
    additionalData_["boughtAmount"] = boughtAmount_;
    additionalData_["valueDate"] = maturityDate_;
    additionalData_["settlement"] = settlement_;

    // If you Buy EURUSD forward, then you buy EUR and sell USD.
    // EUR = foreign, USD = Domestic.
    // You pay in USD, so the Domestic / Sold ccy is the "payer" currency
    Currency boughtCcy = parseCurrency(boughtCurrency_);
    Currency soldCcy = parseCurrency(soldCurrency_);

    Currency payCcy;
    if (payCurrency_.empty()) {
        // If settlement currency is not set, set it to the domestic currency.
        payCcy = soldCcy;
    } else {
        payCcy = parseCurrency(payCurrency_);
        QL_REQUIRE(payCcy == boughtCcy || payCcy == soldCcy,
                   "Settlement currency must be either " << boughtCcy.code() << " or " << soldCcy.code() << ".");
    }

    npvCurrency_ = payCcy.code();

    // The notional and notional ccy will be set in the engine anyway,
    // but we also set this here as a default/fallback in case the engine builder fails.
    clsaSetNotionalCurrency();
    clsaSetNotional();
    /*
    if (settlement_ == "Physical") {
        notional_ = soldAmount_;
        notionalCurrency_ = soldCurrency_;
    } else {
        // for cash settled forwards we take the notional from the settlement ccy leg
        notional_ = soldCcy == payCcy ? soldAmount_ : boughtAmount_;
        notionalCurrency_ = payCcy.code();
    }
    */

    Date maturityDate = parseDate(maturityDate_);

    // Derive settlement date from payment data parameters
    Date payDate;
    if (payDate_.empty()) {
        Natural conventionalLag = 0;
        Calendar conventionalCalendar = NullCalendar();
        BusinessDayConvention conventionalBdc = Unadjusted;
        if (!fxIndex_.empty() && settlement_ == "Cash") {
            std::tie(conventionalLag, conventionalCalendar, conventionalBdc) =
                getFxIndexConventions(fxIndex_.empty() ? boughtCurrency_ + soldCurrency_ : fxIndex_);
        }
        PaymentLag paymentLag;
        if (payLag_.empty())
            paymentLag = conventionalLag;
        else
            paymentLag = parsePaymentLag(payLag_);
        Period payLag = boost::apply_visitor(PaymentLagPeriod(), paymentLag);
        Calendar payCalendar = payCalendar_.empty() ? conventionalCalendar : parseCalendar(payCalendar_);
        BusinessDayConvention payConvention =
            payConvention_.empty() ? conventionalBdc : parseBusinessDayConvention(payConvention_);
        payDate = payCalendar.advance(maturityDate, payLag, payConvention);
    } else {
        payDate = parseDate(payDate_);
        QL_REQUIRE(payDate >= maturityDate, "FX Forward settlement date should equal or exceed the maturity date.");
    }

    additionalData_["payDate"] = payDate;
    maturity_ = std::max(payDate, maturityDate);
    QuantLib::ext::shared_ptr<QuantExt::FxIndex> fxIndex;

    Date fixingDate;
    if (settlement_ == "Cash") {
        // We allow for an empty fxIndex if maturiytDate == payDate in order not to break trades that were previously
        // pricing - we should really require fxIndex whenever cash settlement is specified. If the fxIndex is not
        // given in this case, we assume that the current FX Spot rate is used to determine the settlement amount.
        if (maturityDate <= payDate && !fxIndex_.empty()) {
            Currency nonPayCcy = payCcy == boughtCcy ? soldCcy : boughtCcy;
            fxIndex = buildFxIndex(fxIndex_, nonPayCcy.code(), payCcy.code(), engineFactory->market(),
                                   engineFactory->configuration(MarketContext::pricing));
            // We also allow for an effective fixing date > payDate in order not to break trades that were preivously
            // pricing - this should be an error as well. If this is the case we assume that the current FX Spot
            // rate is used to determine the settlement amount as above.
            fixingDate = fxIndex->fixingCalendar().adjust(maturityDate);
            if (fixingDate <= payDate) {
                requiredFixings_.addFixingDate(fixingDate, fxIndex_, payDate);
            }
        } else {
            QL_REQUIRE(maturityDate >= payDate,
                       "FX settlement index must be specified for non-deliverable forward if value date ("
                           << maturityDate << ") < payDate (" << payDate << ")");
        }
    }
    if (fixingDate != Date()) {
        if (fixingDate <= payDate)
            additionalData_["fixingDate"] = fixingDate;
        else
            additionalData_["adjustedValueDate"] = fixingDate;
    }

    QL_REQUIRE(tradeActions().empty(), "TradeActions not supported for FxForward");

    DLOG("Build FxForward with maturity date " << QuantLib::io::iso_date(maturityDate) << " and pay date "
                                               << QuantLib::io::iso_date(payDate));

    // get pricing engine builder
    QuantLib::ext::shared_ptr<EngineBuilder> builder = engineFactory->builder(tradeType_);
    QL_REQUIRE(builder, "No builder found for " << tradeType_);
    QuantLib::ext::shared_ptr<FxForwardEngineBuilderBase> fxBuilder =
        QuantLib::ext::dynamic_pointer_cast<FxForwardEngineBuilderBase>(builder);

    string tmp = fxBuilder->engineParameter("includeSettlementDateFlows", {}, false, "");
    includeSettlementDateFlows_ = tmp == "" ? false : parseBool(tmp);    
    
    QuantLib::ext::shared_ptr<QuantLib::Instrument> instrument =
        QuantLib::ext::make_shared<QuantExt::FxForward>(boughtAmount_, boughtCcy, soldAmount_, soldCcy, maturityDate, false,
                                                settlement_ == "Physical", payDate, payCcy, fixingDate, fxIndex,
                                                includeSettlementDateFlows_);
    instrument_.reset(new VanillaInstrument(instrument));

    // set pricing engine
    instrument_->qlInstrument()->setPricingEngine(fxBuilder->engine(boughtCcy, soldCcy));
    setSensitivityTemplate(*fxBuilder);

    // Set up Legs
    legs_ = {{QuantLib::ext::make_shared<SimpleCashFlow>(boughtAmount_, payDate)},
             {QuantLib::ext::make_shared<SimpleCashFlow>(soldAmount_, payDate)}};
    legCurrencies_ = {boughtCurrency_, soldCurrency_};
    legPayers_ = {false, true};
}

bool FxForward::isExpired(const Date& date) {
    if (includeSettlementDateFlows_)
        return date > maturity_;
    else
        return date >= maturity_;
}
    
QuantLib::Real FxForward::notional() const {
    // try to get the notional from the additional results of the instrument
    try {
        return instrument_->qlInstrument(true)->result<Real>("currentNotional");
    } catch (const std::exception& e) {
        if (strcmp(e.what(), "currentNotional not provided"))
            ALOG("error when retrieving notional: " << e.what());
    }
    // if not provided, return original/fallback amount
    return notional_;
}

std::string FxForward::notionalCurrency() const {
    // try to get the notional ccy from the additional results of the instrument
    try {
        return instrument_->qlInstrument(true)->result<std::string>("notionalCurrency");
    } catch (const std::exception& e) {
        if (strcmp(e.what(), "notionalCurrency not provided"))
            ALOG("error when retrieving notional ccy: " << e.what());
    }
    // if not provided, return original/fallback value
    return notionalCurrency_;
}

void FxForward::fromXML(XMLNode* node) {
    Trade::fromXML(node);
    XMLNode* fxNode = XMLUtils::getChildNode(node, "FxForwardData");
    QL_REQUIRE(fxNode, "No FxForwardData Node");
    maturityDate_ = XMLUtils::getChildValue(fxNode, "ValueDate", true);
    boughtCurrency_ = XMLUtils::getChildValue(fxNode, "BoughtCurrency", true);
    soldCurrency_ = XMLUtils::getChildValue(fxNode, "SoldCurrency", true);
    boughtAmount_ = XMLUtils::getChildValueAsDouble(fxNode, "BoughtAmount", true);
    soldAmount_ = XMLUtils::getChildValueAsDouble(fxNode, "SoldAmount", true);
    settlement_ = XMLUtils::getChildValue(fxNode, "Settlement", false);
    if (settlement_ == "")
        settlement_ = "Physical";

    if (XMLNode* settlementDataNode = XMLUtils::getChildNode(fxNode, "SettlementData")) {
        payCurrency_ = XMLUtils::getChildValue(settlementDataNode, "Currency", false);
        fxIndex_ = XMLUtils::getChildValue(settlementDataNode, "FXIndex", false);
        payDate_ = XMLUtils::getChildValue(settlementDataNode, "Date", false);

        if (payDate_.empty()) {
            if (XMLNode* rulesNode = XMLUtils::getChildNode(settlementDataNode, "Rules")) {
                payLag_ = XMLUtils::getChildValue(rulesNode, "PaymentLag", false);
                payCalendar_ = XMLUtils::getChildValue(rulesNode, "PaymentCalendar", false);
                payConvention_ = XMLUtils::getChildValue(rulesNode, "PaymentConvention", false);
            }
        }
    }
}

XMLNode* FxForward::toXML(XMLDocument& doc) const {
    XMLNode* node = Trade::toXML(doc);
    XMLNode* fxNode = doc.allocNode("FxForwardData");
    XMLUtils::appendNode(node, fxNode);
    XMLUtils::addChild(doc, fxNode, "ValueDate", maturityDate_);
    XMLUtils::addChild(doc, fxNode, "BoughtCurrency", boughtCurrency_);
    XMLUtils::addChild(doc, fxNode, "BoughtAmount", boughtAmount_);
    XMLUtils::addChild(doc, fxNode, "SoldCurrency", soldCurrency_);
    XMLUtils::addChild(doc, fxNode, "SoldAmount", soldAmount_);
    XMLUtils::addChild(doc, fxNode, "Settlement", settlement_);

    XMLNode* settlementDataNode = doc.allocNode("SettlementData");
    XMLUtils::appendNode(fxNode, settlementDataNode);

    if (!payCurrency_.empty())
        XMLUtils::addChild(doc, settlementDataNode, "Currency", payCurrency_);
    if (!fxIndex_.empty())
        XMLUtils::addChild(doc, settlementDataNode, "FXIndex", fxIndex_);
    if (!payDate_.empty()) {
        XMLUtils::addChild(doc, settlementDataNode, "Date", payDate_);
    } else {
        XMLNode* rulesNode = doc.allocNode("Rules");
        XMLUtils::appendNode(settlementDataNode, rulesNode);
        if (!payLag_.empty())
            XMLUtils::addChild(doc, rulesNode, "PaymentLag", payLag_);
        if (!payCalendar_.empty())
            XMLUtils::addChild(doc, rulesNode, "PaymentCalendar", payCalendar_);
        if (!payConvention_.empty())
            XMLUtils::addChild(doc, rulesNode, "PaymentConvention", payConvention_);
    }

    return node;
}

/*
void FxForward::clsaSetNotionalCurrency_old() {
    static const std::map<std::string, int> currencyPriority = {
        {"EUR", 100}, {"GBP", 99}, {"AUD", 98}, {"NZD", 97}, 
        {"USD", 96}, {"CHF", 95}, {"CAD", 94}, {"NOK", 93},
        {"HKD", 80}, {"SGD", 79}, {"TWD", 78},
        {"CNH", 50}, {"CNY", 49},
        {"KRW", 1}, {"JPY", 0}
    };
    
    // Get priority for both currencies
    auto boughtPriority = currencyPriority.find(boughtCurrency_);
    auto soldPriority = currencyPriority.find(soldCurrency_);
    
    // If both currencies are in our priority map
    if (boughtPriority != currencyPriority.end() && soldPriority != currencyPriority.end()) {
        // Choose the currency with higher priority
        notionalCurrency_ = (boughtPriority->second >= soldPriority->second) ? 
                          boughtCurrency_ : soldCurrency_;
    } else if (boughtPriority != currencyPriority.end()) {
        // If only bought currency is in map, use it
        notionalCurrency_ = boughtCurrency_;
    } else if (soldPriority != currencyPriority.end()) {
        // If only sold currency is in map, use it
        notionalCurrency_ = soldCurrency_;
    } else {
        // If neither currency is in map, default to bought currency
        notionalCurrency_ = boughtCurrency_;
    }
}
*/

void FxForward::clsaSetNotionalCurrency() {
    // 定义货币对和对应的强势货币
    static const std::map<std::string, std::string> currencyPairPriority = {
        // USD pairs
        {"USDJPY", "USD"}, {"JPYUSD", "USD"}, {"USDCNH", "USD"}, {"CNHUSD", "USD"},
        {"USDKRW", "USD"}, {"KRWUSD", "USD"}, {"USDHKD", "USD"}, {"HKDUSD", "USD"},
        {"USDSGD", "USD"}, {"SGDUSD", "USD"}, {"USDTWD", "USD"}, {"TWDUSD", "USD"},
        
        // EUR pairs
        {"EURUSD", "EUR"}, {"USDEUR", "EUR"}, {"EURJPY", "EUR"}, {"JPYEUR", "EUR"},
        {"EURGBP", "EUR"}, {"GBPEUR", "EUR"}, {"EURAUD", "EUR"}, {"AUDEUR", "EUR"},
        {"EURNZD", "EUR"}, {"NZDEUR", "EUR"}, {"EURCHF", "EUR"}, {"CHFEUR", "EUR"},
        
        // GBP pairs
        {"GBPUSD", "GBP"}, {"USDGBP", "GBP"}, {"GBPJPY", "GBP"}, {"JPYGBP", "GBP"},
        {"GBPAUD", "GBP"}, {"AUDGBP", "GBP"}, {"GBPNZD", "GBP"}, {"NZDGBP", "GBP"},
        {"GBPCHF", "GBP"}, {"CHFGBP", "GBP"},
        
        // AUD pairs
        {"AUDUSD", "AUD"}, {"USDAUD", "AUD"}, {"AUDJPY", "AUD"}, {"JPYAUD", "AUD"},
        {"AUDNZD", "AUD"}, {"NZDAUD", "AUD"}, {"AUDCHF", "AUD"}, {"CHFAUD", "AUD"},
        
        // NZD pairs
        {"NZDUSD", "NZD"}, {"USDNZD", "NZD"}, {"NZDJPY", "NZD"}, {"JPYNZD", "NZD"},
        {"NZDCHF", "NZD"}, {"CHFNZD", "NZD"},
        
        // CHF pairs
        {"USDCHF", "USD"}, {"CHFUSD", "USD"}, {"CHFJPY", "CHF"}, {"JPYCHF", "CHF"}
    };
    
    // 组合买入和卖出货币形成货币对
    std::string currencyPair = boughtCurrency_ + soldCurrency_;
    
    // 在映射表中查找货币对
    auto it = currencyPairPriority.find(currencyPair);
    
    if (it != currencyPairPriority.end()) {
        // 如果找到对应的货币对，使用映射的强势货币
        notionalCurrency_ = it->second;
        //DLOG("Currency pair " << currencyPair << " found in priority map. Setting notional currency to " << notionalCurrency_);
    } else {
        // 如果没有找到对应的货币对，默认使用买入货币
        notionalCurrency_ = boughtCurrency_;
        
        // 使用 ALOG 记录警告信息
        ALOG("Warning: Currency pair " << currencyPair << " not found in priority map. "
             << "Defaulting to bought currency " << boughtCurrency_ << " as notional currency. "
             << "Consider adding this currency pair to the priority map.");
    }
}
void FxForward::clsaSetNotional() {
    // Set notional based on the notional currency
    if (notionalCurrency_ == boughtCurrency_) {
        // If notional currency matches bought currency, use bought amount
        notional_ = std::abs(boughtAmount_);
    } else if (notionalCurrency_ == soldCurrency_) {
        // If notional currency matches sold currency, use sold amount
        notional_ = std::abs(soldAmount_);
    } else {
        // If notional currency doesn't match either (shouldn't happen after clsaSetNotionalCurrency),
        // default to bought amount as fallback
        notional_ = std::abs(boughtAmount_);
        ALOG("Warning: notionalCurrency_ " << notionalCurrency_ << " matches neither boughtCurrency_ " 
             << boughtCurrency_ << " nor soldCurrency_ " << soldCurrency_ 
             << ". Defaulting to boughtAmount.");
    }
}

} // namespace data
} // namespace ore
