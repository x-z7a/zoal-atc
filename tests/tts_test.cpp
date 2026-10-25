#include "zoal_atc/tts/synthesis.hpp"

#include <chrono>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace zoal_atc;

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

// FakeSynthesizer says every sentence as a tenth of a second of tone, and
// remembers what it was asked to say and in which voice.
class FakeSynthesizer final : public tts::Synthesizer {
public:
  explicit FakeSynthesizer(bool fail = false) : fail_(fail) {}

  tts::Audio synthesize(const std::string &text, int speaker) override {
    {
      std::lock_guard<std::mutex> lock(mutex);
      said.push_back(text);
      speakers_used.push_back(speaker);
    }
    tts::Audio audio;
    if (fail_) {
      return audio;
    }
    audio.sample_rate_hz = 24000;
    audio.pcm.assign(2400, 1000);
    return audio;
  }
  int speakers() const override { return 11; }

  std::mutex mutex;
  std::vector<std::string> said;
  std::vector<int> speakers_used;

private:
  bool fail_;
};

bool wait_finished(const tts::Speech &speech) {
  for (int i = 0; i < 200 && !speech.finished(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return speech.finished();
}

// Energy at one frequency, by correlation, to show what the radio passes.
double energy(const std::vector<std::int16_t> &pcm, double hz, double rate) {
  double re = 0, im = 0;
  for (std::size_t i = pcm.size() / 2; i < pcm.size(); ++i) {
    const double phase = 2.0 * 3.14159265358979 * hz * static_cast<double>(i) / rate;
    re += pcm[i] * std::cos(phase);
    im += pcm[i] * std::sin(phase);
  }
  return std::sqrt(re * re + im * im);
}

std::vector<std::int16_t> tone(double hz, int rate, int samples) {
  std::vector<std::int16_t> pcm(static_cast<std::size_t>(samples));
  for (int i = 0; i < samples; ++i) {
    pcm[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(
        8000.0 * std::sin(2.0 * 3.14159265358979 * hz * i / rate));
  }
  return pcm;
}

}  // namespace

int main() {
  // The console numbers voices from one; zero is the default speaker.
  require(tts::speaker_for(0, 11, 5) == 5, "no voice is the default speaker");
  require(tts::speaker_for(1, 11, 5) == 0, "voice one is the first speaker");
  require(tts::speaker_for(11, 11, 5) == 10, "voice eleven is the last");
  require(tts::speaker_for(12, 11, 5) == 0, "a larger pool wraps");
  require(tts::speaker_for(3, 0, 5) == 0, "no speakers at all is speaker zero");

  // A reply comes out of the radio tuned to the channel it was said on.
  {
    const std::uint64_t ground = 121'900'000, tower = 118'700'000, atis = 135'000'000;
    require(tts::receiving_com(ground, ground, atis, 2) == 1,
            "Ground on COM1 is heard on COM1 while COM2 is selected");
    require(tts::receiving_com(tower, ground, tower, 1) == 2,
            "Tower on COM2 is heard on COM2 while COM1 is selected");
    require(tts::receiving_com(tower, tower, tower, 2) == 2,
            "both tuned there: the selected one");
    require(tts::receiving_com(tower, tower, tower, 0) == 1,
            "both tuned there, selection unknown: COM1");
    require(tts::receiving_com(ground, tower, atis, 2) == 2,
            "retuned since: the radio being listened to");
    require(tts::receiving_com(0, ground, tower, 2) == 2,
            "an older console names no channel: the selected radio");
  }

  // A clearance is said a sentence at a time, and the short pieces go with
  // their neighbours rather than being said alone.
  {
    const auto parts = tts::sentences(
        "Delta 1616, cleared to Washington National airport. Climb and "
        "maintain five thousand. Squawk 4521.");
    require(parts.size() == 2, "two breaths, got " + std::to_string(parts.size()));
    require(parts[0] == "Delta 1616, cleared to Washington National airport.",
            "first sentence: " + parts[0]);
    require(parts[1] == "Climb and maintain five thousand. Squawk 4521.",
            "a two-word sentence joins the one before: " + parts[1]);

    const auto roger = tts::sentences("Roger. Climb and maintain five thousand.");
    require(roger.size() == 1, "a short opening joins the next sentence");

    const auto altimeter = tts::sentences("Altimeter 29.92, wind calm.");
    require(altimeter.size() == 1 && altimeter[0] == "Altimeter 29.92, wind calm.",
            "a decimal is not a sentence end");
  }

  // A receiver passes speech and loses rumble and hiss: of three equal
  // tones, the one in the voice band comes out far louder than the others.
  {
    const int rate = 24000;
    auto low = tone(80, rate, rate);
    auto mid = tone(1000, rate, rate);
    auto high = tone(8000, rate, rate);
    tts::Audio mixed{std::vector<std::int16_t>(low.size()), rate};
    for (std::size_t i = 0; i < low.size(); ++i) {
      mixed.pcm[i] = static_cast<std::int16_t>((low[i] + mid[i] + high[i]) / 3);
    }
    tts::radio(mixed);
    require(mixed.pcm.size() == static_cast<std::size_t>(rate), "length kept");
    const double voice = energy(mixed.pcm, 1000, rate);
    require(voice > 10 * energy(mixed.pcm, 80, rate), "rumble is cut");
    require(voice > 10 * energy(mixed.pcm, 8000, rate), "hiss is cut");
    int peak = 0;
    for (auto sample : mixed.pcm) {
      peak = std::max(peak, std::abs(static_cast<int>(sample)));
    }
    require(peak > 20000 && peak < 32767, "speech is brought up and not clipped");
  }

  // Sentences become audio in the station's voice, and the first is ready to
  // play before the transmission is finished being synthesised.
  {
    auto fake = std::make_unique<FakeSynthesizer>();
    auto *synth = fake.get();
    tts::SynthesisQueue queue(std::move(fake), 5);
    auto speech = queue.submit(
        "Delta 1616, cleared to Washington National airport. Climb and "
        "maintain five thousand.",
        3, false);
    require(wait_finished(*speech), "synthesis finishes");
    require(!speech->failed(), "and does not fail");
    require(speech->count() == 2, "one audio per sentence");
    tts::Audio first;
    require(speech->ready(0, first) && first.duration_ms() == 100,
            "the first sentence is a tenth of a second");
    std::lock_guard<std::mutex> lock(synth->mutex);
    require(synth->speakers_used.size() == 2 && synth->speakers_used[0] == 2,
            "voice three is speaker two");
  }

  // A synthesizer that cannot speak fails the transmission, so X-Plane's own
  // voice reads it instead of the pilot hearing nothing.
  {
    tts::SynthesisQueue queue(std::make_unique<FakeSynthesizer>(true), 0);
    auto speech = queue.submit("Delta 1616, roger.", 1, false);
    require(wait_finished(*speech), "a failure still finishes");
    require(speech->failed(), "and says so");
    require(speech->count() == 0, "with nothing to play");
  }

  // No synthesizer at all is a failure, not a hang.
  {
    tts::SynthesisQueue queue(nullptr, 0);
    auto speech = queue.submit("Delta 1616, roger.", 1, false);
    require(wait_finished(*speech) && speech->failed(), "no synthesizer fails");
  }

  std::cout << "tts tests passed\n";
  return 0;
}
