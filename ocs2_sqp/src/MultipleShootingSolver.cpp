//
// Created by rgrandia on 09.11.20.
//

#include "ocs2_sqp/MultipleShootingSolver.h"

#include <ocs2_core/OCS2NumericTraits.h>
#include <ocs2_core/constraint/RelaxedBarrierPenalty.h>
#include <ocs2_core/control/FeedforwardController.h>
#include <ocs2_core/misc/LinearInterpolation.h>
#include <ocs2_oc/approximate_model/ChangeOfInputVariables.h>
#include <ocs2_sqp/ConstraintProjection.h>

#include <ocs2_sqp/DynamicsDiscretization.h>
#include <iostream>

namespace ocs2 {

MultipleShootingSolver::MultipleShootingSolver(MultipleShootingSolverSettings settings, const SystemDynamicsBase* systemDynamicsPtr,
                                               const CostFunctionBase* costFunctionPtr, const ConstraintBase* constraintPtr,
                                               const CostFunctionBase* terminalCostFunctionPtr,
                                               const SystemOperatingTrajectoriesBase* operatingTrajectoriesPtr)
    : SolverBase(),
      systemDynamicsPtr_(systemDynamicsPtr->clone()),
      costFunctionPtr_(costFunctionPtr->clone()),
      constraintPtr_(nullptr),
      terminalCostFunctionPtr_(nullptr),
      settings_(std::move(settings)),
      totalNumIterations_(0) {
  if (constraintPtr != nullptr) {
    constraintPtr_.reset(constraintPtr->clone());

    if (settings_.inequalityConstraintMu > 0) {
      penaltyPtr_.reset(new RelaxedBarrierPenalty(settings_.inequalityConstraintMu, settings_.inequalityConstraintDelta));
    }
  }

  if (terminalCostFunctionPtr != nullptr) {
    terminalCostFunctionPtr_.reset(terminalCostFunctionPtr->clone());
  }

  if (operatingTrajectoriesPtr != nullptr) {
    operatingTrajectoriesPtr_.reset(operatingTrajectoriesPtr->clone());
  }
}

MultipleShootingSolver::~MultipleShootingSolver() {
  if (settings_.printSolverStatistics) {
    std::cerr << getBenchmarkingInformation() << std::endl;
  }
}

void MultipleShootingSolver::reset() {
  // Clear solution
  primalSolution_ = PrimalSolution();

  // reset timers
  totalNumIterations_ = 0;
  linearQuadraticApproximationTimer_.reset();
  solveQpTimer_.reset();
  computeControllerTimer_.reset();
}

std::string MultipleShootingSolver::getBenchmarkingInformation() const {
  const auto linearQuadraticApproximationTotal = linearQuadraticApproximationTimer_.getTotalInMilliseconds();
  const auto solveQpTotal = solveQpTimer_.getTotalInMilliseconds();
  const auto computeControllerTotal = computeControllerTimer_.getTotalInMilliseconds();

  const auto benchmarkTotal = linearQuadraticApproximationTotal + solveQpTotal + computeControllerTotal;

  std::stringstream infoStream;
  if (benchmarkTotal > 0.0) {
    const scalar_t inPercent = 100.0;
    infoStream << "\n########################################################################\n";
    infoStream << "The benchmarking is computed over " << totalNumIterations_ << " iterations. \n";
    infoStream << "SQP Benchmarking\t   :\tAverage time [ms]   (% of total runtime)\n";
    infoStream << "\tLQ Approximation   :\t" << linearQuadraticApproximationTimer_.getAverageInMilliseconds() << " [ms] \t\t("
               << linearQuadraticApproximationTotal / benchmarkTotal * inPercent << "%)\n";
    infoStream << "\tSolve QP           :\t" << solveQpTimer_.getAverageInMilliseconds() << " [ms] \t\t("
               << solveQpTotal / benchmarkTotal * inPercent << "%)\n";
    infoStream << "\tCompute Controller :\t" << computeControllerTimer_.getAverageInMilliseconds() << " [ms] \t\t("
               << computeControllerTotal / benchmarkTotal * inPercent << "%)\n";
  }
  return infoStream.str();
}

void MultipleShootingSolver::runImpl(scalar_t initTime, const vector_t& initState, scalar_t finalTime,
                                     const scalar_array_t& partitioningTimes) {
  if (settings_.printSolverStatus) {
    std::cerr << "\n++++++++++++++++++++++++++++++++++++++++++++++++++++++";
    std::cerr << "\n+++++++++++++ SQP solver is initialized ++++++++++++++";
    std::cerr << "\n++++++++++++++++++++++++++++++++++++++++++++++++++++++\n";
  }

  // Determine time discretization, taking into account event times.
  scalar_array_t timeDiscretization = timeDiscretizationWithEvents(initTime, finalTime, settings_.dt, this->getModeSchedule().eventTimes,
                                                                   OCS2NumericTraits<scalar_t>::limitEpsilon());
  const int N = static_cast<int>(timeDiscretization.size()) - 1;

  // Initialize the state and input
  vector_array_t x = initializeStateTrajectory(initState, timeDiscretization, N);
  vector_array_t u = initializeInputTrajectory(timeDiscretization, x, N);

  // Initialize cost
  costFunctionPtr_->setCostDesiredTrajectoriesPtr(&this->getCostDesiredTrajectories());
  if (terminalCostFunctionPtr_) {
    terminalCostFunctionPtr_->setCostDesiredTrajectoriesPtr(&this->getCostDesiredTrajectories());
  }

  for (int iter = 0; iter < settings_.sqpIteration; iter++) {
    if (settings_.printSolverStatus) {
      std::cerr << "SQP iteration: " << iter << "\n";
    }
    // Make QP approximation
    linearQuadraticApproximationTimer_.startTimer();
    setupCostDynamicsEqualityConstraint(*systemDynamicsPtr_, *costFunctionPtr_, constraintPtr_.get(), terminalCostFunctionPtr_.get(),
                                        timeDiscretization, x, u);
    linearQuadraticApproximationTimer_.endTimer();

    // Solve QP
    solveQpTimer_.startTimer();
    const vector_t delta_x0 = initState - x[0];
    vector_array_t delta_x;
    vector_array_t delta_u;
    std::tie(delta_x, delta_u) = getOCPSolution(delta_x0);
    solveQpTimer_.endTimer();

    // Apply step
    bool converged = takeStep(timeDiscretization, delta_x, delta_u, x, u);

    totalNumIterations_++;
    if (converged) {
      break;
    }
  }

  // Store result in PrimalSolution. time, state , input
  primalSolution_.timeTrajectory_ = std::move(timeDiscretization);
  primalSolution_.stateTrajectory_ = std::move(x);
  primalSolution_.inputTrajectory_ = std::move(u);
  primalSolution_.inputTrajectory_.push_back(primalSolution_.inputTrajectory_.back());  // repeat last input to make equal length vectors
  primalSolution_.modeSchedule_ = this->getModeSchedule();
  primalSolution_.controllerPtr_.reset(new FeedforwardController(primalSolution_.timeTrajectory_, primalSolution_.inputTrajectory_));

  computeControllerTimer_.endTimer();

  if (settings_.printSolverStatus) {
    std::cerr << "\n++++++++++++++++++++++++++++++++++++++++++++++++++++++";
    std::cerr << "\n+++++++++++++ SQP solver has terminated ++++++++++++++";
    std::cerr << "\n++++++++++++++++++++++++++++++++++++++++++++++++++++++\n";
  }
}

vector_array_t MultipleShootingSolver::initializeInputTrajectory(const scalar_array_t& timeDiscretization,
                                                                 const vector_array_t& stateTrajectory, int N) const {
  const scalar_t interpolateTill = (totalNumIterations_ > 0) ? primalSolution_.timeTrajectory_.back() : timeDiscretization.front();

  vector_array_t u;
  u.reserve(N);
  for (int i = 0; i < N; i++) {
    const scalar_t ti = timeDiscretization[i];
    if (ti < interpolateTill) {
      // Interpolate previous input trajectory
      u.emplace_back(
          LinearInterpolation::interpolate(timeDiscretization[i], primalSolution_.timeTrajectory_, primalSolution_.inputTrajectory_));
    } else {
      // No previous control at this time-point -> fall back to heuristics
      if (operatingTrajectoriesPtr_) {
        // Ask for operating trajectory between t[k] and t[k+1]. Take the returned input at t[k] as our heuristic.
        const scalar_t tNext = timeDiscretization[i + 1];
        scalar_array_t timeArray;
        vector_array_t stateArray;
        vector_array_t inputArray;
        operatingTrajectoriesPtr_->getSystemOperatingTrajectories(stateTrajectory[i], ti, tNext, timeArray, stateArray, inputArray, false);
        u.push_back(std::move(inputArray.front()));
      } else {  // No information at all. Set inputs to zero.
        u.emplace_back(vector_t::Zero(settings_.n_input));
      }
    }
  }

  return u;
}

vector_array_t MultipleShootingSolver::initializeStateTrajectory(const vector_t& initState, const scalar_array_t& timeDiscretization,
                                                                 int N) const {
  if (totalNumIterations_ == 0) {  // first iteration
    return vector_array_t(N + 1, initState);
  } else {  // interpolation of previous solution
    vector_array_t x;
    x.reserve(N + 1);
    x.push_back(initState);  // Force linearization of the first node around the current state
    for (int i = 1; i < (N + 1); i++) {
      x.emplace_back(
          LinearInterpolation::interpolate(timeDiscretization[i], primalSolution_.timeTrajectory_, primalSolution_.stateTrajectory_));
    }
    return x;
  }
}

std::pair<vector_array_t, vector_array_t> MultipleShootingSolver::getOCPSolution(const vector_t& delta_x0) {
  // Solve the QP
  vector_array_t deltaXSol;
  vector_array_t deltaUSol;
  if (constraintPtr_ && !settings_.qr_decomp) {
    hpipmInterface_.solve(delta_x0, dynamics_, cost_, &constraints_, deltaXSol, deltaUSol, settings_.printSolverStatus);
  } else {  // without constraints, or when using QR decomposition, we have an unconstrained QP.
    hpipmInterface_.solve(delta_x0, dynamics_, cost_, nullptr, deltaXSol, deltaUSol, settings_.printSolverStatus);
  }

  // remap the tilde delta u to real delta u
  if (constraintPtr_ && settings_.qr_decomp) {
    for (int i = 0; i < deltaUSol.size(); i++) {
      deltaUSol[i] = constraints_[i].dfdu * deltaUSol[i];  // creates a temporary because of alias
      deltaUSol[i].noalias() += constraints_[i].dfdx * deltaXSol[i];
      deltaUSol[i] += constraints_[i].f;
    }
  }

  return {deltaXSol, deltaUSol};
}

void MultipleShootingSolver::setupCostDynamicsEqualityConstraint(SystemDynamicsBase& systemDynamics, CostFunctionBase& costFunction,
                                                                 ConstraintBase* constraintPtr, CostFunctionBase* terminalCostFunctionPtr,
                                                                 const scalar_array_t& time, const vector_array_t& x,
                                                                 const vector_array_t& u) {
  // Problem horizon
  const int N = static_cast<int>(time.size()) - 1;

  // Set up for constant state input size. Will be adapted based on constraint handling.
  HpipmInterface::OcpSize ocpSize(N, settings_.n_state, settings_.n_input);

  dynamics_.resize(N);
  cost_.resize(N + 1);
  constraints_.resize(N + 1);
  for (int i = 0; i < N; i++) {
    const scalar_t ti = time[i];
    const scalar_t dt = time[i + 1] - time[i];

    // Dynamics
    // Discretization returns // x_{k+1} = A_{k} * dx_{k} + B_{k} * du_{k} + b_{k}
    dynamics_[i] = rk4Discretization(systemDynamics, ti, x[i], u[i], dt);
    dynamics_[i].f -= x[i + 1];  // make it dx_{k+1} = ...

    // Costs: Approximate the integral with forward euler (correct for dt after adding penalty)
    cost_[i] = costFunction.costQuadraticApproximation(ti, x[i], u[i]);

    if (constraintPtr != nullptr) {
      // C_{k} * dx_{k} + D_{k} * du_{k} + e_{k} = 0
      constraints_[i] = constraintPtr->stateInputEqualityConstraintLinearApproximation(ti, x[i], u[i]);
      if (settings_.qr_decomp) {  // Handle equality constraints using projection.
        // Reduces number of inputs
        ocpSize.nu[i] = settings_.n_input - constraints_[i].f.rows();
        // Projection stored instead of constraint, // TODO: benchmark between lu and qr method. LU seems slightly faster.
        constraints_[i] = luConstraintProjection(constraints_[i]);

        // Adapt dynamics and cost
        changeOfInputVariables(dynamics_[i], constraints_[i].dfdu, constraints_[i].dfdx, constraints_[i].f);
        changeOfInputVariables(cost_[i], constraints_[i].dfdu, constraints_[i].dfdx, constraints_[i].f);
      } else {
        // Declare as general inequalities
        ocpSize.ng[i] = constraints_[i].f.rows();
      }

      // Inequalities as penalty
      if (penaltyPtr_) {
        const auto ineqConstraints = constraintPtr->inequalityConstraintQuadraticApproximation(ti, x[i], u[i]);
        if (ineqConstraints.f.rows() > 0) {
          cost_[i] += penaltyPtr_->penaltyCostQuadraticApproximation(ineqConstraints);
        }
      }
    }

    // Costs: Approximate the integral with forward euler  (correct for dt HERE, after adding penalty)
    cost_[i].dfdxx *= dt;
    cost_[i].dfdux *= dt;
    cost_[i].dfduu *= dt;
    cost_[i].dfdx *= dt;
    cost_[i].dfdu *= dt;
    cost_[i].f *= dt;
  }

  if (terminalCostFunctionPtr != nullptr) {
    cost_[N] = terminalCostFunctionPtr->finalCostQuadraticApproximation(time[N], x[N]);
  } else {
    cost_[N] = ScalarFunctionQuadraticApproximation::Zero(settings_.n_state, 0);
  }

  // Prepare solver size
  hpipmInterface_.resize(std::move(ocpSize));
}

bool MultipleShootingSolver::takeStep(const scalar_array_t& timeDiscretization, const vector_array_t& dx, const vector_array_t& du,
                                      vector_array_t& x, vector_array_t& u) {
  const int N = static_cast<int>(timeDiscretization.size()) - 1;

  // TODO implement line search
  const scalar_t alpha = 1.0;
  
  computeControllerTimer_.startTimer();
  scalar_t deltaUnorm = 0.0;
  for (int i = 0; i < N; i++) {
    deltaUnorm += alpha * du[i].norm();
    u[i] += alpha * du[i];
  }
  scalar_t deltaXnorm = 0.0;
  for (int i = 0; i < (N + 1); i++) {
    deltaXnorm += alpha * dx[i].norm();
    x[i] += alpha * dx[i];
  }

  return deltaUnorm < settings_.deltaTol && deltaXnorm < settings_.deltaTol;
}

scalar_array_t MultipleShootingSolver::timeDiscretizationWithEvents(scalar_t initTime, scalar_t finalTime, scalar_t dt,
                                                                    const scalar_array_t& eventTimes, scalar_t eventDelta) {
  /*
  A simple example here illustrates the mission of this function

  Assume:
    eventTimes = {3.25, 3.4, 3.88, 4.02, 4.5}
    initTime = 3.0
    finalTime = 4.0
    user_defined delta_t = 0.1
    eps = eventDelta. : time added after an event to take the discretization after the mode transition.

  Then the following variables will be:
    timeDiscretization = {3.0, 3.1, 3.2, 3.25 + eps, 3.35, 3.4 + eps, 3.5, 3.6, 3.7, 3.8, 3.88 + eps, 3.98, 4.0}
  */
  assert(dt > 0);
  assert(finalTime > initTime);
  scalar_array_t timeDiscretization;

  // Initialize
  timeDiscretization.push_back(initTime);
  scalar_t nextEventIdx = lookup::findIndexInTimeArray(eventTimes, initTime);

  // Fill iteratively
  scalar_t nextTime = timeDiscretization.back();
  while (timeDiscretization.back() < finalTime) {
    nextTime = nextTime + dt;
    bool nextTimeIsEvent = false;

    // Check if an event has passed
    if (nextEventIdx < eventTimes.size() && nextTime >= eventTimes[nextEventIdx]) {
      nextTime = eventTimes[nextEventIdx];
      nextTimeIsEvent = true;
      nextEventIdx++;
    }

    // Check if final time has passed
    if (nextTime >= finalTime) {
      nextTime = finalTime;
      nextTimeIsEvent = false;
    }

    // Add discretization point (after event for eventTimes)
    if (nextTimeIsEvent) {
      timeDiscretization.push_back(nextTime + eventDelta);
    } else {
      timeDiscretization.push_back(nextTime);
    }
  }

  return timeDiscretization;
}

}  // namespace ocs2