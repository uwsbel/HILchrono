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
// Author: Jason Zhou
// =============================================================================

#include <chrono>
#include <iostream>
#include <stdint.h>

#include "chrono/core/ChRealtimeStep.h"
#include "chrono/physics/ChBodyEasy.h"
#include "chrono/physics/ChSystemSMC.h"

#include "chrono_irrlicht/ChVisualSystemIrrlicht.h"
#include "chrono_vehicle/driver/ChInteractiveDriverIRR.h"
#include "chrono_vehicle/wheeled_vehicle/ChWheeledVehicleVisualSystemIrrlicht.h"

#include "chrono_vehicle/driver/ChInteractiveDriverIRR.h"
#include "chrono_vehicle/wheeled_vehicle/ChWheeledVehicleVisualSystemIrrlicht.h"

#include "chrono_models/vehicle/sedan/Sedan.h"

#include "chrono_vehicle/wheeled_vehicle/vehicle/WheeledVehicle.h"

#include <irrlicht.h>

#include "chrono_vehicle/terrain/RigidTerrain.h"

#include "chrono_hil/ROM/driver/ChROM_IDMFollower.h"
#include "chrono_hil/ROM/driver/ChROM_PathFollowerDriver.h"
#include "chrono_hil/ROM/veh/Ch_8DOF_vehicle.h"
#include "chrono_hil/timer/ChRealtimeCumulative.h"

#include "chrono/core/ChBezierCurve.h"

#include "chrono_models/vehicle/hmmwv/HMMWV.h"

#include "chrono_sensor/ChSensorManager.h"
#include "chrono_sensor/filters/ChFilterAccess.h"
#include "chrono_sensor/filters/ChFilterCameraNoise.h"
#include "chrono_sensor/filters/ChFilterGrayscale.h"
#include "chrono_sensor/filters/ChFilterImageOps.h"
#include "chrono_sensor/filters/ChFilterSave.h"
#include "chrono_sensor/filters/ChFilterVisualize.h"
#include "chrono_sensor/sensors/ChSegmentationCamera.h"

#include "chrono_hil/timer/ChRealtimeCumulative.h"

// Use the namespaces of Chrono
using namespace chrono;
using namespace chrono::irrlicht;
using namespace chrono::vehicle;
using namespace chrono::vehicle::hmmwv;
using namespace chrono::sensor;
using namespace chrono::hil;

// Simulation step sizes
double step_size = 5e-4;
double tire_step_size = 1e-3;

// Simulation end time
double t_end = 1000;

// Time interval between two render frames
double render_step_size = 1.0 / 50; // FPS = 50

ChVector<> initLoc(0, 0, 1.4);
ChQuaternion<> initRot(1, 0, 0, 0);

// Point on chassis tracked by the camera
ChVector<> trackPoint(0.0, 0.0, 1.75);

enum VEH_TYPE { HMMWV, PATROL, AUDI, SEDAN };

