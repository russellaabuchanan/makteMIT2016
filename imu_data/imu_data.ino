#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"

#include "FastLED.h"
#define LED_COUNT 32
struct CRGB leds[LED_COUNT];
#define LED_OUT       A3

#if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
    #include "Wire.h"
#endif

MPU6050 mpu;
//MPU6050 mpu(0x69); // <-- use for AD0 high

bool blinkState = false;

// MPU control/status vars
bool dmpReady = false;  // set true if DMP init was successful
uint8_t mpuIntStatus;   // holds actual interrupt status byte from MPU
uint8_t devStatus;      // return status after each device operation (0 = success, !0 = error)
uint16_t packetSize;    // expected DMP packet size (default is 42 bytes)
uint16_t fifoCount;     // count of all bytes currently in FIFO
uint8_t fifoBuffer[64]; // FIFO storage buffer

// orientation/motion vars
Quaternion quat;           // [w, x, y, z]         quaternion container
VectorInt16 aa;         // [x, y, z]            accel sensor measurements
VectorInt16 aaReal;     // [x, y, z]            gravity-free accel sensor measurements
VectorInt16 aaWorld;    // [x, y, z]            world-frame accel sensor measurements
VectorFloat gravity;    // [x, y, z]            gravity vector
float ypr[3];           // [yaw, pitch, roll]   yaw/pitch/roll container and gravity vector

#define WINDOW 10 //filter window

float rindex[WINDOW];
float pindex[WINDOW];
float yindex[WINDOW];

float rOffset;
float pOffset;
float yOffset;

float outputRPY[3];

float roll_brightness;
float pitch_brightness;

int num;
boolean firstTime;

// ================================================================
// ===               INTERRUPT DETECTION ROUTINE                ===
// ================================================================

volatile bool mpuInterrupt = false;     // indicates whether MPU interrupt pin has gone high
void dmpDataReady() {
    mpuInterrupt = true;
}


// ================================================================
// ===                      INITIAL SETUP                       ===
// ================================================================

void setup() {

    LEDS.addLeds<WS2812B, LED_OUT, GRB>(leds, LED_COUNT);
    LEDS.showColor(CRGB(0, 0, 0));
    LEDS.setBrightness(93); // Limit max current draw to 1A
    LEDS.show();
  
    // join I2C bus (I2Cdev library doesn't do this automatically)
    #if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
        Wire.begin();
        TWBR = 24; // 400kHz I2C clock (200kHz if CPU is 8MHz)
    #elif I2CDEV_IMPLEMENTATION == I2CDEV_BUILTIN_FASTWIRE
        Fastwire::setup(400, true);
    #endif
    
    Serial.begin(115200);
    while (!Serial); // wait for Leonardo enumeration, others continue immediately
    
    // initialize device
    mpu.initialize();

    // verify connection
    Serial.println(F("Testing device connections..."));
    Serial.println(mpu.testConnection() ? F("MPU6050 connection successful") : F("MPU6050 connection failed"));

    // load and configure the DMP
    devStatus = mpu.dmpInitialize();

    // Set Gyro Offsets
    mpu.setXGyroOffset(220);
    mpu.setYGyroOffset(76);
    mpu.setZGyroOffset(-85);
    mpu.setZAccelOffset(1788);

    // make sure it worked (returns 0 if so)
    if (devStatus == 0) {
        // turn on the DMP, now that it's ready
        mpu.setDMPEnabled(true);

        // enable Arduino interrupt detection
        Serial.println(F("Enabling interrupt detection (Arduino external interrupt 0)..."));
        attachInterrupt(0, dmpDataReady, RISING);
        mpuIntStatus = mpu.getIntStatus();

        // set our DMP Ready flag so the main loop() function knows it's okay to use it
        Serial.println(F("DMP ready!"));
        dmpReady = true;

        // get expected DMP packet size for later comparison
        packetSize = mpu.dmpGetFIFOPacketSize();
    } else {
        // ERROR!
        // 1 = initial memory load failed
        // 2 = DMP configuration updates failed
        // (if it's going to break, usually the code will be 1)
        Serial.print(F("DMP Initialization failed (code "));
        Serial.print(devStatus);
        Serial.println(F(")"));
    }

    num = 0;
    firstTime = true;

    rOffset = 0;
    pOffset = 0;
    yOffset = 0;

    roll_brightness = 0;
    pitch_brightness = 0;

}

