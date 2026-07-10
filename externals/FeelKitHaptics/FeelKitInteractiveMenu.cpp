#define _CRT_SECURE_NO_WARNINGS

#include "FeelKitInteractiveMenu.h"
#include "FeelKit.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int readInt(const char* prompt, int defaultVal) {
	std::printf("%s [%d]: ", prompt, defaultVal);
	char buf[64];
	if (!std::fgets(buf, sizeof(buf), stdin)) return defaultVal;
	int val;
	if (std::sscanf(buf, "%d", &val) != 1) return defaultVal;
	return val;
}

float readFloat(const char* prompt, float defaultVal) {
	std::printf("%s [%.2f]: ", prompt, defaultVal);
	char buf[64];
	if (!std::fgets(buf, sizeof(buf), stdin)) return defaultVal;
	float val;
	if (std::sscanf(buf, "%f", &val) != 1) return defaultVal;
	return val;
}

std::string readString(const char* prompt, const char* defaultVal) {
	std::printf("%s [%s]: ", prompt, defaultVal);
	char buf[256];
	if (!std::fgets(buf, sizeof(buf), stdin)) return defaultVal;
	buf[strcspn(buf, "\n")] = '\0';
	if (buf[0] == '\0') return defaultVal;
	return buf;
}

void pressEnter() {
	std::printf("\nPress Enter to continue...");
	while (std::getchar() != '\n') {}
}

} // namespace

//================================================================
// メニュー項目
//================================================================

struct MenuItem {
	int id;
	const char* label;
	void (*action)();
};

static int showMenu(const char* title, const MenuItem* items, int count) {
	while (true) {
		std::printf("\n===== %s =====\n", title);
		for (int i = 0; i < count; i++) {
			std::printf("  %2d. %s\n", items[i].id, items[i].label);
		}
		std::printf("  0. Back\n");
		std::printf("Select: ");

		char buf[16];
		if (!std::fgets(buf, sizeof(buf), stdin)) return 0;
		int sel;
		if (std::sscanf(buf, "%d", &sel) != 1) continue;
		if (sel == 0) return 0;
		for (int i = 0; i < count; i++) {
			if (items[i].id == sel) {
				if (items[i].action) items[i].action();
				return sel;
			}
		}
	}
}

//================================================================
// Audio 解析
//================================================================

static void menuAnalyzeAudioFile() {
	std::string path = readString("WAV path", "BGM.wav");
	std::printf("Analyzing...\n");
	FeelKit::AudioAnalysisResult r = FeelKit::AnalyzeAudioFile(path.c_str());
	if (!r.isValid) { std::printf("  Invalid / not found\n"); return; }
	std::printf("  duration=%.3fs  amplitude=%.4f  peak=%.4f\n", r.durationSeconds, r.averageAmplitude, r.peakAmplitude);
	std::printf("  lowBand=%.4f  highBand=%.4f  sampleRate=%.0f\n", r.lowBandEnergy, r.highBandEnergy, r.sampleRate);
}

static void menuAnalyzeAudioFeatures() {
	std::string path = readString("WAV path", "BGM.wav");
	std::printf("Analyzing features...\n");
	FeelKit::AudioFeatureData d = FeelKit::AnalyzeAudioFeatures(path.c_str());
	if (!d.isValid) { std::printf("  Invalid / not found\n"); return; }
	std::printf("  duration=%.3fs  amplitude=%.4f  peak=%.4f\n", d.durationSeconds, d.averageAmplitude, d.peakAmplitude);
	std::printf("  lowBand=%.4f  highBand=%.4f  envelope=%.4f\n", d.lowBandEnergy, d.highBandEnergy, d.envelopeAverage);
	std::printf("  peaks=%d  attack=%.4f\n", d.peakCount, d.attackStrength);
}

//================================================================
// Audio → Effect 変換
//================================================================

static void menuMakeHapticsFromAudio() {
	std::string path = readString("WAV path", "BGM.wav");
	FeelKit::AudioFeatureData d = FeelKit::AnalyzeAudioFeatures(path.c_str());
	if (!d.isValid) { std::printf("  Invalid\n"); return; }
	FeelKit::HapticParams p = FeelKit::MakeHapticsFromAudio(d);
	std::printf("  HapticParams: power=%.4f  seconds=%.4f\n", p.power, p.seconds);
	FeelKit::Vibrate(p.power, p.seconds);
	std::printf("  -> Vibrate() called\n");
}

