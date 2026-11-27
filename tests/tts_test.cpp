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

  tts::Audio synthesize(const std::string &text, int speaker,
                        const std::string &language) override {
    {
      std::lock_guard<std::mutex> lock(mutex);
      languages.push_back(language);
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
  std::vector<std::string> languages;

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

  // Retuned away, the transmission on the old channel is no longer heard.
  {
    const std::uint64_t ground = 121'900'000, tower = 118'700'000, atis = 135'000'000;
    require(tts::still_receiving(ground, ground, atis), "COM1 still on Ground");
    require(tts::still_receiving(ground, atis, ground), "COM2 still on Ground");
    require(!tts::still_receiving(ground, tower, atis), "both radios retuned away");
    require(!tts::still_receiving(ground, 0, 0), "no radio tuned at all");
    require(tts::still_receiving(0, tower, atis), "an older console names no channel");
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

  // In Spanish and French the spelling alphabet is still ICAO's English words,
  // read as English: espeak's Spanish made "Juliett" "xuljett".
  {
    const std::string taxi =
        "Air Canada cuatro cero nueve seis, ruede a pista uno cinco derecha vía Echo "
        "Yankee, Echo, Hotel tres, Papa, Juliett, mantenga fuera de la pista dos tres.";
    const auto parts = tts::readings(taxi, "es");
    require(parts.size() == 5, "five readings, got " + std::to_string(parts.size()));
    require(!parts[0].english && parts[0].text ==
                "Air Canada cuatro cero nueve seis, ruede a pista uno cinco derecha vía ",
            "the Spanish up to the route: " + parts[0].text);
    require(parts[1].english && parts[1].text == "Echo Yankee, Echo, Hotel",
            "the letters and the commas between them are one English run: " + parts[1].text);
    require(!parts[2].english && parts[2].text == " tres, ",
            "a Spanish digit ends the run: " + parts[2].text);
    require(parts[3].english && parts[3].text == "Papa, Juliett",
            "the next run: " + parts[3].text);
    require(!parts[4].english && parts[4].text == ", mantenga fuera de la pista dos tres.",
            "the rest is Spanish: " + parts[4].text);

    std::string rejoined;
    for (const auto &part : parts) {
      rejoined += part.text;
    }
    require(rejoined == taxi, "nothing is lost between readings");

    const auto french = tts::readings("notifiez que vous avez l'information Bravo.", "fr");
    require(french.size() == 3 && french[1].english && french[1].text == "Bravo",
            "an ATIS letter in French is English");

    const auto english = tts::readings("taxi via Echo Yankee.", "en");
    require(english.size() == 1 && !english[0].english, "an English sentence is one reading");
    require(tts::readings("taxi via Echo.", "").size() == 1, "no language is English");

    const auto word = tts::readings("la papa y el golf.", "es");
    require(word.size() == 1 && !word[0].english,
            "a lower-case word is the language's own, not a letter");
    const auto inside = tts::readings("Deltaplano Lima.", "es");
    require(inside.size() == 3 && !inside[0].english && inside[0].text == "Deltaplano " &&
                inside[1].english && inside[1].text == "Lima",
            "a letter name inside a longer word is not one");
    const auto xray = tts::readings("vía X-ray.", "es");
    require(xray.size() == 3 && xray[1].text == "X-ray", "X-ray keeps its hyphen");
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

  // Another aircraft's transmitter is narrower than the tower's: a pilot's
  // call keeps the middle of the voice and loses more at both edges, so a
  // listener tells a pilot from a controller before the first word ends.
  {
    const int rate = 24000;
    auto edges = [&](const tts::RadioShape &shape) {
      auto low = tone(330, rate, rate);
      auto mid = tone(1000, rate, rate);
      auto high = tone(2900, rate, rate);
      tts::Audio mixed{std::vector<std::int16_t>(low.size()), rate};
      for (std::size_t i = 0; i < low.size(); ++i) {
        mixed.pcm[i] = static_cast<std::int16_t>((low[i] + mid[i] + high[i]) / 3);
      }
      tts::radio(mixed, shape);
      const double voice = energy(mixed.pcm, 1000, rate);
      return (energy(mixed.pcm, 330, rate) + energy(mixed.pcm, 2900, rate)) / voice;
    };
    const double tower = edges(tts::kReceiver);
    const double cockpit = edges(tts::kCockpitTransmitter);
    require(cockpit < tower / 2, "a pilot's call is not narrower than the tower's");
    tts::Audio silence{std::vector<std::int16_t>(100, 0), rate};
    tts::radio(silence, tts::kCockpitTransmitter);
    require(silence.pcm[50] == 0, "silence stays silent");
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

  // A reply in French is read as French, sentence by sentence, and one the
  // console named no language for is English.
  {
    auto fake = std::make_unique<FakeSynthesizer>();
    auto *synth = fake.get();
    tts::SynthesisQueue queue(std::move(fake), 5);
    auto french = queue.submit("Delta un, reçu. Restez à l'écoute.", 3, false, "fr");
    require(wait_finished(*french), "French synthesis finishes");
    auto english = queue.submit("Delta 1, roger.", 3, false);
    require(wait_finished(*english), "English synthesis finishes");
    std::lock_guard<std::mutex> lock(synth->mutex);
    require(synth->languages.size() == 3 && synth->languages[0] == "fr" &&
                synth->languages[1] == "fr" && synth->languages[2].empty(),
            "every French sentence is read as French, and English as English");
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
