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

#include "shelly_diffuser.hpp"

#include "mgos.hpp"
#include "mgos_hap_chars.hpp"

namespace shelly {
namespace hap {
namespace {

// ON is a short press of the main button. OFF requires both the main button
// and the LED-button contacts to be shorted simultaneously for three seconds.
constexpr int kDiffuserOnPressMs = 100;
constexpr int kDiffuserOffPressMs = 3000;

// Do not keep a command pending indefinitely if the diffuser does not report
// the requested state through its atomizer feedback line.
constexpr int kCommandTimeoutMs = 15000;

}  // namespace

DiffuserSwitch::DiffuserSwitch(int id, Input *feedback_in, Output *button_out,
                               Output *led_button_out,
                               struct mgos_config_sw *cfg)
    : ShellySwitch(id, feedback_in, button_out, nullptr, nullptr, cfg),
      feedback_in_(feedback_in),
      led_button_out_(led_button_out),
      pulse_duration_timer_(
          std::bind(&DiffuserSwitch::ButtonPulseCompleteCB, this)),
      command_timeout_timer_(
          std::bind(&DiffuserSwitch::CommandTimeoutCB, this)) {
}

Status DiffuserSwitch::Init() {
  if (!cfg_->enable) {
    LOG(LL_INFO, ("'%s' is disabled", cfg_->name));
    return Status::OK();
  }
  if (feedback_in_ == nullptr || out_ == nullptr ||
      led_button_out_ == nullptr) {
    return mgos::Errorf(
        STATUS_INVALID_ARGUMENT,
        "diffuser feedback input and both button outputs are required");
  }

  // IN2 is the only source of truth for HomeKit's On characteristic.
  feedback_in_->SetInvert(cfg_->in_inverted);
  last_feedback_state_ = ReadState();

  // OUT1 and OUT2 are momentary button actuators, never a persistent
  // representation of diffuser state. Always leave both inactive after boot.
  out_->SetInvert(cfg_->out_inverted);
  led_button_out_->SetInvert(cfg_->out_inverted);
  out_->SetState(false, "diffuser_init");
  led_button_out_->SetState(false, "diffuser_init");

  const auto handler_id = feedback_in_->AddHandler(
      std::bind(&DiffuserSwitch::FeedbackEventHandler, this, _1, _2));
  in_handler_ids_.push_back(handler_id);

  const int id1 = id() - 1;  // IDs used to start at 0, preserve compatibility.
  uint16_t iid =
      SHELLY_HAP_IID_BASE_SWITCH + (SHELLY_HAP_IID_STEP_SWITCH * id1);
  svc_.iid = iid++;
  svc_.serviceType = &kHAPServiceType_Switch;
  svc_.debugDescription = kHAPServiceDebugDescription_Switch;

  AddNameChar(iid++, cfg_->name);
  on_char_ = new mgos::hap::BoolCharacteristic(
      iid++, &kHAPCharacteristicType_On,
      [this](HAPAccessoryServerRef *, const HAPBoolCharacteristicReadRequest *,
             bool *value) {
        *value = ReadState() ^ cfg_->hk_state_inverted;
        return kHAPError_None;
      },
      true /* supports_notification */,
      [this](HAPAccessoryServerRef *, const HAPBoolCharacteristicWriteRequest *,
             bool value) {
        const auto st =
            RequestState(value ^ cfg_->hk_state_inverted, "HomeKit");
        if (!st.ok()) {
          LOG(LL_WARN,
              ("Diffuser command rejected: %s", st.ToString().c_str()));
          return kHAPError_Busy;
        }
        // HomeKit will read the authoritative input state. A successful write
        // acknowledges the request, not an assumed state transition.
        return kHAPError_None;
      },
      kHAPCharacteristicDebugDescription_On);
  state_notify_chars_.push_back(on_char_);
  AddChar(on_char_);

  LOG(LL_INFO,
      ("Diffuser switch initialized: feedback input %d, main button output "
       "%d, LED-button output %d, state %s",
       feedback_in_->id(), out_->id(), led_button_out_->id(),
       OnOff(last_feedback_state_)));
  return Status::OK();
}

StatusOr<std::string> DiffuserSwitch::GetInfo() const {
  return mgos::SPrintf("st:%d in_st:%d inm:%d ininv:%d pending:%d target:%d",
                       ReadState(), GetInputState(), cfg_->in_mode,
                       cfg_->in_inverted, command_pending_, pending_state_);
}

StatusOr<std::string> DiffuserSwitch::GetInfoJSON() const {
  const bool hdim = (SHELLY_HAVE_DUAL_INPUT_MODES ? true : false);
  return mgos::JSONPrintStringf(
      "{id: %d, type: %d, name: %Q, svc_type: %d, hk_state_inverted: %B, "
      "valve_type: %d, in_mode: %d, in_inverted: %B, initial: %d, "
      "state: %B, auto_off: %B, auto_off_delay: %.3f, state_led_en: %d, "
      "out_inverted: %B, hdim: %B, command_pending: %B, "
      "pending_state: %B, on_press_ms: %d, off_press_ms: %d, "
      "off_uses_both_outputs: %B}",
      id(), (int) type(), (cfg_->name ? cfg_->name : ""), cfg_->svc_type,
      cfg_->hk_state_inverted, cfg_->valve_type, cfg_->in_mode,
      cfg_->in_inverted, cfg_->initial_state, ReadState(), cfg_->auto_off,
      cfg_->auto_off_delay, cfg_->state_led_en, cfg_->out_inverted, hdim,
      command_pending_, pending_state_, kDiffuserOnPressMs, kDiffuserOffPressMs,
      true);
}

Status DiffuserSwitch::SetState(const std::string &state_json) {
  int8_t state = -1;
  int8_t toggle = -1;
  json_scanf(state_json.c_str(), state_json.size(), "{state: %B}", &state);
  json_scanf(state_json.c_str(), state_json.size(), "{toggle: %B}", &toggle);
  if (state < 0 && toggle < 0) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "%s or %s is required",
                        "state", "toggle");
  }
  if (state != -1 && toggle != -1) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "only %s or %s can be used",
                        "state", "toggle");
  }
  if (toggle != -1) state = !ReadState();
  return RequestState(state, "RPC");
}

