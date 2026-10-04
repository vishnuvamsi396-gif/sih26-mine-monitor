#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <math.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>

// =====================================================
// WIFI
// =====================================================

const char* ssid = "MineMonitor";
const char* password = "12345678";

WebServer server(80);

// =====================================================
// PIN DEFINITIONS
// =====================================================

#define SDA_PIN 21
#define SCL_PIN 22

#define TRIG_PIN 5
#define ECHO_PIN 18

#define DHT_PIN 4
#define DHT_TYPE DHT22

#define GREEN_LED 25
#define YELLOW_LED 26
#define RED_LED 27

#define BUZZER_PIN 14

// =====================================================
// OLED
// =====================================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDRESS 0x3C

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  -1
);

// =====================================================
// SENSOR OBJECTS
// =====================================================

Adafruit_MPU6050 mpu;
DHT dht(DHT_PIN, DHT_TYPE);

// =====================================================
// SENSOR STATUS
// =====================================================

bool mpuOK = false;
bool oledOK = false;

// =====================================================
// SENSOR VALUES
// =====================================================

float temperature = NAN;
float humidity = NAN;

float distanceCm = NAN;

// HC-SR04 startup reference
float baselineDistance = NAN;

float displacementCm = 0.0;

// =====================================================
// MPU6050 VALUES
// =====================================================

float accelerationMagnitude = 0.0;

float vibration = 0.0;
float tilt = 0.0;

float vibrationX = 0.0;
float vibrationY = 0.0;
float vibrationZ = 0.0;

// =====================================================
// MPU6050 STARTUP BASELINE
// =====================================================

float baselineAx = 0.0;
float baselineAy = 0.0;
float baselineAz = 9.81;

float baselineTilt = 0.0;

bool mpuBaselineReady = false;

// =====================================================
// RISK VARIABLES
// =====================================================

float vibrationRisk = 0.0;
float displacementRisk = 0.0;
float humidityRisk = 0.0;
float tiltRisk = 0.0;
float temperatureRisk = 0.0;

int riskScore = 0;

String risk = "STARTING";

// =====================================================
// TIMERS
// =====================================================

unsigned long lastRead = 0;
unsigned long lastBuzzer = 0;

// =====================================================
// WEB PAGE
// =====================================================

const char webpage[] PROGMEM = R"rawliteral(

<!DOCTYPE html>

<html>

<head>

<meta name="viewport"
content="width=device-width, initial-scale=1">

<title>MineMonitor</title>

<style>

body{
font-family:Arial;
background:#101827;
color:white;
padding:18px;
margin:0;
}

h1{
color:#56d6a0;
}

.grid{
display:grid;
grid-template-columns:repeat(2,1fr);
gap:12px;
}

.card{
background:#202d40;
padding:18px;
border-radius:12px;
}

.label{
color:#bdc8d8;
}

.value{
font-size:22px;
font-weight:bold;
margin-top:10px;
}

#status{
padding:18px;
text-align:center;
border-radius:12px;
font-weight:bold;
background:#16865d;
margin:18px 0;
font-size:20px;
}

@media(max-width:600px){

.grid{
grid-template-columns:1fr;
}

}

</style>

</head>

<body>

<h1>MineMonitor</h1>

<p>
AI-Enabled Mine Subsidence Monitoring System
</p>

<div id="status">
Connecting...
</div>

<div class="grid">

<div class="card">
<div class="label">Risk Score</div>
<div class="value" id="score">--</div>
</div>

<div class="card">
<div class="label">Temperature</div>
<div class="value" id="temp">--</div>
</div>

<div class="card">
<div class="label">Humidity</div>
<div class="value" id="hum">--</div>
</div>

<div class="card">
<div class="label">Floor/Roof Distance</div>
<div class="value" id="dist">--</div>
</div>

<div class="card">
<div class="label">Displacement</div>
<div class="value" id="disp">--</div>
</div>

<div class="card">
<div class="label">Vibration</div>
<div class="value" id="vib">--</div>
</div>

<div class="card">
<div class="label">Tilt</div>
<div class="value" id="tilt">--</div>
</div>

