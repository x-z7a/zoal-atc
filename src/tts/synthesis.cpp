#include "zoal_atc/tts/synthesis.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace zoal_atc::tts {

namespace {

// kMaxWaiting bounds the transmissions waiting for a voice. The reply queue
// in front of it holds sixteen; this matches it, and the oldest routine one is
// the one given up on, to be read by X-Plane's own speech instead.
constexpr std::size_t kMaxWaiting = 16;

// kMinSentenceWords is the shortest piece said on its own. "Roger." alone
// sounds like a different transmission.
constexpr std::size_t kMinSentenceWords = 3;

std::size_t word_count(const std::string &text) {
  std::size_t words = 0;
  bool in_word = false;
  for (const char c : text) {
    const bool space = c == ' ' || c == '\t' || c == '\n' || c == '\r';
    if (!space && !in_word) {
      ++words;
    }
    in_word = !space;
  }
  return words;
}

std::string trim(const std::string &text) {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return "";
  }
  const auto last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

// Biquad is one second-order section from the RBJ audio EQ cookbook.
struct Biquad {
  double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
  double x1 = 0, x2 = 0, y1 = 0, y2 = 0;

  static Biquad pass(bool high, double cutoff_hz, double rate_hz) {
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kQ = 0.7071067811865476;
    const double w0 = 2.0 * kPi * cutoff_hz / rate_hz;
    const double alpha = std::sin(w0) / (2.0 * kQ);
    const double cosw = std::cos(w0);
    const double a0 = 1.0 + alpha;
    Biquad f;
    if (high) {
      f.b0 = (1.0 + cosw) / 2.0 / a0;
      f.b1 = -(1.0 + cosw) / a0;
    } else {
      f.b0 = (1.0 - cosw) / 2.0 / a0;
      f.b1 = (1.0 - cosw) / a0;
    }
    f.b2 = f.b0;
    f.a1 = -2.0 * cosw / a0;
    f.a2 = (1.0 - alpha) / a0;
    return f;
  }

  double operator()(double x) {
    const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
    return y;
  }
};

}  // namespace

int speaker_for(std::uint64_t voice, int speakers, int default_speaker) {
  if (speakers <= 0) {
    return 0;
  }
  if (voice == 0) {
    return std::clamp(default_speaker, 0, speakers - 1);
  }
  return static_cast<int>((voice - 1) % static_cast<std::uint64_t>(speakers));
}

std::vector<std::string> sentences(const std::string &text) {
  std::vector<std::string> out;
  std::string current;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    current.push_back(c);
    const bool end = c == '.' || c == '?' || c == '!';
    const bool boundary = i + 1 == text.size() || text[i + 1] == ' ';
    if (end && boundary) {
      auto sentence = trim(current);
      current.clear();
      if (sentence.empty()) {
        continue;
      }
      if (!out.empty() && word_count(sentence) < kMinSentenceWords) {
        out.back() += " " + sentence;
      } else {
        out.push_back(std::move(sentence));
      }
    }
  }
  auto rest = trim(current);
  if (!rest.empty()) {
    if (!out.empty() && word_count(rest) < kMinSentenceWords) {
      out.back() += " " + rest;
    } else {
      out.push_back(std::move(rest));
    }
  }
  // A transmission that opens with a short sentence keeps it with the next:
  // "Roger. Climb and maintain five thousand." is one breath.
  if (out.size() > 1 && word_count(out.front()) < kMinSentenceWords) {
    out[1] = out.front() + " " + out[1];
    out.erase(out.begin());
  }
  return out;
}

