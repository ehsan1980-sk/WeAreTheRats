// #define TOM

#define TSFLOW
#define BNO085
// #define IMU_USE_RESET
#define IMU_USE_INT
// #include "LSM6DS3.h"
#define FEATURE_INERTIA_SCROLL

#ifdef BNO085
#include "Adafruit_BNO08x.h"
#endif
// #include <Adafruit_Sensor.h>
#include <Wire.h>

#include <bluefruit.h>
// #include <MadgwickAHRS.h>  // Madgwick 1.2.0 by Arduino

#ifdef TSFLOW
#include <TensorFlowLite.h>
#include <tensorflow/lite/micro/all_ops_resolver.h>
#include <tensorflow/lite/micro/micro_error_reporter.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/schema/schema_generated.h>
#endif
#include "battery.h"
#include "local_constants.h"
#ifdef TSFLOW
#include "model.h"
#endif
#include "system.h"
void setBdDAAndName(unsigned char byte3, char *name);

const float accelerationThreshold = 2.5; // threshold of significant in G's

int samplesRead = 0;
#define out_samples 150
int tensorIndex = 0;
#define accl_min -30.0
#define accl_max 30.0
#define gyro_min -15.0
#define gyro_max 15.0
#define roto_min -3.0
#define roto_max 2.0

int deviceMode;
BLEDis bledis;
BLEHidAdafruit blehid;

int deviceId = 0;
unsigned addrByte3;

#ifdef FEATURE_INERTIA_SCROLL
bool inertiaScroll = false;
#define INERTIA_SCROLL_DOWN 0
#define INERTIA_SCROLL_UP 1
bool inertiaScrollDirection = INERTIA_SCROLL_DOWN;
int inertiaScrollLastTimeStamp;
#endif

#ifdef TOM
// Central uart client
BLEClientUart clientUart;
#endif

#ifdef BNO085
// New data available.  currently for keyboard, new data available every 10ms;
// for mouse, every 20ms
bool newData = false;

// Rotation Vector. i, j, k, real
float rtVector[4];

// Linear acceleration, x, y, z
float accl[3];

// gyro, x, y, z
float gyro[3];

// calibration status
int calStatus;

#define BNO08X_RESET -1
struct euler_t {
  float yaw;
  float pitch;
  float roll;
} ypr, ypr0;

Adafruit_BNO08x bno08x(BNO08X_RESET);
sh2_SensorValue_t sensorValue;
// sh2_SensorId_t reportType = SH2_ROTATION_VECTOR; // SH2_ARVR_STABILIZED_RV;
// long reportIntervalUs = 20000;
void setReports() {
  int dataRate;
  if (deviceMode == DEVICE_MOUSE_MODE) {
    dataRate = 20 * 1000;
  } else {
    dataRate = 10 * 1000;
  }

  if (!bno08x.enableReport(SH2_ROTATION_VECTOR, dataRate)) {
    Serial.println("Could not enable rotation vector");
  }
  if (!bno08x.enableReport(SH2_LINEAR_ACCELERATION, dataRate)) {
    Serial.println("Could not enable linear acceleration");
  }
  if (!bno08x.enableReport(SH2_GYROSCOPE_CALIBRATED, dataRate)) {
    Serial.println("Could not enable gyroscope");
  }
}

void quaternionToEuler(float qi, float qj, float qk, float qr, euler_t *ypr,
                       bool degrees = false) {

  float sqr = sq(qr);
  float sqi = sq(qi);
  float sqj = sq(qj);
  float sqk = sq(qk);

  ypr->yaw = atan2(2.0 * (qi * qj + qk * qr), (sqi - sqj - sqk + sqr));
  ypr->pitch = asin(-2.0 * (qi * qk - qj * qr) / (sqi + sqj + sqk + sqr));
  ypr->roll = atan2(2.0 * (qj * qk + qi * qr), (-sqi - sqj + sqk + sqr));

  if (degrees) {
    ypr->yaw *= RAD_TO_DEG;
    ypr->pitch *= RAD_TO_DEG;
    ypr->roll *= RAD_TO_DEG;
  }
}
#endif

#ifdef TSFLOW
// global variables used for TensorFlow Lite (Micro)
tflite::MicroErrorReporter tflErrorReporter;

// pull in all the TFLM ops, you can remove this line and
// only pull in the TFLM ops you need, if would like to reduce
// the compiled size of the sketch.
tflite::AllOpsResolver tflOpsResolver;

const tflite::Model *tflModel = nullptr;
tflite::MicroInterpreter *tflInterpreter = nullptr;
TfLiteTensor *tflInputTensor = nullptr;
TfLiteTensor *tflOutputTensor = nullptr;