static void menuMakeShakeFromAudio() {
	std::string path = readString("WAV path", "BGM.wav");
	FeelKit::AudioFeatureData d = FeelKit::AnalyzeAudioFeatures(path.c_str());
	if (!d.isValid) { std::printf("  Invalid\n"); return; }
	FeelKit::ShakeParams p = FeelKit::MakeShakeFromAudio(d);
	std::printf("  ShakeParams: power=%.4f  seconds=%.4f\n", p.power, p.seconds);
	FeelKit::Shake(p.power, p.seconds);
}

static void menuMakeFlashFromAudio() {
	std::string path = readString("WAV path", "BGM.wav");
	FeelKit::AudioFeatureData d = FeelKit::AnalyzeAudioFeatures(path.c_str());
	if (!d.isValid) { std::printf("  Invalid\n"); return; }
	FeelKit::FlashParams p = FeelKit::MakeFlashFromAudio(d);
	std::printf("  FlashParams: color=0x%08X  seconds=%.4f  strength=%.4f\n", p.color, p.seconds, p.strength);
	FeelKit::Flash(p.color, p.seconds, p.strength);
}

static void menuMakeParticlesFromAudio() {
	std::string path = readString("WAV path", "BGM.wav");
	FeelKit::AudioFeatureData d = FeelKit::AnalyzeAudioFeatures(path.c_str());
	if (!d.isValid) { std::printf("  Invalid\n"); return; }
	FeelKit::ParticleParams p = FeelKit::MakeParticlesFromAudio(d);
	std::printf("  ParticleParams: rate=%.1f  size=%.1f  speed=%.1f  life=%.2f\n", p.spawnRate, p.size, p.speed, p.life);
	std::printf("  radius=%.2f  color=0x%08X\n", p.radius, p.colorRgba);
	FeelKit::CreateParticlesFromAudio(path.c_str());
	std::printf("  -> Emitter created\n");
}

static void menuMakeSlowMotionFromAudio() {
	std::string path = readString("WAV path", "BGM.wav");
	FeelKit::AudioFeatureData d = FeelKit::AnalyzeAudioFeatures(path.c_str());
	if (!d.isValid) { std::printf("  Invalid\n"); return; }
	FeelKit::SlowMotionParams p = FeelKit::MakeSlowMotionFromAudio(d);
	std::printf("  SlowMotionParams: scale=%.4f  seconds=%.4f\n", p.scale, p.seconds);
	FeelKit::SlowMotion(p.scale, p.seconds);
}

//================================================================
// Audio → その他エフェクト
//================================================================

static void menuPlayHapticFromAudio() {
	std::string path = readString("WAV path", "BGM.wav");
	FeelKit::PlayHapticFromAudio(path.c_str());
	std::printf("  Done\n");
}

static void menuPlayShakeFromAudio() {
	std::string path = readString("WAV path", "BGM.wav");
	FeelKit::PlayShakeFromAudio(path.c_str());
	std::printf("  Done\n");
}

static void menuPlayFlashFromAudio() {
	std::string path = readString("WAV path", "BGM.wav");
	FeelKit::PlayFlashFromAudio(path.c_str());
	std::printf("  Done\n");
}

static void menuCreateLightFromAudio() {
	std::string path = readString("WAV path", "BGM.wav");
	FeelKit::CreateLightFromAudio(path.c_str());
	std::printf("  Done (Flash)\n");
}

static void menuCreateCameraMotionFromAudio() {
	std::string path = readString("WAV path", "BGM.wav");
	FeelKit::CreateCameraMotionFromAudio(path.c_str());
	std::printf("  Done (Shake+Zoom)\n");
}

static void menuCreateSlowMotionFromAudio() {
	std::string path = readString("WAV path", "BGM.wav");
	FeelKit::CreateSlowMotionFromAudio(path.c_str());
	std::printf("  Done\n");
}

static void menuSpawnEffectFromAudio() {
	std::string path = readString("WAV path", "BGM.wav");
	FeelKit::EffectDesc d;
	d.textureHandle = readInt("textureHandle", 1);
	d.frameCount = readInt("frameCount", 1);
	d.seconds = readFloat("seconds", 0.5f);
	FeelKit::SpawnEffectFromAudio(d, path.c_str(), FeelKit::Vec2{400, 300});
	std::printf("  Done\n");
}

