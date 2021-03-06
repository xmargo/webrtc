/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/audio_processing/gain_controller2.h"

#include "common_audio/include/audio_util.h"
#include "modules/audio_processing/audio_buffer.h"
#include "modules/audio_processing/include/audio_frame_view.h"
#include "modules/audio_processing/logging/apm_data_dumper.h"
#include "rtc_base/atomicops.h"
#include "rtc_base/checks.h"
#include "rtc_base/strings/string_builder.h"

namespace webrtc {

int GainController2::instance_count_ = 0;

GainController2::GainController2()
    : data_dumper_(
          new ApmDataDumper(rtc::AtomicOps::Increment(&instance_count_))),
      gain_applier_(/*hard_clip_samples=*/false,
                    /*initial_gain_factor=*/0.f),
      adaptive_agc_(new AdaptiveAgc(data_dumper_.get())),
      limiter_(static_cast<size_t>(48000), data_dumper_.get(), "Agc2") {}

GainController2::~GainController2() = default;

void GainController2::Initialize(int sample_rate_hz) {
  RTC_DCHECK(sample_rate_hz == AudioProcessing::kSampleRate8kHz ||
             sample_rate_hz == AudioProcessing::kSampleRate16kHz ||
             sample_rate_hz == AudioProcessing::kSampleRate32kHz ||
             sample_rate_hz == AudioProcessing::kSampleRate48kHz);
  limiter_.SetSampleRate(sample_rate_hz);
  data_dumper_->InitiateNewSetOfRecordings();
  data_dumper_->DumpRaw("sample_rate_hz", sample_rate_hz);
}

void GainController2::Process(AudioBuffer* audio) {
  AudioFrameView<float> float_frame(audio->channels_f(), audio->num_channels(),
                                    audio->num_frames());
  // Apply fixed gain first, then the adaptive one.
  gain_applier_.ApplyGain(float_frame);
  if (adaptive_digital_mode_) {
    adaptive_agc_->Process(float_frame, limiter_.LastAudioLevel());
  }
  limiter_.Process(float_frame);
}

void GainController2::NotifyAnalogLevel(int level) {
  if (analog_level_ != level && adaptive_digital_mode_) {
    adaptive_agc_->Reset();
  }
  analog_level_ = level;
}

void GainController2::ApplyConfig(
    const AudioProcessing::Config::GainController2& config) {
  RTC_DCHECK(Validate(config));
  config_ = config;
  if (gain_applier_.GetGainFactor() != config_.fixed_gain_db) {
    // Reset the limiter to quickly react on abrupt level changes caused by
    // large changes of the fixed gain.
    limiter_.Reset();
  }
  gain_applier_.SetGainFactor(DbToRatio(config_.fixed_gain_db));
  adaptive_digital_mode_ = config_.adaptive_digital_mode;
  adaptive_agc_.reset(
      new AdaptiveAgc(data_dumper_.get(), config_.extra_saturation_margin_db));
}

bool GainController2::Validate(
    const AudioProcessing::Config::GainController2& config) {
  return config.fixed_gain_db >= 0.f &&
         config.extra_saturation_margin_db >= 0.f &&
         config.extra_saturation_margin_db <= 100.f;
}

std::string GainController2::ToString(
    const AudioProcessing::Config::GainController2& config) {
  rtc::StringBuilder ss;
  ss << "{enabled: " << (config.enabled ? "true" : "false") << ", "
     << "fixed_gain_dB: " << config.fixed_gain_db << "}";
  return ss.Release();
}

}  // namespace webrtc
