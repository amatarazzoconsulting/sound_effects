// Included inside SoundLibrary. Recipes preserve classic names while replacing their timbres.
static SynthLayer tone(Waveform wave, double gain, double decay, double cutoff = 6000) {
    SynthLayer l; l.wave = wave; l.gain = gain; l.decay_ms = decay;
    l.cutoff_start = l.cutoff_end = cutoff; return l;
}
static SynthLayer sweep(SynthLayer l, double start, double end, double ms) {
    l.pitch_start = start; l.pitch_end = end; l.sweep_ms = ms; return l;
}
static SynthLayer filtered(SynthLayer l, double start, double end, double ms) {
    l.cutoff_start = start; l.cutoff_end = end; l.filter_ms = ms; return l;
}
static SynthLayer delayed(SynthLayer l, double ms) { l.delay_ms = ms; return l; }
static SynthLayer fm(SynthLayer l, double ratio, double depth, double decay) {
    l.fm_ratio = ratio; l.fm_depth = depth; l.fm_decay_ms = decay; return l;
}
static SynthLayer arp(SynthLayer l, std::initializer_list<int> notes, double step) {
    std::copy(notes.begin(), notes.end(), l.notes.begin());
    l.note_count = static_cast<int>(notes.size()); l.step_ms = step; return l;
}
void recipe(const std::string& name, const std::string& category, double freq, double amp,
            double duration, std::initializer_list<SynthLayer> layers, double release = 60) {
    auto it = std::find_if(m_soundList.begin(), m_soundList.end(), [&](const auto& d) { return d.name == name; });
    if (it == m_soundList.end()) {
        SoundDefinition d; d.name = name; d.keywords = {name}; m_soundList.push_back(d);
        it = std::prev(m_soundList.end());
    }
    it->category = category; it->base_freq = freq; it->amplitude = amp; it->duration_ms = duration;
    it->envelope = {0, 0, 1, release}; it->layers = layers;
    it->pitch_lfo_rate = it->pitch_lfo_depth = it->amplitude_lfo_rate = it->amplitude_lfo_depth = 0;
    it->noise_amount = it->metallic_amount = 0;
    double sum = 0;
    for (const auto& l : it->layers) sum += l.gain;
    if (sum > 1) for (auto& l : it->layers) l.gain /= sum;
}
void designPresets() {
    const auto sine = Waveform::SINE, tri = Waveform::TRIANGLE, pulse = Waveform::SQUARE;
    const auto saw = Waveform::SAWTOOTH, noise = Waveform::NOISE;
    recipe("boing", "cartoon", 160, .65, 500, {sweep(tone(tri, .8, 180), 18, -5, 450), tone(sine, .2, 80)});
    recipe("slide_whistle", "cartoon", 400, .45, 650, {sweep(tone(sine, 1, 500), 12, -12, 600)});
    recipe("cartoon_crash", "impact", 95, .8, 850, {sweep(tone(tri,.6,160),12,-12,120), filtered(tone(noise,.6,180),5000,250,400), fm(tone(sine,.2,240),3.17,4,120)});
    recipe("pop", "cartoon", 170, .55, 100, {sweep(tone(sine,1,22),24,-12,70)});
    recipe("giggle", "cartoon", 340, .4, 650, {arp(tone(tri,1,400),{0,7,2,9,0,5},90)});
    recipe("sad_trombone", "cartoon", 220, .4, 1200, {arp(tone(saw,.7,800,1800),{0,-1,-2,-7},250), tone(sine,.3,800)});
    recipe("magic_sparkle", "fantasy", 880, .35, 800, {arp(fm(tone(sine,.7,380),2,1.5,100),{0,7,12,19,24},110), delayed(tone(tri,.3,200),150)});
    recipe("explosion", "impact", 55, .9, 1500, {sweep(tone(sine,.55,400),20,-12,300), filtered(tone(noise,.8,350),2600,90,700), delayed(tone(noise,.4,450,180),80)});
    recipe("metallic_hit", "impact", 260, .55, 500, {fm(tone(sine,.7,180),3.17,6,140), tone(noise,.3,12,4000)});
    recipe("wood_hit", "impact", 180, .5, 200, {sweep(tone(tri,.8,45,1800),8,-6,70), tone(noise,.2,16,1200)});
    recipe("glass_break", "impact", 1600, .55, 550, {fm(tone(sine,.3,90),4.13,8,80), filtered(tone(noise,.7,180),9000,1500,350), delayed(fm(tone(sine,.25,80),2.71,5,90),90)});
    recipe("laser", "arcade", 420, .5, 280, {sweep(tone(pulse,.65,100,5000),24,-18,220), sweep(tone(tri,.35,140),12,-12,250)});
    recipe("laser_rich", "arcade", 240, .6, 450, {sweep(tone(saw,.6,180),24,-12,320), sweep(fm(tone(sine,.4,220),2,3,150),12,-12,350)});
    recipe("energy_hum", "scifi", 70, .35, 2000, {tone(saw,.5,0,800), fm(tone(sine,.5,0),1.01,.8,2000)});
    recipe("teleport", "scifi", 180, .4, 1100, {sweep(fm(tone(sine,.7,650),2.01,4,1000),0,36,1000), filtered(tone(noise,.3,600),400,6000,1000)});
    recipe("rain", "environment", 150, .35, 3000, {tone(noise,.8,0,4500), fm(tone(sine,.2,0),3.7,2,3000)});
    recipe("wind", "environment", 50, .45, 4000, {filtered(tone(noise,1,0),180,1200,3500)});
    recipe("fire", "environment", 80, .5, 2500, {tone(noise,.7,0,600), arp(tone(noise,.3,0,3000),{0},150)});
    recipe("water_splash", "environment", 500, .5, 650, {filtered(tone(noise,.7,150),5000,400,450), sweep(fm(tone(sine,.3,120),2.7,3,150),12,-24,400)});
    recipe("footstep_concrete", "foley", 100, .6, 160, {sweep(tone(tri,.55,28),8,-10,70), tone(noise,.45,35,1500)});
    recipe("footstep_wood", "foley", 160, .5, 150, {tone(tri,.8,40,1600), delayed(tone(noise,.2,15,900),12)});
    recipe("click", "ui", 800, .16, 30, {tone(tri,1,5,3500)}, 10);
    recipe("ding", "ui", 880, .25, 400, {fm(tone(sine,1,140),2,.6,60)});
    recipe("chime", "ui", 660, .2, 500, {tone(sine,.7,180), delayed(sweep(tone(sine,.3,140),7,7,1),100)});
    recipe("error_buzz", "ui", 160, .18, 200, {tone(pulse,1,100,900)});
    recipe("cha_ching", "arcade", 1000, .3, 450, {arp(fm(tone(sine,1,220),2,1.4,80),{0,7,12},100)});
    recipe("product_reveal", "ui", 440, .2, 700, {arp(tone(tri,.6,450),{0,4,7,12},120), delayed(tone(sine,.4,300),150)});
    recipe("applause", "environment", 0, .35, 2000, {tone(noise,1,0,2600)});
    recipe("sizzle", "environment", 0, .25, 1500, {tone(noise,1,0,5000)});
    recipe("roar", "creature", 65, .55, 1000, {sweep(tone(saw,.55,650,1100),5,-4,900), fm(tone(sine,.25,650),.5,3,500), tone(noise,.2,450,650)});
    recipe("growl", "creature", 85, .45, 800, {fm(tone(sine,.65,500),.5,6,600), tone(saw,.35,500,750)});
    recipe("whoosh", "foley", 0, .5, 600, {filtered(tone(noise,1,0),200,6500,550)});
    recipe("thunder", "environment", 38, .65, 2000, {tone(noise,.8,650,200), sweep(tone(sine,.2,500),5,-12,800)});
    recipe("engine_start", "foley", 35, .4, 1500, {sweep(tone(saw,.7,0,1000),-5,18,1300), tone(noise,.3,700,500)});
    recipe("alarm", "scifi", 660, .3, 1000, {arp(tone(pulse,1,0,2400),{0,7},180)});
    recipe("coin", "arcade", 1000, .3, 300, {arp(tone(pulse,.7,120,6000),{0,12},70), tone(sine,.3,80)});
    recipe("powerup", "arcade", 330, .4, 650, {arp(tone(pulse,.6,350,5000),{0,4,7,12,16,19,24},80), sweep(tone(tri,.4,400),0,24,600)});
    recipe("hurt", "arcade", 240, .5, 300, {sweep(tone(pulse,.8,100,2200),5,-18,220), tone(noise,.2,50,1500)});
    recipe("jump", "arcade", 180, .45, 300, {sweep(tone(tri,.7,160),0,20,250), sweep(tone(pulse,.3,100,2200),0,12,250)});
    recipe("land", "foley", 80, .55, 180, {sweep(tone(sine,.7,50),8,-12,90), tone(noise,.3,40,900)});
    recipe("sword_swing", "fantasy", 0, .5, 300, {filtered(tone(noise,1,80),600,7500,250)});
    recipe("sword_hit", "fantasy", 340, .6, 350, {fm(tone(sine,.6,120),3.41,7,100), tone(noise,.4,30,4500)});

    // Small, low-gain application cues: no global reverb, no hard impulses.
    recipe("ui_light_click", "ui", 1100, .075, 24, {tone(tri,1,4,2800)}, 8);
    recipe("ui_soft_tick", "ui", 1600, .06, 20, {tone(sine,1,3,3200)}, 8);
    recipe("ui_hover", "ui", 720, .065, 45, {sweep(tone(sine,1,12),0,2,35)}, 15);
    recipe("ui_soft_bell", "ui", 880, .12, 450, {fm(tone(sine,1,130),2,.35,45)}, 60);
    recipe("ui_open", "ui", 440, .1, 180, {sweep(tone(tri,.65,70),0,7,130), delayed(tone(sine,.35,70),35)}, 30);
    recipe("ui_close", "ui", 660, .09, 150, {sweep(tone(tri,1,55),0,-7,120)}, 30);
    recipe("ui_confirm", "ui", 660, .12, 230, {arp(tone(sine,1,100),{0,7},80)}, 40);
    recipe("ui_cancel", "ui", 440, .1, 180, {arp(tone(tri,1,65,2200),{0,-5},70)}, 40);
    recipe("ui_notify", "ui", 740, .11, 360, {tone(sine,.6,120), delayed(sweep(tone(sine,.4,100),5,5,1),110)}, 60);
    recipe("ui_toggle_on", "ui", 500, .08, 70, {sweep(tone(tri,1,20),0,7,60)}, 20);
    recipe("ui_toggle_off", "ui", 750, .08, 70, {sweep(tone(tri,1,20),0,-7,60)}, 20);
    recipe("ui_error_soft", "ui", 220, .1, 230, {arp(tone(tri,1,100,1400),{0,-3},90)}, 40);

    recipe("arcade_shoot", "arcade", 700, .45, 160, {sweep(tone(pulse,1,50),12,-24,130)});
    recipe("arcade_ricochet", "arcade", 900, .4, 260, {sweep(tone(tri,.8,90),-5,18,70), delayed(sweep(tone(pulse,.2,40),12,-12,100),70)});
    recipe("arcade_shield", "scifi", 180, .4, 500, {fm(tone(sine,.8,200),1.5,5,150), sweep(tone(tri,.2,120),12,-12,200)});
    recipe("arcade_pickup", "arcade", 660, .3, 240, {arp(tone(pulse,1,90),{0,7,12},65)});
    recipe("arcade_extra_life", "arcade", 440, .35, 700, {arp(tone(pulse,.6,400),{0,4,7,12,7,12},100), tone(tri,.4,400)});
    recipe("arcade_checkpoint", "arcade", 523.25, .3, 550, {arp(tone(tri,1,300),{0,7,12,16},100)});
    recipe("arcade_level_up", "arcade", 330, .35, 900, {arp(tone(pulse,.6,600),{0,4,7,12,16,19,24,28},100), tone(tri,.4,600)});
    recipe("arcade_game_over", "arcade", 330, .35, 1200, {arp(tone(pulse,.5,700,2200),{0,-3,-7,-12},240), sweep(tone(tri,.5,800),0,-12,1000)});
    recipe("arcade_pause", "arcade", 440, .2, 260, {arp(tone(tri,1,120),{7,0},100)});
    recipe("arcade_unpause", "arcade", 440, .2, 260, {arp(tone(tri,1,120),{0,7},100)});
    recipe("arcade_blip", "arcade", 880, .2, 60, {tone(pulse,1,15,4500)}, 15);
    recipe("arcade_menu_move", "arcade", 440, .14, 50, {tone(tri,1,12)}, 15);
    recipe("arcade_countdown", "arcade", 660, .25, 180, {tone(pulse,1,65,4000)});
    recipe("arcade_go", "arcade", 1320, .3, 350, {tone(pulse,.6,150,5000), tone(tri,.4,140)});
    recipe("space_engine", "scifi", 48, .4, 1600, {tone(saw,.65,0,650), filtered(tone(noise,.35,0),200,1000,1300)});
    recipe("space_warp", "scifi", 120, .45, 1000, {sweep(fm(tone(sine,.7,500),1.5,5,700),-12,36,900), filtered(tone(noise,.3,400),300,6000,700)});
    recipe("robot_talk", "scifi", 220, .3, 600, {arp(fm(tone(sine,1,350),2,3,500),{0,7,-5,12,3,-2},80)});
    recipe("radar_ping", "scifi", 1400, .2, 500, {tone(sine,1,130)});
    recipe("retro_jump", "arcade", 140, .35, 240, {sweep(tone(pulse,1,100,4000),0,24,220)});
    recipe("retro_fall", "arcade", 700, .35, 450, {sweep(tone(pulse,1,200,3500),0,-30,400)});
    recipe("retro_hit", "arcade", 120, .5, 180, {sweep(tone(pulse,.6,40,2000),12,-12,80), tone(noise,.4,30,1200)});
    recipe("boss_warning", "arcade", 110, .4, 950, {arp(tone(saw,1,0,1500),{0,1,0,1},180)});
    recipe("magic_cast", "fantasy", 220, .35, 750, {sweep(fm(tone(sine,.65,400),3,2,400),0,24,650), arp(tone(tri,.35,300),{0,7,12,19},120)});
    recipe("heal", "fantasy", 440, .25, 650, {arp(tone(sine,.6,350),{0,4,7,12},130), delayed(tone(tri,.4,250),100)});
    recipe("drum_kick", "percussion", 55, .65, 220, {sweep(tone(sine,1,70),24,0,45)});
    recipe("drum_snare", "percussion", 180, .5, 200, {tone(tri,.4,35), filtered(tone(noise,.6,60),6500,1800,130)});
    recipe("drum_hat", "percussion", 0, .22, 90, {tone(noise,1,18,9000)}, 20);
    recipe("drum_tom", "percussion", 130, .5, 260, {sweep(tone(sine,.85,90),8,-3,120), tone(noise,.15,12,3000)});
    recipe("drum_clap", "percussion", 0, .35, 180, {tone(noise,.4,8,5000), delayed(tone(noise,.3,10,5000),15), delayed(tone(noise,.3,45,3500),30)});
    recipe("drum_rim", "percussion", 600, .35, 80, {fm(tone(sine,.7,12),2.71,3,10), tone(noise,.3,5,2500)}, 20);

    // Motion is applied to colored textures instead of static white-noise beds.
    for (auto& d : m_soundList) {
        if (d.name == "whoosh") { d.layers[0].attack_ms = 260; d.envelope.release_ms = 160; }
        if (d.name == "wind") { d.layers[0].attack_ms = 500; d.amplitude_lfo_rate = .6; d.amplitude_lfo_depth = .25; }
        if (d.name == "rain") { d.layers[0].attack_ms = 200; d.amplitude_lfo_rate = 1.1; d.amplitude_lfo_depth = .1; }
        if (d.name == "fire") { d.amplitude_lfo_rate = 17; d.amplitude_lfo_depth = .55; }
        if (d.name == "applause") { d.amplitude_lfo_rate = 13; d.amplitude_lfo_depth = .5; }
        if (d.name == "sizzle") { d.layers[0].highpass_hz = 1800; d.amplitude_lfo_rate = 21; d.amplitude_lfo_depth = .2; }
        if (d.name == "drum_hat") d.layers[0].highpass_hz = 5000;
    }
    m_soundsByName.clear();
    for (const auto& d : m_soundList) m_soundsByName[d.name] = d;
}
