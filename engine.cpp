/*
    ============================================================================
    PROFESSIONAL REAL-TIME SOUND PRODUCTION ENGINE
    ============================================================================
    Features:
    - 128 simultaneous sound voices
    - Layered synthesis and categorized sound library
    - Real-time parameter modulation
    - Stereo panning and distance attenuation
    - Built-in effects: Echo, Reverb, Chorus, Flanger, Stereo Pong
    - Priority-based voice stealing
    - Natural language descriptor parser
    ============================================================================
*/

#include <array>
#include <fstream>
#include <filesystem>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <sstream>
#include <cctype>
#include <iostream>
#include <vector>
#include <cmath>
#include <random>
#include <unordered_map>
#include <algorithm>
#include <memory>
#include <queue>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <functional>

// ============================================================================
// Configuration
// ============================================================================
constexpr double PI = 3.14159265358979323846;
constexpr int SAMPLE_RATE = 48000;
constexpr int BUFFER_SIZE = 256;          // Small buffer for low latency
constexpr int MAX_VOICES = 128;            // Maximum simultaneous sounds
constexpr int MAX_EFFECT_BUFFER = 48000;   // 1 second buffer for effects
constexpr double MASTER_VOLUME = 0.8;

// ============================================================================
// Utilities
// ============================================================================
double clamp(double val, double min, double max) {
    if (!std::isfinite(val)) throw std::invalid_argument("Non-finite parameter");
    return std::max(min, std::min(val, max));
}

double lerp(double a, double b, double t) {
    return a + t * (b - a);
}

// Fast sine approximation for LFOs
float fastSin(float x) {
    const float B = 4.0f / PI;
    const float C = -4.0f / (PI * PI);
    float y = B * x + C * x * fabs(x);
    return y;
}

// ============================================================================
// Random Number Generator
// ============================================================================
class Random {
public:
    static Random& get() {
        static thread_local Random instance;
        return instance;
    }
    
    double uniform(double min = 0.0, double max = 1.0) {
        std::uniform_real_distribution<double> dist(min, max);
        return dist(m_gen);
    }
    
    int range(int min, int max) {
        std::uniform_int_distribution<int> dist(min, max);
        return dist(m_gen);
    }
    
private:
    Random() : m_gen(std::random_device{}()) {}
    std::mt19937 m_gen;
};

// ============================================================================
// Waveform Generator
// ============================================================================
enum class Waveform { SINE, SQUARE, TRIANGLE, SAWTOOTH, NOISE, SINE_NOISE, METALLIC };

class Oscillator {
public:
    Oscillator(Waveform type = Waveform::SINE) : m_type(type), m_phase(0.0) {}
    
    void setType(Waveform type) { m_type = type; }
    
    double render(double freq_hz, double duty = 0.5, double phaseMod = 0.0) {
        double increment = clamp(freq_hz, 0.0, SAMPLE_RATE * 0.45) / SAMPLE_RATE;
        m_phase += increment;
        m_phase -= std::floor(m_phase);
        
        double sample = 0.0;
        switch (m_type) {
            case Waveform::SINE:
                sample = sin(2.0 * PI * m_phase + phaseMod);
                break;
            case Waveform::SQUARE:
                duty = clamp(duty, 0.05, 0.95);
                sample = (m_phase < duty ? 1.0 : -1.0) + polyBlep(m_phase, increment)
                       - polyBlep(std::fmod(m_phase - duty + 1.0, 1.0), increment);
                break;
            case Waveform::TRIANGLE:
                sample = 2.0 * fabs(2.0 * m_phase - 1.0) - 1.0;
                break;
            case Waveform::SAWTOOTH:
                sample = 2.0 * m_phase - 1.0 - polyBlep(m_phase, increment);
                break;
            case Waveform::NOISE:
                sample = Random::get().uniform(-1.0, 1.0);
                break;
            case Waveform::SINE_NOISE:
                sample = sin(2.0 * PI * m_phase) * 0.7 + Random::get().uniform(-0.3, 0.3);
                break;
            case Waveform::METALLIC:
                m_partialPhase += increment * 2.17;
                m_partialPhase -= std::floor(m_partialPhase);
                sample = sin(2.0 * PI * m_phase) + 0.5 * sin(2.0 * PI * m_partialPhase);
                break;
        }
        return sample;
    }
    
private:
    Waveform m_type;
    double m_phase;
    double m_partialPhase = 0.0;
    static double polyBlep(double phase, double increment) {
        if (increment <= 0) return 0;
        if (phase < increment) { double t = phase / increment; return 2*t - t*t - 1; }
        if (phase > 1 - increment) { double t = (phase - 1) / increment; return t*t + 2*t + 1; }
        return 0;
    }
};

// ============================================================================
// ADSR Envelope
// ============================================================================
struct Envelope {
    double attack_ms = 5.0;
    double decay_ms = 50.0;
    double sustain = 0.7;
    double release_ms = 100.0;
    
    double compute(double time_ms, double duration_ms, bool releasing = false) {
        if (releasing) {
            double release_start = duration_ms;
            if (release_ms <= 0) return 0.0;
            if (time_ms < release_start) return sustain;
            double t = clamp((time_ms - release_start) / release_ms, 0.0, 1.0);
            return compute(duration_ms, duration_ms, false) * (1.0 - t);
        } else {
            if (time_ms < attack_ms) return clamp(time_ms / attack_ms, 0.0, 1.0);
            if (time_ms < attack_ms + decay_ms) {
                double t = clamp((time_ms - attack_ms) / decay_ms, 0.0, 1.0);
                return lerp(1.0, sustain, t);
            }
            return sustain;
        }
    }
};