static void menuPlaySE() {
	std::string path = readString("path", "impact.wav");
	float vol = readFloat("volume", 1.0f);
	FeelKit::PlaySE(path.c_str(), vol);
	std::printf("  -> PlaySE()\n");
}

static void menuPlayBGM() {
	std::string path = readString("path", "BGM.wav");
	int loop = readInt("loop (0/1)", 1);
	FeelKit::PlayBGM(path.c_str(), loop != 0);
	std::printf("  -> PlayBGM()\n");
}

static void menuStopBGM() {
	FeelKit::StopBGM();
	std::printf("  -> StopBGM()\n");
}

static void menuSetMasterVolume() {
	float v = readFloat("volume (0.0~1.0)", 1.0f);
	FeelKit::SetMasterVolume(v);
	std::printf("  -> MasterVolume=%.2f\n", v);
}

static void menuGetMasterVolume() {
	std::printf("  MasterVolume=%.2f\n", FeelKit::GetMasterVolume());
}

//================================================================
// Screen Effects
//================================================================

static void menuShake() {
	float p = readFloat("power", 5.0f);
	float s = readFloat("seconds", 0.3f);
	FeelKit::Shake(p, s);
	std::printf("  -> Shake(%.2f, %.2f)\n", p, s);
}

static void menuFlash() {
	int color = readInt("color (0xRRGGBB)", 0xFFFFFF);
	float s = readFloat("seconds", 0.3f);
	float str = readFloat("strength", 1.0f);
	FeelKit::Flash(0xFF000000u | static_cast<unsigned int>(color), s, str);
	std::printf("  -> Flash()\n");
}

static void menuZoom() {
	float scale = readFloat("scale", 1.2f);
	float s = readFloat("seconds", 0.3f);
	FeelKit::Zoom(scale, s);
	std::printf("  -> Zoom()\n");
}

static void menuHitStop() {
	float s = readFloat("seconds", 0.05f);
	FeelKit::HitStop(s);
	std::printf("  -> HitStop(%.3f)\n", s);
}

static void menuSlowMotion() {
	float scale = readFloat("scale", 0.3f);
	float s = readFloat("seconds", 1.0f);
	FeelKit::SlowMotion(scale, s);
	std::printf("  -> SlowMotion(%.2f, %.2f)\n", scale, s);
}

static void menuGetScreenState() {
	FeelKit::ScreenState st = FeelKit::GetScreenState();
	std::printf("  shakeOffset=(%.2f,%.2f) shakeRot=%.4f zoom=%.2f\n", st.shakeOffset.x, st.shakeOffset.y, st.shakeRotation, st.zoomScale);
	std::printf("  slowMo=%.2f flashColor=0x%08X flashAlpha=%.2f isHitStop=%d\n", st.slowMotionScale, st.flashColor, st.flashAlpha, st.isHitStop ? 1 : 0);
}

//================================================================
// Haptics
//================================================================

static void menuVibrate() {
	float p = readFloat("power", 0.5f);
	float s = readFloat("seconds", 0.2f);
	FeelKit::Vibrate(p, s);
	std::printf("  -> Vibrate()\n");
}

static void menuVibratePattern() {
	std::string name = readString("pattern name", "default");
	FeelKit::VibratePattern(name.c_str());
	std::printf("  -> VibratePattern(%s)\n", name.c_str());
}

static void menuVibrateFromSound() {
	std::string path = readString("WAV path", "BGM.wav");
	FeelKit::VibrateFromSound(path.c_str());
	std::printf("  -> VibrateFromSound()\n");
}

//================================================================
// Visual Effects
//================================================================

static void menuPlayEffect() {
	int tex = readInt("textureHandle", 1);
	float x = readFloat("x", 400.0f);
	float y = readFloat("y", 300.0f);
	int fc = readInt("frameCount", 1);
	float sec = readFloat("seconds", 0.5f);
	int loop = readInt("loop (0/1)", 0);
	int id = FeelKit::PlayEffect(tex, x, y, fc, sec, loop != 0);
	std::printf("  -> PlayEffect id=%d\n", id);
}

static void menuPlayEffectDesc() {
	FeelKit::EffectDesc d;
	d.textureHandle = readInt("textureHandle", 1);
	d.frameCount = readInt("frameCount", 1);
	d.seconds = readFloat("seconds", 0.5f);
	d.loop = readInt("loop (0/1)", 0) != 0;
	float x = readFloat("x", 400.0f);
	float y = readFloat("y", 300.0f);
	int id = FeelKit::PlayEffect(d, FeelKit::Vec2{x, y});
	std::printf("  -> PlayEffect id=%d\n", id);
}

