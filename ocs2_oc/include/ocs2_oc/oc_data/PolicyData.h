/******************************************************************************
Copyright (c) 2017, Farbod Farshidian. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the copyright holder nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
******************************************************************************/

#pragma once

#include <memory>

#include <ocs2_core/Dimensions.h>
#include <ocs2_core/control/ControllerBase.h>
#include <ocs2_core/control/FeedforwardController.h>

namespace ocs2 {

/**
 * This class contains the solver policy information.
 *
 * @tparam STATE_DIM: Dimension of the state space.
 * @tparam INPUT_DIM: Dimension of the control input space.
 */
template <size_t STATE_DIM, size_t INPUT_DIM>
struct PolicyData {
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW

  using dim_t = Dimensions<STATE_DIM, INPUT_DIM>;
  using size_array_t = typename dim_t::size_array_t;
  using scalar_array_t = typename dim_t::scalar_array_t;
  using scalar_array2_t = typename dim_t::scalar_array2_t;
  using state_vector_t = typename dim_t::state_vector_t;
  using state_vector_array_t = typename dim_t::state_vector_array_t;
  using state_vector_array2_t = typename dim_t::state_vector_array2_t;
  using input_vector_t = typename dim_t::input_vector_t;
  using input_vector_array_t = typename dim_t::input_vector_array_t;
  using input_vector_array2_t = typename dim_t::input_vector_array2_t;

  using controller_t = ControllerBase<STATE_DIM, INPUT_DIM>;
  using controller_const_ptr_array_t = std::vector<const controller_t*>;
  using feedforward_controller_t = FeedforwardController<STATE_DIM, INPUT_DIM>;

  scalar_array_t mpcTimeTrajectory_;
  state_vector_array_t mpcStateTrajectory_;
  input_vector_array_t mpcInputTrajectory_;
  scalar_array_t eventTimes_;
  size_array_t subsystemsSequence_;
  std::unique_ptr<controller_t> mpcController_;

  /**'
   * Fills the data.
   * @param [in] timeTrajectoriesPtr: A pointer to the time trajectories containing the output time stamp for state and input trajectories.
   * @param [in] stateTrajectoriesPtr: A pointer to the state trajectories.
   * @param [in] inputTrajectoriesPtr: A pointer to the input trajectories.
   * @param [in] eventTimes: The event time array.
   * @param [in] subsystemsSequencePtr: The subsystem array.
   * @param [in] controllerPtrs: An array of pointers to the controller.
   */
  void fill(const scalar_array2_t* timeTrajectoriesPtr, const state_vector_array2_t* stateTrajectoriesPtr,
            const input_vector_array2_t* inputTrajectoriesPtr, scalar_array_t eventTimes, size_array_t subsystemsSequence,
            controller_const_ptr_array_t controllerPtrs = controller_const_ptr_array_t()) {
    // total number of nodes
    int N = 0;
    for (int i = 0; i < timeTrajectoriesPtr->size(); i++) {
      N += (*timeTrajectoriesPtr)[i].size();
    }

    // fill trajectories
    mpcTimeTrajectory_.clear();
    mpcTimeTrajectory_.reserve(N);
    mpcStateTrajectory_.clear();
    mpcStateTrajectory_.reserve(N);
    mpcInputTrajectory_.clear();
    mpcInputTrajectory_.reserve(N);
    for (int i = 0; i < timeTrajectoriesPtr->size(); i++) {
      mpcTimeTrajectory_.insert(std::end(mpcTimeTrajectory_), std::begin((*timeTrajectoriesPtr)[i]), std::end((*timeTrajectoriesPtr)[i]));
      mpcStateTrajectory_.insert(std::end(mpcStateTrajectory_), std::begin((*stateTrajectoriesPtr)[i]),
                                 std::end((*stateTrajectoriesPtr)[i]));
      mpcInputTrajectory_.insert(std::end(mpcInputTrajectory_), std::begin((*inputTrajectoriesPtr)[i]),
                                 std::end((*inputTrajectoriesPtr)[i]));
    }

    // fill controller
    if (!controllerPtrs.empty()) {
      mpcController_.reset();
      // concatenate controller stock into a single controller
      for (auto controllerPtr : controllerPtrs) {
        if (controllerPtr->empty()) {
          continue;  // some time partitions may be unused
        }

        if (mpcController_) {
          mpcController_->concatenate(controllerPtr);
        } else {
          mpcController_.reset(controllerPtr->clone());
        }
      }
    } else {
      mpcController_.reset(new feedforward_controller_t(mpcTimeTrajectory_, mpcInputTrajectory_));
    }

    // fill logic
    eventTimes_ = std::move(eventTimes);
    subsystemsSequence_ = std::move(subsystemsSequence);
  }
};

}  // namespace ocs2