// ============================================================================
// Audio Effects Processor
// ============================================================================
class DelayLine {
public:
    DelayLine(int max_samples) : m_buffer(std::max(1, max_samples), 0.0), m_pos(0) {}
    
    int maxSize() const { return static_cast<int>(m_buffer.size()); }

    void write(double sample) {
        m_buffer[m_pos] = sample;
        m_pos = (m_pos + 1) % m_buffer.size();
    }
    
    double read(int delay_samples) {
        delay_samples = std::max(1, std::min(delay_samples, maxSize()));
        int pos = (m_pos - delay_samples + maxSize()) % maxSize();
        return m_buffer[pos];
    }
    
private:
    std::vector<double> m_buffer;
    int m_pos;
};

class Echo {
public:
    Echo(double delay_sec = 0.3, double decay = 0.5) 
        : m_delayLine(SAMPLE_RATE * 2), m_delaySamples(static_cast<int>(SAMPLE_RATE * clamp(delay_sec, 1.0 / SAMPLE_RATE, 2.0))), m_decay(clamp(decay, 0.0, 0.99)) {}
    
    double process(double sample) {
        double delayed = m_delayLine.read(m_delaySamples);
        double output = sample + delayed * m_decay;
        m_delayLine.write(sample + delayed * m_decay);
        return output;
    }
    
private:
    DelayLine m_delayLine;
    int m_delaySamples;
    double m_decay;
};

class Reverb {
public:
    Reverb(double room_size = 0.5, double damping = 0.5) 
        : m_delayLines{DelayLine(1426), DelayLine(1781), DelayLine(1973), DelayLine(2098)},
          m_feedback(clamp(room_size, 0.0, 1.0) * 0.8), m_damping(clamp(damping, 0.0, 1.0)) {}
    
    double process(double sample) {
        double wet = 0.0;
        for (auto& delay : m_delayLines) {
            double delayed = delay.read(delay.maxSize());
            delay.write(sample + delayed * m_feedback);
            wet += delayed * 0.25;
        }
        return sample * (1.0 - m_damping) + wet * m_damping;
    }
    
private:
    std::vector<DelayLine> m_delayLines;
    double m_feedback;
    double m_damping;
};

class Chorus {
public:
    Chorus(double rate = 0.5, double depth = 0.003, double mix = 0.5)
        : m_rate(clamp(rate, 0.0, 100.0)), m_depth(clamp(depth, 0.0, 0.1) * SAMPLE_RATE), m_mix(clamp(mix, 0.0, 1.0)), m_phase(0.0) {}
    
    double process(double sample) {
        m_phase += m_rate / SAMPLE_RATE;
        m_phase -= std::floor(m_phase);
        
        double lfo = sin(2.0 * PI * m_phase);
        int delay_samples = (int)(m_depth * (0.5 + lfo * 0.5));
        
        double delayed = m_delayLine.read(delay_samples);
        m_delayLine.write(sample);
        
        return sample * (1.0 - m_mix) + delayed * m_mix;
    }
    
private:
    DelayLine m_delayLine{SAMPLE_RATE / 10};
    double m_rate, m_depth, m_mix, m_phase;
};

class Flanger {
public:
    Flanger(double rate = 0.25, double depth = 0.005, double feedback = 0.7, double mix = 0.5)
        : m_rate(clamp(rate, 0.0, 100.0)), m_depth(clamp(depth, 0.0, 0.05) * SAMPLE_RATE), m_feedback(clamp(feedback, -0.99, 0.99)), m_mix(clamp(mix, 0.0, 1.0)), m_phase(0.0) {}
    
    double process(double sample) {
        m_phase += m_rate / SAMPLE_RATE;
        m_phase -= std::floor(m_phase);
        
        double lfo = sin(2.0 * PI * m_phase);
        int delay_samples = (int)(m_depth * (0.5 + lfo * 0.5));
        
        double delayed = m_delayLine.read(delay_samples);
        m_delayLine.write(sample + delayed * m_feedback);
        
        return sample * (1.0 - m_mix) + delayed * m_mix;
    }
    
private:
    DelayLine m_delayLine{SAMPLE_RATE / 20};
    double m_rate, m_depth, m_feedback, m_mix, m_phase;
};

class StereoPong {
public:
    StereoPong(double delay_sec = 0.25, double decay = 0.6) 
        : m_delaySamples(static_cast<int>(SAMPLE_RATE * clamp(delay_sec, 1.0 / SAMPLE_RATE, 2.0))), m_decay(clamp(decay, 0.0, 0.99)) {}
    
    void process(double& left, double& right) {
        double left_delayed = m_leftDelay.read(m_delaySamples);
        double right_delayed = m_rightDelay.read(m_delaySamples);
        
        double output_left = left + right_delayed * m_decay;
        double output_right = right + left_delayed * m_decay;
        m_leftDelay.write(output_left);
        m_rightDelay.write(output_right);

        left = output_left;
        right = output_right;
    }
    
private:
    DelayLine m_leftDelay{SAMPLE_RATE * 2};
    DelayLine m_rightDelay{SAMPLE_RATE * 2};
    int m_delaySamples;
    double m_decay;
};

