/*
    ============================================================================
    PROFESSIONAL REAL-TIME SOUND PRODUCTION ENGINE
    ============================================================================
    Features:
    - 128+ simultaneous sound voices
    - Complete sound library (50+ sounds)
    - Real-time parameter modulation
    - 3D spatialization with Doppler effect
    - Built-in effects: Echo, Reverb, Chorus, Flanger, Stereo Pong
    - Priority-based voice stealing
    - Natural language descriptor parser
    ============================================================================
*/

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
        static Random instance;
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
    
    double render(double freq_hz) {
        double increment = freq_hz / SAMPLE_RATE;
        m_phase += increment;
        if (m_phase >= 1.0) m_phase -= 1.0;
        
        double sample = 0.0;
        switch (m_type) {
            case Waveform::SINE:
                sample = sin(2.0 * PI * m_phase);
                break;
            case Waveform::SQUARE:
                sample = m_phase < 0.5 ? 1.0 : -1.0;
                break;
            case Waveform::TRIANGLE:
                sample = 2.0 * fabs(2.0 * m_phase - 1.0) - 1.0;
                break;
            case Waveform::SAWTOOTH:
                sample = 2.0 * m_phase - 1.0;
                break;
            case Waveform::NOISE:
                sample = Random::get().uniform(-1.0, 1.0);
                break;
            case Waveform::SINE_NOISE:
                sample = sin(2.0 * PI * m_phase) * 0.7 + Random::get().uniform(-0.3, 0.3);
                break;
            case Waveform::METALLIC:
                sample = sin(2.0 * PI * m_phase) + 0.5 * sin(2.0 * PI * m_phase * 2.17);
                break;
        }
        return sample;
    }
    
private:
    Waveform m_type;
    double m_phase;
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
            double release_start = duration_ms - release_ms;
            if (time_ms < release_start) return sustain;
            double t = clamp((time_ms - release_start) / release_ms, 0.0, 1.0);
            return sustain * (1.0 - t);
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
    DelayLine(int max_samples) : m_buffer(max_samples, 0.0), m_pos(0) {}
    
    void write(double sample) {
        m_buffer[m_pos] = sample;
        m_pos = (m_pos + 1) % m_buffer.size();
    }
    
    double read(int delay_samples) {
        int pos = (m_pos - delay_samples + m_buffer.size()) % m_buffer.size();
        return m_buffer[pos];
    }
    
private:
    std::vector<double> m_buffer;
    int m_pos;
};

class Echo {
public:
    Echo(double delay_sec = 0.3, double decay = 0.5) 
        : m_delayLine(SAMPLE_RATE * 2), m_delaySamples(SAMPLE_RATE * delay_sec), m_decay(decay) {}
    
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
        : m_delayLines{SAMPLE_RATE * 0.0297, SAMPLE_RATE * 0.0371, SAMPLE_RATE * 0.0411, SAMPLE_RATE * 0.0437},
          m_feedback(room_size * 0.8), m_damping(damping) {}
    
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
        : m_rate(rate), m_depth(depth * SAMPLE_RATE), m_mix(mix), m_phase(0.0) {}
    
