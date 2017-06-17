/*
 * Copyright (c) 2014-2017 Cesanta Software Limited
 * All rights reserved
 *
 * This example demonstrates usage of AWS IoT Device Shadow API.
 * http://docs.aws.amazon.com/iot/latest/developerguide/iot-thing-shadows.html
 *
 * mOS takes care of the low-level stuff like MQTT topic and state management
 * and provides simple API to the user's application, consisting of:
 *
 * - State callback, invoked in response to successful get or update
 *   request or when a state delta is received.
 * - A way to provide state updates via simple printf-like function.
 */

#include <stdio.h>

#include "common/cs_dbg.h"
#include "common/platform.h"
#include "frozen/frozen.h"
#include "fw/src/mgos_app.h"
#include "fw/src/mgos_aws_shadow.h"
#include "fw/src/mgos_gpio.h"

#if CS_PLATFORM == CS_P_ESP8266
/*
 * On ESP-12E there is a blue LED connected to GPIO2 (aka U1TX).
 * LED is attached to Vdd, so writing 0 turns it on while 1 turns it off.
 */
#define LED_MOTOR 4
#define LED_FAN 12
#define LED_LIGHT 14
#define LED_OFF 1
#define LED_ON 0
#define BUTTON_MOTOR 0 /* Usually a "Flash" button. */
#define BUTTON_FAN 2 /* PIN 19 */
#define BUTTON_LIGHT 5 /* PIN 20 */
#define BUTTON_PULL MGOS_GPIO_PULL_UP
#define BUTTON_EDGE MGOS_GPIO_INT_EDGE_POS
#elif CS_PLATFORM == CS_P_ESP32
/* Unfortunately, there is no LED on DevKitC, so this is random GPIO. */
#define LED_GPIO 17
#define LED_OFF 0
#define LED_ON 1
#define BUTTON_GPIO 0 /* Usually a "Flash" button. */
#define BUTTON_PULL MGOS_GPIO_PULL_UP
#define BUTTON_EDGE MGOS_GPIO_INT_EDGE_POS
#elif CS_PLATFORM == CS_P_CC3200
/* On CC3200 LAUNCHXL pin 64 is the red LED. */
#define LED_GPIO 64 /* The red LED on LAUNCHXL */
#define LED_OFF 0
#define LED_ON 1
#define BUTTON_GPIO 15                  /* SW2 on LAUNCHXL */
#define BUTTON_PULL MGOS_GPIO_PULL_NONE /* External pull-downs */
#define BUTTON_EDGE MGOS_GPIO_INT_EDGE_NEG
#elif(CS_PLATFORM == CS_P_STM32) && defined(BSP_NUCLEO_F746ZG)
/* Nucleo-144 F746 */
#define LED_GPIO STM32_PIN_PB7 /* Blue LED */
#define LED_OFF 0
#define LED_ON 1
#define BUTTON_GPIO STM32_PIN_PC13 /* Blue user button */
#define BUTTON_PULL MGOS_GPIO_PULL_NONE
#define BUTTON_EDGE MGOS_GPIO_INT_EDGE_POS
#elif(CS_PLATFORM == CS_P_STM32) && defined(BSP_DISCO_F746G)
/* Discovery-0 F746 */
#define LED_GPIO STM32_PIN_PI1 /* Green LED */
#define LED_OFF 0
#define LED_ON 1
#define BUTTON_GPIO STM32_PIN_PI11 /* Blue user button */
#define BUTTON_PULL MGOS_GPIO_PULL_NONE
#define BUTTON_EDGE MGOS_GPIO_INT_EDGE_POS
#else
#error Unknown platform
#endif

static int s_motor,new_motor = false;
static int s_fan,new_fan = false;
static int s_light,new_light = false;
#define STATE_FMT "{motor: %B, fan: %B, light: %B}"

static void report_state(void) {
  char buf[100];
  struct json_out out = JSON_OUT_BUF(buf, sizeof(buf));
  json_printf(&out, STATE_FMT, s_motor, s_fan, s_light);
  LOG(LL_INFO, ("== Reporting state: %s", buf));
  mgos_aws_shadow_updatef(0, "{reported:" STATE_FMT "}", s_motor, s_fan, s_light);
}

