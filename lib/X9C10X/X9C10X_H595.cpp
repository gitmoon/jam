//
//    FILE: X9C10X.cpp
//  AUTHOR: Rob Tillaart
// VERSION: 0.2.3
// PURPOSE: Arduino Library for X9C10X series digital potentiometer.
//     URL: https://github.com/RobTillaart/X9C10X


#include "X9C10X_H595.h"

#define CS_HC595    5
#define CS_HC595_SET()  digitalWrite(CS_HC595, LOW)
#define CS_HC595_RESET()  digitalWrite(CS_HC595, HIGH)

//  minimum pulse width CLOCK = ? us (datasheet);
//  digitalWrite takes enough time on UNO / AVR so clock_delay == 0
//  Note that if clock pulses are long enough the data pulses are too.
#ifdef __AVR__
#define X9C10X_DELAY_MICROS         0
#else
#define X9C10X_DELAY_MICROS         1
#endif

#define X9C10X_UP                   HIGH
#define X9C10X_DOWN                 LOW

#define X9C10X_MAXPOT               99


/////////////////////////////////////////////////////////
//
//  MINIMALISTIC BASE CLASS
//
X9C::X9C()
{
}


void X9C::begin(uint8_t pulsePin, uint8_t directionPin, uint8_t selectPin, uint8_t *latchReg)
{
  _pulsePin     = pulsePin;
  _directionPin = directionPin;
  _selectPin    = selectPin;
  _latchReg     = latchReg;

  //  #7 order of the initialization does matter
  //     as it might introduce an unwanted STORE pulse.
  //     use of pull ups might be wise.
  if(_latchReg == NULL)
  {
    digitalWrite(_selectPin,    HIGH);
  }
  else
  {
    setOutBitHC595();
  } 

  digitalWrite(_pulsePin,     HIGH);
  digitalWrite(_directionPin, HIGH);

  ////// pinMode(_selectPin, OUTPUT);
  pinMode(_pulsePin, OUTPUT);
  pinMode(_directionPin, OUTPUT);

  //  wiper power up time. Page 5.
  delayMicroseconds(500);
}


bool X9C::incr()
{
  _move(X9C10X_UP);
  return true;
}


bool X9C::decr()
{
  _move(X9C10X_DOWN);
  return true;
}


void X9C::store()
{
  //  _pulsePin starts default HIGH
  if(_latchReg == NULL)
  {
    digitalWrite(_selectPin, LOW);
  }
  else
  {
    resetOutBitHC595();
  }
  #if X9C10X_DELAY_MICROS > 0
  delayMicroseconds(X9C10X_DELAY_MICROS);
  #endif
  if(_latchReg == NULL)
  {
    digitalWrite(_selectPin, HIGH);
  }
  else
  {
    setOutBitHC595();
  }
  delay(20);    //  Tcph  page 5
}


/////////////////////////////////////////////////////////
//
//  PROTECTED
//
void X9C::_move(uint8_t direction, uint8_t steps)
{
  digitalWrite(_directionPin, direction);
  delayMicroseconds(3);  // Tdi  (page 5)

  //  _pulsePin starts default HIGH
  if(_latchReg == NULL)
  {
    digitalWrite(_selectPin, LOW);
  }
  else
  {  
    resetOutBitHC595();
  }
  while (steps--)
  {
    digitalWrite(_pulsePin, HIGH);
    #if X9C10X_DELAY_MICROS > 0
    delayMicroseconds(X9C10X_DELAY_MICROS);
    #endif

    digitalWrite(_pulsePin, LOW);
    #if X9C10X_DELAY_MICROS > 0
    delayMicroseconds(X9C10X_DELAY_MICROS);
    #endif
  }
  //  _pulsePin == LOW, (No Store, page 7)
  if(_latchReg == NULL)
  {
    digitalWrite(_selectPin, HIGH);
  }
  else
  {
    setOutBitHC595();
  }
  // reset _pulsePin to default.
  digitalWrite(_pulsePin, HIGH);
}

void X9C::setOutBitHC595()
{
  CS_HC595_SET();
  *_latchReg = *_latchReg | (1 << _selectPin);
  // Serial.printf("s %d", *_latchReg);
  SPI.transfer(*_latchReg);
  CS_HC595_RESET();
}

void X9C::resetOutBitHC595()
{
  CS_HC595_SET();
  *_latchReg = *_latchReg & (~(1 << _selectPin));
  // Serial.printf("r %d", *_latchReg);
  SPI.transfer(~(1 << _selectPin));
  CS_HC595_RESET();
}



/////////////////////////////////////////////////////////
//
//  X9C10X  BASE CLASS
//
X9C10X::X9C10X(uint32_t maxOhm) : X9C()
{
  _maxOhm = maxOhm;
}


uint8_t X9C10X::setPosition(uint8_t position, bool forced)
{
  if (position > 99)
  {
    position = 99;
  }

  //  force to nearest end position first to minimize number of steps.
  if (forced)
  {
    if (position < 50)
    {
      _move(X9C10X_DOWN, 99);
      _position = 0;
    }
    else
    {
      _move(X9C10X_UP, 99);
      _position = 99;
    }
  }
  if (position > _position)
  {
    _move(X9C10X_UP, position - _position);
  }
  if (position < _position)
  {
    _move(X9C10X_DOWN, _position - position);
  }

  _position = position;
  return _position;
}


uint8_t X9C10X::getPosition()
{
  return _position;
}


bool X9C10X::incr()
{
  if (_position >= 99) return false;
  _position++;
  _move(X9C10X_UP);
  return true;
}


bool X9C10X::decr()
{
  if (_position == 0) return false;
  _position--;
  _move(X9C10X_DOWN);
  return true;
}


uint8_t X9C10X::store()
{
  X9C::store();
  return _position;
}


uint8_t X9C10X::restoreInternalPosition(uint8_t position)
{
  if (position > 99)
  {
    position = 99;
  }
  _position = position;
  return _position;
}


//  rounding needed!
uint32_t X9C10X::getOhm()
{
  return (_maxOhm * _position + 49) / 99;
};


uint32_t X9C10X::getMaxOhm()
{
  return _maxOhm;
};


//  rounding needed!
uint8_t X9C10X::Ohm2Position(uint32_t value, bool invert)
{
  if (value > _maxOhm) return 99;
  uint8_t val = (99 * value + _maxOhm/2) / _maxOhm;
  if (invert) return 99 - val;
  return val;
}


uint16_t X9C10X::getType()
{
  return _type;
};


/////////////////////////////////////////////////////////
//
//  SPECIFIC DERIVED DEVICE CLASSES

X9C102::X9C102(uint32_t ohm) : X9C10X(ohm)
{
  _type = 102;
}

X9C103::X9C103(uint32_t ohm) : X9C10X(ohm)
{
  _type = 103;
}

X9C104::X9C104(uint32_t ohm) : X9C10X(ohm)
{
  _type = 104;
}

X9C503::X9C503(uint32_t ohm) : X9C10X(ohm)
{
  _type = 503;
}

HC595::HC595() : latchReg(0x0F)
{

}

void HC595::begin()
{
  pinMode(CS_HC595, OUTPUT);
  CS_HC595_RESET();
  SPI.begin(18, 21, 23);
  int HC595_OUTPUT = 4;
  for(int i = 0; i < HC595_OUTPUT; i++ )
  {
    pot[i].begin(15, 4, i, &latchReg); 
  } 
}

//  -- END OF FILE --