<div class="card">
<div class="label">Baseline Distance</div>
<div class="value" id="base">--</div>
</div>

</div>

<script>

async function updateData(){

try{

const response =
await fetch('/data');

const d =
await response.json();

const status =
document.getElementById('status');

status.textContent =
d.risk + " - " + d.score + "/100";

if(d.risk === "DANGER"){

status.style.background =
"#d93636";

}

else if(d.risk === "WARNING"){

status.style.background =
"#c58a12";

}

else{

status.style.background =
"#16865d";

}

document.getElementById('score').textContent =
d.score + "/100";

document.getElementById('temp').textContent =
d.temp === null ? "--" : d.temp + " °C";

document.getElementById('hum').textContent =
d.hum === null ? "--" : d.hum + " %";

document.getElementById('dist').textContent =
d.dist === null ? "--" : d.dist + " cm";

document.getElementById('disp').textContent =
d.disp + " cm";

document.getElementById('vib').textContent =
d.vib + " m/s²";

document.getElementById('tilt').textContent =
d.tilt + "°";

document.getElementById('base').textContent =
d.base === null ? "--" : d.base + " cm";

}

catch(error){

document.getElementById('status').textContent =
"Waiting for ESP32...";

}

}

updateData();

setInterval(updateData,2000);

</script>

</body>

</html>

)rawliteral";

// =====================================================
// READ ULTRASONIC
// =====================================================

float readDistance(){

digitalWrite(
TRIG_PIN,
LOW
);

delayMicroseconds(3);

digitalWrite(
TRIG_PIN,
HIGH
);

delayMicroseconds(10);

digitalWrite(
TRIG_PIN,
LOW
);

unsigned long duration =
pulseIn(
  ECHO_PIN,
  HIGH,
  30000
);

if(duration == 0)
return NAN;

float d =
duration * 0.0343f / 2.0f;

if(
  d < 2.0 ||
  d > 400.0
)
return NAN;

return d;

}

// =====================================================
// CALIBRATE FLOOR DISTANCE
// =====================================================

void calibrateDistance(){

Serial.println();
Serial.println(
"======================================"
);

Serial.println(
"FLOOR DISTANCE INITIALIZATION"
);

Serial.println(
"Keep ultrasonic sensor fixed..."
);

Serial.println(
"Taking 10 samples..."
);

float sum = 0;

int valid = 0;

for(int i = 0; i < 10; i++){

float d =
readDistance();

if(!isnan(d)){

sum += d;

valid++;

}

Serial.print(
"Distance sample "
);

Serial.print(
i + 1
);

Serial.print(
": "
);

if(isnan(d)){

Serial.println(
"INVALID"
);

}

else{

Serial.print(
d,
1
);

Serial.println(
" cm"
);

}

delay(300);

}

if(valid > 0){

baselineDistance =
sum / valid;

}

else{

baselineDistance =
30.0;

}

distanceCm =
baselineDistance;

displacementCm =
0.0;

Serial.println();

Serial.print(
"INITIAL FLOOR DISTANCE = "
);

Serial.print(
baselineDistance,
1
);

Serial.println(
" cm"
);

Serial.println(
"Initial displacement = 0.0 cm"
);

Serial.println(
"======================================"
);

}

// =====================================================
// INITIALIZE MPU6050 BASELINE
// =====================================================