// Create a static memory buffer for TFLM, the size may need to
// be adjusted based on the model you are using
constexpr int tensorArenaSize = 180 * 1024;
byte tensorArena[tensorArenaSize] __attribute__((aligned(16)));
#endif

// array to map gesture index to a name
const char *GESTURES = "abcdefghijklmnopqrstuvwxyz";

#define NUM_GESTURES 26

// const uint8_t BLEUART_UUID_SERVICE[] =
// {
//     0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
//     0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E
// };
void setup() {
  configGpio();
  Serial.begin(115200);
  int i = 0;
  // while (!Serial) {
  //   digitalWrite(LED_RED, LIGHT_ON);
  //   delay(10);
  //   // if (++i > 1000) {
  //   //   break;
  //   // }
  // }
  digitalWrite(LED_RED, LIGHT_OFF);

#ifdef TSFLOW
  loadTFLiteModel();
#endif

  initAndStartBLE();

#ifdef BNO085
  // Try to initialize!
  if (!bno08x.begin_I2C()) {
    // if (!bno08x.begin_UART(&Serial1)) {  // Requires a device with > 300 byte
    // UART buffer! if (!bno08x.begin_SPI(BNO08X_CS, BNO08X_INT)) {
    Serial.println("Failed to find BNO08x chip");
    systemHaltWithledPattern(LED_RED, 3);
  }
  Serial.println("BNO08x Found!");

  setReports();
  delay(100);
#endif

  // calibrateIMU(250, 250);

  deviceMode = DEVICE_MOUSE_MODE;

  // nrf_gpio_cfg_sense_input(g_ADigitalPinMap[IMU_INT],
  //                        NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);

  //
  // attachInterrupt(IMU_INT, myinthandler, FALLING); // RISING
}

int count = 0;
#define report_freq 1
bool inference_started = false;

#define PRECISION 4
bool startedChar = false;
int t1 = 0;
int ledCount;
bool needSendKeyRelease = false;
float xAngle, yAngle, lastXAngle, lastYAngle;

#define pi 3.14159265358979323846
float calRotation(float x, float x0) {
  float tmp1;
  tmp1 = x - x0;
  if (tmp1 > pi) {
    tmp1 -= pi * 2;
  }
  if (tmp1 < -pi) {
    tmp1 += pi * 2;
  }
  return tmp1;
}