#include "synthesis.h"

// ============================================================================
// Sound Voice (Active Sound Instance)
// ============================================================================
struct SoundVoice {
    // Core
    Oscillator oscillator;
    Oscillator metal{Waveform::METALLIC};
    std::vector<LayerVoice> layers;
    Envelope envelope;
    double frequency = 440.0;
    double amplitude = 0.5;
    double duration_ms = 1000.0;
    
    // Modulation
    double pitch_lfo_rate = 0.0;
    double pitch_lfo_depth = 0.0;
    double amplitude_lfo_rate = 0.0;
    double amplitude_lfo_depth = 0.0;
    
    // Spatial
    double pan = 0.0;
    double distance = 1.0;
    double doppler_shift = 1.0;
    
    // Texture
    double noise_amount = 0.0;
    double metallic_amount = 0.0;
    
    // State
    double elapsed_ms = 0.0;
    bool active = false;
    bool releasing = false;
    int priority = 5;
    
    void update(double delta_ms) {
        if (!active) return;
        
        elapsed_ms += delta_ms;
        if (elapsed_ms >= duration_ms && !releasing) {
            releasing = true;
        }
        if (releasing && elapsed_ms >= duration_ms + envelope.release_ms) {
            active = false;
        }
    }
    
    double render(double& left_out, double& right_out) {
        if (!active) return 0.0;
        
        // Pitch modulation
        double pitch_mod = 0.0;
        if (pitch_lfo_rate > 0.0) {
            double lfo = sin(2.0 * PI * pitch_lfo_rate * (elapsed_ms / 1000.0));
            pitch_mod = lfo * pitch_lfo_depth;
        }
        double final_freq = frequency * pow(2.0, pitch_mod / 12.0) * doppler_shift;
        
        // Independent layers share only the outer voice envelope and spatial controls.
        double sample = 0.0;
        if (layers.empty()) sample = oscillator.render(final_freq);
        else for (auto& layer : layers) sample += layer.render(elapsed_ms, final_freq);
        
        // Add noise texture
        if (layers.empty() && noise_amount > 0.0) {
            sample = sample * (1.0 - noise_amount) + Random::get().uniform(-1.0, 1.0) * noise_amount;
        }
        
        // Add metallic ringing
        if (layers.empty() && metallic_amount > 0.0) {
            sample += metal.render(final_freq * 2.17) * metallic_amount;
        }
        
        // Amplitude modulation
        double amp_mod = 1.0;
        if (amplitude_lfo_rate > 0.0) {
            double lfo = sin(2.0 * PI * amplitude_lfo_rate * (elapsed_ms / 1000.0));
            amp_mod = 0.7 + lfo * amplitude_lfo_depth;
        }
        
        // Envelope
        double env = envelope.compute(elapsed_ms, duration_ms, releasing);
        double final_amp = amplitude * env * amp_mod;
        
        // Distance attenuation
        float distance_attenuation = 1.0f / (1.0f + distance * 0.1f);
        final_amp *= distance_attenuation;
        
        sample *= final_amp;
        
        // Stereo panning
        double left_gain = cos((pan + 1.0) * PI / 4.0);
        double right_gain = sin((pan + 1.0) * PI / 4.0);
        
        left_out += sample * left_gain;
        right_out += sample * right_gain;
        
        return sample;
    }
};

// ============================================================================
// Sound Definition (Preset)
// ============================================================================
struct SoundDefinition {
    std::string name;
    std::vector<std::string> keywords;
    Waveform waveform = Waveform::SINE;
    double base_freq = 440.0;
    double amplitude = 0.5;
    double duration_ms = 500.0;
    Envelope envelope;
    double pitch_lfo_rate = 0.0;
    double pitch_lfo_depth = 0.0;
    double amplitude_lfo_rate = 0.0;
    double amplitude_lfo_depth = 0.0;
    double noise_amount = 0.0;
    double metallic_amount = 0.0;
    int priority = 5;
    std::vector<SynthLayer> layers{};
    std::string category = "classic";
};

// ============================================================================
// Sound Library
// ============================================================================
class SoundLibrary {
public:
    SoundLibrary() {
        initializeSounds();
        designPresets();
    }
    
    const SoundDefinition* findSound(const std::string& name) const {
        auto it = m_soundsByName.find(name);
        if (it != m_soundsByName.end()) return &it->second;
        
        // Whole-word matching in declaration order keeps aliases deterministic.
        const SoundDefinition* best = nullptr;
        size_t bestScore = 0;
        for (const auto& sound : m_soundList) {
            size_t score = 0;
            for (const auto& keyword : sound.keywords) {
                if ((" " + name + " ").find(" " + keyword + " ") != std::string::npos)
                    score += keyword.size();
            }
            if (score > bestScore) { bestScore = score; best = &sound; }
        }
        return best;
    }
    
    const std::vector<SoundDefinition>& getAllSounds() const { return m_soundList; }
    
private:
    void addSound(const SoundDefinition& sound) {
        m_soundList.push_back(sound);
        m_soundsByName[sound.name] = sound;
    }
    