void calibrateMPU(){

if(!mpuOK)
return;

Serial.println();
Serial.println(
"======================================"
);

Serial.println(
"MPU6050 INITIALIZATION"
);

Serial.println(
"Keep sensor completely still..."
);

Serial.println(
"Taking 20 samples..."
);

float sumX = 0;
float sumY = 0;
float sumZ = 0;

int valid = 0;

for(int i = 0; i < 20; i++){

sensors_event_t accel;
sensors_event_t gyro;
sensors_event_t tempEvent;

mpu.getEvent(
  &accel,
  &gyro,
  &tempEvent
);

sumX +=
accel.acceleration.x;

sumY +=
accel.acceleration.y;

sumZ +=
accel.acceleration.z;

valid++;

Serial.print(
"MPU sample "
);

Serial.println(
i + 1
);

delay(100);

}

if(valid > 0){

baselineAx =
sumX / valid;

baselineAy =
sumY / valid;

baselineAz =
sumZ / valid;

// IMPORTANT:
// Save actual startup tilt
baselineTilt =
atan2f(
  baselineAx,
  sqrtf(
    baselineAy * baselineAy +
    baselineAz * baselineAz
  )
) * 180.0 / PI;

mpuBaselineReady =
true;

}

Serial.println();

Serial.print(
"Baseline X = "
);

Serial.print(
baselineAx,
2
);

Serial.println(
" m/s2"
);

Serial.print(
"Baseline Y = "
);

Serial.print(
baselineAy,
2
);

Serial.println(
" m/s2"
);

Serial.print(
"Baseline Z = "
);

Serial.print(
baselineAz,
2
);

Serial.println(
" m/s2"
);

Serial.print(
"Baseline Tilt = "
);

Serial.print(
baselineTilt,
1
);

Serial.println(
" deg"
);

Serial.println(
"MPU6050 baseline initialized."
);

Serial.println(
"Initial vibration = 0"
);

Serial.println(
"Initial tilt reference saved."
);

Serial.println(
"======================================"
);

}

// =====================================================
// CALCULATE RISK
// =====================================================

void calculateRisk(){

// =====================================================
// FLOOR DISPLACEMENT
// =====================================================

bool displacementWarning =
(
  displacementCm >= 3.0 &&
  displacementCm <= 5.0
);

bool displacementHigh =
(
  displacementCm > 5.0
);

bool displacementDanger =
(
  displacementCm > 10.0
);

// =====================================================
// MPU VIBRATION
// =====================================================

bool vibrationHigh =
(
  vibration >= 1.5
);

// Individual axis >2 m/s² = DIRECT DANGER

bool axisVibrationDanger =
(
  vibrationX > 2.0 ||
  vibrationY > 2.0 ||
  vibrationZ > 2.0
);

// =====================================================
// TILT
// =====================================================

bool tiltHigh =
(
  tilt >= 8.0
);

// =====================================================
// HUMIDITY
// =====================================================

// ABOVE 80% = HUMIDITY WARNING

bool humidityHigh =
(
  !isnan(humidity) &&
  humidity > 80.0
);

// =====================================================
// TEMPERATURE
// =====================================================

bool temperatureHigh =
(
  !isnan(temperature) &&
  temperature > 40.0
);

// =====================================================
// INDIVIDUAL RISK VALUES
// =====================================================

vibrationRisk =
vibration / 3.0;

vibrationRisk =
constrain(
  vibrationRisk,
  0.0,
  1.0
);

displacementRisk =
displacementCm / 10.0;

displacementRisk =
constrain(
  displacementRisk,
  0.0,
  1.0
);

// Humidity risk adjusted for 80% threshold
if(!isnan(humidity)){

humidityRisk =
(humidity - 50.0) / 30.0;

humidityRisk =
constrain(
  humidityRisk,
  0.0,
  1.0
);

}

else{

humidityRisk = 0;

}

tiltRisk =
tilt / 15.0;

tiltRisk =
constrain(
  tiltRisk,
  0.0,
  1.0
);

if(!isnan(temperature)){

temperatureRisk =
(temperature - 30.0) / 20.0;

temperatureRisk =
constrain(
  temperatureRisk,
  0.0,
  1.0
);

}

else{

temperatureRisk = 0;

}

// =====================================================
// BASE RISK
// =====================================================

float baseRisk =

0.30 * displacementRisk +

0.30 * vibrationRisk +

0.20 * humidityRisk +

0.15 * tiltRisk +

0.05 * temperatureRisk;

riskScore =
(int)(baseRisk * 100.0);

// =====================================================
// ABNORMAL PARAMETER COUNT
// =====================================================

int abnormalCount = 0;

if(displacementHigh)
abnormalCount++;

if(vibrationHigh)
abnormalCount++;

if(tiltHigh)
abnormalCount++;

if(humidityHigh)
abnormalCount++;

if(temperatureHigh)
abnormalCount++;

// =====================================================
// DIRECT DANGER
// =====================================================

// MPU6050:
// Any axis vibration >2 m/s²

if(axisVibrationDanger){

riskScore =
max(
  riskScore,
  90
);

}

// Floor displacement >10 cm

if(displacementDanger){

riskScore =
max(
  riskScore,
  90
);

}

// =====================================================
// FLOOR WARNING
// =====================================================

// 3–5 cm = WARNING

if(displacementWarning){

riskScore =
max(
  riskScore,
  25
);

}

// =====================================================
// HUMIDITY WARNING
// =====================================================

// Humidity >80% alone = WARNING

if(
  humidityHigh &&
  abnormalCount == 1
){

riskScore =
max(
  riskScore,
  25
);

}

// =====================================================
// COMBINATION CONDITIONS
// =====================================================

// Displacement + vibration

if(
  displacementHigh &&
  vibrationHigh
){

riskScore =
max(
  riskScore,
  75
);

}

// Displacement + tilt

if(
  displacementHigh &&
  tiltHigh
){

riskScore =
max(
  riskScore,
  75
);

}

// Displacement + humidity

if(
  displacementHigh &&
  humidityHigh
){

riskScore =
max(
  riskScore,
  75
);

}

// Vibration + tilt

if(
  vibrationHigh &&
  tiltHigh
){

riskScore =
max(
  riskScore,
  75
);

}

// Vibration + humidity

if(
  vibrationHigh &&
  humidityHigh
){

riskScore =
max(
  riskScore,
  75
);

}

// Tilt + humidity

if(
  tiltHigh &&
  humidityHigh
){

riskScore =
max(
  riskScore,
  75
);

}

// =====================================================
// 3 OR MORE ABNORMAL PARAMETERS
// =====================================================

if(abnormalCount >= 3){

riskScore =
max(
  riskScore,
  90
);

}

// =====================================================
// EXTREME OVERALL VIBRATION
// =====================================================

if(vibration >= 3.0){

riskScore =
max(
  riskScore,
  90
);

}

// =====================================================
// EXTREME TILT
// =====================================================

if(tilt >= 15.0){

riskScore =
max(
  riskScore,
  90
);

}

// =====================================================
// FINAL STATUS
// =====================================================

if(riskScore >= 65){

risk =
"DANGER";

}

else if(riskScore >= 25){

risk =
"WARNING";

}

else{

risk =
"NORMAL";

}

}