void loop() {

  ledCount++;
  // pluse the green led to indicate system alive.
  if (ledCount % 1000 < 30) {
    if (deviceMode == DEVICE_MOUSE_MODE) {
      digitalWrite(LED_GREEN, LIGHT_ON);
      digitalWrite(LED_BLUE, LIGHT_OFF);
    } else {
      digitalWrite(LED_BLUE, LIGHT_ON);
      digitalWrite(LED_GREEN, LIGHT_OFF);
    }

  } else {
    if (digitalRead(MOUSE_ACTIVATE) == HIGH) {
      digitalWrite(LED_GREEN, LIGHT_ON);
    } else {
      digitalWrite(LED_GREEN, LIGHT_OFF);
    }
    if (digitalRead(KEYPAD_ACTIVATE) == HIGH) {
      digitalWrite(LED_BLUE, LIGHT_ON);
    } else {
      digitalWrite(LED_BLUE, LIGHT_OFF);
    }
  }
  scanNavigateButtons();
  scanClickButtons();
#ifdef FEATURE_INERTIA_SCROLL

  if (inertiaScroll) {
    if (millis() - inertiaScrollLastTimeStamp > 200) {
      inertiaScrollLastTimeStamp = millis();

      if (inertiaScrollDirection == INERTIA_SCROLL_DOWN) {
        blehid.mouseScroll(3);
      } else {
        blehid.mouseScroll(-3);
      }
      Serial.println("scroll");
    };
  }
#endif
  // When a key is pressed, tow events shall be generated, KEY_UP and KEY_DOWN.
  // For air writing, when a character is recoganized, only KEY_DOWN event is
  // sent. so I need generate a KEY_UP event. needSendKeyRelease is used for the
  // purpose. At the end of air writing, needSendKeyRelease is set.  In next
  // round loop(), here I send the KEY_UP event and reset the flag.
  if (needSendKeyRelease) {
    needSendKeyRelease = false;
    blehid.keyRelease();
  }

#ifdef BNO085
  // BNO085 pull IMU_INT LOW when data is ready
  // so do nothing in case of IMU_INT high
#ifdef IMU_USE_INT
  if (digitalRead(IMU_INT) == HIGH) {
    return;
    // systemSleep();
  }
#endif
  static uint32_t last = 0;
  long now = micros();
  if (bno08x.getSensorEvent(&sensorValue)) {
  }

  if (newData) {
    uint32_t now = micros();
    newData = false;
    // Serial.print(now - last);
    // Serial.print("\t");
    // last = now;
    // Serial.print(calStatus);
    // Serial.print("\t");
    // // This is accuracy in the range of 0 to 3
    // int i;
    // for (i = 0; i < 4; i++) {
    //   Serial.print("\t");
    //   Serial.print(rtVector[i]);
    // }
    // for (i = 0; i < 3; i++) {
    //   Serial.print("\t");
    //   Serial.print(accl[i]);
    // }
    // for (i = 0; i < 3; i++) {
    //   Serial.print("\t");
    //   Serial.print(gyro[i]);
    // }
    // Serial.println("");

    quaternionToEuler(rtVector[0], rtVector[1], rtVector[2], rtVector[3], &ypr,
                      true);
    xAngle = -ypr.yaw;
    yAngle = ypr.roll;
  }

#endif

  if (deviceMode == DEVICE_MOUSE_MODE) {
    if (count == report_freq - 1) {
      lastXAngle = xAngle;
      lastYAngle = yAngle;
    }
    count++;
    int32_t x;
    int32_t y;
    // digitalWrite(DEBUG_3, HIGH);

    // We do not want to overload the BLE link.
    // so here we send 1 report every (report_freq * 10ms) = 30ms
    if (count % report_freq == 0) {
      x = (xAngle - lastXAngle) * SENSITIVITY_X;

      // xAngle go back to 0 after pass 360 degrees. so here we need add the
      // offsets.
      if (x < -180 * SENSITIVITY_X) {
        x += 360 * SENSITIVITY_X;
      }

      y = (yAngle - lastYAngle) * SENSITIVITY_Y;

      // get rid of movement due to noise.
      if (abs(x) > 8 || abs(y) > 8) {
        // if (abs(accelX) + abs(accelY) + abs(accelZ) > 1.5) {
        // mousePosition(x, y);
        // if (abs(x) < 10) x = 0;
        // if (abs(y) < 10) y = 0;
        // Serial.print(x);
        // Serial.print(",");
        // Serial.print(-y);
        // Serial.print(orientationData.orientation.pitch);
        // // Serial.print(",");
        // Serial.print(",  ");
        // Serial.print(orientationData.orientation.roll);
        // Serial.print(",");
        // Serial.println(orientationData.orientation.pitch);
        // Serial.print(",");

        if (digitalRead(MOUSE_ACTIVATE) == HIGH) {
          blehid.mouseMove(x, -y);
        }
        lastXAngle = xAngle;
        lastYAngle = yAngle;
      }
    }
    // digitalWrite(DEBUG_3, LOW);

    return;
  }

  /*****  Below is for Keyboard  ******/

  // Device in Keyboard mode

  // Capture has not started, ignore until user activate keypad
  if (!startedChar) {
    if (digitalRead(KEYPAD_ACTIVATE) == LOW) {
      samplesRead = -1;
      return;
    } else {
      // User activate keypad, check whether 2s passed since last capture
      // int currentTime = millis();
      // if (currentTime < t1 + 2000) {
      //   return;
      // }
      // t1 = currentTime;
      startedChar = true;
      samplesRead = -1;
    }
  }

  // User finger is on keyboard_activation pad
  // To begin, wait 200ms
  // delay(200);

  // Loop to read 20 samples, at 100Hz, takes 200ms
  // This is better than delay, clear up data in IMU.
  for (int i = 0; i < 20;) {
    while (digitalRead(IMU_INT) == HIGH) {
    }
    if (bno08x.getSensorEvent(&sensorValue)) {
    }
    if (newData) {
      i++;
      newData = false;
    }
  }

  // Keep sampling until user release the ACTIVATE button
  while (true) {

    // User deactivated keypad
    if (digitalRead(KEYPAD_ACTIVATE) == LOW) {
      startedChar = false;
      inference_started = true;
      break;
    }

    if (samplesRead >= out_samples) {
      // Wait for user release the button
      while (digitalRead(KEYPAD_ACTIVATE) == HIGH)
        ;
      startedChar = false;
      inference_started = true;
      break;
    }

// BNO085 pull IMU_INT LOW when data is ready
// so do nothing in case of IMU_INT high
#ifdef IMU_USE_INT
    while (digitalRead(IMU_INT) == HIGH) {
    }
#endif
    if (bno08x.getSensorEvent(&sensorValue)) {
    }

    if (newData) {
      uint32_t now = micros();
      newData = false;

      // Wait for hand to rest
      if (samplesRead == -1) {
        if (abs(accl[0]) + abs(accl[1]) + abs(accl[2]) > 1) {
          // Serial.println("wait idle");
          Serial.print("<");
          continue;
        }
        digitalWrite(LED_BLUE, LIGHT_ON);
        samplesRead = 0;
        continue;
      }

      // wait for hand to move
      if (samplesRead == 0) {
        if (abs(accl[0]) + abs(accl[1]) + abs(accl[2]) < 2) {
          // Serial.println("wait move");
          Serial.print(">");
          continue;
        }
        tensorIndex = 0;
      }

      // Capture samples until keyboard_activation is release.
      quaternionToEuler(rtVector[0], rtVector[1], rtVector[2], rtVector[3],
                        &ypr, false);

      if (samplesRead == 0) {
        ypr0 = ypr;
      }

      tflInputTensor->data.f[tensorIndex++] =
          (accl[0] - accl_min) / (accl_max - accl_min);
      tflInputTensor->data.f[tensorIndex++] =
          (accl[1] - accl_min) / (accl_max - accl_min);
      tflInputTensor->data.f[tensorIndex++] =
          (accl[2] - accl_min) / (accl_max - accl_min);
      tflInputTensor->data.f[tensorIndex++] =
          (gyro[0] - gyro_min) / (gyro_max - gyro_min);
      tflInputTensor->data.f[tensorIndex++] =
          (gyro[1] - gyro_min) / (gyro_max - gyro_min);
      tflInputTensor->data.f[tensorIndex++] =
          (gyro[2] - gyro_min) / (gyro_max - gyro_min);

      tflInputTensor->data.f[tensorIndex++] =
          (calRotation(ypr.yaw, ypr0.yaw) - roto_min) / (roto_max - roto_min);
      tflInputTensor->data.f[tensorIndex++] =
          (calRotation(ypr.pitch, ypr0.pitch) - roto_min) /
          (roto_max - roto_min);
      tflInputTensor->data.f[tensorIndex++] =
          (calRotation(ypr.roll, ypr0.roll) - roto_min) / (roto_max - roto_min);

      samplesRead++;
    }
  }

  // Not enough samples, restart
  if (samplesRead < 45) {
    Serial.print("not enough samples, ");
    Serial.println(samplesRead);
    samplesRead = -1;
    tensorIndex = 0;
    inference_started = false;
    startedChar = false;
    digitalWrite(LED_RED, LIGHT_ON);
    delay(500);
    digitalWrite(LED_RED, LIGHT_OFF);
    return;
  }

  Serial.print("tensor ");
  Serial.println(tensorIndex);

  // drop the last 5 points
  if (tensorIndex < out_samples * 9) {
    tensorIndex -= 5 * 9;
  }

  for (int i = tensorIndex; i < out_samples * 9; i++) {
    tflInputTensor->data.f[i] = 0;
  }

#ifdef TSFLOW
  if (inference_started) {
    inference_started = false;

    // Invoke ML inference
    TfLiteStatus invokeStatus = tflInterpreter->Invoke();
    if (invokeStatus != kTfLiteOk) {
      Serial.println("Invoke failed!");
    }

    // Loop through the output tensor values from the model
    // for (int i = 0; i < NUM_GESTURES; i++) {
    //   Serial.print(GESTURES[i]);
    //   Serial.print(": ");
    //   Serial.println(tflOutputTensor->data.f[i], 6);
    // }
    // Serial.println();

    char ch = '.';
    for (int i = 0; i < NUM_GESTURES; i++) {
      if (tflOutputTensor->data.f[i] > 0.5) {
        ch = GESTURES[i];
        break;
      };
    }
    Serial.println(ch);
    if (ch == '.') {
      digitalWrite(LED_RED, LIGHT_ON);
      delay(500);
      digitalWrite(LED_RED, LIGHT_OFF);
    }

    // Send KEY_DOWN
    blehid.keyPress(ch);

    // Send KEY_UP at next loop
    needSendKeyRelease = true;
  }
#endif
}