    void initializeSounds() {
        // ========== CARTOON / ENTERTAINMENT ==========
        addSound({"boing", {"boing", "spring", "bounce"}, Waveform::TRIANGLE, 440.0, 0.7, 400.0, 
                  {5, 50, 0.6, 80}, 2.0, 1.0});
        
        addSound({"slide_whistle", {"slide", "whistle", "swoop"}, Waveform::SAWTOOTH, 880.0, 0.6, 600.0,
                  {10, 100, 0.5, 100}, 0.0, 0.0, 3.0, 0.3});
        
        addSound({"cartoon_crash", {"crash", "bang", "smash"}, Waveform::NOISE, 200.0, 0.9, 800.0,
                  {1, 200, 0.4, 300}, 0.0, 0.0, 0.0, 0.0, 0.5, 0.3, 3});
        
        addSound({"pop", {"pop", "burst", "balloon"}, Waveform::SQUARE, 600.0, 0.8, 150.0,
                  {1, 30, 0.5, 40}, 0.0, 0.0});
        
        addSound({"giggle", {"giggle", "laugh"}, Waveform::SINE_NOISE, 660.0, 0.5, 800.0,
                  {20, 100, 0.7, 150}, 1.5, 0.5});
        
        addSound({"sad_trombone", {"sad", "trombone", "wah"}, Waveform::SAWTOOTH, 220.0, 0.6, 1200.0,
                  {50, 100, 0.5, 200}, 0.5, 2.0});
        
        addSound({"magic_sparkle", {"sparkle", "twinkle", "magic"}, Waveform::SINE, 1200.0, 0.5, 300.0,
                  {1, 20, 0.8, 50}, 4.0, 1.0, 2.0, 0.2});
        
        // ========== IMPACTS ==========
        addSound({"explosion", {"explosion", "blast", "boom"}, Waveform::NOISE, 80.0, 1.0, 1500.0,
                  {1, 400, 0.3, 500}, 0.0, 0.0, 0.0, 0.0, 0.3, 0.4, 2});
        
        addSound({"metallic_hit", {"metal", "clang", "clank"}, Waveform::METALLIC, 300.0, 0.7, 400.0,
                  {1, 100, 0.5, 150}, 0.0, 0.0, 0.0, 0.0, 0.2, 0.6});
        
        addSound({"wood_hit", {"wood", "thud", "bonk"}, Waveform::TRIANGLE, 150.0, 0.6, 300.0,
                  {2, 80, 0.4, 100}, 0.0, 0.0});
        
        addSound({"glass_break", {"glass", "break", "shatter"}, Waveform::SINE_NOISE, 800.0, 0.8, 600.0,
                  {1, 50, 0.5, 200}, 0.0, 0.0, 0.0, 0.0, 0.4, 0.2});
        
        // ========== SCI-FI / LASERS ==========
        addSound({"laser", {"laser", "zap", "pew"}, Waveform::SAWTOOTH, 1000.0, 0.7, 300.0,
                  {1, 50, 0.6, 80}, 8.0, 3.0});
        
        addSound({"laser_rich", {"laser", "rich", "powerful"}, Waveform::SAWTOOTH, 800.0, 0.8, 400.0,
                  {1, 60, 0.7, 100}, 6.0, 4.0, 0.0, 0.0, 0.1, 0.3});
        
        addSound({"energy_hum", {"energy", "hum", "power"}, Waveform::SINE, 60.0, 0.4, 2000.0,
                  {100, 50, 0.8, 200}, 1.0, 0.5, 1.0, 0.2});
        
        addSound({"teleport", {"teleport", "warp", "portal"}, Waveform::SINE_NOISE, 440.0, 0.7, 1200.0,
                  {50, 200, 0.5, 300}, 2.0, 2.0, 2.0, 0.3, 0.3});
        
        // ========== ENVIRONMENTAL ==========
        addSound({"rain", {"rain", "drizzle"}, Waveform::NOISE, 0.0, 0.3, 3000.0,
                  {10, 50, 0.8, 200}, 0.0, 0.0, 0.5, 0.1});
        
        addSound({"wind", {"wind", "breeze"}, Waveform::NOISE, 0.0, 0.4, 4000.0,
                  {200, 100, 0.7, 300}, 0.2, 0.1, 1.0, 0.2});
        
        addSound({"fire", {"fire", "flame", "crackle"}, Waveform::NOISE, 0.0, 0.5, 2500.0,
                  {1, 50, 0.6, 150}, 0.0, 0.0, 2.0, 0.3, 0.4});
        
        addSound({"water_splash", {"splash", "water", "liquid"}, Waveform::NOISE, 400.0, 0.6, 500.0,
                  {1, 80, 0.4, 120}, 0.0, 0.0, 0.0, 0.0, 0.3});
        
        // ========== FOOTSTEPS ==========
        addSound({"footstep_concrete", {"footstep", "concrete", "walk"}, Waveform::NOISE, 100.0, 0.5, 150.0,
                  {1, 40, 0.3, 60}, 0.0, 0.0, 0.0, 0.0, 0.2, 0.1});
        
        addSound({"footstep_wood", {"footstep", "wood", "floor"}, Waveform::TRIANGLE, 180.0, 0.5, 120.0,
                  {2, 30, 0.4, 50}, 0.0, 0.0});
        
        // ========== UI / INTERFACE ==========
        addSound({"click", {"click", "tap"}, Waveform::SQUARE, 800.0, 0.6, 50.0,
                  {1, 10, 0.5, 20}, 0.0, 0.0});
        
        addSound({"ding", {"ding", "chime", "bell"}, Waveform::SINE, 1046.50, 0.7, 400.0,
                  {1, 100, 0.6, 150}, 0.0, 0.0});
        
        addSound({"chime", {"chime", "positive"}, Waveform::SINE, 880.0, 0.6, 600.0,
                  {5, 100, 0.7, 200}, 0.0, 0.0});
        
        addSound({"error_buzz", {"error", "buzz", "wrong"}, Waveform::SQUARE, 220.0, 0.6, 300.0,
                  {1, 100, 0.5, 100}, 0.0, 0.0});
        
        addSound({"cha_ching", {"cash", "cha-ching", "money"}, Waveform::SINE, 800.0, 0.7, 500.0,
                  {1, 150, 0.5, 100}, 0.0, 0.0, 0.0, 0.0, 0.0, 0.3});
        
        // ========== COMMERCIAL / PRODUCT ==========
        addSound({"product_reveal", {"reveal", "product", "show"}, Waveform::SINE, 1046.50, 0.7, 800.0,
                  {10, 200, 0.6, 150}, 2.0, 0.5});
        
        addSound({"applause", {"applause", "clap", "cheer"}, Waveform::NOISE, 0.0, 0.5, 2000.0,
                  {10, 100, 0.7, 300}, 0.0, 0.0, 1.0, 0.1});
        
        addSound({"sizzle", {"sizzle", "cook", "fry"}, Waveform::NOISE, 0.0, 0.4, 1500.0,
                  {1, 50, 0.5, 200}, 0.0, 0.0, 1.5, 0.2});
        
        // ========== CREATURE / ANIMAL ==========
        addSound({"roar", {"roar", "monster", "dragon"}, Waveform::SAWTOOTH, 80.0, 0.9, 1000.0,
                  {20, 200, 0.5, 300}, 1.0, 1.0, 2.0, 0.2, 0.2});
        
        addSound({"growl", {"growl", "animal"}, Waveform::SAWTOOTH, 120.0, 0.7, 800.0,
                  {30, 100, 0.6, 200}, 0.5, 0.8});
        
        // ========== ADD MORE SOUNDS (42 total) ==========
        addSound({"whoosh", {"whoosh", "swoosh", "fly"}, Waveform::SINE_NOISE, 300.0, 0.5, 600.0,
                  {1, 150, 0.4, 100}, 2.0, 1.0});
        
        addSound({"thunder", {"thunder", "storm"}, Waveform::NOISE, 50.0, 0.8, 2000.0,
                  {20, 400, 0.3, 500}, 0.0, 0.0, 0.0, 0.0, 0.3});
        
        addSound({"engine_start", {"engine", "start", "motor"}, Waveform::SAWTOOTH, 80.0, 0.6, 1500.0,
                  {50, 300, 0.7, 200}, 1.0, 1.0});
        
        addSound({"alarm", {"alarm", "warning", "alert"}, Waveform::SQUARE, 880.0, 0.7, 1000.0,
                  {1, 50, 0.8, 100}, 2.0, 0.0});
        
        addSound({"coin", {"coin", "pickup", "collect"}, Waveform::SINE, 1318.52, 0.6, 300.0,
                  {1, 80, 0.5, 80}, 1.0, 0.3});
        
        addSound({"powerup", {"powerup", "upgrade"}, Waveform::SINE, 880.0, 0.7, 500.0,
                  {10, 100, 0.7, 100}, 3.0, 1.0, 2.0, 0.2});
        
        addSound({"hurt", {"hurt", "damage", "pain"}, Waveform::SAWTOOTH, 200.0, 0.7, 400.0,
                  {1, 100, 0.4, 150}, 0.0, 0.0});
        
        addSound({"jump", {"jump", "hop", "leap"}, Waveform::SINE, 300.0, 0.6, 300.0,
                  {1, 80, 0.5, 80}, 1.5, 0.5});
        
        addSound({"land", {"land", "hit ground"}, Waveform::NOISE, 150.0, 0.5, 200.0,
                  {1, 60, 0.3, 80}, 0.0, 0.0});
        
        addSound({"sword_swing", {"sword", "swing", "slice"}, Waveform::SINE_NOISE, 400.0, 0.6, 300.0,
                  {1, 100, 0.4, 80}, 2.0, 1.0});
        
        addSound({"sword_hit", {"sword", "hit", "strike"}, Waveform::METALLIC, 600.0, 0.7, 250.0,
                  {1, 50, 0.4, 100}, 0.0, 0.0, 0.0, 0.0, 0.2, 0.5});
    }
    
