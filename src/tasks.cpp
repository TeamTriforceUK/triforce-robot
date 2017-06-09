/* Copyright (c) 2017 Cameron A. Craig, Euan W. Mutch, MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute,
 * sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/**
* @file tasks.cpp
* @author Cameron A. Craig, Euan W. Mutch
* @date 15 May 2017
* @copyright 2017 Cameron A. Craig, Euan W. Mutch
* @brief Defines struct to share data between threads.
*/

#include "mbed.h"
#include "tasks.h"
#include "bno055.h"
#include "types.h"
#include "thread_args.h"
#include "tmath.h"
#include "states.h"
#include "utilc-logging.h"
#include "commands.h"
#include "return_codes.h"
#include "tele_params.h"
#include "utils.h"

void task_start(thread_args_t *targs, unsigned task_id) {
  targs->serial->printf("started task %d (%s)\tstack [alloc: %d, used: %d, free: %d]\r\n", task_id, tasks[task_id].name, targs->threads[task_id].stack_size(), targs->threads[task_id].used_stack(), targs->threads[task_id].free_stack());

  // targs->serial->printf("Using %d bytes of stack.\r\n", task_id, tasks[task_id].name);
}

/**
* @brief Execute commands as they become available on the mail queue.
* @param [in/out] targs Thread arguments.
*/
#ifdef TASK_PROCESS_COMMANDS
void task_process_commands(const void *targs) {
  thread_args_t * args = (thread_args_t *) targs;
  task_start(args, TASK_PROCESS_COMMANDS_ID);
  while (args->active) {
    if (args->tasks[TASK_PROCESS_COMMANDS_ID].active) {
      osEvent evt;
      while ((evt = args->command_queue->get(1)).status == osEventMail) {
          command_t *command_q = (command_t*) evt.value.p;
          int err;
          if ((err = command_execute(command_q, args)) != RET_OK) {
            LOG("\rError: %s\r\n", err_to_str(err));
          } else {
            LOG("\rCommand succesful\r\n");
          }
          args->command_queue->free(command_q);
      }
    }
  }
}
#endif

/**
* @brief Creates primative commmand line interface on serial port.
* @param [in/out] targs Thread arguments.
*/
#ifdef TASK_READ_SERIAL
void task_read_serial(const void *targs){
  thread_args_t * args = (thread_args_t *) targs;
  task_start(args, TASK_READ_SERIAL_ID);

  char buffer[100];
  int pos = 0;
  LOG( "$");
  bool readable;
  while (args->active) {
    if (args->tasks[TASK_READ_SERIAL_ID].active) {
      bool readable = args->serial->readable();
      // LOG("test\r\n");
      if (readable) {
        buffer[pos] = args->serial->getc();

        // If ENTER key is pressed, execute command
        if (buffer[pos] == '\r') {
          buffer[pos+1] = NULL;
          LOG("\r\n");
          command_t command;
          // Generate a command structure for the command given
          if (!command_generate(&command, buffer)) {
            LOG("\rCommand not recognised!\r\n");
          } else {
            command_t *command_q = args->command_queue->alloc();
            memcpy(command_q, &command, sizeof(command_t));
            args->command_queue->put(command_q);
          }

          pos = -1;
        }
        if (buffer[pos] == '\b') {
          buffer[pos] = NULL;
          pos--;
        }
        buffer[pos+1] = NULL;
        LOG("\r$ %s", buffer);
        pos++;
      }
    }
  }
}
#endif