// =====================================================
// CONTROL OUTPUTS
// =====================================================

void controlOutputs(){

digitalWrite(
GREEN_LED,
LOW
);

digitalWrite(
YELLOW_LED,
LOW
);

digitalWrite(
RED_LED,
LOW
);

if(risk == "NORMAL"){

digitalWrite(
GREEN_LED,
HIGH
);

noTone(
BUZZER_PIN
);

}

else if(risk == "WARNING"){

digitalWrite(
YELLOW_LED,
HIGH
);

noTone(
BUZZER_PIN
);

}

else{

digitalWrite(
RED_LED,
HIGH
);

if(
  millis() - lastBuzzer >= 1000
){

tone(
  BUZZER_PIN,
  2200,
  300
);

lastBuzzer =
millis();

}

}

}

// =====================================================
// OLED
// =====================================================

void updateOLED(){

if(!oledOK)
return;

display.clearDisplay();

display.setTextColor(
SSD1306_WHITE
);

display.setTextSize(1);

display.setCursor(
0,
0
);

display.println(
"MINE SUBSIDENCE"
);

display.print(
"Risk: "
);

display.print(
risk
);

display.print(
" "
);

display.println(
riskScore
);

display.print(
"Temp: "
);

if(isnan(temperature)){

display.println(
"--"
);

}

else{

display.print(
temperature,
1
);

display.println(
" C"
);

}

display.print(
"Hum: "
);

if(isnan(humidity)){

display.println(
"--"
);

}

else{

display.print(
humidity,
1
);

display.println(
"%"
);

}

display.print(
"Floor: "
);

if(isnan(distanceCm)){

display.println(
"--"
);

}

else{

display.print(
distanceCm,
1
);

display.println(
" cm"
);

}

display.print(
"Disp: "
);

display.print(
displacementCm,
1
);

display.println(
" cm"
);

display.print(
"Vib: "
);

display.print(
vibration,
2
);

display.println(
" m/s2"
);

display.print(
"Tilt: "
);

display.print(
tilt,
1
);

display.println(
" deg"
);

display.display();

}