// ================================================================
// ===                    MAIN PROGRAM LOOP                     ===
// ================================================================

void loop() {
    // if programming failed, don't try to do anything
    if (!dmpReady) return;

    // reset interrupt flag and get INT_STATUS byte
    mpuInterrupt = false;
    mpuIntStatus = mpu.getIntStatus();

    // get current FIFO count
    fifoCount = mpu.getFIFOCount();

    // check for overflow (this should never happen unless our code is too inefficient)
    if ((mpuIntStatus & 0x10) || fifoCount == 1024) 
    {
        // reset so we can continue cleanly
        mpu.resetFIFO();
        Serial.println(F("FIFO overflow!"));

    // otherwise, check for DMP data ready interrupt (this should happen frequently)
    } 
    else if (mpuIntStatus & 0x02) 
    {
        // wait for correct available data length, should be a VERY short wait
        while (fifoCount < packetSize) fifoCount = mpu.getFIFOCount();

        // read a packet from FIFO
        mpu.getFIFOBytes(fifoBuffer, packetSize);
        
        // track FIFO count here in case there is > 1 packet available
        // (this lets us immediately read more without waiting for an interrupt)
        fifoCount -= packetSize;

        // display Euler angles in degrees
        mpu.dmpGetQuaternion(&quat, fifoBuffer);
        mpu.dmpGetGravity(&gravity, &quat);
        mpu.dmpGetYawPitchRoll(ypr, &quat, &gravity);

        if (firstTime == true && num == 0)
        {
          rOffset = ypr[1];
          pOffset = ypr[2];
          yOffset = ypr[0];
        }


        if (firstTime == true && num <WINDOW)
          {
            rindex[num] = ypr[1] - rOffset;
            pindex[num] = ypr[2] - pOffset;
            yindex[num] = ypr[0] - yOffset;
            
            num = num + 1;
          }
        else
        {
          if (num >= WINDOW -1)
          {
            num = 0;
          }
          else
          {
            num = num + 1;
          }
          
          firstTime = false;

        rindex[num] = ypr[1] - rOffset;
        pindex[num] = ypr[2] - pOffset;
        yindex[num] = ypr[0] - yOffset;

        for (int i = 0; i < WINDOW; i++)
        {
          outputRPY[0] += rindex[i];
          outputRPY[1] += pindex[i];
          outputRPY[2] += yindex[i];
        }

          outputRPY[0] = outputRPY[0]/WINDOW;
          outputRPY[1] = outputRPY[1]/WINDOW;
          outputRPY[2] = outputRPY[2]/WINDOW;
        
        Serial.print("RPY\t");
        Serial.print(outputRPY[0] * 180/M_PI);
        Serial.print("\t");
        Serial.print(outputRPY[1] * 180/M_PI);
        Serial.print("\t");
        Serial.print(outputRPY[2] * 180/M_PI);

       }
    
    for(int i = 0; i < LED_COUNT; i++)
    {
      roll_brightness = min(4*max((255*outputRPY[0]/M_PI/2)*sin(2*M_PI*i/LED_COUNT + M_PI),0),255);
      pitch_brightness = min(4*max((255*outputRPY[1]/M_PI/2)*cos(2*M_PI*i/LED_COUNT + M_PI),0),255);

      leds[i].r = (roll_brightness + pitch_brightness)/2;
      leds[i].g = (roll_brightness + pitch_brightness)/2;
      leds[i].b = (roll_brightness + pitch_brightness)/2;
    }

      Serial.print("\t");
      Serial.print(roll_brightness);
      Serial.print("\t");
      Serial.println(pitch_brightness);
      
      LEDS.show();
    }
}