/**
* @brief Set mbed LED0 --> LED3 to represent the arming state.
* @param [in/out] targs Thread arguments.
*/
#ifdef TASK_LED_STATE
void task_state_leds(const void *targs){
  thread_args_t * args = (thread_args_t *) targs;
  task_start(args, TASK_LED_STATE_ID);

  static state_t previous_state = args->state;
  bool first_time = true;
  bool weapon_only_ripple[4] = {true, false, false, false};
  bool tmp_ripple;

  while (args->active) {
    if (args->tasks[TASK_LED_STATE_ID].active) {
      if (args->state != previous_state || first_time) {
        LOG("state change: %s --> %s\r\n", state_to_str(previous_state), state_to_str(args->state));
        switch (args->state) {
          case STATE_DISARMED:
            args->leds[0]->write(false);
            args->leds[1]->write(false);
            args->leds[2]->write(false);
            args->leds[3]->write(false);
            break;
          case STATE_DRIVE_ONLY:
            args->leds[0]->write(true);
            args->leds[1]->write(true);
            args->leds[2]->write(false);
            args->leds[3]->write(false);
            break;
          case STATE_WEAPON_ONLY:
            break;
          case STATE_FULLY_ARMED:
            args->leds[0]->write(true);
            args->leds[1]->write(true);
            args->leds[2]->write(true);
            args->leds[3]->write(true);
            break;
        }
      }

      /* The weapon only state is a special case where fancy LED strobing is used
         We need to do this outside the above switch statement, as we require a
         LED change every loop, rather than just on a state change.
      */
      if (args->state == STATE_WEAPON_ONLY) {
        args->leds[0]->write(weapon_only_ripple[0]);
        args->leds[1]->write(weapon_only_ripple[1]);
        args->leds[2]->write(weapon_only_ripple[2]);
        args->leds[3]->write(weapon_only_ripple[3]);

        // Move LED along by one
        tmp_ripple = weapon_only_ripple[3];
        memmove(&weapon_only_ripple[1], &weapon_only_ripple[0], sizeof(bool)*3);
        weapon_only_ripple[0] = tmp_ripple;
      }

      previous_state = args->state;
      first_time = false;
      Thread::wait(100);
    }
  }
}
#endif

/**
* @brief Convert receiver signals into commands and add to the command queue.
* @param [in/out] targs Thread arguments.
*/
#ifdef TASK_READ_RECEIVERS
void task_read_receiver(const void *targs) {
  thread_args_t * args = (thread_args_t *) targs;
  task_start(args, TASK_READ_RECEIVERS_ID);
  int controller, channel;
  float pw, v, min, max;
  while (args->active) {
    if (args->tasks[TASK_READ_RECEIVERS_ID].active) {
      for (controller = 0; controller < RC_NUMBER_CONTROLLERS; controller++) {
        for (channel = 0; channel < RC_NUMBER_CHANNELS; channel++) {
          // Read raw pulse width
          pw = args->receiver[controller].channel[channel]->pulsewidth();

          // Get min and max pulsewidths fot this channel
          min = args->channel_limits[controller][channel].min;
          max = args->channel_limits[controller][channel].max;

          // Make sure pw doesn't leave bounds due to imperfect calibration
          pw = clamp(pw, min, max);

          // Convert into float value between 0 and 100, based on max and min
          v = ( (pw - min) / (max - min) ) * 100.0f;

          // Set the control value for otehr threads to use
          args->controls[controller].channel[channel] = v;

          // For debugging purposes
          // args->serial->printf("con %d chan %d: [pw: %.0f, min: %.0f, max: %.0f, v: %.0f]\r\n", controller, channel, pw, min, max, v);
        }
      }
    }
    // For debugging purposes
    // Thread::wait(1000);
  }
}
#endif