// =====================================================
// READ ALL SENSORS
// =====================================================

void readSensors(){

// =====================================================
// DHT22
// =====================================================

float newTemperature =
dht.readTemperature();

float newHumidity =
dht.readHumidity();

if(!isnan(newTemperature)){

temperature =
newTemperature;

}

if(!isnan(newHumidity)){

humidity =
newHumidity;

}

// =====================================================
// HC-SR04
// =====================================================

float newDistance =
readDistance();

if(!isnan(newDistance)){

distanceCm =
newDistance;

}

// =====================================================
// DISPLACEMENT FROM STARTUP BASELINE
// =====================================================

if(
  !isnan(distanceCm) &&
  !isnan(baselineDistance)
){

displacementCm =
fabs(
  baselineDistance -
  distanceCm
);

}

else{

displacementCm = 0.0;

}

// =====================================================
// MPU6050
// =====================================================

if(
  mpuOK &&
  mpuBaselineReady
){

sensors_event_t accel;

sensors_event_t gyro;

sensors_event_t tempEvent;

mpu.getEvent(
  &accel,
  &gyro,
  &tempEvent
);

// Current acceleration

float ax =
accel.acceleration.x;

float ay =
accel.acceleration.y;

float az =
accel.acceleration.z;

// ===================================================
// CHANGE FROM STARTUP BASELINE
// ===================================================

vibrationX =
fabs(
  ax - baselineAx
);

vibrationY =
fabs(
  ay - baselineAy
);

vibrationZ =
fabs(
  az - baselineAz
);

// ===================================================
// OVERALL VIBRATION
// ===================================================

float deltaX =
ax - baselineAx;

float deltaY =
ay - baselineAy;

float deltaZ =
az - baselineAz;

vibration =

sqrtf(

  deltaX * deltaX +

  deltaY * deltaY +

  deltaZ * deltaZ

);

// ===================================================
// ACCELERATION MAGNITUDE
// =====================================================

accelerationMagnitude =

sqrtf(

  ax * ax +

  ay * ay +

  az * az

);

// ===================================================
// CURRENT TILT
// ===================================================

float currentTilt =

atan2f(

  ax,

  sqrtf(

    ay * ay +

    az * az

  )

) * 180.0 / PI;

// Change relative to startup orientation

tilt =
fabs(
  currentTilt -
  baselineTilt
);

}

else{

vibration = 0.0;

vibrationX = 0.0;

vibrationY = 0.0;

vibrationZ = 0.0;

tilt = 0.0;

}

// =====================================================
// CALCULATE RISK
// =====================================================

calculateRisk();

// =====================================================
// CONTROL OUTPUTS
// =====================================================

controlOutputs();

// =====================================================
// UPDATE OLED
// =====================================================

updateOLED();

// =====================================================
// SERIAL MONITOR
// =====================================================

Serial.println();

Serial.println(
"======================================"
);

Serial.println(
"     MINE SUBSIDENCE MONITOR"
);

Serial.println(
"======================================"
);

Serial.print(
"Temperature       : "
);

Serial.print(
temperature,
1
);

Serial.println(
" C"
);

Serial.print(
"Humidity          : "
);

Serial.print(
humidity,
1
);

Serial.println(
" %"
);

Serial.print(
"Floor/Roof Dist   : "
);

Serial.print(
distanceCm,
1
);

Serial.println(
" cm"
);

Serial.print(
"Initial Distance  : "
);

Serial.print(
baselineDistance,
1
);

Serial.println(
" cm"
);

Serial.print(
"Displacement      : "
);

Serial.print(
displacementCm,
1
);

Serial.println(
" cm"
);

Serial.print(
"Vibration         : "
);

Serial.print(
vibration,
2
);

Serial.println(
" m/s2"
);

Serial.print(
"MPU X Change      : "
);

Serial.print(
vibrationX,
2
);

Serial.println(
" m/s2"
);

Serial.print(
"MPU Y Change      : "
);

Serial.print(
vibrationY,
2
);

Serial.println(
" m/s2"
);

Serial.print(
"MPU Z Change      : "
);

Serial.print(
vibrationZ,
2
);

Serial.println(
" m/s2"
);

Serial.print(
"Tilt Change       : "
);

Serial.print(
tilt,
1
);

Serial.println(
" deg"
);

Serial.println();

Serial.print(
"TOTAL RISK        : "
);

Serial.print(
riskScore
);

Serial.println(
" %"
);

Serial.print(
"STATUS            : "
);

Serial.println(
risk
);

// =====================================================
// HUMIDITY ALERT
// =====================================================

if(
  !isnan(humidity) &&
  humidity > 80.0
){

Serial.println(
"ALERT: HUMIDITY ABOVE 80%!"
);

}

// =====================================================
// DISPLACEMENT ALERT
// =====================================================

if(displacementCm >= 3.0){

Serial.println(
"ALERT: FLOOR DISPLACEMENT DETECTED!"
);

}

// =====================================================
// MPU AXIS ALERT
// =====================================================

if(
  vibrationX > 2.0 ||
  vibrationY > 2.0 ||
  vibrationZ > 2.0
){

Serial.println(
"ALERT: MPU6050 AXIS VIBRATION > 2 m/s2!"
);

}

Serial.println(
"======================================"
);

}