void DiffuserSwitch::FeedbackEventHandler(Input::Event ev, bool state) {
  if (ev != Input::Event::kChange) return;

  const bool current_state = ReadState();
  if (current_state == last_feedback_state_) return;
  last_feedback_state_ = current_state;

  LOG(LL_INFO, ("Diffuser feedback (IN2): %s", OnOff(current_state)));

  if (command_pending_) {
    feedback_confirmed_ = (current_state == pending_state_);
    TryFinishCommand();
  }

  if (on_char_ != nullptr) on_char_->RaiseEvent();
  (void) state;
}

void DiffuserSwitch::ButtonPulseCompleteCB() {
  pulse_complete_ = true;
  TryFinishCommand();
}

void DiffuserSwitch::CommandTimeoutCB() {
  if (!command_pending_) return;
  LOG(LL_WARN, ("Diffuser did not confirm requested %s state through IN2",
                OnOff(pending_state_)));
  command_pending_ = false;
  pulse_duration_timer_.Clear();
}

void DiffuserSwitch::TryFinishCommand() {
  if (!command_pending_ || !feedback_confirmed_ || !pulse_complete_) return;
  command_pending_ = false;
  command_timeout_timer_.Clear();
  LOG(LL_INFO, ("Diffuser command confirmed by IN2 and button pulse "
                "completed"));
}

Status DiffuserSwitch::RequestState(bool state, const char *source) {
  // Repeated writes for the same target are idempotent. A conflicting write
  // is rejected until the current physical button press has completed.
  if (command_pending_) {
    if (pending_state_ == state) return Status::OK();
    return mgos::Errorf(STATUS_UNAVAILABLE, "diffuser command already pending");
  }

  const bool current_state = ReadState();
  if (current_state == state) return Status::OK();

  const int press_ms = state ? kDiffuserOnPressMs : kDiffuserOffPressMs;

  // ON: briefly short the main button only.
  auto st = out_->Pulse(true, press_ms, source);
  if (!st.ok()) return st;

  // OFF: the diffuser requires the main button and the LED-button contacts
  // to be shorted at the same time. If starting the second pulse fails,
  // immediately release the first output so we never leave a partial press.
  if (!state) {
    st = led_button_out_->Pulse(true, press_ms, source);
    if (!st.ok()) {
      out_->SetState(false, "diffuser_off_command_rollback");
      led_button_out_->SetState(false, "diffuser_off_command_rollback");
      return st;
    }
  }

  pending_state_ = state;
  command_pending_ = true;
  feedback_confirmed_ = false;
  pulse_complete_ = false;
  pulse_duration_timer_.Reset(press_ms, 0);
  command_timeout_timer_.Reset(kCommandTimeoutMs, 0);
  LOG(LL_INFO,
      ("Diffuser: requested %s; %s button pulse for %d ms (%s)", OnOff(state),
       state ? "OUT1 only" : "OUT1 + OUT2 simultaneously", press_ms,
       source ? source : ""));
  return Status::OK();
}

bool DiffuserSwitch::IsIdle() {
  return !command_pending_ && !pulse_duration_timer_.IsValid() &&
         !command_timeout_timer_.IsValid() && ShellySwitch::IsIdle();
}

bool DiffuserSwitch::ReadState() const {
  return feedback_in_ != nullptr && feedback_in_->GetState();
}

}  // namespace hap
}  // namespace shelly
