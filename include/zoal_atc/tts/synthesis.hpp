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

  // synthesize says text in the given speaker's voice. An empty Audio is a
  // failure: the reply is then read by X-Plane's own speech instead.
  virtual Audio synthesize(const std::string &text, int speaker) = 0;

  // speakers is how many voices this synthesizer has.
  virtual int speakers() const = 0;

  // warm loads what the first sentence would otherwise wait for.
  virtual void warm() {}
};

// speaker_for maps the console's voice number onto a synthesizer's speakers.
// The console numbers voices from one, stable per station; zero is no voice at
// all, and gets the default.
int speaker_for(std::uint64_t voice, int speakers, int default_speaker);

// sentences splits a transmission where a controller would draw breath, so the
// first sentence can be on the air while the rest is still being synthesised.
// A short trailing fragment is kept with the sentence before it.
std::vector<std::string> sentences(const std::string &text);

// radio shapes speech into what comes out of a VHF receiver: the band a radio
// passes, 300 to 3,000 Hz, and a little compression so the quiet syllables
// are not lost under the engine. Applied in place.
void radio(Audio &audio);

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

  // submit queues text to be said in the console's voice. Urgent speech is
  // synthesised ahead of anything waiting.
  std::shared_ptr<Speech> submit(const std::string &text, std::uint64_t voice,
                                 bool urgent);

  // shared returns sentence audio that outlives the Speech, for a player that
  // must keep a buffer alive until the mixer lets go of it.
  static std::shared_ptr<const Audio> shared(const Speech &speech,
                                             std::size_t i);

private:
  struct Job {
    std::string text;
    int speaker = 0;
    bool urgent = false;
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
