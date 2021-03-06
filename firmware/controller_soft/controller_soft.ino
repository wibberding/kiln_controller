#define PIN_IGNITE    10
#define PIN_STEP1     9
#define PIN_STEP2     8
#define PIN_STEP3     7
#define PIN_STEP4     6
#define PIN_AUXTEMP   A1
#define PIN_TEMP_CS   4
#define PIN_LOADCELL  A3
#define PIN_FLAME_A   A2
#define PIN_FLAME_D   1
#define PIN_REGLIMIT  5

#define STEP_SPEED 275//in steps per second
#define TEMP_UPDATE 250 //milliseconds
#define MOTOR_TIMEOUT 5000 //milliseconds

#define NO_PORTB_PINCHANGES
#define NO_PORTC_PINCHANGES
#define DISABLE_PCINT_MULTI_SERVICE

#include <Stepper.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_MAX31855.h>
#include <ooPinChangeInt.h>
#include "pushbutton.h"

struct Status {
  unsigned char ignite;
  unsigned char flame;
  unsigned int motor;
  float main_temp;
  float ambient;
  float weight;
  float aux_temp[2];
} status;
uint8_t* status_data = (uint8_t*) &status;

const float step_interval = 1. / STEP_SPEED * 1000.; //milliseconds

//intermediate variables
Adafruit_MAX31855 thermo(PIN_TEMP_CS);
Stepper stepper(2048, PIN_STEP4, PIN_STEP2, PIN_STEP3, PIN_STEP1);
pushbutton reglimit = pushbutton(PIN_REGLIMIT, 5);

char i2c_command;
float next_step;
unsigned long next_temp;
unsigned char motor_active = false;
unsigned long stepper_target = 0;
int n_clicks = 0; //Number of full rotations
boolean limit_state = false;
unsigned long limit_last;

void setup() {
  status.flame = false;
  status.weight = 0.;
  status.aux_temp[0] = 0.;
  status.aux_temp[1] = 0.;
  
  //Setup I2C
  Wire.begin(0x08);
  Wire.onRequest(i2c_update);
  Wire.onReceive(i2c_action);
  
  //Set up regulator stepper
  status.motor = 0;
  
  //setup ignition mosfet
  pinMode(PIN_IGNITE, OUTPUT);
  digitalWrite(PIN_IGNITE, LOW);
  status.ignite = false;
  
  //set initial temperature
  delay(500);
  update_temp();
  next_temp = millis() + TEMP_UPDATE;
}

int dir;
unsigned long now;
void loop() {
  now = millis();
  reglimit.update();
  status.aux_temp[0] = reglimit.n_clicks;
  
  if (stepper_target != status.motor && now > next_step) {
    dir = status.motor < stepper_target ? 1 : -1;
    stepper.step(dir);
    
    //Limit switch tripped
    if (stepper_target == 0) {
      if (reglimit.n_clicks == 0)
        status.motor = 0;
    } else {
      status.motor += dir;
    }
    
    next_step += step_interval;
  }
  
  //put motor to sleep after timeout
  if (motor_active && (now - next_step) > MOTOR_TIMEOUT) {
    digitalWrite(PIN_STEP1, LOW);
    digitalWrite(PIN_STEP2, LOW);
    digitalWrite(PIN_STEP3, LOW);
    digitalWrite(PIN_STEP4, LOW);
    motor_active = false;
  }
  
  //update temperature
  if (now > next_temp) {
    update_temp();
    next_temp += TEMP_UPDATE;
  }
  
  //check flame status
}

void set_regulator(unsigned long pos) {
  motor_active = true;
  reglimit.setDir(status.motor < pos ? 1 : -1);
  if (stepper_target == status.motor)
    next_step = millis(); //Start stepping immediately
  stepper_target = pos;
}

void update_temp() {
  thermo.readAll(status.main_temp, status.ambient);
}

void i2c_update() {
  //update temperatures
  
  if (i2c_command == 'M') {
    Wire.write((byte*) &(status.motor), 4);
  } else if (i2c_command == 'I') {
    Wire.write((byte*) &(status.ignite), 1);
  } else if (i2c_command == 'T') {
    Wire.write((byte*) &(status.main_temp), 4);
  } else if (i2c_command == 'F') {
    Wire.write((byte*) &(status.flame), 1);
  } else {
    Wire.write(status_data, sizeof(struct Status));
  }
  
  i2c_command = 0;
}

byte buffer[32];
void i2c_action(int nbytes) {
  i2c_command = Wire.read();
  
  int i = 0;
  while (Wire.available()) {
    buffer[i++] = Wire.read();
  }
  
  if (nbytes == 1) {
    return; //Command already stored, no arguments
  }
  
  switch (i2c_command) {
    case 'M':
      set_regulator(*((unsigned int*) buffer));
      break;
    case 'I':
      analogWrite(PIN_IGNITE, buffer[0]);
      status.ignite = buffer[0];
      break;
  }
  
  i2c_command = 0;
}