// =====================================================
// WEB DATA
// =====================================================

void handleData(){

String json = "{";

json +=
"\"risk\":\"" +
risk +
"\",";

json +=
"\"score\":" +
String(riskScore) +
",";

// Temperature

json +=
"\"temp\":";

if(isnan(temperature))
json += "null";

else
json +=
String(
  temperature,
  1
);

json += ",";

// Humidity

json +=
"\"hum\":";

if(isnan(humidity))
json += "null";

else
json +=
String(
  humidity,
  1
);

json += ",";

// Distance

json +=
"\"dist\":";

if(isnan(distanceCm))
json += "null";

else
json +=
String(
  distanceCm,
  1
);

json += ",";

// Baseline

json +=
"\"base\":";

if(isnan(baselineDistance))
json += "null";

else
json +=
String(
  baselineDistance,
  1
);

json += ",";

// Displacement

json +=
"\"disp\":" +
String(
  displacementCm,
  1
) +
",";

// Vibration

json +=
"\"vib\":" +
String(
  vibration,
  2
) +
",";

// Tilt

json +=
"\"tilt\":" +
String(
  tilt,
  1
);

json += "}";

server.sendHeader(
"Cache-Control",
"no-store"
);

server.send(
200,
"application/json",
json
);

}

// =====================================================
// SETUP
// =====================================================

