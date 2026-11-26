#ifndef ZOAL_ATC_TTS_SYNTHESIS_HPP
#define ZOAL_ATC_TTS_SYNTHESIS_HPP

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace zoal_atc::tts {

// Audio is one stretch of mono 16-bit speech.
struct Audio {
  std::vector<std::int16_t> pcm;
  int sample_rate_hz = 0;

  std::uint64_t duration_ms() const {
    if (sample_rate_hz <= 0) {
      return 0;
    }
    return static_cast<std::uint64_t>(pcm.size()) * 1000u /
           static_cast<std::uint64_t>(sample_rate_hz);
  }
};

// Synthesizer turns words into a voice. It is called off the X-Plane main
// thread, one sentence at a time.
class Synthesizer {
public:
  virtual ~Synthesizer() = default;

  // synthesize says text in the given speaker's voice, read as language (an
  // ISO 639-1 code; empty is English). An empty Audio is a failure: the reply
  // is then read by X-Plane's own speech instead.
  virtual Audio synthesize(const std::string &text, int speaker,
                           const std::string &language) = 0;

  // speakers is how many voices this synthesizer has.
  virtual int speakers() const = 0;

  // warm loads what the first sentence would otherwise wait for.
  virtual void warm() {}
};

// speaker_for maps the console's voice number onto a synthesizer's speakers.
// The console numbers voices from one, stable per station; zero is no voice at
// all, and gets the default.
int speaker_for(std::uint64_t voice, int speakers, int default_speaker);

// receiving_com is the radio a transmission on channel_hz comes out of: the
// COM tuned to it, as a real receiver would. With both tuned there it is the
// one the pilot has selected; with neither — a reply from a console that does
// not name its channel, or a pilot who retuned in the second it took to
// arrive — it is the selected radio, because that is the one being listened
// to. Returns 1 or 2. selected_com is 1, 2, or 0 when unknown.
int receiving_com(std::uint64_t channel_hz, std::uint64_t com1_hz,
                  std::uint64_t com2_hz, int selected_com);

// sentences splits a transmission where a controller would draw breath, so the
// first sentence can be on the air while the rest is still being synthesised.
// A short trailing fragment is kept with the sentence before it.
std::vector<std::string> sentences(const std::string &text);

// RadioShape is the band a radio passes and how hard it drives the speech
// through its compression.
struct RadioShape {
  double low_hz;
  double high_hz;
  double drive;
};

// kReceiver is a controller heard on a VHF receiver: 300 to 3,000 Hz.
inline constexpr RadioShape kReceiver{300.0, 3000.0, 2.5};
// kCockpitTransmitter is another aircraft heard on it: a boom microphone in a
// loud cockpit, keyed through the aircraft's own transmitter, which is
// narrower and driven harder than a ground station's.
inline constexpr RadioShape kCockpitTransmitter{450.0, 2400.0, 4.0};

// radio shapes speech into what comes out of a VHF receiver: the band a radio
// passes, and a little compression so the quiet syllables are not lost under
// the engine. Applied in place.
void radio(Audio &audio, const RadioShape &shape = kReceiver);

// Reading is one stretch of a sentence and the language it is read in.
struct Reading {
  std::string text;
  // english is a run of ICAO spelling-alphabet words in a sentence in another
  // language, read as English reads them.
  bool english = false;
};

// readings splits a sentence in language (ISO 639-1, empty is English) into
// the stretches read in that language and the runs of spelling-alphabet words
// read as English.
//
// The alphabet is ICAO's words in every language a controller here speaks
// (Annex 10 Vol II 5.2.1.3): a Spanish or French controller says "Echo
// Yankee", not "Eco Yanqui", and says it the way ICAO spells it out, which is
// English's. Read with espeak's Spanish, "Juliett" was "xuljˈett" and
// "Uniform" "unifˈoɾm", stressed on the last syllable; with its English they
// are "dʒˌuːlɪˈɛt" and "jˈuːnɪfˌɔːɹm". A word is one of the alphabet only as
// the console writes it, capitalised and on its own, so a lower-case "papa"
// stays the language's own word. The punctuation between two alphabet words
// goes with the run; the punctuation around it stays with its neighbours. An
// English sentence is one reading.
//
// A language whose controllers say a letter their own way keeps that letter
// in the language (native_letter in synthesis.cpp). Spanish and French have
// none.
std::vector<Reading> readings(const std::string &sentence, const std::string &language);

// Speech is one transmission being synthesised, sentence by sentence. The
// worker appends to it; the flight loop plays what is ready.
class Speech {
public:
  // ready returns sentence i once it has been synthesised.
  bool ready(std::size_t i, Audio &out) const;
  // finished is true once every sentence has been synthesised or one failed.
  bool finished() const;
  // failed is true when a sentence could not be synthesised.
  bool failed() const;
  std::size_t count() const;

  void add(Audio audio);
  void finish(bool failed);

private:
  mutable std::mutex mutex_;
  std::vector<std::shared_ptr<const Audio>> audio_;
  bool finished_ = false;
  bool failed_ = false;
  friend class SynthesisQueue;
};

// SynthesisQueue synthesises transmissions on a thread of its own, urgent ones
// first, so the flight loop never waits for a voice.
class SynthesisQueue {
public:
  explicit SynthesisQueue(std::unique_ptr<Synthesizer> synthesizer,
                          int default_speaker = 0);
  ~SynthesisQueue();

  SynthesisQueue(const SynthesisQueue &) = delete;
  SynthesisQueue &operator=(const SynthesisQueue &) = delete;

  // submit queues text to be said in the console's voice, read as language
  // (empty is English), through a radio of that shape. Urgent speech is
  // synthesised ahead of anything waiting.
  std::shared_ptr<Speech> submit(const std::string &text, std::uint64_t voice,
                                 bool urgent, const std::string &language = "",
                                 const RadioShape &shape = kReceiver);

  // shared returns sentence audio that outlives the Speech, for a player that
  // must keep a buffer alive until the mixer lets go of it.
  static std::shared_ptr<const Audio> shared(const Speech &speech,
                                             std::size_t i);

private:
  struct Job {
    std::string text;
    int speaker = 0;
    bool urgent = false;
    std::string language;
    RadioShape shape = kReceiver;
    std::shared_ptr<Speech> speech;
  };

  void run();

  std::unique_ptr<Synthesizer> synthesizer_;
  int default_speaker_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<Job> jobs_;
  bool stopping_ = false;
  std::thread thread_;
};

}  // namespace zoal_atc::tts

#endif  // ZOAL_ATC_TTS_SYNTHESIS_HPP
