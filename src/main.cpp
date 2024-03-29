#include <Arduino.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <Wire.h>
#include <Servo.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////////////////////////////

// Global Variables
bool imu_started = false;
bool gyro_calibrated = false;
long loop_timer;

// Define MPU Variables
const int mpu_address = 0x68;
int gyro_cal_int; // Gyroscope calibration int counter
float gyro_x_offset, gyro_y_offset, gyro_z_offset; // Gyroscope roll, pitch, yaw calibration offsets
float AccX, AccY, AccZ, temperature, GyroX, GyroY, GyroZ; // Raw MPU data
float total_vector_acc;
float roll_angle_acc, pitch_angle_acc;
float roll_angle_acc_trim = -0.893;
float pitch_angle_acc_trim = -1.740;
float roll_angle, pitch_angle, yaw_angle;
float roll_level_adjust, pitch_level_adjust;

// Define Quadcopter Inputs
int throttle, throttle_mod;
int reciever_roll_input, reciever_pitch_input, reciever_yaw_input;
int tuning_trimming, tuning_dir, pb1, pb2, pb3, pb4;
int pb1_last = 1;
int pb2_last = 1;
int pb3_last = 1;
int pb4_last = 1;

// Define PID Controllers
// float roll_Kp = 3.0; // Roll p gain
// float roll_Ki = 0.02; // Roll i gain
// float roll_Kd = 12.0; // Roll d gain
float roll_Kp = 0.7; // Roll p gain
float roll_Ki = 0.0; // Roll i gain
float roll_Kd = 0.0; // Roll d gain
float roll_lim = 400.0; // Roll limit +/-
float gyro_roll_input, roll_setpoint, roll_error, roll_previous_error, roll_int_error, roll_output; // Input from gyroscope

float pitch_Kp = roll_Kp; // Pitch p gain
float pitch_Ki = roll_Ki; // Pitch i gain
float pitch_Kd = roll_Kd; // Pitch d gain
float pitch_lim = roll_lim; // Pitch limit +/-
float gyro_pitch_input, pitch_setpoint, pitch_error, pitch_previous_error, pitch_int_error, pitch_output; // Input from gyroscope

float yaw_Kp = 3.0; // Yaw p gain
float yaw_Ki = 0.02; // Yaw i gain
float yaw_Kd = 0.0; // Yaw d gain
float yaw_lim = roll_lim; // Yaw limit +/-
float gyro_yaw_input, yaw_setpoint, yaw_error, yaw_previous_error, yaw_int_error, yaw_output; // Input from gyroscope

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Radio
////////////////////////////////////////////////////////////////////////////////////////////////////////

// Define data struct for recieving
// Max size of this struct is 32 bytes - NRF24L01 buffer limit
struct Data_Package
{
  byte j1x_VAL;
  byte j1y_VAL;
  byte j1b_VAL;
  byte j2x_VAL;
  byte j2y_VAL;
  byte j2b_VAL;
  byte pot1_VAL;
  byte pot2_VAL;
  byte t1_VAL;
  byte t2_VAL;
  byte pb1_VAL;
  byte pb2_VAL;
  byte pb3_VAL;
  byte pb4_VAL;
};

Data_Package data; // Create a variable with the above structure

// Define RF
RF24 radio(7, 8); // CE, CSN
const byte address[6] = "00001";
unsigned long lastReceiveTime = 0;
unsigned long currentTime = 0;

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Servos
////////////////////////////////////////////////////////////////////////////////////////////////////////

// Define Servos
bool esc_armed = false;
int esc_armed_int = 0;
#define bm1_PIN 2 // Brushless motor 1
#define bm2_PIN 3 // Brushless motor 2
#define bm3_PIN 4 // Brushless motor 3
#define bm4_PIN 5 // Brushless motor 4
Servo BM1;
Servo BM2;
Servo BM3;
Servo BM4;
int bm1, bm2, bm3, bm4; // ESC input FL, FR, RL, RR

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Functions
////////////////////////////////////////////////////////////////////////////////////////////////////////