#ifdef TOM

/*------------------------------------------------------------------*/
/* Central
 *------------------------------------------------------------------*/
void scan_callback(ble_gap_evt_adv_report_t *report) {
  // Since we configure the scanner with filterUuid()
  // Scan callback only invoked for device with bleuart service advertised
  // Connect to the device with bleuart service in advertising packet
  Bluefruit.Central.connect(report);
}

void cent_connect_callback(uint16_t conn_handle) {
  // Get the reference to current connection
  BLEConnection *connection = Bluefruit.Connection(conn_handle);

  char peer_name[32] = {0};
  connection->getPeerName(peer_name, sizeof(peer_name));

  Serial.print("[Cent] Connected to ");
  Serial.println(peer_name);
  ;

  if (clientUart.discover(conn_handle)) {
    // Enable TXD's notify
    clientUart.enableTXD();
  } else {
    // disconnect since we couldn't find bleuart service
    Bluefruit.disconnect(conn_handle);
  }
}

void cent_disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  (void)conn_handle;
  (void)reason;

  Serial.println("[Cent] Disconnected");
}

/**
 * Callback invoked when uart received data
 * @param cent_uart Reference object to the service where the data
 * arrived. In this example it is clientUart
 */
void cent_bleuart_rx_callback(BLEClientUart &cent_uart) {
  char str[20 + 1] = {0};
  cent_uart.read(str, 20);

  Serial.print("[Cent] RX: ");
  Serial.println(str);

  // blehid.keyPress(str[0]);
  // needSendKeyRelease = true;

  if (str[0] == 'a') {
    if (deviceMode == DEVICE_MOUSE_MODE) {
      deviceMode = DEVICE_KEYBOARD_MODE;
      Serial.println("swithc to keyboard");
    } else {
      deviceMode = DEVICE_MOUSE_MODE;
      Serial.println("swithc to mouse");
    }
  }
}

