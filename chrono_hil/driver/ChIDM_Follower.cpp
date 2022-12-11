// =============================================================================
// PROJECT CHRONO - http://projectchrono.org
//
// Copyright (c) 2014 projectchrono.org
// All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file at the top level of the distribution and at
// http://projectchrono.org/license-chrono.txt.
//
// =============================================================================
// Authors: Simone Benatti
// =============================================================================
//
// Custom drivers for the NSF project.
// Both are specialization of ChPathFollowerDriver
// The leader will adjust its behavior depending on the traveled distance
// The follower will adjust the speed to reach a target gap with the leader
//
// =============================================================================

#include "ChIDM_Follower.h"
#include "chrono_vehicle/wheeled_vehicle/vehicle/WheeledVehicle.h"

#include <algorithm>
#include <climits>
#include <fstream>
#include <iostream>
#include <sstream>
namespace chrono {
namespace hil {

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
void ChIDMFollower::Synchronize(double time, double step, double lead_distance,
                                double lead_speed) {
  // In this portion we adjust the target speed according to custom piece-wise
  // sinusoidal defined in behavior_data. We use the driver model explained
  // here, using a desired speed instead:
  // https://traffic-simulation.de/info/info_IDM.html the parameters are: start
  // [miles], end [miles], v0 desired v [m/s], T desired time headway [s],
  // desired space headway [m], a: accel reate a [m/s^2], b: comfort decel
  // [m/s^2], delta: accel exponent
  dist += (m_vehicle.GetChassis()->GetPos() - previousPos).Length();
  previousPos = m_vehicle.GetChassis()->GetPos();

  double s = lead_distance - behavior_data[6];
  double v = m_vehicle.GetChassis()->GetSpeed();
  double delta_v = v - lead_speed;
  double s_star =
      behavior_data[2] +
      ChMax(0.0, v * behavior_data[1] +
                     (v * delta_v) /
                         (2 * sqrt(behavior_data[3] * behavior_data[4])));
  double dv_dt =
      behavior_data[3] *
      (1 - pow(v / behavior_data[0], behavior_data[5]) - pow(s_star / s, 2));

  // integrate intended acceleration into theoretical soeed
  thero_speed = thero_speed + dv_dt * step;
  double v_ms = ChMax(0.0, thero_speed);

  // to avoid large negative value during self drive
  if (thero_speed < 0) {
    thero_speed = 0;
  }

  SetDesiredSpeed(v_ms);

  ChPathFollowerDriver::Synchronize(time);
}

double ChIDMFollower::Get_Dist() { return dist; }

void ChIDMFollower::Set_TheroSpeed(float target_thero_speed) {
  thero_speed = target_thero_speed;
}

} // end namespace hil
} // end namespace chrono