    #include "presets.h"

    std::vector<SoundDefinition> m_soundList;
    std::unordered_map<std::string, SoundDefinition> m_soundsByName;
};

// ============================================================================
// Real-Time Sound Engine
// ============================================================================
class SoundEngine {
public:
    SoundEngine() = default;

    // Play a sound by name
    void play(const std::string& soundName, double volume = 1.0, double pan = 0.0, double distance = 1.0, double pitchShift = 1.0) {
        volume = clamp(volume, 0.0, 1.0);
        pan = clamp(pan, -1.0, 1.0);
        distance = clamp(distance, 0.0, 1e6);
        pitchShift = clamp(pitchShift, 0.01, 100.0);
        const SoundDefinition* def = m_library.findSound(soundName);
        if (!def) return;
        play(*def, volume, pan, distance, pitchShift);
    }

    // Caller-defined layers can be used without modifying the built-in library.
    void play(const SoundDefinition& definition, double volume = 1.0, double pan = 0.0,
              double distance = 1.0, double pitchShift = 1.0) {
        const auto* def = &definition;
        volume = clamp(volume, 0.0, 1.0); pan = clamp(pan, -1.0, 1.0);
        distance = clamp(distance, 0.0, 1e6); pitchShift = clamp(pitchShift, .01, 100.0);
        validateDefinition(definition);
        std::lock_guard<std::mutex> lock(m_voiceMutex);
        
        // Find free voice or lowest priority voice to steal
        int lowestPriority = 999;
        int lowestIndex = -1;
        int freeIndex = -1;
        
        for (int i = 0; i < MAX_VOICES; i++) {
            if (!m_voices[i].active) {
                freeIndex = i;
                break;
            }
            if (m_voices[i].priority < lowestPriority) {
                lowestPriority = m_voices[i].priority;
                lowestIndex = i;
            }
        }
        
        int targetIndex = (freeIndex != -1) ? freeIndex : lowestIndex;
        if (targetIndex == -1 || (freeIndex == -1 && def->priority < lowestPriority)) return;
        
        // Initialize voice
        SoundVoice& voice = m_voices[targetIndex];
        voice = SoundVoice{};
        voice.oscillator.setType(def->waveform);
        for (size_t i = 0; i < def->layers.size(); ++i)
            voice.layers.emplace_back(def->layers[i], static_cast<std::uint32_t>(i + 1));
        voice.frequency = def->base_freq * pitchShift;
        voice.amplitude = def->amplitude * volume;
        voice.duration_ms = def->duration_ms;
        voice.envelope = def->envelope;
        voice.pitch_lfo_rate = def->pitch_lfo_rate;
        voice.pitch_lfo_depth = def->pitch_lfo_depth;
        voice.amplitude_lfo_rate = def->amplitude_lfo_rate;
        voice.amplitude_lfo_depth = def->amplitude_lfo_depth;
        voice.noise_amount = def->noise_amount;
        voice.metallic_amount = def->metallic_amount;
        voice.pan = pan;
        voice.distance = distance;
        voice.priority = def->priority;
        voice.elapsed_ms = 0.0;
        voice.active = true;
        voice.releasing = false;
    }
    