void resetData()
{
  data.j1x_VAL = 127;
  data.j1y_VAL = 127;
  data.j2x_VAL = 127;
  data.j2y_VAL = 127;
  data.j1b_VAL = 1;
  data.j2b_VAL = 1;
  data.pot1_VAL = 0;
  data.pot2_VAL = 0;
  data.t1_VAL = 1;
  data.t2_VAL = 1;
  data.pb1_VAL = 1;
  data.pb2_VAL = 1;
  data.pb3_VAL = 1;
  data.pb4_VAL = 1;
}

void getRCtransmission()
{
  if (radio.available()) // If data is available read it
  {
    radio.read(&data, sizeof(Data_Package));
    lastReceiveTime = millis(); // Get lastRecievedTime
  }

  currentTime = millis(); // Get currentTime
  
  if (currentTime - lastReceiveTime > 1000) // If data hasn't been read for over 1 second reset data
  {
    resetData();
  }
}

void setupMPUregisters()
{
  // Activate the MPU-6050
  Wire.beginTransmission(mpu_address); // Start communicating with the MPU-6050
  Wire.write(0x6B); // Send the requested starting register
  Wire.write(0x00); // Set the requested starting register
  Wire.endTransmission(); // End the transmission
  // Configure the accelerometer (+/- 8g)
  Wire.beginTransmission(mpu_address); // Start communicating with the MPU-6050
  Wire.write(0x1C); // Send the requested starting register
  Wire.write(0x10); // Set the requested starting register
  Wire.endTransmission(); // End the transmission
  // Configure the gyroscope (500 deg/s full scale)
  Wire.beginTransmission(mpu_address); // Start communicating with the MPU-6050
  Wire.write(0x1B); // Send the requested starting register
  Wire.write(0x08); // Set the requested starting register
  Wire.endTransmission(); // End the transmission
}

void readMPUdata()
{
  // Read raw gyroscope and accelerometer data
  Wire.beginTransmission(mpu_address); // Start communicating with the MPU-6050
  Wire.write(0x3B); // Send the requested starting register
  Wire.endTransmission(); // End the transmission
  Wire.requestFrom(mpu_address, 14, true); //Request 14 bytes from the MPU-6050
  AccX = -(Wire.read() << 8 | Wire.read());
  AccY = -(Wire.read() << 8 | Wire.read());
  AccZ = (Wire.read() << 8 | Wire.read());
  temperature = Wire.read() << 8 | Wire.read();
  GyroX = -(Wire.read() << 8 | Wire.read());
  GyroY = -(Wire.read() << 8 | Wire.read());
  GyroZ = (Wire.read() << 8 | Wire.read());
}

