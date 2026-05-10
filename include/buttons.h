#pragma once
#include <Bounce2.h>

// Конфигурация кнопок
#define BTN_UP      1
#define BTN_DOWN    2
#define BTN_LEFT    41
#define BTN_RIGHT   42
#define BTN_OK      40
#define BTN_POWER   6


// Объекты для обработки кнопок
Bounce btnUp = Bounce();
Bounce btnDown = Bounce();
Bounce btnLeft = Bounce();
Bounce btnRight = Bounce();
Bounce btnOK = Bounce();
Bounce btnPower = Bounce();

void updateButtons() {
  btnUp.update();
  btnDown.update();
  btnLeft.update();
  btnRight.update();
  btnOK.update();
  btnPower.update();
}