#endif

#define DOUBLE_CLICK_INTERVAL 300
#define MOUSE_STEPS_PER_CLICK 5
int lastUpTime, lastDownTime, lastKey;
uint8_t navigateButtons[4] = {KEYPAD_LEFT, KEYPAD_RIGHT, KEYPAD_UP,
                              KEYPAD_DOWN};
uint8_t navigateButtonLastState[4] = {HIGH, HIGH, HIGH, HIGH};
uint8_t navigateButtonInDoubleClickMode[4] = {0, 0, 0, 0};
uint8_t navigateButtonSingleClickKeyboardCode[4] = {
    HID_KEY_ARROW_LEFT, HID_KEY_ARROW_RIGHT, HID_KEY_ARROW_UP,
    HID_KEY_ARROW_DOWN};
uint8_t navigateButtonDoubleClickKeyboardCode[4] = {
    HID_KEY_HOME, HID_KEY_END, HID_KEY_PAGE_UP, HID_KEY_PAGE_DOWN};

int8_t navigateButtonSingleClickMouseCode[4][2] = {{-MOUSE_STEPS_PER_CLICK, 0},
                                                   {MOUSE_STEPS_PER_CLICK, 0},
                                                   {0, -MOUSE_STEPS_PER_CLICK},
                                                   {0, MOUSE_STEPS_PER_CLICK}};
int8_t navigateButtonDoubleClickMouseCode[4] = {MOUSE_BUTTON_BACKWARD,
                                                MOUSE_BUTTON_FORWARD, -1, 1};
uint32_t navigateButtonLastDownTime[4];
uint32_t skipScroll;

uint8_t clickButtons[] = {MOUSE_LEFT, MOUSE_RIGHT, MOUSE_ACTIVATE,
                          KEYPAD_ACTIVATE};
uint8_t clickButtonLastState[] = {HIGH, HIGH, HIGH, HIGH};
uint8_t clickButtonCode[] = {MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT, 0, 0};
uint8_t clickButtonKeyboardCode[] = {HID_KEY_ENTER, HID_KEY_BACKSPACE, 0, 0};

void scanOneClickButton(uint8_t keyIndex) {

  uint8_t state = digitalRead(clickButtons[keyIndex]);
  if (state == clickButtonLastState[keyIndex]) { // no change
    return;
  }

  delay(3);
  state = digitalRead(clickButtons[keyIndex]);
  if (state == clickButtonLastState[keyIndex]) { // no change
    return;
  }

  //  edge is detected
  clickButtonLastState[keyIndex] = state;

  switch (clickButtons[keyIndex]) {
  case MOUSE_ACTIVATE:
    deviceMode = DEVICE_MOUSE_MODE;
    setReports();
    break;
  case KEYPAD_ACTIVATE:
    deviceMode = DEVICE_KEYBOARD_MODE;
    setReports();
    break;
  default:
    if (deviceMode == DEVICE_MOUSE_MODE) {
      if (state == LOW) {
        if (keyIndex == 1) {
          // hack the backspace button for device switching
          setDeviceId();
        } else {
          blehid.mouseButtonPress(clickButtonCode[keyIndex]);
          Serial.println("mouse button down");
        }
      } else {
        blehid.mouseButtonRelease();
        Serial.println("mouse button up");
      }
    } else {
      if (state == LOW) {
        uint8_t keycodes[6] = {HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE,
                               HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE};
        if (keyIndex == 1) {
          // hack the backspace button for device switching
          setDeviceId();
        } else {
          keycodes[0] = clickButtonKeyboardCode[keyIndex];
          blehid.keyboardReport(0, keycodes);
          Serial.println("key button down");
        }

      } else {
        blehid.keyRelease();
        Serial.println("key button up");
      }
    }
  }
}