void getRollPitch(float roll_angle_acc_trim, float pitch_angle_acc_trim)
{
  // Step 1: Get accelerometer and gyroscope data
  readMPUdata();

  // Step 2: Subtract gyroscope offsets
  if (gyro_calibrated == true)
  {
    GyroX -= gyro_x_offset;
    GyroY -= gyro_y_offset;
    GyroZ -= gyro_z_offset;
  }

  // Step 3: Gyroscope angle calculations
  // 0.0000611 = dt / 65.5, where dt = 0.004
  roll_angle += GyroX * 0.0000611;
  pitch_angle += GyroY * 0.0000611;
  
  // Step 4: Correct roll and pitch for IMU yawing
  // 0.000001066 = 0.0000611 * (PI / 180) -> sin function uses radians
  roll_angle += pitch_angle * sin(GyroZ * 0.000001066); // If IMU has yawed transfer the pitch angle to the roll angel
  pitch_angle -= roll_angle * sin(GyroZ * 0.000001066); // If IMU has yawed transfer the roll angle to the pitch angel
  
  // Step 5: Accelerometer angle calculation
  // 57.296 = 180 / PI -> asin function uses radians
  total_vector_acc = sqrt((AccX * AccX) + (AccY * AccY) + (AccZ * AccZ)); // Calculate the total accelerometer vector

  if(abs(AccY) < total_vector_acc) // Prevent asin function producing a NaN
  {
    roll_angle_acc = asin((float)AccY/total_vector_acc) * -57.296;
  }
  if(abs(AccX) < total_vector_acc) // Prevent asin function producing a NaN
  {
    pitch_angle_acc = asin((float)AccX/total_vector_acc) * 57.296;
  }

  // Step 6: Correct for roll_angle_acc and pitch_angle_acc offsets (found manually)
  roll_angle_acc -= roll_angle_acc_trim;
  pitch_angle_acc -= pitch_angle_acc_trim;

  // Step 7: Set roll and pitch angle depending on if IMU has already started or not
  if(imu_started && gyro_calibrated) // If the IMU is started and gyro is calibrated
  {
    // roll_angle = roll_angle * 0.98 + roll_angle_acc * 0.02; // Correct the drift of the gyro roll angle with the accelerometer roll angle
    // pitch_angle = pitch_angle * 0.98 + pitch_angle_acc * 0.02; // Correct the drift of the gyro pitch angle with the accelerometer pitch angle
    roll_angle = roll_angle * 0.9996 + roll_angle_acc * 0.0004; // Correct the drift of the gyro roll angle with the accelerometer roll angle
    pitch_angle = pitch_angle * 0.9996 + pitch_angle_acc * 0.0004; // Correct the drift of the gyro pitch angle with the accelerometer pitch angle
  }
  else // On first start
  {
    roll_angle = roll_angle_acc;
    pitch_angle = pitch_angle_acc;
    imu_started = true; // Set the IMU started flag
  }

  roll_level_adjust = roll_angle * 15; // Calculate the roll angle correction
  pitch_level_adjust = pitch_angle * 15; // Calculate the pitch angle correction
}

void getPIDoutput(float roll_Kp, float roll_Ki, float roll_Kd) // Get PID output
{
  pitch_Kp = roll_Kp;
  pitch_Ki = roll_Ki;
  pitch_Kd = roll_Kd;

  // Roll
  roll_error = gyro_roll_input - roll_setpoint;

  if (throttle > 1050)
  {
    roll_int_error += roll_Ki * roll_error;
    if (roll_int_error > roll_lim) roll_int_error = roll_lim; // Deal with integral wind up
    else if (roll_int_error < -1 * roll_lim) roll_int_error = -1 * roll_lim;
  }
  else if (throttle < 1050) roll_int_error = 0;

  roll_output = (roll_Kp * roll_error) + roll_int_error + (roll_Kd * (roll_error - roll_previous_error));
  if(roll_output > roll_lim) roll_output = roll_lim; // Limit roll output
  else if(roll_output < roll_lim * -1) roll_output = roll_lim * -1;

  roll_previous_error = roll_error;

  // Pitch
  pitch_error = gyro_pitch_input - pitch_setpoint;

  if (throttle > 1050)
  {
    pitch_int_error += pitch_Ki * pitch_error;
    if (pitch_int_error > pitch_lim) pitch_int_error = pitch_lim; // Deal with integral wind up
    else if (pitch_int_error < -1 * pitch_lim) pitch_int_error = -1 * pitch_lim;
  }
  else if (throttle < 1050) pitch_int_error = 0;

  pitch_output = (pitch_Kp * pitch_error) + pitch_int_error + (pitch_Kd * (pitch_error - pitch_previous_error));
  if(pitch_output > pitch_lim) pitch_output = pitch_lim; // Limit pitch output
  else if(pitch_output < pitch_lim * -1) pitch_output = pitch_lim * -1;

  pitch_previous_error = pitch_error;

  // Yaw
  yaw_error = gyro_yaw_input - yaw_setpoint;

  if (throttle > 1050)
  {
    yaw_int_error += yaw_Ki * yaw_error;
    if (yaw_int_error > yaw_lim) yaw_int_error = yaw_lim; // Deal with integral wind up
    else if (yaw_int_error < -1 * yaw_lim) yaw_int_error = -1 * yaw_lim;
  }
  else if (throttle < 1050) yaw_int_error = 0;

  yaw_output = (yaw_Kp * yaw_error) + yaw_int_error + (yaw_Kd * (yaw_error - yaw_previous_error));
  if(yaw_output > yaw_lim) yaw_output = yaw_lim; // Limit yaw output
  else if(yaw_output < yaw_lim * -1) yaw_output = yaw_lim * -1;

  yaw_previous_error = yaw_error;
}