static void menuLoadEffect() {
	std::string name = readString("effect name", "test_effect");
	FeelKit::EffectDesc d;
	d.textureHandle = readInt("textureHandle", 1);
	d.frameCount = readInt("frameCount", 4);
	d.seconds = readFloat("seconds", 0.5f);
	bool ok = FeelKit::LoadEffect(name.c_str(), d);
	std::printf("  -> LoadEffect: %s\n", ok ? "OK" : "FAIL");
}

static void menuPlayEffectByName() {
	std::string name = readString("effect name", "test_effect");
	float x = readFloat("x", 400.0f);
	float y = readFloat("y", 300.0f);
	int id = FeelKit::PlayEffect(name.c_str(), FeelKit::Vec2{x, y});
	std::printf("  -> PlayEffect id=%d\n", id);
}

static void menuPlayEffectLod() {
	std::string name = readString("effect name", "test_effect");
	float x = readFloat("x", 400.0f);
	float y = readFloat("y", 300.0f);
	float z = readFloat("z", 0.0f);
	float dist = readFloat("distance", 300.0f);
	int id = FeelKit::PlayEffectLod(name.c_str(), FeelKit::Vec3{x, y, z}, dist);
	std::printf("  -> PlayEffectLod id=%d\n", id);
}

static void menuEffectPoolStatus() {
	std::printf("  Active effects: %d\n", FeelKit::EffectPoolGetActiveCount());
}

static void menuEffectPoolKillAll() {
	FeelKit::EffectPoolKillAll();
	std::printf("  All effects killed\n");
}

static void menuSpawnDecal() {
	FeelKit::DecalDesc d;
	d.textureName = "decal_tex";
	d.positionX = readFloat("position.x", 400.0f);
	d.positionY = readFloat("position.y", 300.0f);
	d.positionZ = readFloat("position.z", 0.0f);
	d.scale = readFloat("scale", 1.0f);
	d.lifeSeconds = readFloat("lifeSeconds", 5.0f);
	int id = FeelKit::SpawnDecal(d);
	std::printf("  -> Decal spawned: id=%d\n", id);
}

static void menuCreateTrail() {
	FeelKit::TrailDesc d;
	d.lifeSeconds = readFloat("lifeSeconds", 0.5f);
	d.width = readFloat("width", 2.0f);
	d.maxPoints = readInt("maxPoints", 100);
	TrailHandle h = FeelKit::CreateTrail(d);
	std::printf("  -> Trail created: handle=%d\n", h);
}

static void menuCreateAfterImage() {
	FeelKit::AfterImageDesc d;
	d.intervalSeconds = readFloat("intervalSeconds", 0.05f);
	d.lifeSeconds = readFloat("lifeSeconds", 0.3f);
	d.fadeSpeed = readFloat("fadeSpeed", 1.0f);
	AfterImageHandle h = FeelKit::CreateAfterImage(d);
	std::printf("  -> AfterImage created: handle=%d\n", h);
}

static void menuEmit() {
	FeelKit::EmitterDesc d;
	d.shape = EmitterShape::circle;
	d.radius = readFloat("radius", 1.0f);
	d.particleDesc.spawnRate = readFloat("spawnRate", 10.0f);
	d.particleDesc.size = readFloat("size", 4.0f);
	d.particleDesc.initialSpeed = readFloat("initialSpeed", 2.0f);
	d.particleDesc.lifeSeconds = readFloat("lifeSeconds", 2.0f);
	d.particleDesc.colorRgba = 0xFFFFFFFFu;
	EmitterHandle h = FeelKit::Emit(d);
	std::printf("  -> Emitter created: handle=%d\n", h);
}

static void menuAddScreenEffect() {
	FeelKit::ScreenEffectDesc d;
	d.effectName = "test_screen_effect";
	d.intensity = readFloat("intensity", 1.0f);
	d.durationSeconds = readFloat("durationSeconds", 1.0f);
	FeelKit::AddScreenEffect(d);
	std::printf("  -> ScreenEffect added\n");
}

//================================================================
// Impact Generation
//================================================================

