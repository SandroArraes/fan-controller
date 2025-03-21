#include "fan.h"


const char* modeToStr(Mode mode){
	switch(mode){
		case Mode::Auto: return "Auto";
		case Mode::Low:  return "Low";
		case Mode::High: return "High";
		case Mode::Full: return "Full";
	}
	return "UNKNOWN";
}

// This can handle 2 tachometers
static volatile uint32_t tachoCounters[TACHOMETER_MAX] = {0};
static void (*onTachoCbs[TACHOMETER_MAX])() = {
	[](){ tachoCounters[0]++; },
	[](){ tachoCounters[1]++; },
};
static uint8_t onTachoCbsCnt = 0;


Fan::Fan(const FanDef* _def, const Sensor* sensor): def(_def), sensor(sensor) {

	// Determinate min & max speed
	minSpeed = 100;
	maxSpeed = 0;
	for(int j = 0; j < def->speedCurveLength; j++){
		auto& speed = def->speedCurve[j].speed;

		if(speed > 0 && speed < minSpeed)
			minSpeed = speed;
		if(speed < 100 && speed > maxSpeed)
			maxSpeed = speed;
	}

	// Configure PWM
	static bool configured25KHz = false;
	if((def->pin == 9 || def->pin == 10) && !configured25KHz){
		// Setup 25KHz PWM on pin 9 and 10 to match the intel fan specs
		// Configure Timer 1 for PWM @ 25 kHz.
		TCCR1A = 0;           // undo the configuration done by...
		TCCR1B = 0;           // ...the Arduino core library
		TCNT1  = 0;           // reset timer
		TCCR1A = _BV(COM1A1)  // non-inverted PWM on ch. A
		       | _BV(COM1B1)  // same on ch; B
		       | _BV(WGM11);  // mode 10: ph. correct PWM, TOP = ICR1
		TCCR1B = _BV(WGM13)   // ditto
		       | _BV(CS10);   // prescaler = 1
		ICR1   = 320;         // TOP = 320

		configured25KHz = true;
	}

	// Configure pins
	pinMode(def->pin, OUTPUT);

	// Configure tachometer if any
	if(def->tachoPin != (uint8_t)-1){
		if(onTachoCbsCnt >= TACHOMETER_MAX){
			Serial.println("Warning: cannot handle so many tachometers. Please edit TACHOMETER_MAX in " __FILE__);
		}
		else {
			tachoCounter = &tachoCounters[onTachoCbsCnt];

			pinMode(def->tachoPin, INPUT_PULLUP);
			attachInterrupt(digitalPinToInterrupt(def->tachoPin), onTachoCbs[onTachoCbsCnt], RISING);
			onTachoCbsCnt++;
		}
		resetTacho();
	}
}

void Fan::setSpeed(uint8_t speed){
	this->speed = speed;

	switch (def->pin) {
		case 9:
			OCR1A = speed / 100.0 * 320;
			break;
		case 10:
			OCR1B = speed / 100.0 * 320;
			break;
		default:
			analogWrite(def->pin, speed / 100.0 * 255);
			break;
	}
}


void Fan::setModeSpeed(Mode mode){
	switch(mode){
		case Mode::Auto:
			setSpeed(calcSpeed(sensor->smoothedTemp()));
			break;
		case Mode::Low:
			setSpeed(minSpeed);
			break;
		case Mode::High:
			setSpeed(maxSpeed);
			break;
		case Mode::Full:
			setSpeed(100);
			break;
	}
}

uint16_t Fan::getRPM() const {
	//   2 pulse per revolution
	return (*tachoCounter / 2ul) * (60.0  / ((millis() - tachoTimer) / 1000.0));
}


uint8_t Fan::calcSpeed(double temp) const {
	SpeedCurvePoint* start = nullptr;
	SpeedCurvePoint* end = nullptr;

	for(int i = 0 ; i < def->speedCurveLength ; i++){

		if(def->speedCurve[i].temp > temp){
			end = &def->speedCurve[i];
			break;
		}
		start = &def->speedCurve[i];
	}

	if(start == nullptr){
		return end->speed;
	}
	if(end == nullptr){
		return start->speed;
	}

	//interpolate
	double pitch = (double)(end->speed - start->speed) / (double)(end->temp - start->temp);
	double offset = start->speed - pitch * start->temp;
	return pitch * temp + offset;
}