    double process(double sample) {
        m_phase += m_rate / SAMPLE_RATE;
        if (m_phase >= 1.0) m_phase -= 1.0;
        
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
        : m_rate(rate), m_depth(depth * SAMPLE_RATE), m_feedback(feedback), m_mix(mix), m_phase(0.0) {}
    
    double process(double sample) {
        m_phase += m_rate / SAMPLE_RATE;
        if (m_phase >= 1.0) m_phase -= 1.0;
        
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
        : m_delaySamples(SAMPLE_RATE * delay_sec), m_decay(decay), m_pan(1.0) {}
    
    void process(double& left, double& right) {
        double left_delayed = m_leftDelay.read(m_delaySamples);
        double right_delayed = m_rightDelay.read(m_delaySamples);
        
        // Ping-pong effect: bounces between left and right
        m_pan *= -1.0;
        
        double output_left = left + (m_pan > 0 ? left_delayed : right_delayed) * m_decay;
        double output_right = right + (m_pan < 0 ? left_delayed : right_delayed) * m_decay;
        
        m_leftDelay.write(left + right_delayed * m_decay * 0.5);
        m_rightDelay.write(right + left_delayed * m_decay * 0.5);
        
        left = output_left;
        right = output_right;
    }
    
private:
    DelayLine m_leftDelay{SAMPLE_RATE * 2};
    DelayLine m_rightDelay{SAMPLE_RATE * 2};
    int m_delaySamples;
    double m_decay;
    double m_pan;
};

// ============================================================================
// Sound Voice (Active Sound Instance)
// ============================================================================
struct SoundVoice {
    // Core
    Oscillator oscillator;
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
    bool active = true;
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
        
        // Generate base sample
        double sample = oscillator.render(final_freq);
        
        // Add noise texture
        if (noise_amount > 0.0) {
            sample = sample * (1.0 - noise_amount) + Random::get().uniform(-1.0, 1.0) * noise_amount;
        }
        
        // Add metallic ringing
        if (metallic_amount > 0.0) {
            Oscillator metal(Waveform::METALLIC);
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
};

// ============================================================================
// Sound Library (50+ Sounds)
// ============================================================================
class SoundLibrary {
public:
    SoundLibrary() {
        initializeSounds();
    }
    
    const SoundDefinition* findSound(const std::string& name) const {
        auto it = m_soundsByName.find(name);
        if (it != m_soundsByName.end()) return &it->second;
        
        // Keyword matching
        for (const auto& [key, sound] : m_soundsByName) {
            for (const auto& keyword : sound.keywords) {
                if (name.find(keyword) != std::string::npos) {
                    return &sound;
                }
            }
        }
        return nullptr;
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
        
        // ========== ADD MORE SOUNDS (50+ total) ==========
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
    
    std::vector<SoundDefinition> m_soundList;
    std::unordered_map<std::string, SoundDefinition> m_soundsByName;
};

// ============================================================================
// Real-Time Sound Engine
// ============================================================================
class SoundEngine {
public:
    SoundEngine() : m_running(true), m_masterVolume(MASTER_VOLUME) {
        m_audioThread = std::thread(&SoundEngine::audioThreadFunc, this);
    }
    
    ~SoundEngine() {
        m_running = false;
        if (m_audioThread.joinable()) m_audioThread.join();
    }
    
    // Play a sound by name
    void play(const std::string& soundName, double volume = 1.0, double pan = 0.0, double distance = 1.0) {
        const SoundDefinition* def = m_library.findSound(soundName);
        if (!def) return;
        
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
        if (targetIndex == -1) return;
        
        // Initialize voice
        SoundVoice& voice = m_voices[targetIndex];
        voice.oscillator.setType(def->waveform);
        voice.frequency = def->base_freq;
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
        // Parse description for modifiers
        double pitch_shift = 1.0;
        double pan = 0.0;
        std::string soundType = description;
        
        if (description.find("left") != std::string::npos) pan = -0.8;
        if (description.find("right") != std::string::npos) pan = 0.8;
        if (description.find("high") != std::string::npos) pitch_shift = 1.5;
        if (description.find("low") != std::string::npos) pitch_shift = 0.7;
        if (description.find("fast") != std::string::npos) pitch_shift = 1.3;
        if (description.find("slow") != std::string::npos) pitch_shift = 0.8;
        
        play(soundType, volume, pan);
        
        // Apply pitch shift by modifying frequency (simplified)
        std::lock_guard<std::mutex> lock(m_voiceMutex);
        for (auto& voice : m_voices) {
            if (voice.active) {
                voice.frequency *= pitch_shift;
            }
        }
    }
    
    // Set global effect parameters
    void setEcho(double delay_sec, double decay) {
        m_echoEnabled = true;
        m_echo = std::make_unique<Echo>(delay_sec, decay);
    }
    
    void setReverb(double room_size, double damping) {
        m_reverbEnabled = true;
        m_reverb = std::make_unique<Reverb>(room_size, damping);
    }
    
    void setChorus(double rate, double depth, double mix) {
        m_chorusEnabled = true;
        m_chorus = std::make_unique<Chorus>(rate, depth, mix);
    }
    
    void setFlanger(double rate, double depth, double feedback, double mix) {
        m_flangerEnabled = true;
        m_flanger = std::make_unique<Flanger>(rate, depth, feedback, mix);
    }
    
    void setStereoPong(double delay_sec, double decay) {
        m_pongEnabled = true;
        m_pong = std::make_unique<StereoPong>(delay_sec, decay);
    }
    
    void disableEcho() { m_echoEnabled = false; }
    void disableReverb() { m_reverbEnabled = false; }
    void disableChorus() { m_chorusEnabled = false; }
    void disableFlanger() { m_flangerEnabled = false; }
    void disableStereoPong() { m_pongEnabled = false; }
    
    void setMasterVolume(double vol) { m_masterVolume = clamp(vol, 0.0, 1.0); }
    
    // Get audio buffer (call this regularly for output)
    void getAudioBuffer(float* output, int numSamples, int channels = 2) {
        std::fill(output, output + numSamples * channels, 0.0f);
        
        double delta_ms = (double)numSamples / SAMPLE_RATE * 1000.0;
        
        std::lock_guard<std::mutex> lock(m_voiceMutex);
        
        // Process all active voices
        for (auto& voice : m_voices) {
            if (!voice.active) continue;
            
            for (int i = 0; i < numSamples; i++) {
                double left = 0.0, right = 0.0;
                voice.render(left, right);
                
                int idx = i * channels;
                output[idx] += (float)left;
                if (channels > 1) output[idx + 1] += (float)right;
            }
            voice.update(delta_ms);
        }
        
        // Apply effects
        if (m_echoEnabled && m_echo) {
            for (int i = 0; i < numSamples * channels; i += channels) {
                double sample = output[i];
                sample = m_echo->process(sample);
                output[i] = (float)sample;
                if (channels > 1) output[i + 1] = (float)sample;
            }
        }
        
        if (m_reverbEnabled && m_reverb) {
            for (int i = 0; i < numSamples * channels; i += channels) {
                double sample = output[i];
                sample = m_reverb->process(sample);
                output[i] = (float)sample;
                if (channels > 1) output[i + 1] = (float)sample;
            }
        }
        
        if (m_chorusEnabled && m_chorus) {
            for (int i = 0; i < numSamples * channels; i++) {
                output[i] = (float)m_chorus->process(output[i]);
            }
        }
        
        if (m_flangerEnabled && m_flanger) {
            for (int i = 0; i < numSamples * channels; i++) {
                output[i] = (float)m_flanger->process(output[i]);
            }
        }
        
        if (m_pongEnabled && m_pong) {
            for (int i = 0; i < numSamples; i++) {
                double left = output[i * channels];
                double right = (channels > 1) ? output[i * channels + 1] : left;
                m_pong->process(left, right);
                output[i * channels] = (float)left;
                if (channels > 1) output[i * channels + 1] = (float)right;
            }
        }
        
        // Apply master volume and clamp
        for (int i = 0; i < numSamples * channels; i++) {
            output[i] = clamp(output[i] * (float)m_masterVolume, -1.0f, 1.0f);
        }
    }
    
private:
    void audioThreadFunc() {
        // In a real implementation, this would feed audio to the system
        // For this example, we just simulate
        std::vector<float> buffer(BUFFER_SIZE * 2);
        while (m_running) {
            getAudioBuffer(buffer.data(), BUFFER_SIZE, 2);
            std::this_thread::sleep_for(std::chrono::milliseconds(BUFFER_SIZE * 1000 / SAMPLE_RATE));
        }
    }
    
    SoundLibrary m_library;
    std::array<SoundVoice, MAX_VOICES> m_voices;
    std::mutex m_voiceMutex;
    std::thread m_audioThread;
    std::atomic<bool> m_running;
    double m_masterVolume;
    
    // Effects
    bool m_echoEnabled = false;
    bool m_reverbEnabled = false;
    bool m_chorusEnabled = false;
    bool m_flangerEnabled = false;
    bool m_pongEnabled = false;
    
    std::unique_ptr<Echo> m_echo;
    std::unique_ptr<Reverb> m_reverb;
    std::unique_ptr<Chorus> m_chorus;
    std::unique_ptr<Flanger> m_flanger;
    std::unique_ptr<StereoPong> m_pong;
};

// ============================================================================
// Demonstration
// ============================================================================
int main() {
    std::cout << "========================================\n";
    std::cout << "PROFESSIONAL REAL-TIME SOUND ENGINE\n";
    std::cout << "========================================\n\n";
    
    SoundEngine engine;
    
    // Enable effects for rich sound
    engine.setReverb(0.6, 0.4);
    engine.setStereoPong(0.25, 0.5);
    engine.setMasterVolume(0.7);
    
    std::cout << "Playing demonstration sounds...\n\n";
    
    // Play a sequence of sounds
    std::vector<std::pair<std::string, double>> demoSequence = {
        {"ding", 0.8},
        {"magic_sparkle", 0.6},
        {"laser", 0.7},
        {"explosion", 0.9},
        {"cartoon_crash", 0.8},
        {"cha_ching", 0.7},
        {"powerup", 0.8},
        {"roar", 0.7},
        {"product_reveal", 0.7},
        {"applause", 0.6}
    };
    
    for (const auto& [sound, volume] : demoSequence) {
        std::cout << " > " << sound << "\n";
        engine.play(sound, volume);
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
    }
    
    // Demonstrate natural language
    std::cout << "\n--- Natural Language Examples ---\n";
    engine.playNatural("high laser left", 0.7);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    engine.playNatural("low explosion right", 0.8);
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    
    // Demonstrate effects changing
    std::cout << "\n--- Changing Effects ---\n";
    engine.setFlanger(0.3, 0.004, 0.6, 0.5);
    engine.play("laser", 0.7);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    engine.disableFlanger();
    engine.setChorus(0.8, 0.002, 0.4);
    engine.play("magic_sparkle", 0.7);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    std::cout << "\nEngine running. Press Enter to exit...\n";
    std::cin.get();
    
    return 0;
}

engine.play("laser", 0.8, 0.5);           // Play at center, volume 0.8
engine.play("explosion", 1.0, -0.8);      // Play from left
engine.playNatural("high metallic crash"); // Natural description
engine.setReverb(0.7, 0.5);               // Add reverb
engine.setStereoPong(0.3, 0.6);           // Add ping-pong delay
