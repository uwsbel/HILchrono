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

#include "chrono_models/vehicle/sedan/Sedan.h"

#include <irrlicht.h>

#include "chrono_sensor/ChSensorManager.h"
#include "chrono_sensor/filters/ChFilterAccess.h"
#include "chrono_sensor/filters/ChFilterCameraNoise.h"
#include "chrono_sensor/filters/ChFilterGrayscale.h"
#include "chrono_sensor/filters/ChFilterImageOps.h"
#include "chrono_sensor/filters/ChFilterSave.h"
#include "chrono_sensor/filters/ChFilterVisualize.h"
#include "chrono_sensor/sensors/ChSegmentationCamera.h"

#include "chrono_vehicle/terrain/RigidTerrain.h"

#include "chrono_hil/ROM/driver/ChROM_IDMFollower.h"
#include "chrono_hil/ROM/driver/ChROM_PathFollowerDriver.h"
#include "chrono_hil/ROM/veh/Ch_8DOF_vehicle.h"
#include "chrono_hil/timer/ChRealtimeCumulative.h"

#include "chrono/core/ChBezierCurve.h"

// Use the namespaces of Chrono
using namespace chrono;
using namespace chrono::irrlicht;
using namespace chrono::geometry;
using namespace chrono::hil;
using namespace chrono::vehicle;
using namespace chrono::sensor;

enum VEH_TYPE { HMMWV, PATROL, AUDI, SEDAN };

enum IDM_TYPE {
  AGG,
  NORMAL,
  CONS
}; // aggressive, normal, or conservative driver

std::vector<VEH_TYPE> vehicle_types = {
    HMMWV, AUDI,  PATROL, AUDI, SEDAN, HMMWV, HMMWV, AUDI, SEDAN, HMMWV,
    SEDAN, HMMWV, AUDI,   AUDI, HMMWV, SEDAN, HMMWV, AUDI, AUDI};
std::vector<IDM_TYPE> idm_types = {
    AGG, NORMAL, NORMAL, CONS, AGG,  CONS,   NORMAL, NORMAL, NORMAL, AGG,
    AGG, CONS,   AGG,    CONS, CONS, NORMAL, AGG,    AGG,    CONS};

std::random_device rd{};
std::mt19937 gen{rd()};

