/* Copyright (c) 2017 Cameron A. Craig, MIT License
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
* @file commands.cpp
* @author Cameron A. Craig
* @date 18 May 2017
* @copyright 2017 Cameron A. Craig
* @brief Provides functions to implement available commands as well as
*        helper functions for parsing command strings and printing commands.
*/

#include "mbed.h"
#include "commands.h"
#include "utilc-logging.h"
#include "utils.h"
#include "states.h"
#include "return_codes.h"
#include "thread_args.h"
#include "types.h"

const char * command_get_str(command_id_t id){
  switch(id){
    case FULLY_DISARM:
      return available_commands[0].name;
    case PARTIAL_DISARM:
      return available_commands[1].name;
    case PARTIAL_ARM:
      return available_commands[2].name;
    case FULLY_ARM:
      return available_commands[3].name;
    case STATUS:
      return available_commands[4].name;
    default:
      return "INVALID COMMAND";
  }
}

int command_generate(command_t *command, char *buffer){
  //Get the size of the entire command string
  size_t command_len = strlen(buffer);
  char command_str[command_len];
  memcpy(command_str, buffer, command_len);

  //Seperate commands into parts
  char param_part[2][10];
  char command_part[10];

  char * pch;
  int part = 0;
  pch = strtok (command_str," ");
  while (pch != NULL){
    if(part == 0){
      strncpy(command_part, pch, strlen(pch));
    } else {
      strncpy(param_part[part-1], pch, strlen(pch));
    }
    pch = strtok (NULL, " ");
    part++;
  }

  //Find a matching command for the given command string
  int i;
  for(i = 0; i < NUM_COMMANDS; i++){
    size_t cplen = strlen(command_part);
    char *command_compare;
    command_compare = (char *) command_get_str(available_commands[i].id);
    size_t cclen = strlen(command_compare);

    if(strncmp(command_compare, command_part, MIN(cclen, cplen)) == 0){
      //When a matching command is found, populate the command
      command->id = available_commands[i].id;
      command->name = available_commands[i].name;
      //TODO: Set command parameters for parameterised commands
      //return true if matching command is found
      return RET_OK;
    }
  }

  //Return false (0) if no match is found
  return RET_ERROR;
}

int command_execute(command_t *command, thread_args_t *targs){
  switch(command->id){
    case FULLY_DISARM:
      return command_fully_disarm(command, targs);
    case PARTIAL_DISARM:
      return command_partial_disarm(command, targs);
    case PARTIAL_ARM:
      return command_partial_arm(command, targs);
    case FULLY_ARM:
      return command_fully_arm(command, targs);
    case STATUS:
      return command_status(command, targs);
    default:
      return RET_ERROR;
  }
}

int command_fully_disarm(command_t *command, thread_args_t *targs){
  if(targs->state == STATE_DISARMED){
    return RET_ALREADY_DISARMED;
  }
  targs->state = STATE_DISARMED;
  return RET_OK;
}

int command_partial_disarm(command_t *command, thread_args_t *targs){
  switch(targs->state){
    case STATE_DISARMED:
      return RET_ALREADY_DISARMED;
    case STATE_DRIVE_ONLY:
      targs->state = STATE_DISARMED;
      return RET_OK;
    case STATE_WEAPON_ONLY:
      targs->state = STATE_DRIVE_ONLY;
      return RET_OK;
    case STATE_FULLY_ARMED:
      targs->state = STATE_WEAPON_ONLY;
      return RET_OK;
    default:
      return RET_ERROR;
  }
}
int command_partial_arm(command_t *command, thread_args_t *targs){
  switch(targs->state){
    case STATE_DISARMED:
      targs->state = STATE_DRIVE_ONLY;
      return RET_OK;
    case STATE_DRIVE_ONLY:
      targs->state = STATE_WEAPON_ONLY;
      return RET_OK;
    case STATE_WEAPON_ONLY:
      targs->state = STATE_FULLY_ARMED;
      return RET_OK;
    case STATE_FULLY_ARMED:
      return RET_ALREADY_ARMED;
    default:
      return RET_ERROR;
  }
}
int command_fully_arm(command_t *command, thread_args_t *targs){
  if(targs->state == STATE_FULLY_ARMED){
    return RET_ALREADY_ARMED;
  }
  targs->state = STATE_FULLY_ARMED;
  return RET_OK;
}

int command_status(command_t *command, thread_args_t *targs){
  LOG("\rStatus: %s\r\n", state_to_str(targs->state));
  // LOG("\r(ESCS) D1: %d, D2: %d, D3: %d, W1: %d, W2: %d\r\n",
  //   targs->outputs.wheel_1,
  //   targs->outputs.wheel_2,
  //   targs->outputs.wheel_3,
  //   targs->outputs.weapon_motor_1,
  //   targs->outputs.weapon_motor_2,
  // );
  // TODO: RPMs
  // LOG("\r(RPMS) W1: W2: D1: \r\n");
  LOG("\r(Orientation) detected: %s, overidden: %s\r\n",
    orientation_to_str(targs->orientation_detected),
    orientation_to_str(targs->orientation_override)
  );
  LOG("\r              heading: %d, pitch: %d, roll: %d\r\n", targs->orientation.heading, targs->orientation.pitch, targs->orientation.roll);
  return RET_OK;
}