    // Play with natural language description
    void playNatural(const std::string& description, double volume = 1.0) {
        double pitch = 1.0, pan = 0.0;
        std::istringstream input(description);
        std::string word, sound;
        while (input >> word) {
            std::transform(word.begin(), word.end(), word.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (word == "left") pan = -0.8;
            else if (word == "right") pan = 0.8;
            else if (word == "high") pitch = 1.5;
            else if (word == "low") pitch = 0.7;
            else if (word == "fast") pitch = 1.3;
            else if (word == "slow") pitch = 0.8;
            else { if (!sound.empty()) sound += ' '; sound += word; }
        }
        play(sound, volume, pan, 1.0, pitch);
    }

    // Set global effect parameters
    void setEcho(double delay_sec, double decay) {
        std::array<std::unique_ptr<Echo>, 2> effects{std::make_unique<Echo>(delay_sec, decay), std::make_unique<Echo>(delay_sec, decay)};
        std::lock_guard<std::mutex> lock(m_voiceMutex);
        m_echo = std::move(effects);
        m_echoEnabled = true;
    }
    
    void setReverb(double room_size, double damping) {
        std::array<std::unique_ptr<Reverb>, 2> effects{std::make_unique<Reverb>(room_size, damping), std::make_unique<Reverb>(room_size, damping)};
        std::lock_guard<std::mutex> lock(m_voiceMutex);
        m_reverb = std::move(effects);
        m_reverbEnabled = true;
    }
    
    void setChorus(double rate, double depth, double mix) {
        std::array<std::unique_ptr<Chorus>, 2> effects{std::make_unique<Chorus>(rate, depth, mix), std::make_unique<Chorus>(rate, depth, mix)};
        std::lock_guard<std::mutex> lock(m_voiceMutex);
        m_chorus = std::move(effects);
        m_chorusEnabled = true;
    }
    
    void setFlanger(double rate, double depth, double feedback, double mix) {
        std::array<std::unique_ptr<Flanger>, 2> effects{std::make_unique<Flanger>(rate, depth, feedback, mix), std::make_unique<Flanger>(rate, depth, feedback, mix)};
        std::lock_guard<std::mutex> lock(m_voiceMutex);
        m_flanger = std::move(effects);
        m_flangerEnabled = true;
    }
    
    void setStereoPong(double delay_sec, double decay) {
        auto effect = std::make_unique<StereoPong>(delay_sec, decay);
        std::lock_guard<std::mutex> lock(m_voiceMutex);
        m_pongEnabled = true;
        m_pong = std::move(effect);
    }
    
    void disableEcho() { std::lock_guard<std::mutex> lock(m_voiceMutex); m_echoEnabled = false; }
    void disableReverb() { std::lock_guard<std::mutex> lock(m_voiceMutex); m_reverbEnabled = false; }
    void disableChorus() { std::lock_guard<std::mutex> lock(m_voiceMutex); m_chorusEnabled = false; }
    void disableFlanger() { std::lock_guard<std::mutex> lock(m_voiceMutex); m_flangerEnabled = false; }
    void disableStereoPong() { std::lock_guard<std::mutex> lock(m_voiceMutex); m_pongEnabled = false; }
    
    void setMasterVolume(double vol) { std::lock_guard<std::mutex> lock(m_voiceMutex); m_masterVolume = clamp(vol, 0.0, 1.0); }
    
    // Get audio buffer (call this regularly for output)
    void getAudioBuffer(float* output, int numSamples, int channels = 2) {
        if (numSamples < 0 || (channels != 1 && channels != 2) || (!output && numSamples > 0))
            throw std::invalid_argument("Expected valid output, nonnegative frames, and 1 or 2 channels");
        std::lock_guard<std::mutex> lock(m_voiceMutex);
        for (int i = 0; i < numSamples; ++i) {
            double left = 0.0, right = 0.0;
            for (auto& voice : m_voices) {
                if (!voice.active) continue;
                voice.render(left, right);
                voice.update(1000.0 / SAMPLE_RATE);
            }
            if (m_echoEnabled) { left = m_echo[0]->process(left); right = m_echo[1]->process(right); }
            if (m_reverbEnabled) { left = m_reverb[0]->process(left); right = m_reverb[1]->process(right); }
            if (m_chorusEnabled) { left = m_chorus[0]->process(left); right = m_chorus[1]->process(right); }
            if (m_flangerEnabled) { left = m_flanger[0]->process(left); right = m_flanger[1]->process(right); }
            if (m_pongEnabled) m_pong->process(left, right);
            const size_t idx = static_cast<size_t>(i) * channels;
            output[idx] = static_cast<float>(clamp((channels == 1 ? (left + right) * 0.5 : left) * m_masterVolume, -1.0, 1.0));
            if (channels == 2) output[idx + 1] = static_cast<float>(clamp(right * m_masterVolume, -1.0, 1.0));
        }
    }

private:
    static void validateDefinition(const SoundDefinition& d) {
        auto range = [](double value, double low, double high) {
            if (!std::isfinite(value) || value < low || value > high)
                throw std::invalid_argument("Invalid custom synthesis parameter");
        };
        range(d.priority, 0, 10);
        range(d.base_freq, 0, 20000); range(d.amplitude, 0, 1); range(d.duration_ms, 0, 600000);
        range(d.envelope.attack_ms, 0, 600000); range(d.envelope.decay_ms, 0, 600000);
        range(d.envelope.sustain, 0, 1); range(d.envelope.release_ms, 0, 600000);
        range(d.pitch_lfo_rate, 0, 1000); range(d.pitch_lfo_depth, 0, 96);
        range(d.amplitude_lfo_rate, 0, 1000); range(d.amplitude_lfo_depth, 0, 1);
        range(d.noise_amount, 0, 1); range(d.metallic_amount, 0, 1);
        if (d.layers.size() > 8) throw std::invalid_argument("At most eight synthesis layers");
        for (const auto& layer : d.layers) layer.validate();
    }
    SoundLibrary m_library;
    std::array<SoundVoice, MAX_VOICES> m_voices;
    std::mutex m_voiceMutex;
    double m_masterVolume = MASTER_VOLUME;
    
    // Effects
    bool m_echoEnabled = false;
    bool m_reverbEnabled = false;
    bool m_chorusEnabled = false;
    bool m_flangerEnabled = false;
    bool m_pongEnabled = false;
    
    std::array<std::unique_ptr<Echo>, 2> m_echo;
    std::array<std::unique_ptr<Reverb>, 2> m_reverb;
    std::array<std::unique_ptr<Chorus>, 2> m_chorus;
    std::array<std::unique_ptr<Flanger>, 2> m_flanger;
    std::unique_ptr<StereoPong> m_pong;
};

// ============================================================================
// Demonstration
// ============================================================================
#ifndef SOUND_ENGINE_NO_MAIN
// Raw PCM: signed 16-bit little-endian, interleaved stereo, 48 kHz, no header.
int main(int argc, char** argv) {
    try {
        std::string path = "demo.pcm", wavPath, sound, category;
        bool rawRequested = false, all = false, list = false, effects = false;
        double seconds = 3.0;
        for (int i = 1; i < argc; ++i) {
            std::string option = argv[i];
            if (option == "--help") {
                std::cout << "Usage: sound_effects [--output PCM_FILE] [--wav-output WAV_FILE] [--all | --category NAME | --sound NAME_OR_DESCRIPTION] [--list] [--effects] [--seconds 0.001..600]\n"
                          << "Default: demo sequence to demo.pcm. Format: s16le, 48000 Hz, stereo.\n";
                return 0;
            }
            if (option == "--list") { list = true; continue; }
            if (option == "--effects") { effects = true; continue; }
            if (option == "--all") { all = true; continue; }
            if (i + 1 >= argc) throw std::invalid_argument("Missing value for " + option);
            std::string value = argv[++i];
            if (option == "--output") { path = value; rawRequested = true; }
            else if (option == "--wav-output") {
                if (value.empty()) throw std::invalid_argument("Empty WAV output path");
                wavPath = value;
            }
            else if (option == "--sound") sound = value;
            else if (option == "--category") { category = value; all = true; }
            else if (option == "--seconds") {
                size_t used = 0;
                seconds = std::stod(value, &used);
                if (used != value.size() || !std::isfinite(seconds) || seconds < .001 || seconds > 600)
                    throw std::invalid_argument("Seconds must be in 0.001..600");
            } else throw std::invalid_argument("Unknown option: " + option);
        }
        if (all && !sound.empty()) throw std::invalid_argument("Use --all or --sound, not both");
        SoundLibrary library;
        if (list) {
            for (const auto& preset : library.getAllSounds())
                if (category.empty() || preset.category == category)
                    std::cout << preset.name << "\t" << preset.category << "\n";
            return 0;
        }
        // Validate natural descriptions using the same modifier rules as playNatural.
        if (!sound.empty()) {
            std::istringstream words(sound);
            std::string word, lookup;
            while (words >> word) {
                std::transform(word.begin(), word.end(), word.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (word != "high" && word != "low" && word != "fast" && word != "slow" && word != "left" && word != "right") {
                    if (!lookup.empty()) lookup += ' ';
                    lookup += word;
                }
            }
            if (!library.findSound(lookup)) throw std::invalid_argument("Unknown sound: " + sound);
        }
        SoundEngine engine;
        if (effects) { engine.setReverb(.6, .25); engine.setStereoPong(.25, .25); }
        std::vector<std::string> sequence = sound.empty()
            ? std::vector<std::string>{"ding", "magic_sparkle", "laser", "explosion", "cartoon_crash", "cha_ching", "powerup", "roar", "product_reveal", "applause"}
            : std::vector<std::string>{sound};
        if (all) {
            sequence.clear();
            for (const auto& preset : library.getAllSounds())
                if (category.empty() || preset.category == category) sequence.push_back(preset.name);
            if (sequence.empty()) throw std::invalid_argument("Unknown or empty preset category");
        }
        std::vector<int> frameCounts;
        std::uint64_t totalFrames = 0;
        for (const auto& description : sequence) {
            double length = seconds;
            if (all) {
                const auto* preset = library.findSound(description);
                length = std::max(length, (preset->duration_ms + preset->envelope.release_ms) / 1000.0 + .5);
            }
            int frames = static_cast<int>(std::llround(length * SAMPLE_RATE));
            frameCounts.push_back(frames);
            totalFrames += frames;
        }
        const std::uint64_t dataBytes = totalFrames * 4;
        if (!wavPath.empty() && dataBytes > std::numeric_limits<std::uint32_t>::max() - 36ULL)
            throw std::invalid_argument("Output exceeds RIFF WAV size limit");
        const bool writeRaw = rawRequested || wavPath.empty();
        if (writeRaw && !wavPath.empty() &&
            std::filesystem::weakly_canonical(path) == std::filesystem::weakly_canonical(wavPath))
            throw std::invalid_argument("PCM and WAV output paths must differ");
        std::ofstream output, wav;
        if (writeRaw) {
            output.open(path, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("Cannot open output: " + path);
        }
        if (!wavPath.empty()) {
            wav.open(wavPath, std::ios::binary | std::ios::trunc);
            if (!wav) throw std::runtime_error("Cannot open WAV output: " + wavPath);
            auto little = [&](std::uint32_t value, int bytes) {
                for (int n = 0; n < bytes; ++n) wav.put(static_cast<char>((value >> (8 * n)) & 255));
            };
            wav.write("RIFF", 4); little(static_cast<std::uint32_t>(36 + dataBytes), 4);
            wav.write("WAVEfmt ", 8); little(16, 4); little(1, 2); little(2, 2);
            little(SAMPLE_RATE, 4); little(SAMPLE_RATE * 4, 4); little(4, 2); little(16, 2);
            wav.write("data", 4); little(static_cast<std::uint32_t>(dataBytes), 4);
        }
        std::array<float, BUFFER_SIZE * 2> buffer{};
        std::uint64_t startFrame = 0;
        for (size_t track = 0; track < sequence.size(); ++track) {
            const auto& description = sequence[track];
            const int total = frameCounts[track];
            std::cout << static_cast<double>(startFrame) / SAMPLE_RATE << "s " << description << '\n';
            startFrame += total;
            engine.playNatural(description, .7);
            for (int frame = 0; frame < total;) {
                int count = std::min(BUFFER_SIZE, total - frame);
                engine.getAudioBuffer(buffer.data(), count);
                for (int n = 0; n < count * 2; ++n) {
                    int sample = static_cast<int>(std::lround(buffer[n] * 32767.0));
                    auto bits = static_cast<std::uint16_t>(sample);
                    const char bytes[2] = {static_cast<char>(bits & 255), static_cast<char>(bits >> 8)};
                    if (writeRaw) output.write(bytes, 2);
                    if (!wavPath.empty()) wav.write(bytes, 2);
                }
                frame += count;
            }
        }
        if (writeRaw) {
            output.close();
            if (!output) throw std::runtime_error("Failed writing output: " + path);
            std::cout << "Wrote " << path << " (s16le, 48000 Hz, stereo)\n";
        }
        if (!wavPath.empty()) {
            wav.close();
            if (!wav) throw std::runtime_error("Failed writing WAV output: " + wavPath);
            std::cout << "Wrote " << wavPath << " (16-bit PCM WAV, 48000 Hz, stereo)\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
#endif
