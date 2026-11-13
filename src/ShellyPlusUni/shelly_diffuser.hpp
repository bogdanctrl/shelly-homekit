/*
 * Copyright (c) Shelly-HomeKit Contributors
 * All rights reserved
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "shelly_switch.hpp"

namespace shelly {
namespace hap {

// HomeKit switch facade for a diffuser controlled by two button contacts.
// OUT1 is used for ON; OUT1 and OUT2 together are used for OFF. IN2 is the
// authoritative source of the diffuser's actual state.
class DiffuserSwitch final : public ShellySwitch {
 public:
  DiffuserSwitch(int id, Input *feedback_in, Output *button_out,
                 Output *led_button_out, struct mgos_config_sw *cfg);
  ~DiffuserSwitch() override = default;

  Status Init() override;
  StatusOr<std::string> GetInfo() const override;
  StatusOr<std::string> GetInfoJSON() const override;
  Status SetState(const std::string &state_json) override;
  bool IsIdle() override;

 private:
  void FeedbackEventHandler(Input::Event ev, bool state);
  void ButtonPulseCompleteCB();
  void CommandTimeoutCB();
  void TryFinishCommand();
  Status RequestState(bool state, const char *source);
  bool ReadState() const;

  Input *const feedback_in_;
  Output *const led_button_out_;
  mgos::hap::Characteristic *on_char_ = nullptr;
  mgos::Timer pulse_duration_timer_;
  mgos::Timer command_timeout_timer_;
  bool last_feedback_state_ = false;
  bool command_pending_ = false;
  bool pending_state_ = false;
  bool feedback_confirmed_ = false;
  bool pulse_complete_ = true;
};

}  // namespace hap
}  // namespace shelly