int main(int argc, char *argv[]) {
  vehicle::SetDataPath(CHRONO_DATA_DIR + std::string("vehicle/"));
  // ========== Chrono::Vehicle HMMWV vehicle ===============
  // Create the HMMWV vehicle, set parameters, and initialize

  VEH_TYPE rom_type = VEH_TYPE::HMMWV;

  float init_height = 0.45;
  std::string vehicle_filename;
  std::string tire_filename;
  std::string powertrain_filename;
  std::string rom_json;

  switch (rom_type) {
  case VEH_TYPE::HMMWV:
    vehicle_filename = vehicle::GetDataFile("hmmwv/vehicle/HMMWV_Vehicle.json");
    tire_filename = vehicle::GetDataFile("hmmwv/tire/HMMWV_TMeasyTire.json");
    powertrain_filename =
        vehicle::GetDataFile("hmmwv/powertrain/HMMWV_ShaftsPowertrain.json");
    rom_json =
        std::string(STRINGIFY(HIL_DATA_DIR)) + "/rom/hmmwv/hmmwv_rom.json";
    init_height = 0.45;
    break;
  case VEH_TYPE::PATROL:
    vehicle_filename =
        vehicle::GetDataFile("Nissan_Patrol/json/suv_Vehicle.json");
    tire_filename =
        vehicle::GetDataFile("Nissan_Patrol/json/suv_TMeasyTire.json");
    powertrain_filename =
        vehicle::GetDataFile("Nissan_Patrol/json/suv_ShaftsPowertrain.json");
    rom_json =
        std::string(STRINGIFY(HIL_DATA_DIR)) + "/rom/patrol/patrol_rom.json";
    init_height = 0.45;
    break;
  case VEH_TYPE::AUDI:
    vehicle_filename = vehicle::GetDataFile("audi/json/audi_Vehicle.json");
    tire_filename = vehicle::GetDataFile("audi/json/audi_TMeasyTire.json");
    powertrain_filename =
        vehicle::GetDataFile("audi/json/audi_SimpleMapPowertrain.json");
    rom_json = std::string(STRINGIFY(HIL_DATA_DIR)) + "/rom/audi/audi_rom.json";
    init_height = 0.20;
    break;
  case VEH_TYPE::SEDAN:
    vehicle_filename = vehicle::GetDataFile("sedan/vehicle/Sedan_Vehicle.json");
    tire_filename = vehicle::GetDataFile("sedan/tire/Sedan_TMeasyTire.json");
    powertrain_filename =
        vehicle::GetDataFile("sedan/powertrain/Sedan_SimpleMapPowertrain.json");
    rom_json =
        std::string(STRINGIFY(HIL_DATA_DIR)) + "/rom/sedan/sedan_rom.json";
    init_height = 0.20;
    break;
  default:
    return -1;
  }
  // Create the Sedan vehicle, set parameters, and initialize
  WheeledVehicle my_vehicle(vehicle_filename, ChContactMethod::SMC);
  auto ego_chassis = my_vehicle.GetChassis();
  my_vehicle.Initialize(ChCoordsys<>(initLoc, initRot));
  my_vehicle.GetChassis()->SetFixed(false);
  auto powertrain = ReadPowertrainJSON(powertrain_filename);
  my_vehicle.InitializePowertrain(powertrain);
  my_vehicle.SetChassisVisualizationType(VisualizationType::MESH);
  my_vehicle.SetSuspensionVisualizationType(VisualizationType::MESH);
  my_vehicle.SetSteeringVisualizationType(VisualizationType::MESH);
  my_vehicle.SetWheelVisualizationType(VisualizationType::MESH);

  // Create and initialize the tires
  for (auto &axle : my_vehicle.GetAxles()) {
    for (auto &wheel : axle->GetWheels()) {
      auto tire = ReadTireJSON(tire_filename);
      tire->SetStepsize(step_size / 2);
      my_vehicle.InitializeTire(tire, wheel, VisualizationType::MESH);
    }
  }

  std::shared_ptr<Ch_8DOF_vehicle> rom_veh =
      chrono_types::make_shared<Ch_8DOF_vehicle>(rom_json, init_height);
  rom_veh->SetInitPos(initLoc + ChVector<>(0.0, 4.0, init_height));
  rom_veh->SetInitRot(0.0);
  rom_veh->Initialize(my_vehicle.GetSystem());

  // Initialize terrain
  RigidTerrain terrain(my_vehicle.GetSystem());

  double terrainLength = 200.0; // size in X direction
  double terrainWidth = 200.0;  // size in Y direction

  ChContactMaterialData minfo;
  minfo.mu = 0.9f;
  minfo.cr = 0.01f;
  minfo.Y = 2e7f;
  auto patch_mat = minfo.CreateMaterial(ChContactMethod::SMC);
  std::shared_ptr<RigidTerrain::Patch> patch;
  patch = terrain.AddPatch(patch_mat, CSYSNORM, terrainLength, terrainWidth);
  patch->SetTexture(vehicle::GetDataFile("terrain/textures/dirt.jpg"), 200,
                    200);
  patch->SetColor(ChColor(0.8f, 0.8f, 0.5f));
  terrain.Initialize();

  // Create a body that camera attaches to
  auto attached_body = std::make_shared<ChBody>();
  my_vehicle.GetSystem()->AddBody(attached_body);
  attached_body->SetPos(ChVector<>(0.0, 0.0, 0.0));
  attached_body->SetCollide(false);
  attached_body->SetBodyFixed(true);

  // Create camera
  // Create the camera sensor
  auto manager =
      chrono_types::make_shared<ChSensorManager>(my_vehicle.GetSystem());
  float intensity = 1.2;
  manager->scene->AddPointLight({0, 0, 1e8}, {intensity, intensity, intensity},
                                1e12);
  manager->scene->SetAmbientLight({.1, .1, .1});
  manager->scene->SetSceneEpsilon(1e-3);
  manager->scene->EnableDynamicOrigin(true);
  manager->scene->SetOriginOffsetThreshold(500.f);

  auto cam = chrono_types::make_shared<ChCameraSensor>(
      attached_body, // body camera is attached to
      35,            // update rate in Hz
      chrono::ChFrame<double>(
          ChVector<>(5.0, -5.0, 1.0),
          Q_from_Euler123(ChVector<>(0.0, 0.0, C_PI / 2))), // offset pose
      1920,                                                 // image width
      1080,                                                 // image
      1.608f, 1); // fov, lag, exposure cam->SetName("Camera Sensor");

  cam->PushFilter(
      chrono_types::make_shared<ChFilterVisualize>(1920, 1080, "test", false));
  // Provide the host access to the RGBA8 buffer
  // cam->PushFilter(chrono_types::make_shared<ChFilterRGBA8Access>());
  manager->AddSensor(cam);

  auto cam2 = chrono_types::make_shared<ChCameraSensor>(
      attached_body, // body camera is attached to
      35,            // update rate in Hz
      chrono::ChFrame<double>(
          ChVector<>(5.0, 9.0, 1.0),
          Q_from_Euler123(ChVector<>(0.0, 0.0, -C_PI / 2))), // offset pose
      1920,                                                  // image width
      1080,                                                  // image
      1.608f, 1); // fov, lag, exposure cam->SetName("Camera Sensor");

  cam2->PushFilter(
      chrono_types::make_shared<ChFilterVisualize>(1920, 1080, "test", false));
  // Provide the host access to the RGBA8 buffer
  // cam->PushFilter(chrono_types::make_shared<ChFilterRGBA8Access>());
  manager->AddSensor(cam2);

  manager->Update();

  // Set the time response for steering and throttle keyboard inputs.
  double steering_time = 1.0; // time to go from 0 to +1 (or from 0 to -1)
  double throttle_time = 1.0; // time to go from 0 to +1
  double braking_time = 0.3;  // time to go from 0 to +1

  // Number of simulation steps between miscellaneous events
  int render_steps = (int)std::ceil(render_step_size / step_size);

  // Initialize simulation frame counters
  int step_number = 0;
  int render_frame = 0;
  double time = 0.0;

  ChRealtimeCumulative realtime_timer;

  while (time < t_end) {

    if (step_number == 0) {
      realtime_timer.Reset();
    }

    time = my_vehicle.GetSystem()->GetChTime();

    // End simulation
    if (time >= t_end)
      break;

    // Driver inputs
    DriverInputs driver_inputs;

    // Time-based drive inputs
    if (time < 3.0f) {
      driver_inputs.m_throttle = 0.0;
      driver_inputs.m_braking = 0.0;
      driver_inputs.m_steering = 0.0;
    } else if (time >= 3.0f && time < 8.0f) {
      driver_inputs.m_throttle = 0.5;
      driver_inputs.m_braking = 0.0;
      driver_inputs.m_steering = 0.0;
    } else if (time >= 8.0f && time < 12.0f) {
      driver_inputs.m_throttle = 0.0;
      driver_inputs.m_braking = 0.4;
      driver_inputs.m_steering = 0.0;
    } else {
      driver_inputs.m_throttle = 0.0;
      driver_inputs.m_braking = 0.0;
      driver_inputs.m_steering = 0.0;
    }

    // Update modules (process inputs from other modules)
    terrain.Synchronize(time);
    my_vehicle.Synchronize(time, driver_inputs, terrain);
    // Advance simulation for one timestep for all modules
    terrain.Advance(step_size);
    my_vehicle.Advance(step_size);
    rom_veh->Advance(time, driver_inputs);

    manager->Update();

    // Increment frame number
    step_number++;

    realtime_timer.Spin(time);
  }
}