/**
* @brief Change the arming state based on the arming switch.
* @param [in/out] targs Thread arguments.
*/
#ifdef TASK_ARMING
void task_arming(const void *targs) {
  thread_args_t * args = (thread_args_t *) targs;
  task_start(args, TASK_ARMING_ID);

  bool drive_switch, weapon_switch, drive_arm, weapon_arm, drive_stalled, weapon_stalled;

  while (args->active) {
    if (args->tasks[TASK_ARMING_ID].active) {
      weapon_switch = (args->controls[0].channel[RC_0_ARM_SWITCH] > RC_SWITCH_MIDPOINT);
      drive_switch = (args->controls[1].channel[RC_1_ARM_SWITCH] > RC_SWITCH_MIDPOINT);

      /* If a transmitter is lost (turned off/out of range),
         disable arming for that TX.
      */
      drive_stalled = is_drive_stalled(args);
      weapon_stalled = is_weapon_stalled(args);

      weapon_arm = weapon_switch && !weapon_stalled &&
        BETWEEN(args->controls[0].channel[RC_0_THROTTLE], 0, 2) &&
        BETWEEN(args->controls[0].channel[RC_0_ELEVATION], 45, 55) &&
        BETWEEN(args->controls[0].channel[RC_0_RUDDER], 45, 55) &&
        BETWEEN(args->controls[0].channel[RC_0_AILERON], 45, 55);

      drive_arm = drive_switch && !drive_stalled &&
        BETWEEN(args->controls[1].channel[RC_1_THROTTLE], 0, 2) &&
        BETWEEN(args->controls[1].channel[RC_1_ELEVATION], 45, 55) &&
        BETWEEN(args->controls[1].channel[RC_1_RUDDER], 45, 55) &&
        BETWEEN(args->controls[1].channel[RC_1_AILERON], 45, 55);

      //Debug: Print RX values
      // args->serial->printf("drive_switch : %.0f (%s)\r\n", args->controls[0].channel[RC_0_ARM_SWITCH], drive_switch ? "On" : "Off");
      // args->serial->printf("weapon_switch: %.0f (%s)\r\n", args->controls[1].channel[RC_1_ARM_SWITCH], weapon_switch ? "On" : "Off");

      switch (args->state) {
        /* From the fully armed state we can only decrease the arm state,
        so we don't need to be concerned with the stick positions. */
        case STATE_FULLY_ARMED:
          if (!drive_switch && !weapon_switch) {
            args->state = STATE_DISARMED;
          } else if (drive_switch && !weapon_switch) {
            args->state = STATE_DRIVE_ONLY;
          } else if (!drive_switch && weapon_switch) {
            args->state = STATE_WEAPON_ONLY;
          }
          break;
        case STATE_DRIVE_ONLY:
          if (!drive_switch) {
            args->state = STATE_DISARMED;
          } else if (weapon_arm) {
            args->state = STATE_FULLY_ARMED;
          }
          break;
        case STATE_WEAPON_ONLY:
          if (!weapon_switch) {
            args->state = STATE_DISARMED;
          } else if (drive_arm) {
            args->state = STATE_FULLY_ARMED;
          }
          break;
        case STATE_DISARMED:
          if (drive_arm && weapon_arm) {
            args->state = STATE_FULLY_ARMED;
          } else if (drive_arm) {
            args->state = STATE_DRIVE_ONLY;
          } else if (weapon_arm) {
            args->state = STATE_WEAPON_ONLY;
          }
      }
    }
    Thread::wait(1000);
  }
}
#endif

