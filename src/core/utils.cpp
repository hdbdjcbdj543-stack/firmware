#include "utils.h"
#include "core/wifi/wifi_common.h" //to return MAC addr
#include "scrollableTextArea.h"
#include <globals.h>

/*********************************************************************
**  Function: backToMenu
**  sets the global var to be be used in the options second parameter
**  and returnToMenu will be user do handle the breaks of all loops

when using loopfunctions with an option to "Back to Menu", use:

add this option:
    options.push_back({"Main Menu", [=]() { backToMenu(); }});

while(1) {
    if(returnToMenu) break; // stop this loop and return to the previous loop

    ...
    loopOptions(options);
    ...
}
*/

void backToMenu() { returnToMenu = true; }

void addOptionToMainMenu() {
    returnToMenu = false;
    options.push_back({"Main Menu", backToMenu});
}

/***************************************************************************************
** Function name: getBattery()
** Description:   Returns the battery value from 1-100
***************************************************************************************/
int getBattery() {
#ifdef USE_BQ27220_VIA_I2C
    // Use BQ27220 fuel gauge for accurate battery reading
    float pct = bq.getChargePcnt();
    // Guard against library/device errors returning out-of-range values
    if (pct <= 0.0f) return 1;
    if (pct > 100.0f) return 100;
    return (int)pct;
#endif
#ifdef ANALOG_BAT_PIN
#ifndef ANALOG_BAT_MULTIPLIER
#define ANALOG_BAT_MULTIPLIER 2.0f
#endif
    static bool adcInitialized = false;
    if (!adcInitialized) {
        pinMode(ANALOG_BAT_PIN, INPUT);
        adcInitialized = true;
    }
    uint32_t adcReading = analogReadMilliVolts(ANALOG_BAT_PIN);
    float actualVoltage = (float)adcReading * ANALOG_BAT_MULTIPLIER;
    const float MIN_VOLTAGE = 3300.0f;
    const float MAX_VOLTAGE = 4150.0f;
    float percent = ((actualVoltage - MIN_VOLTAGE) / (MAX_VOLTAGE - (MIN_VOLTAGE + 50.0f))) * 100.0f;

    if (percent < 0) percent = 1;
    if (percent > 100) percent = 100;
    return (int)percent;
#endif
    return 0;
}

void updateClockTimezone() {
    timeClient.begin();
    timeClient.update();

    timeClient.setTimeOffset(bruceConfig.tmz * 3600);

    localTime = timeClient.getEpochTime() + (bruceConfig.dst ? 3600 : 0);

#if defined(HAS_RTC)
    struct tm *timeinfo = localtime(&localTime);
    RTC_TimeTypeDef TimeStruct;
    TimeStruct.Hours = timeinfo->tm_hour;
    TimeStruct.Minutes = timeinfo->tm_min;
    TimeStruct.Seconds = timeinfo->tm_sec;
    _rtc.SetTime(&TimeStruct);
    updateTimeStr(_rtc.getTimeStruct());
#else
    rtc.setTime(localTime);
    updateTimeStr(rtc.getTimeStruct());
    clock_set = true;
#endif
    // Update Internal clock to system time
    struct timeval tv = {.tv_sec = localTime};
    settimeofday(&tv, nullptr);
}

void updateTimeStr(struct tm timeInfo) {
    if (bruceConfig.clock24hr) {
        // Use 24 hour format
        snprintf(
            timeStr, sizeof(timeStr), "%02d:%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec
        );
    } else {
        // Use 12 hour format with AM/PM
        int hour12 = (timeInfo.tm_hour == 0)   ? 12
                     : (timeInfo.tm_hour > 12) ? timeInfo.tm_hour - 12
                                               : timeInfo.tm_hour;
        const char *ampm = (timeInfo.tm_hour < 12) ? "AM" : "PM";

        snprintf(
            timeStr, sizeof(timeStr), "%02d:%02d:%02d %s", hour12, timeInfo.tm_min, timeInfo.tm_sec, ampm
        );
    }
}

void showDeviceInfo() {}

