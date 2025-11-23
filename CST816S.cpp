/*
   MIT License

  Copyright (c) 2021 Felix Biego

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#include "Arduino.h"
#include <Wire.h>
#include <FunctionalInterrupt.h>

#include "CST816S.h"

/*!
    @brief  Constructor for CST816S
  @param	sda
      i2c data pin
  @param	scl
      i2c clock pin
  @param	rst
      touch reset pin
  @param	irq
      touch interrupt pin
*/
// Added TwoWire reference
CST816S::CST816S(int sda, int scl, int rst, int irq, TwoWire &wire) : _wire(wire)
{
    _rotation = 0;
    _sda = sda;
    _scl = scl;
    _rst = rst;
    _irq = irq;
}

CST816S::CST816S(int sda, int scl, int rst, int irq, int rotation, TwoWire &wire) : _wire(wire)
{
    _rotation = rotation;
    _sda = sda;
    _scl = scl;
    _rst = rst;
    _irq = irq;
}

void CST816S::setSize(int w, int h)
{
    _width = w;
    _height = h;
}

uint8_t CST816S::rotateGesture(uint8_t gestureID)
{
    static const uint8_t rotation90[5] = {0, 0x03, 0x04, 0x02, 0x01};
    static const uint8_t rotation180[5] = {0, 0x02, 0x01, 0x04, 0x03};
    static const uint8_t rotation270[5] = {0, 0x04, 0x03, 0x01, 0x02};
    // checking if non rotation specific gesture
    if (gestureID < 1 || gestureID > 4 || _rotation == 0)
    {
        return gestureID;
    }

    switch (_rotation)
    {
    case 1:
        return rotation90[gestureID];
    case 2:
        return rotation180[gestureID];
    case 3:
        return rotation270[gestureID];
    default:
        return gestureID; // No rotation or unknown rotation
    }
}

void CST816S::rotatePoint(int &x, int &y)
{
    int oldX = x;
    int oldY = y;
    switch (_rotation)
    {
    case 1:
        x = oldY;
        y = _width - 1 - oldX;
        break;
    case 2:
        x = _width  - 1 - oldX;
        y = _height - 1 - oldY;
        break;
    case 3:
        x = _height - 1 - oldY;
        y = oldX;
        break;
    default: // No rotation or unknown rotation
        break;
    }
}

/*!
    @brief  read touch data
*/
void CST816S::read_touch()
{
    uint8_t data_raw[8];
    i2c_read(CST816S_ADDRESS, 0x01, data_raw, 6);

    data.gestureID = rotateGesture(data_raw[0]);
    data.points = data_raw[1];
    data.event = data_raw[2] >> 6;
    data.x = ((data_raw[2] & 0xF) << 8) + data_raw[3];
    data.y = ((data_raw[4] & 0xF) << 8) + data_raw[5];
    rotatePoint(data.x, data.y);
}

/*!
    @brief  handle interrupts
*/
void CST816S::handleISR()
{
    _event_available = true;
    if (userISR != nullptr)
    {
        userISR();
    }
}

/*!
    @brief  enable double click
*/
void CST816S::enable_double_click()
{
    uint8_t enableDoubleTap = 0x01; // Set EnDClick (bit 0) to enable double-tap
    i2c_write(CST816S_ADDRESS, 0xEC, &enableDoubleTap, 1);
}

/*!
    @brief  Disable auto sleep mode
*/
void CST816S::disable_auto_sleep()
{
    uint8_t disableAutoSleep = 0xFE; // Non-zero value disables auto sleep
    i2c_write(CST816S_ADDRESS, 0xFE, &disableAutoSleep, 1);
}

/*!
    @brief  Enable auto sleep mode
*/
void CST816S::enable_auto_sleep()
{
    uint8_t enableAutoSleep = 0x00; // 0 value enables auto sleep
    i2c_write(CST816S_ADDRESS, 0xFE, &enableAutoSleep, 1);
}