static void menuAnalyzeImpact() {
	float v = readFloat("velocity", 5.0f);
	float m = readFloat("mass", 1.0f);
	FeelKit::ImpactAnalysisResult r = FeelKit::AnalyzeImpact(v, m);
	std::printf("  power=%.4f  seconds=%.4f\n", r.power, r.seconds);
}

static void menuGenerateImpactHaptics() {
	float v = readFloat("velocity", 5.0f);
	float m = readFloat("mass", 1.0f);
	FeelKit::GenerateImpactHaptics(v, m);
	std::printf("  -> Vibrate()\n");
}

static void menuGenerateImpactShake() {
	float v = readFloat("velocity", 5.0f);
	float m = readFloat("mass", 1.0f);
	FeelKit::GenerateImpactShake(v, m);
	std::printf("  -> Shake()\n");
}

static void menuGenerateImpactFeedback() {
	float v = readFloat("velocity", 8.0f);
	float m = readFloat("mass", 1.0f);
	float a = readFloat("angle", 0.0f);
	FeelKit::ImpactFeedbackResult r = FeelKit::GenerateImpactFeedback(v, m, a);
	std::printf("  vibration=%.4f shake=%.4f hitStop=%.4f\n", r.vibrationPower, r.shakePower, r.hitStopSeconds);
	std::printf("  seVol=%.4f particle=%.4f\n", r.seVolume, r.particleIntensity);
}

static void menuGenerateExplosionFeedback() {
	float px = readFloat("position.x", 0.0f);
	float py = readFloat("position.y", 0.0f);
	float pz = readFloat("position.z", 0.0f);
	float r = readFloat("radius", 5.0f);
	float pow = readFloat("power", 3.0f);
	FeelKit::ExplosionFeedbackResult res = FeelKit::GenerateExplosionFeedback(FeelKit::Vec3{px, py, pz}, r, pow);
	std::printf("  vibe=%.4f shake=%.4f se=%.4f effect=%.4f\n", res.vibrationPower, res.shakePower, res.seVolume, res.effectIntensity);
}

static void menuPlayImpact() {
	float x = readFloat("position.x", 0.0f);
	float y = readFloat("position.y", 0.0f);
	float z = readFloat("position.z", 0.0f);
	float intensity = readFloat("intensity", 0.5f);
	FeelKit::PlayImpact(FeelKit::Vec3{x, y, z}, intensity);
	std::printf("  Done\n");
}

static void menuPlayExplosion() {
	float x = readFloat("position.x", 0.0f);
	float y = readFloat("position.y", 0.0f);
	float z = readFloat("position.z", 0.0f);
	float r = readFloat("radius", 5.0f);
	float pow = readFloat("power", 3.0f);
	FeelKit::PlayExplosion(FeelKit::Vec3{x, y, z}, r, pow);
	std::printf("  Done\n");
}

//================================================================
// Animation → Haptics
//================================================================

static void menuGenerateHapticsFromAnimation() {
	std::string name = readString("animation name", "run");
	FeelKit::GenerateHapticsFromAnimation(name.c_str());
	std::printf("  -> Timeline queued\n");
}

static void menuGenerateFootsteps() {
	std::string name = readString("animation name", "walk");
	FeelKit::GenerateFootsteps(name.c_str());
	std::printf("  -> Timeline queued\n");
}

static void menuSetAnimationPattern() {
	std::string name = readString("animation name", "custom");
	FeelKit::AnimationHapticPattern pat;
	pat.continuousPower = readFloat("continuousPower", 0.5f);
	pat.continuousDuration = readFloat("continuousDuration", 0.1f);
	pat.footstepInterval = readFloat("footstepInterval", 0.0f);
	pat.footstepPower = readFloat("footstepPower", 0.3f);
	FeelKit::SetAnimationHapticPattern(name.c_str(), pat);
	std::printf("  -> Registered\n");
}

//================================================================
// Image / Text
//================================================================

static void menuSpawnTextEffect() {
	std::string text = readString("text", "Hello FeelKit!");
	FeelKit::SpawnTextEffect(text.c_str());
	std::printf("  -> TextEffect spawned\n");
}

//================================================================
// Timeline / Sequence
//================================================================