void setup(){

Serial.begin(
115200
);

delay(2000);

Serial.println();

Serial.println(
"======================================"
);

Serial.println(
" AI MINE SUBSIDENCE MONITORING SYSTEM"
);

Serial.println(
"======================================"
);

// =====================================================
// PINS
// =====================================================

pinMode(
TRIG_PIN,
OUTPUT
);

pinMode(
ECHO_PIN,
INPUT
);

pinMode(
GREEN_LED,
OUTPUT
);

pinMode(
YELLOW_LED,
OUTPUT
);

pinMode(
RED_LED,
OUTPUT
);

pinMode(
BUZZER_PIN,
OUTPUT
);

digitalWrite(
TRIG_PIN,
LOW
);

// =====================================================
// START GREEN DURING INITIALIZATION
// =====================================================

digitalWrite(
GREEN_LED,
HIGH
);

digitalWrite(
YELLOW_LED,
LOW
);

digitalWrite(
RED_LED,
LOW
);

noTone(
BUZZER_PIN
);

// =====================================================
// I2C
// =====================================================

Wire.begin(
SDA_PIN,
SCL_PIN
);

Serial.println(
"I2C : READY"
);

// =====================================================
// DHT22
// =====================================================

dht.begin();

delay(1000);

Serial.println(
"DHT22 : READY"
);

// =====================================================
// MPU6050
// =====================================================

if(
mpu.begin(
  0x68,
  &Wire
)
){

mpuOK = true;

Serial.println(
"MPU6050 : OK"
);

mpu.setAccelerometerRange(
MPU6050_RANGE_8_G
);

mpu.setGyroRange(
MPU6050_RANGE_500_DEG
);

mpu.setFilterBandwidth(
MPU6050_BAND_21_HZ
);

}

else{

mpuOK = false;

Serial.println(
"MPU6050 : FAILED"
);

}

// =====================================================
// OLED
// =====================================================

if(
display.begin(
  SSD1306_SWITCHCAPVCC,
  OLED_ADDRESS
)
){

oledOK = true;

Serial.println(
"OLED : OK"
);

display.clearDisplay();

display.setTextColor(
SSD1306_WHITE
);

display.setTextSize(1);

display.setCursor(
0,
0
);

display.println(
"MINE SUBSIDENCE"
);

display.setCursor(
0,
15
);

display.println(
"INITIALIZING..."
);

display.setCursor(
0,
30
);

display.println(
"KEEP SENSOR STILL"
);

display.display();

}

else{

oledOK = false;

Serial.println(
"OLED : FAILED"
);

}

// =====================================================
// INITIALIZE FLOOR DISTANCE
// =====================================================

calibrateDistance();

// =====================================================
// INITIALIZE MPU6050
// =====================================================

calibrateMPU();

// =====================================================
// FORCE INITIAL STATE TO NORMAL
// =====================================================

risk =
"NORMAL";

riskScore =
0;

displacementCm =
0.0;

vibration =
0.0;

tilt =
0.0;

// =====================================================
// WIFI
// =====================================================

Serial.println();

Serial.println(
"STARTING WIFI..."
);

WiFi.mode(
WIFI_AP
);

bool wifiOK =
WiFi.softAP(
  ssid,
  password
);

if(wifiOK){

Serial.println(
"WiFi : ENABLED"
);

Serial.print(
"SSID : "
);

Serial.println(
ssid
);

Serial.print(
"PASSWORD : "
);

Serial.println(
password
);

Serial.print(
"IP : "
);

Serial.println(
WiFi.softAPIP()
);

}

else{

Serial.println(
"WiFi : FAILED"
);

}

// =====================================================
// WEB SERVER
// =====================================================

server.on(
"/",
[](){

server.send_P(
200,
"text/html",
webpage
);

}
);

server.on(
"/data",
handleData
);

server.begin();

Serial.println(
"WEB SERVER : STARTED"
);

// =====================================================
// INITIAL GREEN STATE
// =====================================================

digitalWrite(
GREEN_LED,
HIGH
);

digitalWrite(
YELLOW_LED,
LOW
);

digitalWrite(
RED_LED,
LOW
);

noTone(
BUZZER_PIN
);

Serial.println();

Serial.println(
"======================================"
);

Serial.println(
"SYSTEM READY"
);

Serial.println(
"INITIAL STATE : NORMAL / GREEN"
);

Serial.println(
"======================================"
);

Serial.print(
"Floor baseline : "
);

Serial.print(
baselineDistance,
1
);

Serial.println(
" cm"
);

Serial.print(
"MPU baseline X : "
);

Serial.print(
baselineAx,
2
);

Serial.println(
" m/s2"
);

Serial.print(
"MPU baseline Y : "
);

Serial.print(
baselineAy,
2
);

Serial.println(
" m/s2"
);

Serial.print(
"MPU baseline Z : "
);

Serial.print(
baselineAz,
2
);

Serial.println(
" m/s2"
);

Serial.println();

Serial.println(
"Connect to WiFi: MineMonitor"
);

Serial.println(
"Open: http://192.168.4.1"
);

Serial.println(
"======================================"
);

// =====================================================
// FIRST READING
// =====================================================

delay(1000);

readSensors();

}

// =====================================================
// LOOP
// =====================================================

void loop(){

server.handleClient();

if(
  millis() - lastRead >= 2000
){

lastRead =
millis();

readSensors();

}

delay(2);

}