/*!
    @brief  Set the auto sleep time
    @param  seconds Time in seconds (1-255) before entering standby mode after inactivity
*/
void CST816S::set_auto_sleep_time(int seconds)
{
    if (seconds < 1)
    {
        seconds = 1; // Enforce minimum value of 1 second
    }
    else if (seconds > 255)
    {
        seconds = 255; // Enforce maximum value of 255 seconds
    }

    uint8_t sleepTime = static_cast<uint8_t>(seconds); // Convert int to uint8_t
    i2c_write(CST816S_ADDRESS, 0xF9, &sleepTime, 1);
}

/*!
    @brief  initialize the touch screen
  @param	interrupt
      type of interrupt FALLING, RISING..
*/
void CST816S::begin(int interrupt)
{
    // Changed config to make I2C initilization 400kHz
    _wire.begin(_sda, _scl, 400000);

    pinMode(_irq, INPUT);
    pinMode(_rst, OUTPUT);

    digitalWrite(_rst, HIGH);
    delay(50);
    digitalWrite(_rst, LOW);
    delay(5);
    digitalWrite(_rst, HIGH);
    delay(50);

    i2c_read(CST816S_ADDRESS, 0x15, &data.version, 1);
    delay(5);
    i2c_read(CST816S_ADDRESS, 0xA7, data.versionInfo, 3);

    attachInterrupt(_irq, std::bind(&CST816S::handleISR, this), interrupt);
}

/*!
    @brief  Attaches a user-defined callback function to be triggered on an interrupt event from the CST816S touch controller.
    @param  callback  A function to be called when an interrupt event occurs, must have no parameters and return void.
*/
void CST816S::attachUserInterrupt(std::function<void()> callback)
{
    userISR = callback;
}

/*!
    @brief  check for a touch event
*/
bool CST816S::available()
{
    if (_event_available)
    {
        read_touch();
        _event_available = false;
        return true;
    }
    return false;
}

/*!
    @brief  put the touch screen in standby mode
*/
void CST816S::sleep()
{
    digitalWrite(_rst, LOW);
    delay(5);
    digitalWrite(_rst, HIGH);
    delay(50);
    uint8_t standby_value = 0x03;
    i2c_write(CST816S_ADDRESS, 0xA5, &standby_value, 1);
}

/*!
    @brief  get the gesture event name
*/
String CST816S::gesture()
{
    switch (data.gestureID)
    {
    case NONE:
        return "NONE";
        break;
    case SWIPE_DOWN:
        return "SWIPE DOWN";
        break;
    case SWIPE_UP:
        return "SWIPE UP";
        break;
    case SWIPE_LEFT:
        return "SWIPE LEFT";
        break;
    case SWIPE_RIGHT:
        return "SWIPE RIGHT";
        break;
    case SINGLE_CLICK:
        return "SINGLE CLICK";
        break;
    case DOUBLE_CLICK:
        return "DOUBLE CLICK";
        break;
    case LONG_PRESS:
        return "LONG PRESS";
        break;
    default:
        return "UNKNOWN";
        break;
    }
}

void CST816S::setRotation(int rotation)
{
    _rotation = rotation % 4;
}

/*!
    @brief  read data from i2c
  @param	addr
      i2c device address
  @param	reg_addr
      device register address
  @param	reg_data
      array to copy the read data
  @param	length
      length of data
*/
uint8_t CST816S::i2c_read(uint16_t addr, uint8_t reg_addr, uint8_t *reg_data, uint32_t length)
{
    _wire.beginTransmission(addr);
    _wire.write(reg_addr);
    if (_wire.endTransmission(true))
        return -1;
    _wire.requestFrom(addr, length, true);
    for (int i = 0; i < length; i++)
    {
        *reg_data++ = _wire.read();
    }
    return 0;
}

/*!
    @brief  write data to i2c
  @brief  read data from i2c
  @param	addr
      i2c device address
  @param	reg_addr
      device register address
  @param	reg_data
      data to be sent
  @param	length
      length of data
*/
uint8_t CST816S::i2c_write(uint8_t addr, uint8_t reg_addr, const uint8_t *reg_data, uint32_t length)
{
    _wire.beginTransmission(addr);
    _wire.write(reg_addr);
    for (int i = 0; i < length; i++)
    {
        _wire.write(*reg_data++);
    }
    if (_wire.endTransmission(true))
        return -1;
    return 0;
}