static void menuPlaySequence() {
	int count = readInt("event count", 4);
	std::vector<FeelKit::SequenceEvent> events;
	events.reserve(static_cast<size_t>(count));
	for (int i = 0; i < count; i++) {
		FeelKit::SequenceEvent ev;
		ev.timeSeconds = readFloat("timeSeconds", static_cast<float>(i) * 0.3f);
		std::string name = readString("eventName (vibrate/shake/hit_stop)", "vibrate");
		static std::vector<std::string> nameStorage;
		nameStorage.push_back(name);
		ev.eventName = nameStorage.back().c_str();
		ev.param1 = readFloat("param1 (power/scale)", 0.5f);
		ev.param2 = readFloat("param2 (seconds)", 0.1f);
		events.push_back(ev);
	}
	FeelKit::PlaySequence(events.data(), count);
	std::printf("  -> Timeline queued (%d events), running for 3 seconds...\n", count);
	for (int i = 0; i < 180; i++) {
		FeelKit::Update(1.0f / 60.0f);
	}
	std::printf("  Done\n");
}

//================================================================
// Debug
//================================================================

static void menuShowHapticTimeline() {
	FeelKit::ShowHapticTimeline();
}

static void menuShowEffectDebugger() {
	FeelKit::ShowEffectDebugger();
}

static void menuDrawAISenses() {
	FeelKit::AISenseDebugDesc d;
	d.position = FeelKit::Vec3{0, 0, 0};
	d.sightRange = readFloat("sightRange", 10.0f);
	d.hearingRange = readFloat("hearingRange", 20.0f);
	d.alertLevel = readInt("alertLevel", 3);
	FeelKit::DrawAISenses(d);
}

static void menuApplyOcclusion() {
	float lx = readFloat("listener.x", 0.0f);
	float ly = readFloat("listener.y", 0.0f);
	float lz = readFloat("listener.z", 0.0f);
	float sx = readFloat("source.x", 10.0f);
	float sy = readFloat("source.y", 0.0f);
	float sz = readFloat("source.z", 0.0f);
	FeelKit::ApplyOcclusion(FeelKit::Vec3{lx, ly, lz}, FeelKit::Vec3{sx, sy, sz});
}

//================================================================
// カテゴリメニュー
//================================================================

static const MenuItem kAudioAnalysisItems[] = {
	{1, "AnalyzeAudioFile", menuAnalyzeAudioFile},
	{2, "AnalyzeAudioFeatures", menuAnalyzeAudioFeatures},
};

static void menuAudioAnalysis() {
	showMenu("Audio Analysis", kAudioAnalysisItems, 2);
}

static const MenuItem kAudioConvertItems[] = {
	{1, "MakeHapticsFromAudio + Vibrate", menuMakeHapticsFromAudio},
	{2, "MakeShakeFromAudio + Shake", menuMakeShakeFromAudio},
	{3, "MakeFlashFromAudio + Flash", menuMakeFlashFromAudio},
	{4, "MakeParticlesFromAudio + Emit", menuMakeParticlesFromAudio},
	{5, "MakeSlowMotionFromAudio + SlowMotion", menuMakeSlowMotionFromAudio},
	{6, "PlayHapticFromAudio (one-shot)", menuPlayHapticFromAudio},
	{7, "PlayShakeFromAudio (one-shot)", menuPlayShakeFromAudio},
	{8, "PlayFlashFromAudio (one-shot)", menuPlayFlashFromAudio},
};

static void menuAudioConvert() {
	showMenu("Audio → Effect Conversion", kAudioConvertItems, 8);
}

static const MenuItem kAudioEffectItems[] = {
	{1, "CreateLightFromAudio", menuCreateLightFromAudio},
	{2, "CreateCameraMotionFromAudio", menuCreateCameraMotionFromAudio},
	{3, "CreateSlowMotionFromAudio", menuCreateSlowMotionFromAudio},
	{4, "SpawnEffectFromAudio", menuSpawnEffectFromAudio},
	{5, "PlaySE", menuPlaySE},
	{6, "PlayBGM / StopBGM", menuPlayBGM},
	{7, "SetMasterVolume / GetMasterVolume", menuSetMasterVolume},
};

static void menuAudioEffects() {
	showMenu("Audio Playback & Other", kAudioEffectItems, 7);
}

static const MenuItem kScreenItems[] = {
	{1, "Shake", menuShake},
	{2, "Flash", menuFlash},
	{3, "Zoom", menuZoom},
	{4, "HitStop", menuHitStop},
	{5, "SlowMotion", menuSlowMotion},
	{6, "GetScreenState", menuGetScreenState},
};

static void menuScreen() {
	showMenu("Screen Effects", kScreenItems, 6);
}

