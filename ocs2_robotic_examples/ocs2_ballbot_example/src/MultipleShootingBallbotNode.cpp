//
// Created by rgrandia on 09.11.20.
//

#include <ros/init.h>

#include <ocs2_core/constraint/ConstraintBase.h>
#include <ocs2_ros_interfaces/mpc/MPC_ROS_Interface.h>
#include "ocs2_ballbot_example/BallbotInterface.h"
#include "ocs2_mpc/MPC_Settings.h"
#include "ocs2_sqp/MultipleShootingMpc.h"
#include "ocs2_sqp/MultipleShootingSolver.h"

int main(int argc, char** argv) {
  const std::string robotName = "ballbot";

  // task file
  std::vector<std::string> programArgs{};
  ::ros::removeROSArgs(argc, argv, programArgs);
  if (programArgs.size() <= 1) {
    throw std::runtime_error("No task file specified. Aborting.");
  }
  std::string taskFileFolderName = std::string(programArgs[1]);

  // Initialize ros node
  ros::init(argc, argv, robotName + "_mpc");
  ros::NodeHandle nodeHandle;

  // Robot interface
  ocs2::ballbot::BallbotInterface ballbotInterface(taskFileFolderName);

  // Set this one up.
  ocs2::MultipleShootingSolverSettings settings;
  settings.dt = 0.1;
  settings.n_state = 10;
  settings.n_input = 3;
  settings.sqpIteration = 5;
  settings.deltaTol = 1e-3;
  settings.printSolverStatistics = true;
  settings.printSolverStatus = false;

  ocs2::mpc::Settings mpcSettings = ballbotInterface.mpcSettings();
  std::unique_ptr<ocs2::MultipleShootingMpc> mpc(new ocs2::MultipleShootingMpc(
      mpcSettings, settings, &ballbotInterface.getDynamics(), &ballbotInterface.getCost(), ballbotInterface.getConstraintPtr(),
      ballbotInterface.getTerminalCostPtr(), &ballbotInterface.getOperatingPoints()));

  ocs2::MPC_ROS_Interface mpcNode(*mpc, robotName);
  mpcNode.launchNodes(nodeHandle);

  // Successful exit
  return 0;
}