void radio(Audio &audio) {
  if (audio.pcm.empty() || audio.sample_rate_hz <= 0) {
    return;
  }
  const double rate = static_cast<double>(audio.sample_rate_hz);
  // Two sections each side: a 24 dB/octave skirt, which is what makes it sound
  // like a receiver rather than a telephone.
  Biquad high1 = Biquad::pass(true, 300.0, rate);
  Biquad high2 = Biquad::pass(true, 300.0, rate);
  Biquad low1 = Biquad::pass(false, 3000.0, rate);
  Biquad low2 = Biquad::pass(false, 3000.0, rate);

  std::vector<double> shaped(audio.pcm.size());
  double peak = 0;
  for (std::size_t i = 0; i < audio.pcm.size(); ++i) {
    double x = static_cast<double>(audio.pcm[i]) / 32768.0;
    x = low2(low1(high2(high1(x))));
    // Soft compression: quiet syllables come up, peaks are rounded off.
    x = std::tanh(2.5 * x);
    shaped[i] = x;
    peak = std::max(peak, std::fabs(x));
  }
  const double gain = peak > 0 ? 0.85 / peak : 0;
  for (std::size_t i = 0; i < shaped.size(); ++i) {
    const double y = std::clamp(shaped[i] * gain, -1.0, 1.0);
    audio.pcm[i] = static_cast<std::int16_t>(std::lround(y * 32767.0));
  }
}

bool Speech::ready(std::size_t i, Audio &out) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (i >= audio_.size()) {
    return false;
  }
  out = *audio_[i];
  return true;
}

bool Speech::finished() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return finished_;
}

bool Speech::failed() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return failed_;
}

std::size_t Speech::count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return audio_.size();
}

void Speech::add(Audio audio) {
  std::lock_guard<std::mutex> lock(mutex_);
  audio_.push_back(std::make_shared<const Audio>(std::move(audio)));
}

void Speech::finish(bool failed) {
  std::lock_guard<std::mutex> lock(mutex_);
  finished_ = true;
  failed_ = failed_ || failed;
}

std::shared_ptr<const Audio> SynthesisQueue::shared(const Speech &speech,
                                                    std::size_t i) {
  std::lock_guard<std::mutex> lock(speech.mutex_);
  if (i >= speech.audio_.size()) {
    return nullptr;
  }
  return speech.audio_[i];
}

SynthesisQueue::SynthesisQueue(std::unique_ptr<Synthesizer> synthesizer,
                               int default_speaker)
    : synthesizer_(std::move(synthesizer)), default_speaker_(default_speaker) {
  thread_ = std::thread([this] { run(); });
}

SynthesisQueue::~SynthesisQueue() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  wake_.notify_all();
  if (thread_.joinable()) {
    thread_.join();
  }
  for (auto &job : jobs_) {
    job.speech->finish(true);
  }
}

std::shared_ptr<Speech> SynthesisQueue::submit(const std::string &text,
                                               std::uint64_t voice,
                                               bool urgent) {
  auto speech = std::make_shared<Speech>();
  Job job{text,
          speaker_for(voice, synthesizer_ ? synthesizer_->speakers() : 0,
                      default_speaker_),
          urgent, speech};
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (jobs_.size() >= kMaxWaiting) {
      auto oldest = std::find_if(jobs_.begin(), jobs_.end(),
                                 [](const Job &waiting) { return !waiting.urgent; });
      if (oldest == jobs_.end()) {
        oldest = jobs_.begin();
      }
      oldest->speech->finish(true);
      jobs_.erase(oldest);
    }
    if (urgent) {
      jobs_.push_front(std::move(job));
    } else {
      jobs_.push_back(std::move(job));
    }
  }
  wake_.notify_one();
  return speech;
}

void SynthesisQueue::run() {
  if (synthesizer_) {
    synthesizer_->warm();
  }
  for (;;) {
    Job job;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      wake_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
      if (stopping_) {
        return;
      }
      job = std::move(jobs_.front());
      jobs_.pop_front();
    }
    bool failed = !synthesizer_;
    for (const auto &sentence : sentences(job.text)) {
      if (failed) {
        break;
      }
      Audio audio = synthesizer_->synthesize(sentence, job.speaker);
      if (audio.pcm.empty()) {
        failed = true;
        break;
      }
      radio(audio);
      job.speech->add(std::move(audio));
    }
    job.speech->finish(failed);
  }
}

}  // namespace zoal_atc::tts