void setup()
{
  Serial.begin(57600); // For debugging
  Wire.begin(); // Start the I2C as master

  // Define radio communication
  radio.begin();
  radio.openReadingPipe(0, address);
  radio.setAutoAck(false);
  radio.setDataRate(RF24_250KBPS);
  radio.setPALevel(RF24_PA_LOW);
  radio.startListening();

  // Set default values
  resetData();

  // MPU setup (reset the sensor through the power management register)
  setupMPUregisters();

  // Servos setup
  BM1.attach(bm1_PIN, 1000, 2000); // (pin, min pulse width, max pulse width in microseconds)
  BM2.attach(bm2_PIN, 1000, 2000);
  BM3.attach(bm3_PIN, 1000, 2000);
  BM4.attach(bm4_PIN, 1000, 2000);

  loop_timer = micros(); // Reset the loop timer
}

void loop()
{
  loop_timer = micros();
  // Step 1: Get MPU data
  getRollPitch(roll_angle_acc_trim, pitch_angle_acc_trim);

  gyro_roll_input = (gyro_roll_input * 0.7) + ((GyroX / 65.5) * 0.3); // 65.5 = 1 deg/s
  gyro_pitch_input = (gyro_pitch_input * 0.7) + ((GyroY / 65.5) * 0.3); // 65.5 = 1 deg/s
  gyro_yaw_input = (gyro_yaw_input * 0.7) + ((GyroZ / 65.5) * 0.3); // 65.5 = 1 deg/s

  // Step 2: Get transmission from RC controller
  getRCtransmission();
  throttle = map(data.pot1_VAL, 0, 255, 1000, 2000);
  throttle_mod = map(data.j1y_VAL, 0, 255, -200, 200);
  throttle = throttle + throttle_mod;
  if (throttle < 1000) throttle = 1000;
  reciever_roll_input = map(data.j2x_VAL, 0, 255, 1000, 2000);
  reciever_pitch_input = map(data.j2y_VAL, 0, 255, 2000, 1000);
  reciever_yaw_input = map(data.j1x_VAL, 0, 255, 1000, 2000);
  tuning_trimming = data.t1_VAL;
  tuning_dir = data.t2_VAL;
  pb1 = data.pb1_VAL;
  pb2 = data.pb2_VAL;
  pb3 = data.pb3_VAL;
  pb4 = data.pb4_VAL;

  // Step 3: PID tuning/roll and pitch trimming
  if (tuning_trimming == 0) // If tuning is on
  {
    if (tuning_dir == 0) // If tuning direction is in the positive direction
    {
      // Tuning roll/pitch P gain
      if (pb1 != pb1_last)
        if (pb1 == 0) roll_Kp += 0.1;
      pb1_last = pb1;
      // Tuning roll/pitch I gain
      if (pb2 != pb2_last)
        if (pb2 == 0) roll_Ki += 0.001;
      pb2_last = pb2;
      // Tuning roll/pitch D gain
      if (pb3 != pb3_last)
        if (pb3 == 0) roll_Kd += 0.1;
      pb3_last = pb3;
    }
    else if (tuning_dir == 1) // If tuning direction is in the negative direction
    {
      // Tuning roll/pitch P gain
      if (pb1 != pb1_last)
        if (pb1 == 0) roll_Kp -= 0.1;
      pb1_last = pb1;
      // Tuning roll/pitch I gain
      if (pb2 != pb2_last)
        if (pb2 == 0) roll_Ki -= 0.001;
      pb2_last = pb2;
      // Tuning roll/pitch D gain
      if (pb3 != pb3_last)
        if (pb3 == 0) roll_Kd -= 0.1;
      pb3_last = pb3;
    }
  }
  else if (tuning_trimming == 1) // If trimming is on
  {
    // Subtract pitch trim
    if (pb1 != pb1_last)
      if (pb1 == 0) pitch_angle_acc_trim -= 0.5;
    pb1_last = pb1;
    // Add pitch trim
    if (pb2 != pb2_last)
      if (pb2 == 0) pitch_angle_acc_trim += 0.5;
    pb2_last = pb2;
    // Subtract roll trim
    if (pb3 != pb3_last)
      if (pb3 == 0) roll_angle_acc_trim -= 0.5;
    pb3_last = pb3;
    // Add roll trim
    if (pb4 != pb4_last)
      if (pb4 == 0) roll_angle_acc_trim += 0.5;
    pb4_last = pb4;
  }
  // Protect against going negative to avoid unwanted behaviour
  if (roll_Kp < 0) roll_Kp = 0;
  if (roll_Ki < 0) roll_Ki = 0;
  if (roll_Kd < 0) roll_Kd = 0;

  // Step 4: Calculate setpoints
  roll_setpoint = 0;
  if(reciever_roll_input > 1520) roll_setpoint = reciever_roll_input - 1520;
  else if(reciever_roll_input < 1480) roll_setpoint = reciever_roll_input - 1480;
  roll_setpoint -= roll_level_adjust; // Subtract roll angle correction from the standardized receiver roll input value
  roll_setpoint /= 3; // Divide roll setpoint for the PID roll controller by 3 to get angles in degrees

  pitch_setpoint = 0;
  if(reciever_pitch_input > 1520) pitch_setpoint = reciever_pitch_input - 1520;
  else if(reciever_pitch_input < 1480) pitch_setpoint = reciever_pitch_input - 1480;
  pitch_setpoint -= pitch_level_adjust; // Subtract pitch angle correction from the standardized receiver pitch input value
  pitch_setpoint /= 3; // Divide pitch setpoint for the PID pitch controller by 3 to get angles in degrees

  yaw_setpoint = 0;
  if(throttle > 1050) // Do not yaw when turning off the motors.
  {
    if(reciever_yaw_input > 1520) yaw_setpoint = reciever_yaw_input - 1520;
    else if(reciever_yaw_input < 1480) yaw_setpoint = reciever_yaw_input - 1480;
    yaw_setpoint /= 3; // Divide yaw setpoint for the PID yaw controller by 3 to get angles in degrees
  }

  // Step 5: Get PID output
  getPIDoutput(roll_Kp, roll_Ki, roll_Kd);

  // Step 6: Calculate BM inputs (arm esc's -> calibrate gyro -> run)
  if ((esc_armed == false) && (gyro_calibrated == false))
  {
    bm1 = throttle;
    bm2 = throttle;
    bm3 = throttle;
    bm4 = throttle;
    if (esc_armed_int < 7500)
    {
      esc_armed_int += 1;
    }
    else if (esc_armed_int == 7500)
    {
      esc_armed = true;
      esc_armed_int += 1;
    }
  }
  else if ((esc_armed == true) && (gyro_calibrated == false))
  {
    bm1 = throttle;
    bm2 = throttle;
    bm3 = throttle;
    bm4 = throttle;
    if (gyro_cal_int < 2000)
    {
      gyro_x_offset += GyroX;
      gyro_y_offset += GyroY;
      gyro_z_offset += GyroZ;
      gyro_cal_int += 1;
    }
    else if (gyro_cal_int == 2000)
    {
      gyro_x_offset /= 2000;
      gyro_y_offset /= 2000;
      gyro_z_offset /= 2000;
      gyro_calibrated = true;
      gyro_cal_int += 1;
    }
  }
  else if ((esc_armed == true) && (gyro_calibrated == true))
  {
    // Step 7: Calculate esc input
    if (throttle > 1800) throttle = 1800; // We need some room to keep full control at full throttle.
    bm1 = throttle - roll_output - pitch_output + yaw_output; // Calculate the pulse for bm1 (front-left - CW)
    bm2 = throttle + roll_output - pitch_output - yaw_output; // Calculate the pulse for bm2 (front-right - CCW)
    bm3 = throttle - roll_output + pitch_output - yaw_output; // Calculate the pulse for bm3 (rear-left - CCW)
    bm4 = throttle + roll_output + pitch_output + yaw_output; // Calculate the pulse for bm4 (rear-right - CW)
    // bm1 = throttle - pitch_output; // Calculate the pulse for bm1 (front-left - CW)
    // bm2 = throttle - pitch_output; // Calculate the pulse for bm2 (front-right - CCW)
    // bm3 = throttle + pitch_output; // Calculate the pulse for bm3 (rear-left - CCW)
    // bm4 = throttle + pitch_output; // Calculate the pulse for bm4 (rear-right - CW)
  }
  
  BM1.writeMicroseconds(bm1);
  BM2.writeMicroseconds(bm2);
  BM3.writeMicroseconds(bm3);
  BM4.writeMicroseconds(bm4);

  // Serial.print(esc_armed);
  // Serial.print(", ");
  // Serial.print(gyro_calibrated);
  // Serial.print(", ");
  // Serial.print(gyro_x_offset);
  // Serial.print(", ");
  // Serial.print(gyro_y_offset);
  // Serial.print(", ");
  // Serial.println(gyro_z_offset);

  // Serial.print(roll_angle_acc);
  // Serial.print(", ");
  // Serial.println(pitch_angle_acc);

  // Serial.print(reciever_roll_input);
  // Serial.print(", ");
  // Serial.print(reciever_pitch_input);
  // Serial.print(", ");
  // Serial.println(reciever_yaw_input);

  // Serial.print(roll_angle_acc);
  // Serial.print(", ");
  // Serial.print(pitch_angle_acc);
  // Serial.print(", ");
  // Serial.print(roll_angle);
  // Serial.print(", ");
  // Serial.println(pitch_angle);
  // Serial.print(", ");
  // Serial.print(gyro_yaw_input);
  // Serial.print(", ");
  // Serial.print(roll_setpoint);
  // Serial.print(", ");
  // Serial.print(pitch_setpoint);
  // Serial.print(", ");
  // Serial.print(yaw_setpoint);
  // Serial.print(", ");
  // Serial.print(roll_error);
  // Serial.print(", ");
  // Serial.print(pitch_error);
  // Serial.print(", ");
  // Serial.print(yaw_error);
  // Serial.print(", ");
  // Serial.print(roll_output);
  // Serial.print(", ");
  // Serial.print(pitch_output);
  // Serial.print(", ");
  // Serial.println(yaw_output);

  // Serial.print(bm1);
  // Serial.print(", ");
  // Serial.print(bm2);
  // Serial.print(", ");
  // Serial.print(bm3);
  // Serial.print(", ");
  // Serial.println(bm4);

  // Serial.print(throttle);
  // Serial.print(", ");
  // Serial.println(reciever_yaw_input);

  // Serial.print(tuning_trimming);
  // Serial.print(", ");
  // Serial.print(tuning_dir);
  // Serial.print(", ");
  // Serial.print(pb1);
  // Serial.print(", ");
  // Serial.print(pb2);
  // Serial.print(", ");
  // Serial.print(pb3);
  // Serial.print(", ");
  // Serial.print(roll_Kp);
  // Serial.print(", ");
  // Serial.print(roll_Ki);
  // Serial.print(", ");
  // Serial.println(roll_Kd);

  // Serial.println(micros() - loop_timer);
  while(micros() - loop_timer < 4000); // Wait until the loop_timer reaches 4000us (250Hz) before starting the next loop
}