#if defined(HAS_TOUCH)
/*********************************************************************
** Function: touchHeatMap
** Touchscreen Mapping, include this function after reading the touchPoint
**********************************************************************/
void touchHeatMap(struct TouchPoint t) {
    int third_x = tftWidth / 3;
    int third_y = tftHeight / 3;

    if (t.x > third_x * 0 && t.x < third_x * 1 && t.y > third_y) PrevPress = true;
    if (t.x > third_x * 1 && t.x < third_x * 2 && ((t.y > third_y && t.y < third_y * 2) || t.y > tftHeight))
        SelPress = true;
    if (t.x > third_x * 2 && t.x < third_x * 3) NextPress = true;
    if (t.x > third_x * 0 && t.x < third_x * 1 && t.y < third_y) EscPress = true;
    if (t.x > third_x * 1 && t.x < third_x * 2 && t.y < third_y) UpPress = true;
    if (t.x > third_x * 1 && t.x < third_x * 2 && t.y > third_y * 2 && t.y < third_y * 3) DownPress = true;
    /*
                        Touch area Map
                ________________________________ 0
                |   Esc   |   UP    |         |
                |_________|_________|         |_> third_y
                |         |   Sel   |         |
                |         |_________|  Next   |_> third_y*2
                |  Prev   |  Down   |         |
                |_________|_________|_________|_> third_y*3
                |__Prev___|___Sel___|__Next___| 20 pixel touch area where the touchFooter is drawn
                0         L third_x |         |
                                    Lthird_x*2|
                                              Lthird_x*3
    */
}

#endif

String getOptionsJSON() {
    String menutype = "regular_menu";
    if (menuOptionType == 0) menutype = "main_menu";
    else if (menuOptionType == 1) menutype = "sub_menu";

    String response = "{\"width\":" + String(tftWidth) + ", \"height\":" + String(tftHeight) +
                      ",\"menu\":\"" + menutype + "\",\"menu_title\":\"" + menuOptionLabel +
                      "\", \"options\":[";
    int i = 0;
    int sel = 0;
    for (auto opt : options) {
        response += "{\"n\":" + String(i) + ",\"label\":\"" + opt.label + "\"}";
        if (opt.hovered) sel = i;
        i++;
        if (i < options.size()) response += ",";
    }
    response += "], \"active\":" + String(sel) + "}";
    return response;
}

/*********************************************************************
** Function: i2c_bulk_write
** Sends múltiple registers via I2C using a compact table.
   bulk_data example..
   const uint8_t bulk_data[] = {
      2, 0x00, 0x00,       // <- datalen = 2, reg = 0x00, data = 0x00
      3, 0x01, 0x00, 0x02, // <- datalen = 3, reg = 0x01, data = 0x00, 0x02
      0 };                 // <- datalen 0 is end of data.
**********************************************************************/
void i2c_bulk_write(TwoWire *wire, uint8_t addr, const uint8_t *bulk_data) {
    const uint8_t *p = bulk_data;
    while (true) {
        uint8_t datalen = *p++;
        if (datalen == 0) { break; } // --- end of table ---
        uint8_t reg = *p++;
        wire->beginTransmission(addr);
        wire->write(reg);
        for (uint8_t i = 0; i < datalen - 1; i++) { wire->write(*p++); }
        uint8_t error = wire->endTransmission();
        if (error != 0) { log_e("I2C Write error %d", error); }
        delay(1);
    }
}

String formatTimeDecimal(uint32_t totalMillis) {
    uint16_t minutes = totalMillis / 60000;
    float seconds = (totalMillis % 60000) / 1000.0;

    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%02d:%06.3f", minutes, seconds);
    return String(buffer);
}

void printMemoryUsage(const char *msg) {
    Serial.printf(
        "%s:\nPSRAM: [Free: %lu, max alloc: %lu],\nRAM: [Free: %lu, "
        "max alloc: %lu]\n\n",
        msg,
        ESP.getFreePsram(),
        ESP.getMaxAllocPsram(),
        ESP.getFreeHeap(),
        ESP.getMaxAllocHeap()
    );
}

String repeatString(int length, String character) {
    String result = "";
    for (int i = 0; i < length; i++) { result += character; }
    return result;
}

String formatBytes(uint64_t bytes) {
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unitIndex = 0;
    float size = bytes;

    while (size >= 1024.0 && unitIndex < 4) {
        size /= 1024.0;
        unitIndex++;
    }

    if (unitIndex == 0) {
        return String(bytes) + " " + units[unitIndex];
    } else {
        return String(size, 2) + " " + units[unitIndex];
    }
}
