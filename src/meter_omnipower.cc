/*
 Copyright (C) 2018-2019 Fredrik Öhrström

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include"dvparser.h"
#include"meters.h"
#include"meters_common_implementation.h"
#include"wmbus.h"
#include"wmbus_utils.h"
#include"util.h"

#include<map>
#include<memory.h>
#include<stdio.h>
#include<string>
#include<time.h>
#include<vector>

struct MeterOmnipower : public virtual ElectricityMeter, public virtual MeterCommonImplementation {
    MeterOmnipower(WMBus *bus, string& name, string& id, string& key);

    double totalEnergyConsumption();
    double currentPowerConsumption();
    double totalEnergyProduction();
    double currentPowerProduction();

    void printMeter(Telegram *t,
                    string *human_readable,
                    string *fields, char separator,
                    string *json,
                    vector<string> *envs);

private:
    void handleTelegram(Telegram *t);
    void processContent(Telegram *t);

    double total_energy_ {};
    double current_power_ {};
};

MeterOmnipower::MeterOmnipower(WMBus *bus, string& name, string& id, string& key) :
    MeterCommonImplementation(bus, name, id, key, OMNIPOWER_METER, MANUFACTURER_KAM, LinkMode::C1)
{
    addMedia(0x02);
    MeterCommonImplementation::bus()->onTelegram(calll(this,handleTelegram,Telegram*));
}

double MeterOmnipower::totalEnergyConsumption()
{
    return total_energy_;
}

double MeterOmnipower::currentPowerConsumption()
{
    return current_power_;
}

double MeterOmnipower::totalEnergyProduction()
{
    return 0.0;
}

double MeterOmnipower::currentPowerProduction()
{
    return 0.0;
}

void MeterOmnipower::handleTelegram(Telegram *t) {

    if (!isTelegramForMe(t)) {
        // This telegram is not intended for this meter.
        return;
    }

    verbose("(omnipower) %s %02x%02x%02x%02x ",
            name().c_str(),
            t->a_field_address[0], t->a_field_address[1], t->a_field_address[2],
            t->a_field_address[3]);

    t->expectVersion("omnipower", 0x01);

    if (t->isEncrypted() && !useAes() && !t->isSimulated()) {
        warning("(omnipower) warning: telegram is encrypted but no key supplied!\n");
    }
    if (useAes()) {
        vector<uchar> aeskey = key();
        decryptMode5_AES_CBC(t, aeskey);
    } else {
        t->content = t->payload;
    }
    logTelegram("(omnipower) log", t->parsed, t->content);
    int content_start = t->parsed.size();
    processContent(t);
    if (isDebugEnabled()) {
        t->explainParse("(omnipower)", content_start);
    }
    triggerUpdate(t);
}

void MeterOmnipower::processContent(Telegram *t)
{
    // Meter record:
    // 04 dif (32 Bit Integer/Binary Instantaneous value)
    // 83 vif (Energy Wh)
    // 3b vife (Forward flow contribution only)
    // xx xx xx xx (total energy)

    map<string,pair<int,DVEntry>> values;
    parseDV(t, t->content, t->content.begin(), t->content.size(), &values);

    int offset;
    extractDVdouble(&values, "04833B", &offset, &total_energy_);
    t->addMoreExplanation(offset, " total power (%f kwh)", total_energy_);
}

unique_ptr<ElectricityMeter> createOmnipower(WMBus *bus, string& name, string& id, string& key)
{
    return unique_ptr<ElectricityMeter>(new MeterOmnipower(bus,name,id,key));
}

void MeterOmnipower::printMeter(Telegram *t,
                                string *human_readable,
                                string *fields, char separator,
                                string *json,
                                vector<string> *envs)
{

    char buf[65536];
    buf[65535] = 0;

    snprintf(buf, sizeof(buf)-1, "%s\t%s\t% 3.3f kwh\t% 3.3f kwh\t%s",
             name().c_str(),
             t->id.c_str(),
             totalEnergyConsumption(),
             currentPowerConsumption(),
             datetimeOfUpdateHumanReadable().c_str());

    *human_readable = buf;

    snprintf(buf, sizeof(buf)-1, "%s%c%s%c%f%c%f%c%s",
             name().c_str(), separator,
             t->id.c_str(), separator,
             totalEnergyConsumption(), separator,
             currentPowerConsumption(), separator,
             datetimeOfUpdateRobot().c_str());

    *fields = buf;

#define Q(x,y) "\""#x"\":"#y","
#define QS(x,y) "\""#x"\":\""#y"\","
#define QSE(x,y) "\""#x"\":\""#y"\""

    snprintf(buf, sizeof(buf)-1, "{"
             QS(media,electricity)
             QS(meter,omnipower)
             QS(name,%s)
             QS(id,%s)
             Q(total_energy_consumption_kwh,%f)
             Q(current_power_consumption_kw,%f)
             QSE(timestamp,%s)
             "}",
             name().c_str(),
             t->id.c_str(),
             totalEnergyConsumption(),
             currentPowerConsumption(),
             datetimeOfUpdateRobot().c_str());

    *json = buf;

    envs->push_back(string("METER_JSON=")+*json);
    envs->push_back(string("METER_TYPE=omnipower"));
    envs->push_back(string("METER_ID=")+t->id);
    envs->push_back(string("METER_TOTAL_ENERGY_CONSUMPTION_KWH=")+to_string(totalEnergyConsumption()));
    envs->push_back(string("METER_CURRENT_POWER_CONSUMPTION_KW=")+to_string(currentPowerConsumption()));
    envs->push_back(string("METER_TIMESTAMP=")+datetimeOfUpdateRobot());
}
