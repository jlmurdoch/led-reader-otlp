/*
 * This example is *specifically* written for the Adafruit QT Py ESP32-S2.
 *
 * Install the ESP32 board library for the Adafruit QT Py ESP32-S2: 
 * - Under File > Preferences... > Settings > Additional boards manager URLs:
 * - Paste: https://github.com/espressif/arduino-esp32/blob/master/package.json
 *
 * Also the user must recompile the NanoPB libraries against the latest OpenTelemetry Proto available at:
 * https://github.com/open-telemetry/opentelemetry-proto 
 *
 * Enable "USB CDC On Boot" to allow Serial console output to work.
 */

#include <SPI.h>
#include "src/connection_details.h"
#include "src/hwclock/hwclock.h"
#include "src/neopixelSPI/neopixelSPI.h"
#include "src/otel-protobuf/otel-protobuf.h"
#include "src/send-protobuf/send-protobuf.h"

// Average blink time is 5ms (4680us max observed on a Honeywell AS302P)
struct LEDStatus {
    const int sensorPin;
    // Set to true when there's data to be sent
    volatile bool unseenChange;
    // 64-bit to align with OpenTelemetry value size
    volatile unsigned long long blinkCount;
};

// Use the RX pin on the QT Py and reset the counter and change flag
LEDStatus ledStatus = { RX, false, 0 };

// Use SPI to set Neopixel LED
SPIClass *SPI1 = NULL;

// For locking with interrupts
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// Interrupt handler to process a rising pulse:
// - increment the counter
// - set a flag
void IRAM_ATTR blinkResponseRising() {
    // Increment and set the unseen flag in a protective fashion
    portENTER_CRITICAL_ISR(&mux);
    ledStatus.blinkCount++;
    ledStatus.unseenChange = true;
    portEXIT_CRITICAL_ISR(&mux);
}

void processMetric(uint64_t value) {
    uint8_t payloadData[MAX_PROTOBUF_BYTES] = { 0 };

    // Create the data store with data structure (default)
    Resourceptr ptr = NULL;
    ptr = addOteldata();

    // Example load metric
    addResAttr(ptr, "service.name", "jlm-home");

    uint64_t *countervalue = (uint64_t *)malloc(sizeof(uint64_t));
    *countervalue = value;
    addMetric(ptr, "blinks", "SmartMeter Blink Count", "1", METRIC_SUM, AGG_CUMULATIVE, 1);
    addDatapoint(ptr, AS_INT, countervalue);
    addDpAttr(ptr,"sensor","DIY Sensor");

    // for debug to show whats in the payload
    printOteldata(ptr);
    
    size_t payloadSize = buildProtobuf(ptr, payloadData, MAX_PROTOBUF_BYTES);
    // Send the data if there's something there
    if(payloadSize > 0) {
      // Define OTEL_SSL if SSL is required
      sendProtobuf(OTEL_HOST, OTEL_PORT, OTEL_URI, OTEL_XSFKEY, payloadData, payloadSize);
    } 

    // Free the data store
    freeOteldata(ptr);
}

void setup() {
  // Send output on serial console - need to enable CDC on boot to work
  Serial.begin(115200);

  // Connect to WiFi
  WiFi.begin(WIFI_SSID, WIFI_PSK);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("...Waiting for WiFi..."));
    delay(5000);
  }
  Serial.println(F("WiFi Connected"));

  // Set up interrupt to receive a rising pulse from the phototransistor
  pinMode(digitalPinToInterrupt(ledStatus.sensorPin), INPUT_PULLDOWN);
  attachInterrupt(ledStatus.sensorPin, blinkResponseRising, RISING);

  // Neopixel hacked using SPI: SCK(36), MISO(37), MOSI(35), SS(34)
  SPI1 = new SPIClass(FSPI);
  SPI1->begin(SCK, MISO, PIN_NEOPIXEL, SS);

  // NTP is required for OpenTelemetry
  setHWClock(NTP_HOST);
  Serial.println(F("NTP Synced"));
}

void loop() {
  // If we see an change in value, caused by interrupt
  if (ledStatus.unseenChange == true) {
    // Reset the "seen" flag in a protective fashion
    portENTER_CRITICAL(&mux);
    ledStatus.unseenChange = false;
    portEXIT_CRITICAL(&mux);
        
    // Turn on the Neopixel LED using SPI
    setNeopixelOn(SPI1);

    // OpenTelemetry output
    processMetric(ledStatus.blinkCount);

    // Serial output
    Serial.println(ledStatus.blinkCount);

    // Turn off the Neopixel LED using SPI
    setNeopixelOff(SPI1);
  }
  
  // Check every 10 secs
  delay(10000);
}
