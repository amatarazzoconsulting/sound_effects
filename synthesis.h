// Layer parameters use Hz, milliseconds, linear gain, and semitones.
// Included after Oscillator; each layer keeps independent phase, filter and PRNG state.
struct SynthLayer {
    Waveform wave = Waveform::SINE;
    double gain = 1, ratio = 1;
    double pitch_start = 0, pitch_end = 0, sweep_ms = 200;
    double delay_ms = 0, attack_ms = 2, decay_ms = 200;
    double cutoff_start = 6000, cutoff_end = 6000, filter_ms = 200;
    double highpass_hz = 20, duty = .5;
    double fm_ratio = 2, fm_depth = 0, fm_decay_ms = 100;
    std::array<int, 8> notes{};
    int note_count = 0;
    double step_ms = 80;

    void validate() const {
        auto check = [](double v, double lo, double hi) {
            if (!std::isfinite(v) || v < lo || v > hi) throw std::invalid_argument("Invalid synthesis layer");
        };
        check(gain, 0, 1); check(ratio, .01, 32);
        check(pitch_start, -96, 96); check(pitch_end, -96, 96);
        check(sweep_ms, .01, 600000); check(delay_ms, 0, 600000);
        check(attack_ms, 0, 600000); check(decay_ms, 0, 600000);
        check(cutoff_start, 20, 20000); check(cutoff_end, 20, 20000); check(filter_ms, .01, 600000);
        check(highpass_hz, 0, 10000); check(duty, .05, .95);
        check(fm_ratio, .01, 32); check(fm_depth, 0, 20); check(fm_decay_ms, .01, 600000);
        check(step_ms, 1, 600000);
        if (note_count < 0 || note_count > 8) throw std::invalid_argument("Invalid arpeggio size");
        for (int note : notes) check(note, -96, 96);
    }
};

class LayerVoice {
public:
    explicit LayerVoice(const SynthLayer& layer, std::uint32_t seed)
        : config(layer), oscillator(layer.wave), noise(seed * 747796405u + 1u) {
        pitch = std::exp2(config.pitch_start / 12.0);
        endPitch = std::exp2(config.pitch_end / 12.0);
        pitchStep = std::exp2((config.pitch_end - config.pitch_start) / (12 * config.sweep_ms * 48));
        cutoff = config.cutoff_start;
        cutoffStep = std::pow(config.cutoff_end / config.cutoff_start, 1 / (config.filter_ms * 48));
        coefficient = 1 - std::exp(-2 * PI * cutoff / SAMPLE_RATE);
        endCoefficient = 1 - std::exp(-2 * PI * config.cutoff_end / SAMPLE_RATE);
        dcCoefficient = 1 - std::exp(-2 * PI * config.highpass_hz / SAMPLE_RATE);
        decayStep = config.decay_ms > 0 ? std::exp(-1 / (config.decay_ms * 48)) : 1;
        fmLevel = config.fm_depth;
        fmStep = std::exp(-1 / (config.fm_decay_ms * 48));
        for (size_t i = 0; i < noteRatios.size(); ++i) noteRatios[i] = std::exp2(config.notes[i] / 12.0);
    }

    double render(double elapsed_ms, double baseFrequency) {
        const double t = elapsed_ms - config.delay_ms;
        if (t < 0) return 0;
        double frequency = baseFrequency * config.ratio * (t >= config.sweep_ms ? endPitch : pitch);
        if (config.note_count) {
            int step = static_cast<int>(t / config.step_ms) % config.note_count;
            frequency *= noteRatios[step];
        }
        frequency = clamp(frequency, 0, 21600);
        if (t < config.sweep_ms) pitch *= pitchStep;
        double sample;
        if (config.wave == Waveform::NOISE || config.wave == Waveform::SINE_NOISE) {
            noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5;
            sample = static_cast<double>(noise) / std::numeric_limits<std::uint32_t>::max() * 2 - 1;
            if (config.wave == Waveform::SINE_NOISE) sample = .25 * sample + .75 * oscillator.render(frequency);
        } else {
            double fm = 0;
            if (config.fm_depth > 0) {
                fmPhase += std::min(frequency * config.fm_ratio, 21600.0) / SAMPLE_RATE;
                fmPhase -= std::floor(fmPhase);
                fm = std::sin(2 * PI * fmPhase) * fmLevel;
                fmLevel *= fmStep;
            }
            sample = oscillator.render(frequency, config.duty, fm);
        }
        if (config.cutoff_start != config.cutoff_end) {
            if (t >= config.filter_ms) coefficient = endCoefficient;
            else {
                coefficient = 1 - std::exp(-2 * PI * cutoff / SAMPLE_RATE);
                cutoff *= cutoffStep;
            }
        }
        low1 += coefficient * (sample - low1);
        low2 += coefficient * (low1 - low2);
        dc += dcCoefficient * (low2 - dc);
        double envelope = config.attack_ms > 0 ? std::min(1.0, t / config.attack_ms) : 1;
        envelope *= decayLevel;
        decayLevel *= decayStep;
        return (low2 - dc) * config.gain * envelope;
    }
private:
    SynthLayer config;
    Oscillator oscillator;
    std::uint32_t noise;
    double fmPhase = 0, low1 = 0, low2 = 0, dc = 0;
    double pitch = 1, endPitch = 1, pitchStep = 1, cutoff = 6000, cutoffStep = 1;
    double coefficient = 0, endCoefficient = 0, dcCoefficient = 0;
    double decayLevel = 1, decayStep = 1, fmLevel = 0, fmStep = 1;
    std::array<double, 8> noteRatios{};
};
