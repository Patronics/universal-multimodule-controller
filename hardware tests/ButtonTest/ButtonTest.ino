
#include <Bounce2.h>

#define LED_PIN 33

//UI buttons
#define OK_BUTTON_PIN 28
#define BACK_BUTTON_PIN 32
#define UP_BUTTON_PIN 27
#define DOWN_BUTTON_PIN 29
#define LEFT_BUTTON_PIN 26
#define RIGHT_BUTTON_PIN 31
#define SYS_BUTTON_PIN 25
#define MDL_BUTTON_PIN 30



enum NavButton {
  NO_BUTTON_PRESSED,  //value corresponds to 0, so falsy
  OK_BUTTON,          //value 1, etc.
  BACK_BUTTON,
  UP_BUTTON,
  DOWN_BUTTON,
  LEFT_BUTTON,
  RIGHT_BUTTON,
  SYS_BUTTON,
  MDL_BUTTON
};

const uint16_t BUTTON_DEBOUNCE_INTERVAL = 5;
#define BUTTON_INPUT_TYPE INPUT_PULLUP
#define BUTTON_PRESSED_STATE LOW

Bounce2::Button okButton;
Bounce2::Button backButton;
Bounce2::Button upButton;
Bounce2::Button downButton;
Bounce2::Button leftButton;
Bounce2::Button rightButton;
Bounce2::Button sysButton;
Bounce2::Button mdlButton;

void setup() {
  Serial.begin(115200); //init USB serial
  Serial.print("starting button test");

  pinMode(LED_PIN,OUTPUT);
  // put your setup code here, to run once:
  //bounce2 setup
  okButton.attach(OK_BUTTON_PIN, BUTTON_INPUT_TYPE);
  backButton.attach(BACK_BUTTON_PIN, BUTTON_INPUT_TYPE);
  upButton.attach(UP_BUTTON_PIN, BUTTON_INPUT_TYPE);
  downButton.attach(DOWN_BUTTON_PIN, BUTTON_INPUT_TYPE);
  leftButton.attach(LEFT_BUTTON_PIN, BUTTON_INPUT_TYPE);
  rightButton.attach(RIGHT_BUTTON_PIN, BUTTON_INPUT_TYPE);
  sysButton.attach(SYS_BUTTON_PIN, BUTTON_INPUT_TYPE);
  mdlButton.attach(MDL_BUTTON_PIN, BUTTON_INPUT_TYPE);

  //debounce interval
  okButton.interval(BUTTON_DEBOUNCE_INTERVAL);
  backButton.interval(BUTTON_DEBOUNCE_INTERVAL);
  upButton.interval(BUTTON_DEBOUNCE_INTERVAL);
  downButton.interval(BUTTON_DEBOUNCE_INTERVAL);
  leftButton.interval(BUTTON_DEBOUNCE_INTERVAL);
  rightButton.interval(BUTTON_DEBOUNCE_INTERVAL);
  sysButton.interval(BUTTON_DEBOUNCE_INTERVAL);
  mdlButton.interval(BUTTON_DEBOUNCE_INTERVAL);

  okButton.setPressedState(BUTTON_PRESSED_STATE);
  backButton.setPressedState(BUTTON_PRESSED_STATE);
  upButton.setPressedState(BUTTON_PRESSED_STATE);
  downButton.setPressedState(BUTTON_PRESSED_STATE);
  leftButton.setPressedState(BUTTON_PRESSED_STATE);
  rightButton.setPressedState(BUTTON_PRESSED_STATE);
  sysButton.setPressedState(BUTTON_PRESSED_STATE);
  mdlButton.setPressedState(BUTTON_PRESSED_STATE);
}

void handleNavButton(NavButton currentButton){
  digitalWrite(LED_PIN, HIGH );
  Serial.print(currentButton);
  //delay(1000);
}

void loop() {
  // put your main code here, to run repeatedly:
  okButton.update();
  backButton.update();
  upButton.update();
  downButton.update();
  leftButton.update();
  rightButton.update();
  sysButton.update();
  mdlButton.update();


  NavButton currentNavButton = NO_BUTTON_PRESSED;
  if (okButton.pressed()) {
    currentNavButton = OK_BUTTON;
  }
  if (backButton.pressed()) {
    currentNavButton = BACK_BUTTON;
  }
  if (upButton.pressed()) {
    currentNavButton = UP_BUTTON;
  }
  if (downButton.pressed()) {
    currentNavButton = DOWN_BUTTON;
  }
  if (leftButton.pressed()) {
    currentNavButton = LEFT_BUTTON;
  }
  if (rightButton.pressed()) {
    currentNavButton = RIGHT_BUTTON;
  }
  if (sysButton.pressed()) {
    currentNavButton = SYS_BUTTON;
  }
  if (mdlButton.pressed()) {
    currentNavButton = MDL_BUTTON;
  }
  digitalWrite(LED_PIN, LOW );
  if(currentNavButton){   //NO_BUTTON_PRESSED is falsy
    handleNavButton(currentNavButton);
  }

}