int main(int argc, char *argv[]) {

  // Create a physical system
  ChSystemSMC sys;
  std::vector<std::shared_ptr<Ch_8DOF_vehicle>> rom_vec; // rom vehicle vector
  std::vector<std::shared_ptr<ChROM_PathFollowerDriver>>
      driver_vec; // rom driver vector
  std::vector<std::shared_ptr<ChROM_IDMFollower>> idm_vec;
  int num_rom = vehicle_types.size();

  // Create the terrain
  RigidTerrain terrain(&sys);

  ChContactMaterialData minfo;
  minfo.mu = 0.9f;
  minfo.cr = 0.01f;
  minfo.Y = 2e7f;
  auto patch_mat = minfo.CreateMaterial(ChContactMethod::SMC);

  vehicle::SetDataPath(CHRONO_DATA_DIR + std::string("vehicle/"));

  std::shared_ptr<RigidTerrain::Patch> patch;
  patch = terrain.AddPatch(patch_mat, CSYSNORM, 300, 300);
  // patch->SetColor(ChColor(0.5f, 0.5f, 0.5f));

  terrain.Initialize();

  // add terrain with weighted textures
  auto terrain_mesh = chrono_types::make_shared<ChTriangleMeshConnected>();
  terrain_mesh->LoadWavefrontMesh(std::string(STRINGIFY(HIL_DATA_DIR)) +
                                      "/ring/terrain0103/ring_terrain_50.obj",
                                  false, true);
  terrain_mesh->Transform(ChVector<>(0, 0, 0),
                          ChMatrix33<>(1)); // scale to a different size
  auto terrain_shape = chrono_types::make_shared<ChTriangleMeshShape>();
  terrain_shape->SetMesh(terrain_mesh);
  terrain_shape->SetName("terrain");
  terrain_shape->SetMutable(false);

  auto terrain_body = chrono_types::make_shared<ChBody>();
  terrain_body->SetPos({0, 0, 0.0});
  terrain_body->AddVisualShape(terrain_shape);
  terrain_body->SetBodyFixed(true);
  terrain_body->SetCollide(false);

  sys.AddBody(terrain_body);

  std::string hmmwv_rom_json =
      std::string(STRINGIFY(HIL_DATA_DIR)) + "/rom/hmmwv/hmmwv_rom.json";
  std::string patrol_rom_json =
      std::string(STRINGIFY(HIL_DATA_DIR)) + "/rom/patrol/patrol_rom.json";
  std::string audi_rom_json =
      std::string(STRINGIFY(HIL_DATA_DIR)) + "/rom/audi/audi_rom.json";
  std::string sedan_rom_json =
      std::string(STRINGIFY(HIL_DATA_DIR)) + "/rom/sedan/sedan_rom.json";

  // initialize all idm roms
  for (int i = 0; i < num_rom; i++) {
    std::string rom_json;
    float init_height;

    if (vehicle_types[i] == HMMWV) {
      rom_json = hmmwv_rom_json;
      init_height = 0.45;
    } else if (vehicle_types[i] == PATROL) {
      rom_json = patrol_rom_json;
      init_height = 0.45;
    } else if (vehicle_types[i] == AUDI) {
      rom_json = audi_rom_json;
      init_height = 0.20;
    } else if (vehicle_types[i] == SEDAN) {
      rom_json = sedan_rom_json;
      init_height = 0.20;
    }

    std::shared_ptr<Ch_8DOF_vehicle> rom_veh =
        chrono_types::make_shared<Ch_8DOF_vehicle>(rom_json, init_height);

    // determine initial position and initial orientation
    float deg_sec = (CH_C_PI * 1.0) / (num_rom);
    ChVector<> initLoc =
        ChVector<>(50.0 * cos(deg_sec * i), 50.0 * sin(deg_sec * i), 0.5);
    float rot_deg = deg_sec * i + CH_C_PI_2;
    if (rot_deg > CH_C_2PI) {
      rot_deg = rot_deg - CH_C_2PI;
    }
    rom_veh->SetInitPos(initLoc);
    rom_veh->SetInitRot(rot_deg);
    rom_veh->Initialize(&sys);
    rom_vec.push_back(rom_veh);

    // initialize driver
    std::shared_ptr<ChBezierCurve> path = ChBezierCurve::read(
        STRINGIFY(HIL_DATA_DIR) +
            std::string("/ring/terrain0103/ring50_closed.txt"),
        true);

    std::shared_ptr<ChROM_PathFollowerDriver> driver =
        chrono_types::make_shared<ChROM_PathFollowerDriver>(
            rom_vec[i], path, 2.0, 6.0, 0.4, 0.0, 0.0, 0.4, 0.0, 0.0);
    driver_vec.push_back(driver);

    // initialize idm control
    std::vector<double> params;
    if (idm_types[i] == AGG) {
      params.push_back(5.0);
      params.push_back(0.1);
      params.push_back(5.0);
      params.push_back(3.5);
      params.push_back(2.5);
      params.push_back(4.0);
      params.push_back(6.0);
    } else if (idm_types[i] == CONS) {
      params.push_back(5.0);
      params.push_back(0.7);
      params.push_back(8.0);
      params.push_back(2.5);
      params.push_back(1.5);
      params.push_back(4.0);
      params.push_back(6.0);
    } else if (idm_types[i] == NORMAL) {
      params.push_back(5.0);
      params.push_back(0.2);
      params.push_back(6.0);
      params.push_back(3.0);
      params.push_back(2.1);
      params.push_back(4.0);
      params.push_back(6.0);
    }

    std::shared_ptr<ChROM_IDMFollower> idm_controller =
        chrono_types::make_shared<ChROM_IDMFollower>(rom_vec[i], driver_vec[i],
                                                     params);
    if (i != num_rom - 1) {
      idm_controller->SetSto(true, 0.1, 0.8, 0.2, 0.2);
    }

    idm_vec.push_back(idm_controller);
  }

  auto attached_body = std::make_shared<ChBody>();
  sys.AddBody(attached_body);
  attached_body->SetPos(ChVector<>(0.0, 0.0, 0.0));
  attached_body->SetCollide(false);
  attached_body->SetBodyFixed(true);

  // now lets run our simulation
  float time = 0;
  int step_number = 0; // time step counter
  float step_size = rom_vec[0]->GetStepSize();

  // Create the camera sensor
  auto manager = chrono_types::make_shared<ChSensorManager>(&sys);
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
          ChVector<>(0.0, 0.0, 100.0),
          Q_from_Euler123(ChVector<>(0.0, C_PI / 2, 0.0))), // offset pose
      1280,                                                 // image width
      720,                                                  // image
      1.608f, 1); // fov, lag, exposure cam->SetName("Camera Sensor");

  cam->PushFilter(
      chrono_types::make_shared<ChFilterVisualize>(1280, 720, "test", false));
  // Provide the host access to the RGBA8 buffer
  // cam->PushFilter(chrono_types::make_shared<ChFilterRGBA8Access>());
  manager->AddSensor(cam);

  manager->Update();

  ChRealtimeCumulative realtime_timer;

  while (true) {
    if (step_number == 0) {
      realtime_timer.Reset();
    }

    // get the controls for this time step
    // Driver inputs

    for (int i = 0; i < num_rom; i++) {
      // update idm
      int ld_idx = (i + 1) % num_rom;

      // Compute critical information
      float raw_dis =
          (rom_vec[ld_idx]->GetPos() - rom_vec[i]->GetPos()).Length();
      float temp = 1 - (raw_dis * raw_dis) / (2.0 * 50.f * 50.f);
      if (temp > 1) {
        temp = 1;
      } else if (temp < -1) {
        temp = -1;
      }

      float theta = abs(acos(temp));
      float act_dis = theta * 50.f;

      // for the center control vehicle
      /*
      if (i == num_rom - 1) {
        if (step_number == 200000) {
          std::vector<double> temp_params;
          temp_params.push_back(2.0);
          temp_params.push_back(0.7);
          temp_params.push_back(8.0);
          temp_params.push_back(2.5);
          temp_params.push_back(0.3);
          temp_params.push_back(4.0);
          temp_params.push_back(6.0);
          idm_vec[i]->SetBehaviorParams(temp_params);
        }

        if (step_number == 600000) {
          std::vector<double> temp_params;
          temp_params.push_back(3.0);
          temp_params.push_back(0.7);
          temp_params.push_back(8.0);
          temp_params.push_back(2.5);
          temp_params.push_back(0.5);
          temp_params.push_back(4.0);
          temp_params.push_back(6.0);
          idm_vec[i]->SetBehaviorParams(temp_params);
        }

        if (step_number == 2000000) {
          std::vector<double> temp_params;
          temp_params.push_back(4.5);
          temp_params.push_back(0.7);
          temp_params.push_back(8.0);
          temp_params.push_back(2.5);
          temp_params.push_back(0.5);
          temp_params.push_back(4.0);
          temp_params.push_back(6.0);
          idm_vec[i]->SetBehaviorParams(temp_params);
        }

        if (step_number == 3000000) {
          std::vector<double> temp_params;
          temp_params.push_back(5.0);
          temp_params.push_back(0.7);
          temp_params.push_back(8.0);
          temp_params.push_back(2.5);
          temp_params.push_back(0.5);
          temp_params.push_back(4.0);
          temp_params.push_back(6.0);
          idm_vec[i]->SetBehaviorParams(temp_params);
        }

        std::cout << "step:" << step_number << std::endl;
      }
      */

      idm_vec[i]->Synchronize(time, step_size, act_dis,
                              (rom_vec[ld_idx]->GetVel()).Length());

      DriverInputs driver_inputs;
      driver_inputs = driver_vec[i]->GetDriverInput();
      rom_vec[i]->Advance(time, driver_inputs);
    }

    time += step_size;
    step_number += 1;

    sys.DoStepDynamics(step_size);
    manager->Update();

    // enforce soft real time if needed
    // realtime_timer.Spin(time);
  }
  return 0;
}