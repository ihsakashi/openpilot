#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <assert.h>
#include <sys/time.h>
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/resource.h>

#include <map>
#include <set>

#include <android/sensor.h>

#include "messaging.hpp"
#include "common/timing.h"
#include "common/swaglog.h"

#define SENSOR_ACCELEROMETER 1
#define SENSOR_MAGNETOMETER 2
#define SENSOR_GYRO 4
#define SENSOR_MAGNETOMETER_UNCALIBRATED 3
#define SENSOR_GYRO_UNCALIBRATED 5
#define SENSOR_PROXIMITY 6
#define SENSOR_LIGHT 7

volatile sig_atomic_t do_exit = 0;
volatile sig_atomic_t re_init_sensors = 0;

namespace {

void set_do_exit(int sig) {
  do_exit = 1;
}

void sigpipe_handler(int sig) {
  LOGE("SIGPIPE received");
  re_init_sensors = true;
}

void sensor_loop() {
  LOG("*** sensor loop");
  while (!do_exit) {
    PubMaster pm({"sensorEvents"});

    // sensor manager
    ASensorManager *manager = ASensorManager_getInstance();
    assert(manager != NULL);

    // list
    ASensorList list;
    int count = ASensorManager_getSensorList(manager, &list);
    assert(count != NULL);
    LOG("%d sensors found", count);

    if (getenv("SENSOR_TEST")) {
      exit(count);
    }

    // specs
    for (int i = 0; i < count; i++) {
      LOGD("sensor %4d: %4d %60s  %d-%ld us", i, ASensor_getHandle(list[i]), ASensor_getName(list[i]), ASensor_getMinDelay(list[i]), ASensor_getMaxDelay(list[i]);
    }

    // types
    std::set<int> sensor_types = {
      ASENSOR_TYPE_ACCELEROMETER,
      ASENSOR_TYPE_MAGNETIC_FIELD_UNCALIBRATED,
      ASENSOR_TYPE_MAGNETIC_FIELD,
      ASENSOR_TYPE_GYROSCOPE_UNCALIBRATED,
      ASENSOR_TYPE_GYROSCOPE,
      ASENSOR_TYPE_PROXIMITY,
      ASENSOR_TYPE_LIGHT,
    };

    // defined
    std::map<int, int64_t> sensors = {
      {SENSOR_GYRO_UNCALIBRATED, ms2ns(10)},
      {SENSOR_MAGNETOMETER_UNCALIBRATED, ms2ns(100)},
      {SENSOR_ACCELEROMETER, ms2ns(10)},
      {SENSOR_GYRO, ms2ns(10)},
      {SENSOR_MAGNETOMETER, ms2ns(100)},
      {SENSOR_PROXIMITY, ms2ns(100)},
      {SENSOR_LIGHT, ms2ns(100)}
    };

    // make event queue
    int looperId = 1;
    ALooper *looper;
    looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    assert(looper != NULL)
    ASensorEventQueue* event_queue = ASensorManager_createEventQueue(manager, looper, looperId, NULL, NULL);
    assert(event_queue != NULL);

    // turn on
    for (auto sensor : sensor_types) {
        assert(ASensorEventQueue_enableSensor(event_queue, sensor) == 0);
    }

    // get events
    int numEvents = 1
    int kTimeoutMilliSecs = 10000;
    ASensorEvent data;
    int identity = 0;

    while (!do_exit) {
      int log_events = 0;
      for (int i=0; i < n; i++) {
        if (sensor_types.find(buffer[i].type) != sensor_types.end()) {
          log_events++;
        }
      }

      // create msg
      MessageBuilder msg;
      auto sensor_events = msg.initEvent().initSensorEvents(log_events);

      int log_i = 0;
      for (int i = 0; i < n; i++) {
        memset(&data, 0, sizeof(data));

        identity = ALooper_pollAll(kTimeoutMilliSecs, NULL, NULL, NULL);
        if (identity != looperId) {
          LOG("timed out");
          continue;
        }

        if ( ASensorEventQueue_getEvents(event_queue, &data, 1) < 1 ) {
          LOG("no pending events");
          continue;
        }

        if (sensor_types.find(data.type) == sensor_types.end()) {
          continue;
        }

        auto log_event = sensor_events[log_i];
        log_event.setSource(cereal::SensorEventData::SensorSource::ANDROID);
        log_event.setVersion(data.version);
        log_event.setSensor(data.sensor);
        log_event.setType(data.type);
        log_event.setTimestamp(data.timestamp);

        // give data
        switch (data.type) {
          case SENSOR_TYPE_ACCELEROMETER: {
            auto svec = log_event.initAcceleration();
            kj::ArrayPtr<const float> vs(&data.acceleration.v[0], 3);
            svec.setV(vs);
            svec.setStatus(data.acceleration.status);
            break;
          }
          case SENSOR_TYPE_MAGNETIC_FIELD_UNCALIBRATED: {
            auto svec = log_event.initMagneticUncalibrated();
            // assuming the uncalib and bias floats are contiguous in memory
            kj::ArrayPtr<const float> vs(&data.uncalibrated_magnetic.uncalib[0], 6);
            svec.setV(vs);
            break;
          }
          case SENSOR_TYPE_MAGNETIC_FIELD: {
            auto svec = log_event.initMagnetic();
            kj::ArrayPtr<const float> vs(&data.magnetic.v[0], 3);
            svec.setV(vs);
            svec.setStatus(data.magnetic.status);
            break;
          }
          case SENSOR_TYPE_GYROSCOPE_UNCALIBRATED: {
            auto svec = log_event.initGyroUncalibrated();
            // assuming the uncalib and bias floats are contiguous in memory
            kj::ArrayPtr<const float> vs(&data.uncalibrated_gyro.uncalib[0], 6);
            svec.setV(vs);
            break;
          }
          case SENSOR_TYPE_GYROSCOPE: {
            auto svec = log_event.initGyro();
            kj::ArrayPtr<const float> vs(&data.gyro.v[0], 3);
            svec.setV(vs);
            svec.setStatus(data.gyro.status);
            break;
          }
          case SENSOR_TYPE_PROXIMITY: {
            log_event.setProximity(data.distance);
            break;
          }
          case SENSOR_TYPE_LIGHT: {
            log_event.setLight(data.light);
            break;
          }
        }

        log_i++;
      }
    
    // send results
    pm.send("sensorEvents", msg);

    if (re_init_sensors){
      LOGE("Resetting sensors");
      re_init_sensors = false;
      break;
    }
  }
  
  // turn off sensors
  for (auto sensor : sensor_types) {
      assert(ASensorEventQueue_disableSensor(event_queue, sensor) == 0);
  }
  // free
  ASensorManager_destroyEventQueue(manager, event_queue);
}

}// Namespace end

int main(int argc, char *argv[]) {
  setpriority(PRIO_PROCESS, 0, -13);
  signal(SIGINT, (sighandler_t)set_do_exit);
  signal(SIGTERM, (sighandler_t)set_do_exit);
  signal(SIGPIPE, (sighandler_t)sigpipe_handler);

  sensor_loop();

  return 0;
}
