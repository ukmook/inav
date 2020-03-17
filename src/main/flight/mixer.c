/*
 * This file is part of Cleanflight.
 *
 * Cleanflight is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Cleanflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Cleanflight.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"

FILE_COMPILE_FOR_SPEED

#include "build/debug.h"

#include "common/axis.h"
#include "common/filter.h"
#include "common/maths.h"
#include "common/utils.h"
#include "common/global_functions.h"

#include "config/feature.h"
#include "config/parameter_group.h"
#include "config/parameter_group_ids.h"

#include "drivers/pwm_output.h"
#include "drivers/pwm_mapping.h"
#include "drivers/time.h"

#include "fc/config.h"
#include "fc/rc_controls.h"
#include "fc/rc_modes.h"
#include "fc/runtime_config.h"

#include "flight/failsafe.h"
#include "flight/imu.h"
#include "flight/mixer.h"
#include "flight/pid.h"
#include "flight/servos.h"

#include "navigation/navigation.h"

#include "rx/rx.h"

#include "sensors/battery.h"

FASTRAM int16_t motor[MAX_SUPPORTED_MOTORS];
FASTRAM int16_t motor_disarmed[MAX_SUPPORTED_MOTORS];
static float motorMixRange;
static float mixerScale = 1.0f;
static EXTENDED_FASTRAM motorMixer_t currentMixer[MAX_SUPPORTED_MOTORS];
static EXTENDED_FASTRAM uint8_t motorCount = 0;
EXTENDED_FASTRAM int mixerThrottleCommand;
static EXTENDED_FASTRAM int throttleIdleValue = 0;
static EXTENDED_FASTRAM int motorValueWhenStopped = 0;
static reversibleMotorsThrottleState_e reversibleMotorsThrottleState = MOTOR_DIRECTION_FORWARD;
static EXTENDED_FASTRAM int throttleDeadbandLow = 0;
static EXTENDED_FASTRAM int throttleDeadbandHigh = 0;
static EXTENDED_FASTRAM int throttleRangeMin = 0;
static EXTENDED_FASTRAM int throttleRangeMax = 0;
static EXTENDED_FASTRAM int8_t motorYawMultiplier = 1;

PG_REGISTER_WITH_RESET_TEMPLATE(reversibleMotorsConfig_t, reversibleMotorsConfig, PG_REVERSIBLE_MOTORS_CONFIG, 0);

PG_RESET_TEMPLATE(reversibleMotorsConfig_t, reversibleMotorsConfig,
    .deadband_low = 1406,
    .deadband_high = 1514,
    .neutral = 1460
);

PG_REGISTER_WITH_RESET_TEMPLATE(mixerConfig_t, mixerConfig, PG_MIXER_CONFIG, 3);

PG_RESET_TEMPLATE(mixerConfig_t, mixerConfig,
    .motorDirectionInverted = 0,
    .platformType = PLATFORM_MULTIROTOR,
    .hasFlaps = false,
    .appliedMixerPreset = -1, //This flag is not available in CLI and used by Configurator only
    .fwMinThrottleDownPitchAngle = 0
);

#ifdef BRUSHED_MOTORS
#define DEFAULT_PWM_PROTOCOL    PWM_TYPE_BRUSHED
#define DEFAULT_PWM_RATE        16000
#else
#define DEFAULT_PWM_PROTOCOL    PWM_TYPE_STANDARD
#define DEFAULT_PWM_RATE        400
#endif

#define DEFAULT_MAX_THROTTLE    1850

PG_REGISTER_WITH_RESET_TEMPLATE(motorConfig_t, motorConfig, PG_MOTOR_CONFIG, 5);

PG_RESET_TEMPLATE(motorConfig_t, motorConfig,
    .motorPwmProtocol = DEFAULT_PWM_PROTOCOL,
    .motorPwmRate = DEFAULT_PWM_RATE,
    .maxthrottle = DEFAULT_MAX_THROTTLE,
    .mincommand = 1000, 
    .motorAccelTimeMs = 0,
    .motorDecelTimeMs = 0,
    .throttleIdle = 15.0f,
    .throttleScale = 1.0f,
    .motorPoleCount = 14            // Most brushless motors that we use are 14 poles
);

PG_REGISTER_ARRAY(motorMixer_t, MAX_SUPPORTED_MOTORS, primaryMotorMixer, PG_MOTOR_MIXER, 0);

typedef void (*motorRateLimitingApplyFnPtr)(const float dT);
static EXTENDED_FASTRAM motorRateLimitingApplyFnPtr motorRateLimitingApplyFn;

int getThrottleIdleValue(void)
{
    if (!throttleIdleValue) {
        throttleIdleValue = motorConfig()->mincommand + (((motorConfig()->maxthrottle - motorConfig()->mincommand) / 100.0f) * motorConfig()->throttleIdle);
    }

    return throttleIdleValue;
}

static void computeMotorCount(void)
{
    motorCount = 0;
    for (int i = 0; i < MAX_SUPPORTED_MOTORS; i++) {
        // check if done
        if (primaryMotorMixer(i)->throttle == 0.0f) {
            break;
        }
        motorCount++;
    }
}

uint8_t getMotorCount(void) {
    return motorCount;
}

float getMotorMixRange(void)
{
    return motorMixRange;
}

bool mixerIsOutputSaturated(void)
{
    return motorMixRange >= 1.0f;
}

void mixerUpdateStateFlags(void)
{
    DISABLE_STATE(FIXED_WING_LEGACY);
    DISABLE_STATE(MULTIROTOR);
    DISABLE_STATE(ROVER);
    DISABLE_STATE(BOAT);
    DISABLE_STATE(AIRPLANE);
    DISABLE_STATE(MOVE_FORWARD_ONLY);

    if (mixerConfig()->platformType == PLATFORM_AIRPLANE) {
        ENABLE_STATE(FIXED_WING_LEGACY);
        ENABLE_STATE(AIRPLANE);
        ENABLE_STATE(ALTITUDE_CONTROL);
        ENABLE_STATE(MOVE_FORWARD_ONLY);
    } if (mixerConfig()->platformType == PLATFORM_ROVER) {
        ENABLE_STATE(ROVER);
        ENABLE_STATE(FIXED_WING_LEGACY);
        ENABLE_STATE(MOVE_FORWARD_ONLY);
    } if (mixerConfig()->platformType == PLATFORM_BOAT) {
        ENABLE_STATE(BOAT);
        ENABLE_STATE(FIXED_WING_LEGACY);
        ENABLE_STATE(MOVE_FORWARD_ONLY);
    } else if (mixerConfig()->platformType == PLATFORM_MULTIROTOR) {
        ENABLE_STATE(MULTIROTOR);
        ENABLE_STATE(ALTITUDE_CONTROL);
    } else if (mixerConfig()->platformType == PLATFORM_TRICOPTER) {
        ENABLE_STATE(MULTIROTOR);
        ENABLE_STATE(ALTITUDE_CONTROL);
    } else if (mixerConfig()->platformType == PLATFORM_HELICOPTER) {
        ENABLE_STATE(MULTIROTOR);
        ENABLE_STATE(ALTITUDE_CONTROL);
    } 

    if (mixerConfig()->hasFlaps) {
        ENABLE_STATE(FLAPERON_AVAILABLE);
    } else {
        DISABLE_STATE(FLAPERON_AVAILABLE);
    }
}

void nullMotorRateLimiting(const float dT)
{
    UNUSED(dT);
}

void applyMotorRateLimiting(const float dT)
{
    static float motorPrevious[MAX_SUPPORTED_MOTORS] = { 0 };

    if (feature(FEATURE_REVERSIBLE_MOTORS)) {
        // FIXME: Don't apply rate limiting in 3D mode
        for (int i = 0; i < motorCount; i++) {
            motorPrevious[i] = motor[i];
        }
    }
    else {
        // Calculate max motor step
        const uint16_t motorRange = motorConfig()->maxthrottle - throttleIdleValue;
        const float motorMaxInc = (motorConfig()->motorAccelTimeMs == 0) ? 2000 : motorRange * dT / (motorConfig()->motorAccelTimeMs * 1e-3f);
        const float motorMaxDec = (motorConfig()->motorDecelTimeMs == 0) ? 2000 : motorRange * dT / (motorConfig()->motorDecelTimeMs * 1e-3f);

        for (int i = 0; i < motorCount; i++) {
            // Apply motor rate limiting
            motorPrevious[i] = constrainf(motor[i], motorPrevious[i] - motorMaxDec, motorPrevious[i] + motorMaxInc);

            // Handle throttle below min_throttle (motor start/stop)
            if (motorPrevious[i] < throttleIdleValue) {
                if (motor[i] < throttleIdleValue) {
                    motorPrevious[i] = motor[i];
                }
                else {
                    motorPrevious[i] = throttleIdleValue;
                }
            }
        }
    }

    // Update motor values
    for (int i = 0; i < motorCount; i++) {
        motor[i] = motorPrevious[i];
    }
}

void mixerInit(void)
{
    computeMotorCount();
    loadPrimaryMotorMixer();
    // in 3D mode, mixer gain has to be halved
    if (feature(FEATURE_REVERSIBLE_MOTORS)) {
        mixerScale = 0.5f;
    }

    throttleDeadbandLow = PWM_RANGE_MIDDLE - rcControlsConfig()->mid_throttle_deadband;
    throttleDeadbandHigh = PWM_RANGE_MIDDLE + rcControlsConfig()->mid_throttle_deadband;

    mixerResetDisarmedMotors();

    if (motorConfig()->motorAccelTimeMs || motorConfig()->motorDecelTimeMs) {
        motorRateLimitingApplyFn = applyMotorRateLimiting;
    } else {
        motorRateLimitingApplyFn = nullMotorRateLimiting;
    }

    if (mixerConfig()->motorDirectionInverted) {
        motorYawMultiplier = -1;
    } else {
        motorYawMultiplier = 1;
    }
}

void mixerResetDisarmedMotors(void)
{
    int motorZeroCommand;
    
    if (feature(FEATURE_REVERSIBLE_MOTORS)) {
        motorZeroCommand = reversibleMotorsConfig()->neutral;
        throttleRangeMin = throttleDeadbandHigh;
        throttleRangeMax = motorConfig()->maxthrottle;
    } else {
        motorZeroCommand = motorConfig()->mincommand;
        throttleRangeMin = getThrottleIdleValue();
        throttleRangeMax = motorConfig()->maxthrottle;
    }

    reversibleMotorsThrottleState = MOTOR_DIRECTION_FORWARD;

    if (feature(FEATURE_MOTOR_STOP)) {
        motorValueWhenStopped = motorZeroCommand;
    } else {
        motorValueWhenStopped = getThrottleIdleValue();
    }

    // set disarmed motor values
    for (int i = 0; i < MAX_SUPPORTED_MOTORS; i++) {
        motor_disarmed[i] = motorZeroCommand;
    }
}

static uint16_t handleOutputScaling(
    int16_t input,          // Input value from the mixer
    int16_t stopThreshold,  // Threshold value to check if motor should be rotating or not
    int16_t onStopValue,    // Value sent to the ESC when min rotation is required - on motor_stop it is STOP command, without motor_stop it's a value that keeps rotation
    int16_t inputScaleMin,  // Input range - min value
    int16_t inputScaleMax,  // Input range - max value
    int16_t outputScaleMin, // Output range - min value
    int16_t outputScaleMax, // Output range - max value
    bool moveForward        // If motor should be rotating FORWARD or BACKWARD
)
{
    int value;
    if (moveForward && input < stopThreshold) {
        //Send motor stop command
        value = onStopValue;
    }
    else if (!moveForward && input > stopThreshold) {
        //Send motor stop command
        value = onStopValue;
    }
    else {
        //Scale input to protocol output values
        value = scaleRangef(input, inputScaleMin, inputScaleMax, outputScaleMin, outputScaleMax);
        value = constrain(value, outputScaleMin, outputScaleMax);
    }
    return value;
}

void FAST_CODE writeMotors(void)
{
    for (int i = 0; i < motorCount; i++) {
        uint16_t motorValue;

#ifdef USE_DSHOT
        // If we use DSHOT we need to convert motorValue to DSHOT ranges
        if (isMotorProtocolDigital()) {

            if (feature(FEATURE_REVERSIBLE_MOTORS)) {
                if (reversibleMotorsThrottleState == MOTOR_DIRECTION_FORWARD) {
                    motorValue = handleOutputScaling(
                        motor[i],
                        throttleRangeMin,
                        DSHOT_DISARM_COMMAND,
                        throttleRangeMin,
                        throttleRangeMax,
                        DSHOT_3D_DEADBAND_HIGH,
                        DSHOT_MAX_THROTTLE,
                        true
                    );
                } else {
                    motorValue = handleOutputScaling(
                        motor[i],
                        throttleRangeMax,
                        DSHOT_DISARM_COMMAND,
                        throttleRangeMin,
                        throttleRangeMax,
                        DSHOT_MIN_THROTTLE,
                        DSHOT_3D_DEADBAND_LOW,
                        false
                    );
                }
            }
            else {
                motorValue = handleOutputScaling(
                    motor[i],
                    throttleIdleValue,
                    DSHOT_DISARM_COMMAND,
                    motorConfig()->mincommand,
                    motorConfig()->maxthrottle,
                    DSHOT_MIN_THROTTLE,
                    DSHOT_MAX_THROTTLE,
                    true
                );
            }
        }
        else {
            if (feature(FEATURE_REVERSIBLE_MOTORS)) {
                if (reversibleMotorsThrottleState == MOTOR_DIRECTION_FORWARD) {
                    motorValue = handleOutputScaling(
                        motor[i],
                        throttleRangeMin,
                        motor[i],
                        throttleRangeMin,
                        throttleRangeMax,
                        reversibleMotorsConfig()->deadband_high,
                        motorConfig()->maxthrottle,
                        true
                    );
                } else {
                    motorValue = handleOutputScaling(
                        motor[i],
                        throttleRangeMax,
                        motor[i],
                        throttleRangeMin,
                        throttleRangeMax,
                        motorConfig()->mincommand,
                        reversibleMotorsConfig()->deadband_low,
                        false
                    );
                }
            } else {
                motorValue = motor[i];
            }

        }
#else
        // We don't define USE_DSHOT
        motorValue = motor[i];
#endif

        pwmWriteMotor(i, motorValue);
    }
}

void writeAllMotors(int16_t mc)
{
    // Sends commands to all motors
    for (int i = 0; i < motorCount; i++) {
        motor[i] = mc;
    }
    writeMotors();
}

void stopMotors(void)
{
    writeAllMotors(feature(FEATURE_REVERSIBLE_MOTORS) ? reversibleMotorsConfig()->neutral : motorConfig()->mincommand);

    delay(50); // give the timers and ESCs a chance to react.
}

void stopPwmAllMotors(void)
{
    pwmShutdownPulsesForAllMotors(motorCount);
}

static int getReversibleMotorsThrottleDeadband(void)
{
    int directionValue;

    if (reversibleMotorsThrottleState == MOTOR_DIRECTION_BACKWARD) {
        directionValue = reversibleMotorsConfig()->deadband_low;
    } else {
        directionValue = reversibleMotorsConfig()->deadband_high;
    }

    return feature(FEATURE_MOTOR_STOP) ? reversibleMotorsConfig()->neutral : directionValue;
}

void FAST_CODE mixTable(const float dT)
{
    int16_t input[3];   // RPY, range [-500:+500]
    // Allow direct stick input to motors in passthrough mode on airplanes
    if (STATE(FIXED_WING_LEGACY) && FLIGHT_MODE(MANUAL_MODE)) {
        // Direct passthru from RX
        input[ROLL] = rcCommand[ROLL];
        input[PITCH] = rcCommand[PITCH];
        input[YAW] = rcCommand[YAW];
    }
    else {
        input[ROLL] = axisPID[ROLL];
        input[PITCH] = axisPID[PITCH];
        input[YAW] = axisPID[YAW];
    }

    // Initial mixer concept by bdoiron74 reused and optimized for Air Mode
    int16_t rpyMix[MAX_SUPPORTED_MOTORS];
    int16_t rpyMixMax = 0; // assumption: symetrical about zero.
    int16_t rpyMixMin = 0;

    // motors for non-servo mixes
    for (int i = 0; i < motorCount; i++) {
        rpyMix[i] =
            (input[PITCH] * currentMixer[i].pitch +
            input[ROLL] * currentMixer[i].roll +
            -motorYawMultiplier * input[YAW] * currentMixer[i].yaw) * mixerScale;

        if (rpyMix[i] > rpyMixMax) rpyMixMax = rpyMix[i];
        if (rpyMix[i] < rpyMixMin) rpyMixMin = rpyMix[i];
    }

    int16_t rpyMixRange = rpyMixMax - rpyMixMin;
    int16_t throttleRange;
    int16_t throttleMin, throttleMax;
    // static int16_t throttlePrevious = 0;   // Store the last throttle direction for deadband transitions

    // Find min and max throttle based on condition.
#ifdef USE_GLOBAL_FUNCTIONS
    if (GLOBAL_FUNCTION_FLAG(GLOBAL_FUNCTION_FLAG_OVERRIDE_THROTTLE)) {
        throttleRangeMin = throttleIdleValue;
        throttleRangeMax = motorConfig()->maxthrottle;
        mixerThrottleCommand = constrain(globalFunctionValues[GLOBAL_FUNCTION_ACTION_OVERRIDE_THROTTLE], throttleMin, throttleMax); 
    } else
#endif
    if (feature(FEATURE_REVERSIBLE_MOTORS)) {

        if (rcCommand[THROTTLE] >= (throttleDeadbandHigh) || STATE(SET_REVERSIBLE_MOTORS_FORWARD)) {
            /*
             * Throttle is above deadband, FORWARD direction
             */
            reversibleMotorsThrottleState = MOTOR_DIRECTION_FORWARD;
            throttleRangeMax = motorConfig()->maxthrottle;
            throttleRangeMin = throttleDeadbandHigh;
            DISABLE_STATE(SET_REVERSIBLE_MOTORS_FORWARD);
        } else if (rcCommand[THROTTLE] <= throttleDeadbandLow) {
            /*
             * Throttle is below deadband, BACKWARD direction
             */
            reversibleMotorsThrottleState = MOTOR_DIRECTION_BACKWARD;
            throttleRangeMax = throttleDeadbandLow;
            throttleRangeMin = motorConfig()->mincommand;
        }


        motorValueWhenStopped = getReversibleMotorsThrottleDeadband();
        mixerThrottleCommand = constrain(rcCommand[THROTTLE], throttleRangeMin, throttleRangeMax);
    } else {
        mixerThrottleCommand = rcCommand[THROTTLE];
        throttleRangeMin = throttleIdleValue;
        throttleRangeMax = motorConfig()->maxthrottle;

        // Throttle scaling to limit max throttle when battery is full
    #ifdef USE_GLOBAL_FUNCTIONS
        mixerThrottleCommand = ((mixerThrottleCommand - throttleMin) * getThrottleScale(motorConfig()->throttleScale)) + throttleMin;
    #else
        mixerThrottleCommand = ((mixerThrottleCommand - throttleMin) * motorConfig()->throttleScale) + throttleMin;
    #endif
        // Throttle compensation based on battery voltage
        if (feature(FEATURE_THR_VBAT_COMP) && isAmperageConfigured() && feature(FEATURE_VBAT)) {                
            mixerThrottleCommand = MIN(throttleMin + (mixerThrottleCommand - throttleMin) * calculateThrottleCompensationFactor(), throttleMax);
        }
    }

    throttleMin = throttleRangeMin;
    throttleMax = throttleRangeMax;

    throttleRange = throttleMax - throttleMin;

    #define THROTTLE_CLIPPING_FACTOR    0.33f
    motorMixRange = (float)rpyMixRange / (float)throttleRange;
    if (motorMixRange > 1.0f) {
        for (int i = 0; i < motorCount; i++) {
            rpyMix[i] /= motorMixRange;
        }

        // Allow some clipping on edges to soften correction response
        throttleMin = throttleMin + (throttleRange / 2) - (throttleRange * THROTTLE_CLIPPING_FACTOR / 2);
        throttleMax = throttleMin + (throttleRange / 2) + (throttleRange * THROTTLE_CLIPPING_FACTOR / 2);
    } else {
        throttleMin = MIN(throttleMin + (rpyMixRange / 2), throttleMin + (throttleRange / 2) - (throttleRange * THROTTLE_CLIPPING_FACTOR / 2));
        throttleMax = MAX(throttleMax - (rpyMixRange / 2), throttleMin + (throttleRange / 2) + (throttleRange * THROTTLE_CLIPPING_FACTOR / 2));
    }

    // Now add in the desired throttle, but keep in a range that doesn't clip adjusted
    // roll/pitch/yaw. This could move throttle down, but also up for those low throttle flips.
    if (ARMING_FLAG(ARMED)) {
        const motorStatus_e currentMotorStatus = getMotorStatus();
        for (int i = 0; i < motorCount; i++) {
            motor[i] = rpyMix[i] + constrain(mixerThrottleCommand * currentMixer[i].throttle, throttleMin, throttleMax);

            if (failsafeIsActive()) {
                motor[i] = constrain(motor[i], motorConfig()->mincommand, motorConfig()->maxthrottle);
            } else {
                motor[i] = constrain(motor[i], throttleRangeMin, throttleRangeMax);
            }

            // Motor stop handling
            if (currentMotorStatus != MOTOR_RUNNING) {
                motor[i] = motorValueWhenStopped;
            }
        }
    } else {
        for (int i = 0; i < motorCount; i++) {
            motor[i] = motor_disarmed[i];
        }
    }

    /* Apply motor acceleration/deceleration limit */
    motorRateLimitingApplyFn(dT);
}

motorStatus_e getMotorStatus(void)
{
    if (failsafeRequiresMotorStop() || (!failsafeIsActive() && STATE(NAV_MOTOR_STOP_OR_IDLE))) {
        return MOTOR_STOPPED_AUTO;
    }

    if (calculateThrottleStatus(feature(FEATURE_REVERSIBLE_MOTORS) ? THROTTLE_STATUS_TYPE_COMMAND : THROTTLE_STATUS_TYPE_RC) == THROTTLE_LOW) {
        if ((STATE(FIXED_WING_LEGACY) || !STATE(AIRMODE_ACTIVE)) && (!(navigationIsFlyingAutonomousMode() && navConfig()->general.flags.auto_overrides_motor_stop)) && (!failsafeIsActive())) {
            return MOTOR_STOPPED_USER;
        }
    }

    return MOTOR_RUNNING;
}

void loadPrimaryMotorMixer(void) {
    for (int i = 0; i < MAX_SUPPORTED_MOTORS; i++) {
        currentMixer[i] = *primaryMotorMixer(i);
    }
}