#ifdef TASK_FAILSAFE
void task_failsafe(const void *targs) {
  thread_args_t * args = (thread_args_t *) targs;
  task_start(args, TASK_FAILSAFE_ID);

  bool drive_inactive, weapon_inactive;

  while (args->active) {
    if (args->tasks[TASK_FAILSAFE_ID].active) {
      weapon_inactive = is_weapon_stalled(args);
      drive_inactive = is_drive_stalled(args);

      switch (args->state) {
        case STATE_FULLY_ARMED:
          if (drive_inactive && weapon_inactive) {
            args->state = STATE_DISARMED;
          } else if (drive_inactive && !weapon_inactive) {
            args->state = STATE_WEAPON_ONLY;
          } else if (!drive_inactive && weapon_inactive) {
            args->state = STATE_DRIVE_ONLY;
          }
          break;
        case STATE_DRIVE_ONLY:
          if (drive_inactive) {
            args->state = STATE_DISARMED;
          }
          break;
        case STATE_WEAPON_ONLY:
          if (weapon_inactive){
            args->state = STATE_DISARMED;
          }
          break;
      }
    }
    // For debugging purposes
    // Thread::wait(1000);
    // args->serial->printf("drive stall timer: %d ms\r\n", args->receiver[1].channel[0]->stallTimer.read_ms());
    // args->serial->printf("drive stall timer: %d ms\r\n", args->receiver[1].channel[1]->stallTimer.read_ms());
    // args->serial->printf("drive stall timer: %d ms\r\n", args->receiver[1].channel[2]->stallTimer.read_ms());
    // args->serial->printf("drive stall timer: %d ms\r\n", args->receiver[1].channel[3]->stallTimer.read_ms());
    // args->serial->printf("drive stall timer: %d ms\r\n", args->receiver[1].channel[4]->stallTimer.read_ms());
    // args->serial->printf("drive stall timer: %d ms\r\n", args->receiver[1].channel[5]->stallTimer.read_ms());
    // args->serial->printf("weapon stall timer: %d ms\r\n", args->receiver[0].channel[0]->stallTimer.read_ms());
    // args->serial->printf("weapon stall timer: %d ms\r\n", args->receiver[0].channel[1]->stallTimer.read_ms());
    // args->serial->printf("weapon stall timer: %d ms\r\n", args->receiver[0].channel[2]->stallTimer.read_ms());
    // args->serial->printf("weapon stall timer: %d ms\r\n", args->receiver[0].channel[3]->stallTimer.read_ms());
    // args->serial->printf("weapon stall timer: %d ms\r\n", args->receiver[0].channel[4]->stallTimer.read_ms());
    // args->serial->printf("weapon stall timer: %d ms\r\n", args->receiver[0].channel[5]->stallTimer.read_ms());
  }
}
#endif

/** Output to OmniMixer and ESCs
 *
 * Sends calculated output values to devices
 */