void update_led(void) {
  mgos_gpio_write(LED_MOTOR, (s_motor ? LED_ON : LED_OFF));
  mgos_gpio_write(LED_FAN, (s_fan ? LED_ON : LED_OFF));
  mgos_gpio_write(LED_LIGHT, (s_light ? LED_ON : LED_OFF));
}

static void aws_shadow_state_handler(void *arg, enum mgos_aws_shadow_event ev,
                                     uint64_t version,
                                     const struct mg_str reported,
                                     const struct mg_str desired,
                                     const struct mg_str reported_md,
                                     const struct mg_str desired_md) {
  LOG(LL_INFO, ("== Event: %d (%s), version: %llu", ev, mgos_aws_shadow_event_name(ev), version));
  if (ev == MGOS_AWS_SHADOW_CONNECTED) {
    report_state();
    return;
  }
  if (ev != MGOS_AWS_SHADOW_GET_ACCEPTED &&
      ev != MGOS_AWS_SHADOW_UPDATE_DELTA) {
    return;
  }
  LOG(LL_INFO, ("Reported state: %.*s", (int) reported.len, reported.p));
  LOG(LL_INFO, ("Desired state : %.*s", (int) desired.len, desired.p));
  LOG(LL_INFO, ("Reported metadata: %.*s", (int) reported_md.len, reported_md.p));
  LOG(LL_INFO, ("Desired metadata : %.*s", (int) desired_md.len, desired_md.p));
  /*
   * Here we extract values from previosuly reported state (if any)
   * and then override it with desired state (if present).
   */
  json_scanf(reported.p, reported.len, STATE_FMT, &s_motor, &s_fan, &s_light);
  json_scanf(desired.p, desired.len, STATE_FMT, &s_motor, &s_fan, &s_light);
  update_led();
  if (ev == MGOS_AWS_SHADOW_UPDATE_DELTA) {
    report_state();
  }
  (void) arg;
}

static void button_cb(int pin, void *arg) {
  /* We do not change local state here. In response to this update AWS will
   * generate a delta event which will be processed as any other. */
  
  if(pin==BUTTON_MOTOR){
	  new_motor = !s_motor;
	  LOG(LL_INFO, ("Motor state changed by button press to %B", new_motor));
  }
  if(pin==BUTTON_FAN){
	  new_fan = !s_fan;
	  LOG(LL_INFO, ("Fan state changed by button press to %B", new_fan));
  }
  if(pin==BUTTON_LIGHT){
	  new_light = !s_light;
	  LOG(LL_INFO, ("Light state changed by button press to %B", new_light));
  }
  mgos_aws_shadow_updatef(0, "{desired:{motor: %B, fan: %B, light: %B}}",
                                         new_motor, new_fan, new_light);
  (void) pin;
  (void) arg;
}

enum mgos_app_init_result mgos_app_init(void) {
  mgos_gpio_set_mode(LED_MOTOR, MGOS_GPIO_MODE_OUTPUT);
  mgos_gpio_set_mode(LED_FAN, MGOS_GPIO_MODE_OUTPUT);
  mgos_gpio_set_mode(LED_LIGHT, MGOS_GPIO_MODE_OUTPUT);
  
  update_led();
  mgos_gpio_set_button_handler(BUTTON_MOTOR, BUTTON_PULL, BUTTON_EDGE,
                               50 /* debounce_ms */, button_cb, NULL);
  mgos_gpio_set_button_handler(BUTTON_FAN, BUTTON_PULL, BUTTON_EDGE,
							   50 /* debounce_ms */, button_cb, NULL);
  mgos_gpio_set_button_handler(BUTTON_LIGHT, BUTTON_PULL, BUTTON_EDGE,
							   50 /* debounce_ms */, button_cb, NULL);
  mgos_aws_shadow_set_state_handler(aws_shadow_state_handler, NULL);
  return MGOS_APP_INIT_SUCCESS;
}