void scanClickButtons() {

  // Only mouse left and right click
  for (int i = 0; i < 4; i++) {
    scanOneClickButton(i);
  }
}

void scanOneNavigateButton(uint8_t keyIndex) {
  // detect edge
  uint8_t state = digitalRead(navigateButtons[keyIndex]);
  if (state == navigateButtonLastState[keyIndex]) { // no change

    // For mouse, when the button is pressed and held,  we need continue send
    // mouseMove() event. This need be done without wait.
    if (state == LOW && deviceMode == DEVICE_MOUSE_MODE) {
      if (navigateButtonInDoubleClickMode[keyIndex]) {
        // Serial.print("mouse scroll: ");
        // Serial.println(navigateButtonDoubleClickMouseCode[keyIndex]);
        // in press and hold mode, Scroll too fast, skip every x
        skipScroll++;
        if (skipScroll % 6 == 0) {
          blehid.mouseScroll(navigateButtonDoubleClickMouseCode[keyIndex]);
        }
      } else {
        // Serial.print("mouse move: ");
        // Serial.print(navigateButtonSingleClickMouseCode[keyIndex][0]);
        // Serial.print(" ");
        // Serial.println(navigateButtonSingleClickMouseCode[keyIndex][1]);
        blehid.mouseMove(navigateButtonSingleClickMouseCode[keyIndex][0],
                         navigateButtonSingleClickMouseCode[keyIndex][1]);
      }
    }
    return;
  };

  delay(1);
  state = digitalRead(navigateButtons[keyIndex]);
  if (state == navigateButtonLastState[keyIndex]) { // only noise
    return;
  }

  navigateButtonLastState[keyIndex] = state;
  uint32_t time1 = millis();
  bool doubleClick = false;

  // high -> low
  if (state == LOW) {

#ifdef FEATURE_INERTIA_SCROLL
    if (inertiaScroll) {
      inertiaScroll = false;
      Serial.println("end inertia scroll");
      return;
    }
#endif

    // If the button was pressed again within threshold, it's a double click
    if (time1 - navigateButtonLastDownTime[keyIndex] < DOUBLE_CLICK_INTERVAL) {
      doubleClick = true;

      // For Mouse. save the double click flag, used for repeat events.
      if (navigateButtons[keyIndex] == KEYPAD_UP ||
          navigateButtons[keyIndex] == KEYPAD_DOWN) {
        navigateButtonInDoubleClickMode[keyIndex] = true;
      }
    }
    if (deviceMode == DEVICE_KEYBOARD_MODE) {
      // keyboard mode
      uint8_t keycodes[6] = {HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE,
                             HID_KEY_NONE, HID_KEY_NONE, HID_KEY_NONE};
      if (doubleClick) {
        keycodes[0] = navigateButtonDoubleClickKeyboardCode[keyIndex];
      } else {
        keycodes[0] = navigateButtonSingleClickKeyboardCode[keyIndex];
      }

      // Serial.print("key: ");
      // Serial.println(keycodes[0]);
      blehid.keyboardReport(0, keycodes);
    } else {
      // mouse mode
      if (doubleClick) {
        switch (navigateButtons[keyIndex]) {
        case KEYPAD_UP:
        case KEYPAD_DOWN:
          Serial.print("mouse scroll: ");
          Serial.println(navigateButtonDoubleClickMouseCode[keyIndex]);
          blehid.mouseScroll(navigateButtonDoubleClickMouseCode[keyIndex]);
          skipScroll = 0;
          break;
        case KEYPAD_LEFT:
        case KEYPAD_RIGHT:
#ifdef FEATURE_INERTIA_SCROLL
          inertiaScroll = true;
          inertiaScrollLastTimeStamp = millis();
          Serial.println("start inertia scroll");
          if (navigateButtons[keyIndex] == KEYPAD_LEFT) {
            inertiaScrollDirection = INERTIA_SCROLL_DOWN;
          } else {
            inertiaScrollDirection = INERTIA_SCROLL_UP;
          }

#else
          blehid.mouseButtonPress(navigateButtonDoubleClickMouseCode[keyIndex]);
          Serial.print("mouse db ");
          Serial.println(navigateButtonDoubleClickMouseCode[keyIndex]);
#endif
        }

      } else {
        // Serial.print("mouse move: ");
        // Serial.print(navigateButtonSingleClickMouseCode[keyIndex][0]);
        // Serial.print(" ");
        // Serial.println(navigateButtonSingleClickMouseCode[keyIndex][1]);
        blehid.mouseMove(navigateButtonSingleClickMouseCode[keyIndex][0],
                         navigateButtonSingleClickMouseCode[keyIndex][1]);
      }
    }
    navigateButtonLastDownTime[keyIndex] = time1;
  } else {
    // low -> high

    // Reset double click flag.
    navigateButtonInDoubleClickMode[keyIndex] = false;

    if (deviceMode == DEVICE_KEYBOARD_MODE) {
      // keyboard mode. sent key release event
      // Serial.println("key released");
      blehid.keyRelease();
    } else {
      // mouse mode
      if (doubleClick) {
        switch (navigateButtons[keyIndex]) {
        case KEYPAD_LEFT:
        case KEYPAD_RIGHT:
          blehid.mouseButtonRelease();
          break;
        default:
          break;
        }
      }
    }
  }
}