#ifdef TASK_SET_ESCS
void task_set_escs(const void *targs) {
  thread_args_t * args = (thread_args_t *) targs;
  task_start(args, TASK_SET_ESCS_ID);

  while(args->active) {
    if (args->tasks[TASK_SET_ESCS_ID].active) {
      // Calculate output values
      // args->outputs.wheel_1 = args->controls[1].channel[RC_1_THROTTLE];
      // args->outputs.wheel_2 = args->controls[1].channel[RC_1_THROTTLE];
      // args->outputs.wheel_3 = args->controls[1].channel[RC_1_THROTTLE];

    // Set weapon motor ESCs
    args->outputs.weapon_motor_1 = args->controls[0].channel[RC_0_THROTTLE];
    args->outputs.weapon_motor_2 = args->controls[0].channel[RC_0_THROTTLE];
    args->outputs.weapon_motor_3 = args->controls[0].channel[RC_0_THROTTLE];



    float x = args->controls[1].channel[RC_1_AILERON] - 50.0f;
    float y = args->controls[1].channel[RC_1_ELEVATION] - 50.0f;

    float theta = (float)atan2((double)x, (double)y);
    float magnitude = (float)sqrt((double)((x*x)+(y*y)));

    if(magnitude > 5.0f) {

        float vx = magnitude * sin(theta);
        float vy = magnitude * cos(theta);
        const float sqrt3o2 = 1.0*sqrt(3.0)/2.0;

        float w0 = -vx;                   // v dot [-1, 0] / 25mm
        float w1 = 0.5*vx - sqrt3o2 * vy; // v dot [1/2, -sqrt(3)/2] / 25mm
        float w2 = 0.5*vx + sqrt3o2 * vy; // v dot [1/2, +sqrt(3)/2] / 25mm
        // #if defined (PC_DEBUGGING) && defined (DEBUG_CONTROLS)
        // pc.printf("Calculated Controls: (%7.2f) \t (%7.2f) \t (%7.2f) \r\n", w0, w1, w2);
        // #endif
        float w0_speed =  map(w0, -70, 70, 0, 100);
        float w1_speed =  map(w1, -70, 70, 0, 100);
        float w2_speed =  map(w2, -70, 70, 0, 100);

        /* Add in rotation */
        // #if defined (PC_DEBUGGING) && defined (DEBUG_CONTROLS)
        // pc.printf("Mapped Controls: (%7.2f) \t (%7.2f) \t (%7.2f) \r\n", w0_speed, w1_speed, w2_speed);
        // #endif
        args->outputs.wheel_1 += w0_speed -50;
        args->outputs.wheel_2 += w1_speed -50;
        args->outputs.wheel_3 += w2_speed -50;


    } else {
        args->outputs.wheel_1 = 50;
        args->outputs.wheel_2 = 50;
        args->outputs.wheel_3 = 50;
    }

     args->outputs.wheel_1 += args->controls[1].channel[RC_1_RUDDER] - 50;
     args->outputs.wheel_2 += args->controls[1].channel[RC_1_RUDDER] - 50;
     args->outputs.wheel_3 += args->controls[1].channel[RC_1_RUDDER] - 50;



    /* Clamp outputs to correct range */
    args->outputs.wheel_1 = clamp(args->outputs.wheel_1, 0, 100);
    args->outputs.wheel_2 = clamp(args->outputs.wheel_2, 0, 100);
    args->outputs.wheel_3 = clamp(args->outputs.wheel_3, 0, 100);

      switch (args->state) {
        case STATE_FULLY_ARMED:
          args->escs.weapon[0]->setThrottle(args->outputs.weapon_motor_1);
          args->escs.weapon[1]->setThrottle(args->outputs.weapon_motor_2);
          args->escs.weapon[2]->setThrottle(args->outputs.weapon_motor_3);
          args->escs.drive[0]->setThrottle(args->outputs.wheel_1);
          args->escs.drive[1]->setThrottle(args->outputs.wheel_2);
          args->escs.drive[2]->setThrottle(args->outputs.wheel_3);
          break;
        case STATE_DRIVE_ONLY:
          args->escs.drive[0]->setThrottle(args->outputs.wheel_1);
          args->escs.drive[1]->setThrottle(args->outputs.wheel_2);
          args->escs.drive[2]->setThrottle(args->outputs.wheel_3);
          break;
        case STATE_WEAPON_ONLY:
          args->escs.weapon[0]->setThrottle(args->outputs.weapon_motor_1);
          args->escs.weapon[1]->setThrottle(args->outputs.weapon_motor_2);
          args->escs.weapon[2]->setThrottle(args->outputs.weapon_motor_3);
          break;
        case STATE_DISARMED:
          args->escs.drive[0]->failsafe();
          args->escs.drive[1]->failsafe();
          args->escs.drive[2]->failsafe();
          args->escs.weapon[0]->failsafe();
          args->escs.weapon[1]->failsafe();
          args->escs.weapon[2]->failsafe();
      }
    }
  }
}
#endif

#ifdef TASK_CALC_ORIENTATION
void task_calc_orientation(const void *targs) {
  thread_args_t * args = (thread_args_t *) targs;
  task_start(args, TASK_CALC_ORIENTATION_ID);

  while (args->active) {
    if (args->tasks[TASK_CALC_ORIENTATION_ID].active) {
      /* If there is an error then we maintain the same
       * orientation to stop random control flipping */
      if (!bno055_healthy()) {
          LOG("ERROR: BNO055 has an error/status problem!!!\r\n");
      } else {
          /* Read in the Euler angles */
          args->orientation = bno055_read_euler_angles();

          /* We are upside down in range -30 -> -90
           * the sensor will report -60 when inverted */
          if (args->orientation.roll < -30 && args->orientation.roll > -90){
              args->inverted = true;
          } else {
              args->inverted = false;
          }
          #if defined (PC_DEBUGGING) && defined (DEBUG_ORIENTATION)
          args->serial->printf("Inverted= %s \t (%7.2f) \r\n", args->inverted ? "true" : "false", orientation.roll);
          #endif
      }
    }
  }
}
#endif