static const MenuItem kHapticItems[] = {
	{1, "Vibrate", menuVibrate},
	{2, "VibratePattern", menuVibratePattern},
	{3, "VibrateFromSound", menuVibrateFromSound},
};

static void menuHaptics() {
	showMenu("Haptics / Vibration", kHapticItems, 3);
}

static const MenuItem kVisualEffectItems[] = {
	{1, "PlayEffect (params)", menuPlayEffect},
	{2, "PlayEffect (EffectDesc)", menuPlayEffectDesc},
	{3, "LoadEffect & PlayEffect by name", menuLoadEffect},
	{4, "PlayEffectLod", menuPlayEffectLod},
	{5, "EffectPool: status", menuEffectPoolStatus},
	{6, "EffectPool: kill all", menuEffectPoolKillAll},
	{7, "SpawnDecal", menuSpawnDecal},
	{8, "CreateTrail", menuCreateTrail},
	{9, "CreateAfterImage", menuCreateAfterImage},
	{10, "Emit (particle emitter)", menuEmit},
	{11, "AddScreenEffect", menuAddScreenEffect},
};

static void menuVisualEffects() {
	showMenu("Visual Effects", kVisualEffectItems, 11);
}

static const MenuItem kImpactItems[] = {
	{1, "AnalyzeImpact", menuAnalyzeImpact},
	{2, "GenerateImpactHaptics", menuGenerateImpactHaptics},
	{3, "GenerateImpactShake", menuGenerateImpactShake},
	{4, "GenerateImpactFeedback", menuGenerateImpactFeedback},
	{5, "GenerateExplosionFeedback", menuGenerateExplosionFeedback},
	{6, "PlayImpact", menuPlayImpact},
	{7, "PlayExplosion", menuPlayExplosion},
};

static void menuImpactGen() {
	showMenu("Impact Generation", kImpactItems, 7);
}

static const MenuItem kAnimationItems[] = {
	{1, "GenerateHapticsFromAnimation", menuGenerateHapticsFromAnimation},
	{2, "GenerateFootsteps", menuGenerateFootsteps},
	{3, "SetAnimationHapticPattern", menuSetAnimationPattern},
};

static void menuAnimation() {
	showMenu("Animation → Haptics", kAnimationItems, 3);
}

static const MenuItem kTimelineItems[] = {
	{1, "PlaySequence (build + execute 3s)", menuPlaySequence},
};

static void menuTimeline() {
	showMenu("Timeline / Sequence", kTimelineItems, 1);
}

static const MenuItem kImageItems[] = {
	{1, "SpawnTextEffect", menuSpawnTextEffect},
	{2, "CreateParticlesFromAudio", menuMakeParticlesFromAudio},
};

static void menuImage() {
	showMenu("Image / Text / Audio Particles", kImageItems, 2);
}

static const MenuItem kDebugItems[] = {
	{1, "ShowHapticTimeline", menuShowHapticTimeline},
	{2, "ShowEffectDebugger", menuShowEffectDebugger},
	{3, "DrawAISenses", menuDrawAISenses},
	{4, "ApplyOcclusion", menuApplyOcclusion},
};

static void menuDebug() {
	showMenu("Debug & Visualization", kDebugItems, 4);
}

//================================================================
// トップメニュー
//================================================================

static const MenuItem kTopItems[] = {
	{1,  "Audio Analysis (AnalyzeAudioFile / Features)", menuAudioAnalysis},
	{2,  "Audio → Effect Conversion", menuAudioConvert},
	{3,  "Audio Playback & Other", menuAudioEffects},
	{4,  "Screen Effects (Shake/Flash/Zoom/HitStop/SlowMo)", menuScreen},
	{5,  "Haptics / Vibration", menuHaptics},
	{6,  "Visual Effects (Particle/Sprite/Trail/Decal)", menuVisualEffects},
	{7,  "Impact Generation", menuImpactGen},
	{8,  "Animation → Haptics", menuAnimation},
	{9,  "Image / Text / Audio Particles", menuImage},
	{10, "Timeline / Sequence", menuTimeline},
	{11, "Debug & Visualization", menuDebug},
};

//================================================================
// エントリ
//================================================================

int runInteractiveMenu() {
	while (true) {
		int sel = showMenu("FeelKit Interactive Menu", kTopItems, 11);
		if (sel == 0) break;
	}
	return 0;
}