void scanNavigateButtons() {
  for (int i = 0; i < 4; i++) {
    scanOneNavigateButton(i);
  }
}

void configGpio() {
  // enable battery measuring.
  pinMode(VBAT_ENABLE, OUTPUT);
  // Due to hardware limitation, do not set to high on Seeed nrf52
  digitalWrite(VBAT_ENABLE, LOW);

  // Read charge state. Low is charging.
  pinMode(BAT_CHARGE_STATE, INPUT);

  // Set charge mode. Set to high charging current (100mA)
  pinMode(PIN_CHARGING_CURRENT, OUTPUT);
  digitalWrite(PIN_CHARGING_CURRENT, LOW);

  pinMode(LED_BLUE, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);

  // Mystery of why !Serial not ready:
  // The "Serial" is always valid for an Arduino Uno, therefor that piece of
  // code does not wait. In the Leonardo, the "Serial" could be zero, if the
  // serial monitor has not been opened yet.

  // while (!Serial) {
  //   digitalWrite(LED_RED, LIGHT_ON);
  //   delay(10);
  //   digitalWrite(LED_RED, LIGHT_OFF);
  //   delay(100);
  // }

#ifdef IMU_USE_RESET
  pinMode(IMU_RESET, OUTPUT);
  // Reset IMU
  digitalWrite(IMU_RESET, HIGH);
  delay(0.1);
  digitalWrite(IMU_RESET, LOW);
  delay(0.1);
  digitalWrite(IMU_RESET, HIGH);
#endif

#ifdef IMU_USE_INT
  pinMode(IMU_INT, INPUT_PULLUP);
#endif

  pinMode(MOUSE_ACTIVATE, INPUT_PULLUP);
  pinMode(MOUSE_RIGHT, INPUT_PULLUP);
  pinMode(MOUSE_LEFT, INPUT_PULLUP);
  pinMode(KEYPAD_LEFT, INPUT_PULLUP);
  pinMode(KEYPAD_RIGHT, INPUT_PULLUP);
  pinMode(KEYPAD_CENTER, INPUT_PULLUP);
  pinMode(KEYPAD_UP, INPUT_PULLUP);
  pinMode(KEYPAD_DOWN, INPUT_PULLUP);

  digitalWrite(MOUSE_ACTIVATE, HIGH);
  digitalWrite(MOUSE_RIGHT, HIGH);
  digitalWrite(MOUSE_LEFT, HIGH);
  digitalWrite(KEYPAD_LEFT, HIGH);
  digitalWrite(KEYPAD_RIGHT, HIGH);
  digitalWrite(KEYPAD_CENTER, HIGH);
  digitalWrite(KEYPAD_UP, HIGH);
  digitalWrite(KEYPAD_DOWN, HIGH);

  digitalWrite(LED_RED, LIGHT_OFF);
  digitalWrite(LED_BLUE, LIGHT_OFF);
  digitalWrite(LED_GREEN, LIGHT_OFF);
}

#ifdef TSFLOW
void loadTFLiteModel() {
  // get the TFL representation of the model byte array
  tflModel = tflite::GetModel(model);
  if (tflModel->version() != TFLITE_SCHEMA_VERSION) {
    Serial.println("Model schema mismatch!");
    systemHaltWithledPattern(LED_RED, 1);
  }

  // Create an interpreter to run the model
  tflInterpreter =
      new tflite::MicroInterpreter(tflModel, tflOpsResolver, tensorArena,
                                   tensorArenaSize, &tflErrorReporter);

  // Allocate memory for the model's input and output tensors
  if (tflInterpreter->AllocateTensors() != kTfLiteOk) {
    Serial.println("AllocateTensors failed!");
    systemHaltWithledPattern(LED_RED, 2);
  };

  // Get pointers for the model's input and output tensors
  tflInputTensor = tflInterpreter->input(0);
  tflOutputTensor = tflInterpreter->output(0);
}
#endif