#ifdef TASK_COLLECT_TELEMETRY
void task_collect_telemetry(const void *targs) {
  thread_args_t * args = (thread_args_t *) targs;
  task_start(args, TASK_COLLECT_TELEMETRY_ID);

  euler_t e;
  int i;
  while (args->active) {
    if (args->tasks[TASK_COLLECT_TELEMETRY_ID].active) {
      for (i = 0; i < NUM_TELE_COMMANDS; i++) {
        switch (tele_commands[i].id) {
          case CID_RING_RPM:
            // TODO(camieac): Add support for RPM sensing
            tele_commands[i].param.f = 0.00f;
            break;
          case CID_CON_1_RPM:
            // TODO(camieac): Add support for RPM sensing
            tele_commands[i].param.f = 0.00f;
            break;
          case CID_CON_2_RPM:
            // TODO(camieac): Add support for RPM sensing
            tele_commands[i].param.f = 0.00f;
            break;
          case CID_ACCEL_X:
          case CID_ACCEL_Y:
          case CID_ACCEL_Z:
            e = bno055_read_accel();
            tele_commands[CID_ACCEL_X].param.f = e.x;
            tele_commands[CID_ACCEL_Y].param.f = e.y;
            tele_commands[CID_ACCEL_Z].param.f = e.z;
            // We do x, y and z in one op, so skip 2 once done
            if (i == CID_ACCEL_X) {
              i+=2;
            }
            break;
          case CID_PITCH:
          case CID_ROLL:
          case CID_YAW:
            e = bno055_read_euler_angles();
            tele_commands[CID_PITCH].param.f = e.pitch;
            tele_commands[CID_ROLL].param.f = e.roll;
            tele_commands[CID_YAW].param.f = e.heading;
            // We do x, y and z in one op, so skip 2 once done
            if (i == CID_PITCH) {
              i+=2;
            }
            break;
          case CID_WEAPON_VOLTAGE:
          case CID_DRIVE_VOLTAGE:
            break;
          case CID_AMBIENT_TEMP:
            tele_commands[i].param.i = bno055_read_temp();
            break;
          case CID_ESP_LED:
            //tele_commands[i].param.i = vi;
            break;
          case CID_ARM_STATUS:
            tele_commands[i].param.i = args->state;
            break;
          default:
            args->serial->puts("UNSUPPORTED TELE COMMAND\r\n");
        }
      }
      wait(1);
    }
  }
}
#endif

#ifdef TASK_STREAM_TELEMETRY
void task_stream_telemetry(const void *targs) {
  thread_args_t * args = (thread_args_t *) targs;
  task_start(args, TASK_STREAM_TELEMETRY_ID);

  int i = 0;
  while (args->active) {
    /* The ESP looks for a carriage return character to delimit a command. */
    if (args->tasks[TASK_STREAM_TELEMETRY_ID].active) {
      for (i = 0; i < NUM_TELE_COMMANDS; i++) {
        switch (tele_commands[i].type) {
          case CT_FLOAT:
            args->esp_serial->printf(
              "%s %.2f\r",
              tele_commands[i].name,
              tele_commands[i].param.f);
          break;
          case CT_INT:
            args->esp_serial->printf(
              "%s %d\r",
              tele_commands[i].name,
              tele_commands[i].param.i);
            break;
          case CT_BOOLEAN:
            args->esp_serial->printf(
              "%s %s\r",
              tele_commands[i].name,
              tele_commands[i].param.b ? "ON" : "OFF");
            break;
          case CT_NONE:
          default:
            args->serial->printf("Type not yet supported for streaming.\r\n");
            break;
        }
      }
      wait(1);
    }
  }
}
#endif


