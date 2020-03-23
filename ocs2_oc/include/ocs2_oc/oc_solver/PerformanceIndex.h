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

#include <ostream>

#include <ocs2_core/Dimensions.h>

namespace ocs2 {

/**
 * Defines the performance indices for a rollout
 */
struct PerformanceIndex {
  using scalar_t = typename Dimensions<0, 0>::scalar_t;

  scalar_t merit = 0.0;
  scalar_t totalCost = 0.0;
  scalar_t stateEqConstraintISE = 0.0;
  scalar_t stateEqFinalConstraintISE = 0.0;
  scalar_t stateInputEqConstraintISE = 0.0;
  scalar_t inequalityConstraintISE = 0.0;
  scalar_t inequalityConstraintPenalty = 0.0;
};

inline std::ostream& operator<<(std::ostream& stream, const PerformanceIndex& performanceIndex) {
  stream << "rollout merit: " << performanceIndex.merit << '\n';
  stream << "rollout cost:  " << performanceIndex.totalCost << '\n';
  stream << "state equality constraints ISE:       " << performanceIndex.stateEqConstraintISE << '\n';
  stream << "state equality final constraints ISE: " << performanceIndex.stateEqFinalConstraintISE << '\n';
  stream << "state-input equality constraints ISE: " << performanceIndex.stateInputEqConstraintISE << '\n';
  stream << "inequality constraints ISE:           " << performanceIndex.inequalityConstraintISE << '\n';
  stream << "inequality constraints penalty:       " << performanceIndex.inequalityConstraintPenalty << '\n';
  return stream;
}

}  // namespace ocs2