void startAdv(void) {
  Bluefruit.Advertising.clearData();
  // Advertising packet
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addAppearance(BLE_APPEARANCE_GENERIC_HID);

  // Include BLE HID service
  Bluefruit.Advertising.addService(blehid);

  // There is enough room for 'Name' in the advertising packet
  Bluefruit.Advertising.addName();

  /* Start Advertising
   * - Enable auto advertising if disconnected
   * - Interval:  fast mode = 20 ms, slow mode = 152.5 ms
   * - Timeout for fast mode is 30 seconds
   * - Start(timeout) with timeout = 0 will advertise forever (until
   * connected)
   *
   * For recommended advertising interval
   * https://developer.apple.com/library/content/qa/qa1931/_index.html
   */
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244); // in unit of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(30);   // number of seconds in fast mode
  Bluefruit.Advertising.start(0); // 0 = Don't stop advertising after n seconds
}
void initAndStartBLE() {

#ifdef TOM
  // Initialize Bluefruit with max concurrent connections as Peripheral = 1,
  // Central = 1. SRAM usage required by SoftDevice will increase with number
  // of connections
  Bluefruit.begin(1, 1);
  Bluefruit.Central.setConnInterval(100, 200);
  // min = 9*1.25=11.25 ms, max = 16*1.25=20ms

  // Callbacks for Central
  Bluefruit.Central.setConnectCallback(cent_connect_callback);
  Bluefruit.Central.setDisconnectCallback(cent_disconnect_callback);
#else
  Bluefruit.begin();
#endif
  // Bluefruit.setAddr(&addr);
  // HID Device can have a min connection interval of 9*1.25 = 11.25 ms
  Bluefruit.Periph.setConnInterval(9, 16);
  // min = 9*1.25=11.25 ms, max = 16*1.25=20ms

  Bluefruit.setTxPower(4); // Check bluefruit.h for supported values
  Bluefruit.setName("Rat0");

  // Configure and Start Device Information Service
  bledis.setManufacturer("Ergo");
  bledis.setModel("Ergo");
  bledis.begin();
  blehid.begin();
  unsigned char addr[6];
  Bluefruit.getAddr(addr);
  addrByte3 = addr[3];

#ifdef TOM
  // Init BLE Central Uart Serivce
  clientUart.begin();
  clientUart.setRxCallback(cent_bleuart_rx_callback);

  /* Start Central Scanning
   * - Enable auto scan if disconnected
   * - Interval = 100 ms, window = 80 ms
   * - Filter only accept bleuart service
   * - Don't use active scan
   * - Start(timeout) with timeout = 0 will scan forever (until connected)
   */
  Bluefruit.Scanner.setRxCallback(scan_callback);
  Bluefruit.Scanner.restartOnDisconnect(true);
  Bluefruit.Scanner.setInterval(160, 5); // in unit of 0.625 ms
  Bluefruit.Scanner.filterUuid(BLEUART_UUID_SERVICE);
  Bluefruit.Scanner.useActiveScan(false);
  Bluefruit.Scanner.start(0); // 0 = Don't stop scanning after n seconds
#endif
  // Set up and start advertising
  startAdv();
}

void printBDA() {
  ble_gap_addr_t addr = Bluefruit.getAddr();
  Serial.print("BDA type: ");
  Serial.print(addr.addr_type);
  for (int i = 0; i < 6; i++) {
    Serial.print(" ");
    Serial.print(addr.addr[i]);
  }
  Serial.println("");
}

void setBdDAAndName(unsigned char byte3, char *name) {
  /**
Set the local Bluetooth identity address.

The local Bluetooth identity address is the address that identifies this
device to other peers. The address type must be either @ref
BLE_GAP_ADDR_TYPE_PUBLIC or @ref BLE_GAP_ADDR_TYPE_RANDOM_STATIC.
Note
The identity address cannot be changed while advertising, scanning or creating a
connection. This address will be distributed to the peer during bonding. If the
address changes, the address stored in the peer device will not be valid and the
ability to reconnect using the old address will be lost. By default the
SoftDevice will set an address of type BLE_GAP_ADDR_TYPE_RANDOM_STATIC upon
being enabled. The address is a random number populated during the IC
manufacturing process and remains unchanged for the lifetime of each IC.
   *
   */
  Serial.print("before: ");
  printBDA();
  ble_gap_addr_t addr = Bluefruit.getAddr();

  Bluefruit.disconnect(Bluefruit.connHandle());
  Bluefruit.Advertising.stop();
  addr.addr[3] = byte3;
  Bluefruit.setAddr(&addr);
  startAdv();
  Bluefruit.setName(name);
  Serial.println(name);

  Serial.print("after: ");
  printBDA();
}

void setDeviceId() {
  if (deviceId == 0) {
    deviceId = 1;
    setBdDAAndName((unsigned char)(addrByte3 + 0x32), (char *)"Rat1");
  } else {
    deviceId = 0;
    setBdDAAndName(addrByte3, (char *)"Rat0");
  }
}