void task_print_channels(const void *targs) {
  thread_args_t * args = (thread_args_t *) targs;

  while (args->active) {
    int controller, channel;
    for (controller = 0; controller < RC_NUMBER_CONTROLLERS; controller++) {
      args->serial->printf("Controller %d\r\n", controller+1);
      for (channel = 0; channel < RC_NUMBER_CHANNELS; channel++) {
        args->serial->printf("Channel %d: %d\r\n", channel+1, convert_pulsewidth(args->receiver[controller].channel[channel]->pulsewidth()));
      }
    }
    Thread::wait(1000);
  }
}

#ifdef TASK_CALIBRATE_CHANNELS
void task_calibrate_channels(const void *targs) {
  thread_args_t * args = (thread_args_t *) targs;
  task_start(args, TASK_CALIBRATE_CHANNELS_ID);

  int controller, channel;
  unsigned calibration_time;
  unsigned calibration_time_ms = 10000;
  unsigned calibration_tick = 100;
  float tmp;

  while (args->active) {
    // args->serial->printf("t %d is %s\r\n", TASK_CALIBRATE_CHANNELS, args->tasks[TASK_CALIBRATE_CHANNELS].active ? "true" : "false");
    if (args->tasks[TASK_CALIBRATE_CHANNELS_ID].active == true) {
      args->serial->printf("t %d is %s\r\n", TASK_CALIBRATE_CHANNELS_ID, args->tasks[TASK_CALIBRATE_CHANNELS_ID].active ? "true" : "false");

      args->serial->printf("Controller calibration beginning,\r\n");
      args->serial->printf("move controller sticks & switches to extremities.\r\n");
      calibration_time = calibration_time_ms;

      // Set all limits to extremes
      for (controller = 0; controller < RC_NUMBER_CONTROLLERS; controller++) {
        for (channel = 0; channel < RC_NUMBER_CHANNELS; channel++) {
          args->channel_limits[controller][channel].min = 10000.0f;
          args->channel_limits[controller][channel].max = -10000.0f;
        }
      }

      // Find min and max pulsewidths for each channel
      while(calibration_time > 0) {

        if(calibration_time % 1000 == 0){
          // Countdown
          args->serial->printf("%.0f...", calibration_time / 1000.0f);
        }

        for (controller = 0; controller < RC_NUMBER_CONTROLLERS; controller++) {
          for (channel = 0; channel < RC_NUMBER_CHANNELS; channel++) {
            tmp = args->receiver[controller].channel[channel]->pulsewidth();
            // Find min
            if (tmp < args->channel_limits[controller][channel].min) {
              args->channel_limits[controller][channel].min = tmp;
            }

            // Find max
            if (tmp > args->channel_limits[controller][channel].max) {
              args->channel_limits[controller][channel].max = tmp;
            }
          }
        }
        Thread::wait(calibration_tick);
        calibration_time -= calibration_tick;
      }

      // End countdown
      args->serial->printf("\r\n");

      //Print the results
      for (controller = 0; controller < RC_NUMBER_CONTROLLERS; controller++) {
        args->serial->printf("Controller %d\r\n", controller+1);
        for (channel = 0; channel < RC_NUMBER_CHANNELS; channel++) {
          args->serial->printf("\tChannel %d: min: %.2fs, max: %.2fs, range: %.2fs\r\n",
          channel+1,
          args->channel_limits[controller][channel].min,
          args->channel_limits[controller][channel].max,
          args->channel_limits[controller][channel].max -
          args->channel_limits[controller][channel].min
        );
        }
      }

      // De-activate task to prevent further repititions
      args->tasks[TASK_CALIBRATE_CHANNELS_ID].active = false;

    }

    // No need to poll continuously
    Thread::wait(500);
  }
}
#endif
