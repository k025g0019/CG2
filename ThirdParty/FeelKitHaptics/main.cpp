#define _CRT_SECURE_NO_WARNINGS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <hidsdi.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <thread>
#include <vector>

#include <hidapi.h>
#include "libusb.h"
#include "FeelKit.h"
#include "FeelKitHaptics.h"
#include "FeelKitInteractiveMenu.h"

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ws2_32.lib")

constexpr WORD kWaveFormatPcm = 0x0001;
constexpr WORD kWaveFormatIeeeFloat = 0x0003;

//================================================================
// ファイル設定
//================================================================
constexpr wchar_t kAudioFilePath[] = L"BGM.wav";
constexpr uint16_t kNintendoVendorId = 0x057E;
constexpr uint16_t kSwitch2ProControllerProductId = 0x2069;

constexpr int kSilentThresholdDb = -40;
constexpr int kMaxRumbleAmplitude = 1003;
constexpr int kPulseThreshold = 36;
constexpr int kPulseAmplitudeBoost = 120;
constexpr int kMinRefreshIntervalMs = 4;
constexpr unsigned int kBulkTransferTimeoutMs = 100;
constexpr size_t kHidReportSize = 64;
constexpr int kActiveHapticThreshold = 48;
constexpr char kDebugLogFilePath[] = "runlog.txt";
constexpr int kPrimePacketCount = 12;
constexpr int kPrimePacketIntervalMs = 4;
constexpr DWORD kHidWriteTimeoutMs = 100;
constexpr bool kUseHidSendExperiment = true;
constexpr unsigned short kWebServerStartPort = 8765;
constexpr unsigned short kWebServerEndPort = 8775;
constexpr char kFrameScriptPath[] = "/frames.js";
constexpr char kAudioHttpPath[] = "/BGM.wav";

auto kWebHidPlayerHtmlPart1 = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>FeelKit 振動調整</title>
  <style>
    body {
      margin: 0;
      background: #10141c;
      color: #f2f4f8;
      font-family: "Segoe UI", sans-serif;
    }
    main {
      max-width: 920px;
      margin: 0 auto;
      padding: 32px 20px 48px;
    }
    h1 {
      margin: 0 0 8px;
      font-size: 32px;
    }
    p {
      line-height: 1.6;
      color: #c7d0dc;
    }
    .panel {
      background: #171d28;
      border: 1px solid #283142;
      border-radius: 16px;
      padding: 20px;
      margin-top: 20px;
    }
    .buttonRow {
      display: flex;
      flex-wrap: wrap;
      gap: 12px;
      margin-top: 16px;
    }
    button {
      border: none;
      border-radius: 999px;
      background: #3d7eff;
      color: #ffffff;
      padding: 12px 20px;
      font-size: 16px;
      cursor: pointer;
    }
    button.secondary {
      background: #3a4456;
    }
    button:disabled {
      background: #2a3240;
      color: #7f8b9f;
      cursor: not-allowed;
    }
    .statusGrid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
      gap: 12px;
      margin-top: 16px;
    }
    .statusCard {
      background: #111722;
      border-radius: 12px;
      padding: 16px;
    }
    .statusTitle {
      font-size: 12px;
      letter-spacing: 0.08em;
      text-transform: uppercase;
      color: #8fa1ba;
    }
    .statusValue {
      margin-top: 8px;
      font-size: 18px;
    }
    pre {
      background: #0b1018;
      border-radius: 12px;
      padding: 16px;
      overflow: auto;
      white-space: pre-wrap;
      word-break: break-word;
      min-height: 220px;
      margin-top: 16px;
    }
    .tuningGrid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
      gap: 12px;
      margin-top: 16px;
    }
    .controlGroup {
      background: #111722;
      border-radius: 12px;
      padding: 14px;
    }
    .controlHeader {
      display: flex;
      justify-content: space-between;
      gap: 12px;
      font-size: 14px;
      color: #d9e2ef;
    }
    .controlGroup input[type="range"] {
      width: 100%;
      margin-top: 12px;
    }
    .controlHint {
      margin-top: 8px;
      font-size: 12px;
      line-height: 1.5;
      color: #9fb0c8;
    }
  </style>
</head>
<body>
  <main>
    <h1>FeelKit 振動調整</h1>
    <p>1. HID出力を有効化  2. HID接続  3. 同期再生 の順で押してください。</p>

    <section class="panel">
      <div class="buttonRow">
        <button id="connectUsbButton">HID出力を有効化</button>
        <button id="connectHidButton">HID接続</button>
        <button id="playButton" disabled>同期再生</button>
        <button id="stopButton" class="secondary" disabled>停止</button>
      </div>

      <div class="buttonRow">
        <input id="audioFileInput" type="file" accept="audio/*">
      </div>

      <div class="buttonRow">
        <label for="tuningPresetSelect">既定セット</label>
        <select id="tuningPresetSelect">
          <option value="music">music</option>
          <option value="impact">impact</option>
          <option value="switchFine">switchFine</option>
          <option value="xboxStable">xboxStable</option>
        </select>
        <button id="applyTuningPresetButton" class="secondary">既定セットを適用</button>
        <button id="saveCustomPresetButton" class="secondary">現在値をcustom保存</button>
        <button id="saveCustomSwitchPresetButton" class="secondary">customSwitch保存</button>
        <button id="saveCustomXboxPresetButton" class="secondary">customXbox保存</button>
        <span id="currentTuningPresetStatus">現在: manual</span>
      </div>
		<div class="statusGrid">
        <div class="statusCard">
          <div class="statusTitle">USB</div>
          <div id="usbStatus" class="statusValue">未接続</div>
        </div>
        <div class="statusCard">
          <div class="statusTitle">HID</div>
          <div id="hidStatus" class="statusValue">未接続</div>
        </div>
        <div class="statusCard">
  <div class="statusTitle">再生</div>
		  <div id="playbackStatus" class="statusValue">待機</div>
		</div>

		<div class="statusCard">
		  <div class="statusTitle">再生時間</div>
		  <div id="playbackTimeStatus" class="statusValue">00:00.000</div>
		</div>
        <div class="statusCard">
          <div class="statusTitle">曲</div>
          <div id="trackStatus" class="statusValue">BGM.wav</div>
        </div>
        <div class="statusCard">
          <div class="statusTitle">Switch要約</div>
          <div id="switchPreviewStatus" class="statusValue">未解析</div>
        </div>
        <div class="statusCard">
          <div class="statusTitle">Xbox要約</div>
          <div id="xboxPreviewStatus" class="statusValue">未解析</div>
        </div>
      </div>
		<div class="controlGroup">
		  <div class="controlHeader">
		    <span>リアルタイム周波数表示</span>
		    <span>0〜500Hz</span>
		  </div>
		  <canvas id="spectrumCanvas" width="860" height="180"></canvas>
		  <div class="controlHint">左が低音、右が高音。音に合わせてバーが上下します。</div>
		</div>
      <pre id="logBox">準備完了。</pre>
    </section>
      <div class="tuningGrid">
        <div class="controlGroup">
          <div class="controlHeader"><span>振幅カーブ</span><span id="shapeExponentValue">1.85</span></div>
          <input id="shapeExponentInput" type="range" min="1.00" max="2.20" step="0.05" value="1.85">
          <div class="controlHint">小さい: 弱い音も出やすい / 大きい: 弱い音が出にくい</div>
        </div>
        <div class="controlGroup">
          <div class="controlHeader"><span>有効しきい値</span><span id="activeThresholdValue">16</span></div>
          <input id="activeThresholdInput" type="range" min="0" max="96" step="1" value="16">
          <div class="controlHint">小さい: 振動しやすい / 大きい: 小さい音を切る</div>
        </div>
        <div class="controlGroup">
          <div class="controlHeader"><span>左 最小振動</span><span id="leftMinAmpValue">72</span></div>
          <input id="leftMinAmpInput" type="range" min="0" max="128" step="1" value="72">
          <div class="controlHint">小さい: 弱くなる / 大きい: 低音が常に強くなりやすい</div>
        </div>
        <div class="controlGroup">
          <div class="controlHeader"><span>右 最小振動</span><span id="rightMinAmpValue">64</span></div>
          <input id="rightMinAmpInput" type="range" min="0" max="128" step="1" value="64">
          <div class="controlHint">小さい: 異音が減りやすい / 大きい: 高音が鳴きやすい</div>
        </div>
        <div class="controlGroup">
          <div class="controlHeader"><span>右 細かさ 最小</span><span id="rightCadenceMinValue">2</span></div>
          <input id="rightCadenceMinInput" type="range" min="1" max="4" step="1" value="2">
          <div class="controlHint">小さい: 細かい / 大きい: 粗い</div>
        </div>
        <div class="controlGroup">
          <div class="controlHeader"><span>右 細かさ 最大</span><span id="rightCadenceMaxValue">5</span></div>
          <input id="rightCadenceMaxInput" type="range" min="2" max="6" step="1" value="5">
          <div class="controlHint">小さい: 全体に細かくなる / 大きい: 全体に粗くなる</div>
        </div>
        <div class="controlGroup">
          <div class="controlHeader"><span>右 1発の長さ</span><span id="rightPulseWidthMaxValue">3</span></div>
          <input id="rightPulseWidthMaxInput" type="range" min="1" max="4" step="1" value="3">
          <div class="controlHint">小さい: 短く細かい / 大きい: 長く重い</div>
        </div>
        <div class="controlGroup">
          <div class="controlHeader"><span>右 診断振幅</span><span id="testRightAmpValue">96</span></div>
          <input id="testRightAmpInput" type="range" min="0" max="256" step="1" value="96">
          <div class="controlHint">小さい: 安全寄り / 大きい: 異音の境界を探りやすい</div>
        </div>
        <div class="controlGroup">
          <div class="controlHeader"><span>振動 下限</span><span id="outputMinAmpValue">0</span></div>
          <input id="outputMinAmpInput" type="range" min="0" max="256" step="1" value="0">
          <div class="controlHint">小さい: 弱い音まで残る / 大きい: 小さい音を持ち上げる</div>
        </div>
        <div class="controlGroup">
          <div class="controlHeader"><span>振動 上限</span><span id="outputMaxAmpValue">1003</span></div>
          <input id="outputMaxAmpInput" type="range" min="64" max="2000" step="1" value="1003">
          <div class="controlHint">小さい: 全体を抑える / 大きい: 強い音までそのまま出す</div>
        </div>
        <div class="controlGroup">
          <div class="controlHeader"><span>プリセット数 最小</span><span id="presetCountMinValue">8</span></div>
          <input id="presetCountMinInput" type="range" min="4" max="24" step="1" value="8">
          <div class="controlHint">小さい: 差が大きい / 大きい: 細かい差が出る</div>
        </div>
        <div class="controlGroup">
          <div class="controlHeader"><span>プリセット数 最大</span><span id="presetCountMaxValue">24</span></div>
          <input id="presetCountMaxInput" type="range" min="8" max="45" step="1" value="24">
          <div class="controlHint">小さい: 安全寄り / 大きい: 表現は増えるが異音も出やすい</div>
        </div>
        <div class="controlGroup">
          <div class="controlHeader"><span>プリセット数 固定</span><span id="fixedPresetCountValue">16</span></div>
          <input id="fixedPresetCountInput" type="range" min="4" max="45" step="1" value="16">
          <div class="controlHint">固定を使う時の数。8/16/24/45 で比べると境界を探りやすい</div>
        </div>
        <div class="controlGroup">
          <div class="controlHeader"><span>プリセット数 固定モード</span><span id="fixedPresetEnabledValue">OFF</span></div>
          <input id="fixedPresetEnabledInput" type="checkbox">
          <div class="controlHint">OFF: 最小〜最大の範囲で自動 / ON: 固定数だけを使う</div>
        </div>
      </div>

      <div class="buttonRow">
        <button id="testRightNoiseButton" class="secondary">右だけ診断</button>
        <button id="resetTuningButton" class="secondary">調整を初期化</button>
      </div>

      <div class="tuningGrid">
        <div class="controlGroup">
          <div class="controlHeader"><span>衝撃の鋭さ</span><span id="layerTransientOnsetValue">8.5</span></div>
          <input id="layerTransientOnsetInput" type="range" min="2.0" max="14.0" step="0.1" value="8.5">
          <div class="controlHint">小さい: 丸い / 大きい: アタックが強い</div>
        </div>
        <div class="controlGroup">
          <div class="controlHeader"><span>低音の残り方</span><span id="layerBodyCarryValue">0.86</span></div>
          <input id="layerBodyCarryInput" type="range" min="0.40" max="0.98" step="0.01" value="0.86">
          <div class="controlHint">小さい: すぐ切れる / 大きい: 余韻が残る</div>
        </div>
        <div class="controlGroup">
          <div class="controlHeader"><span>高音の細かさ</span><span id="layerTextureRiseValue">5.2</span></div>
          <input id="layerTextureRiseInput" type="range" min="0.5" max="10.0" step="0.1" value="5.2">
          <div class="controlHint">小さい: おとなしい / 大きい: 細かい質感が出る</div>
        </div>
        <div class="controlGroup">
          <div class="controlHeader"><span>高音の残り方</span><span id="layerTextureCarryValue">0.58</span></div>
          <input id="layerTextureCarryInput" type="range" min="0.20" max="0.90" step="0.01" value="0.58">
          <div class="controlHint">小さい: すぐ切れる / 大きい: 細かさが続く</div>
        </div>
        <div class="controlGroup">
          <div class="controlHeader"><span>周期の出やすさ</span><span id="layerRhythmPeriodicityValue">0.9</span></div>
          <input id="layerRhythmPeriodicityInput" type="range" min="0.1" max="1.5" step="0.01" value="0.9">
          <div class="controlHint">小さい: 周期を無視 / 大きい: 回転や連打が出やすい</div>
        </div>
      </div>

      <div class="tuningGrid">
        <div class="controlGroup">
          <div class="controlHeader"><span>Switch 高音 安全しきい値</span><span id="switchTextureSafetyThresholdValue">0.56</span></div>
          <input id="switchTextureSafetyThresholdInput" type="range" min="0.20" max="0.90" step="0.01" value="0.56">
          <div class="controlHint">小さい: 安全補正が早く入る / 大きい: 高音をそのまま出しやすい</div>
        </div>
        <div class="controlGroup">
          <div class="controlHeader"><span>Switch 高音 安全クランプ</span><span id="switchTextureSafetyClampValue">0.88</span></div>
          <input id="switchTextureSafetyClampInput" type="range" min="0.50" max="1.00" step="0.01" value="0.88">
          <div class="controlHint">小さい: 異音を抑えやすい / 大きい: 高音の細かさを残しやすい</div>
        </div>
        <div class="controlGroup">
          <div class="controlHeader"><span>Switch 右 高音ブースト</span><span id="switchTextureRightBoostValue">22.0</span></div>
          <input id="switchTextureRightBoostInput" type="range" min="0.0" max="48.0" step="1.0" value="22.0">
          <div class="controlHint">小さい: おとなしい / 大きい: 高音の質感が出やすい</div>
        </div>
        <div class="controlGroup">
          <div class="controlHeader"><span>Xbox 左 衝撃ブースト</span><span id="xboxImpactLeftBoostValue">42.0</span></div>
          <input id="xboxImpactLeftBoostInput" type="range" min="0.0" max="96.0" step="1.0" value="42.0">
          <div class="controlHint">小さい: 軽い / 大きい: 衝撃が重くなる</div>
        </div>
        <div class="controlGroup">
          <div class="controlHeader"><span>Xbox 左 低音ブースト</span><span id="xboxBodyLeftBoostValue">24.0</span></div>
          <input id="xboxBodyLeftBoostInput" type="range" min="0.0" max="64.0" step="1.0" value="24.0">
          <div class="controlHint">小さい: すっきり / 大きい: 低音の押し出しが強い</div>
        </div>
        <div class="controlGroup">
          <div class="controlHeader"><span>Xbox 右 高音減衰</span><span id="xboxTextureDampingValue">0.82</span></div>
          <input id="xboxTextureDampingInput" type="range" min="0.40" max="1.00" step="0.01" value="0.82">
          <div class="controlHint">小さい: 高音を丸める / 大きい: 高音を残しやすい</div>
        </div>
      </div>
)HTML";
auto kWebHidPlayerHtmlPart1B = R"HTML(
      <div class="tuningGrid">
        <div class="controlGroup">
          <div class="controlHeader"><span>Switch 右偏り許容</span><span id="switchSummaryRightBalanceLimitValue">1.45</span></div>
          <input id="switchSummaryRightBalanceLimitInput" type="range" min="1.00" max="2.00" step="0.01" value="1.45">
          <div class="controlHint">小さい: 右を早めに抑える / 大きい: 右を残しやすい</div>
        </div>
        <div class="controlGroup">
          <div class="controlHeader"><span>Switch 右自動抑制</span><span id="switchSummaryRightClampScaleValue">0.90</span></div>
          <input id="switchSummaryRightClampScaleInput" type="range" min="0.70" max="1.00" step="0.01" value="0.90">
          <div class="controlHint">小さい: 強く抑える / 大きい: ほぼ抑えない</div>
        </div>
        <div class="controlGroup">
          <div class="controlHeader"><span>Xbox 左不足しきい値</span><span id="xboxSummaryLeftMinAverageValue">110</span></div>
          <input id="xboxSummaryLeftMinAverageInput" type="range" min="40" max="220" step="1" value="110">
          <div class="controlHint">小さい: 補正しにくい / 大きい: 左を持ち上げやすい</div>
        </div>
        <div class="controlGroup">
          <div class="controlHeader"><span>Xbox 左自動補正</span><span id="xboxSummaryLeftBoostScaleValue">1.08</span></div>
          <input id="xboxSummaryLeftBoostScaleInput" type="range" min="1.00" max="1.30" step="0.01" value="1.08">
          <div class="controlHint">小さい: ほぼ補正しない / 大きい: 左を持ち上げる</div>
        </div>
        <div class="controlGroup">
          <div class="controlHeader"><span>Xbox 右偏り許容</span><span id="xboxSummaryRightBalanceLimitValue">0.95</span></div>
          <input id="xboxSummaryRightBalanceLimitInput" type="range" min="0.70" max="1.20" step="0.01" value="0.95">
          <div class="controlHint">小さい: 右を早めに抑える / 大きい: 右を残しやすい</div>
        </div>
        <div class="controlGroup">
          <div class="controlHeader"><span>Xbox 右自動抑制</span><span id="xboxSummaryRightClampScaleValue">0.90</span></div>
          <input id="xboxSummaryRightClampScaleInput" type="range" min="0.70" max="1.00" step="0.01" value="0.90">
          <div class="controlHint">小さい: 強く抑える / 大きい: ほぼ抑えない</div>
        </div>
      </div>

      

    <audio id="audioPlayer" src="/BGM.wav" preload="auto"></audio>
  </main>

  <script src="/frames.js"></script>
  <script>
)HTML";
auto kWebHidPlayerHtmlPart2 = R"HTML(
    const vendorId = 0x057E;
    const productId = 0x2069;
    const usbInterfaceNumber = 1;
    const frameIntervalMs = window.konntoConfig.frameIntervalMs;
    let vibrationFrames = window.konntoConfig.frames.slice();
    const initCommands = window.konntoConfig.initCommands;
    const hapticPatternTable = window.konntoConfig.hapticPatternTable;
    const defaultActiveHapticThreshold = window.konntoConfig.activeHapticThreshold;

    let currentUsbDevice = null;
    let currentUsbEndpoint = null;
    let currentHidDevice = null;
    let playbackTimer = null;
    let playbackSessionId = 0;
    let vibrationCounter = 0;
    let leftPatternPhase = 0;
    let rightPatternPhase = 0;
    let currentFrameIndex = 0;
    let selectedAudioObjectUrl = null;
    let currentAudioArrayBuffer = null;
    let currentDecodedAudioBuffer = null;
    let playbackAudioContext = null;
    let playbackAudioSource = null;
	let spectrumAnalyser = null;
	let spectrumDataArray = null;
	let spectrumAnimationId = null;
    let playbackStartPerformanceMs = 0.0;
    let lastExtractedFeatureFrames = [];
    let lastFeatureStatistics = null;
    let lastLayeredHapticFrames = [];
    let currentLeftPhaseStep = 1;
    let currentRightPhaseStep = 1;
    let currentLeftToneOffset = 0.0;
    let currentRightToneOffset = 0.0;
    let currentXboxGamepadIndex = -1;
    let currentOutputDeviceKind = "none";
    let currentHapticsProfileName = "adaptive";
    let currentTuningPresetName = "manual";
    const hapticsProfiles = {
      adaptive: {
        impactScale: 1.0,
        bodyScale: 1.0,
        textureScale: 1.0,
        rhythmScale: 1.0,
        rightBoostScale: 1.0,
        leftBoostScale: 1.0
      },
      music: {
        impactScale: 1.0,
        bodyScale: 1.0,
        textureScale: 1.0,
        rhythmScale: 0.8,
        rightBoostScale: 1.0,
        leftBoostScale: 1.0
      },
      impact: {
        impactScale: 1.35,
        bodyScale: 1.15,
        textureScale: 0.7,
        rhythmScale: 0.55,
        rightBoostScale: 0.8,
        leftBoostScale: 1.25
      },
      rotor: {
        impactScale: 0.7,
        bodyScale: 0.95,
        textureScale: 0.7,
        rhythmScale: 1.45,
        rightBoostScale: 0.75,
        leftBoostScale: 1.15
      },
      engine: {
        impactScale: 0.85,
        bodyScale: 1.2,
        textureScale: 0.55,
        rhythmScale: 1.3,
        rightBoostScale: 0.65,
        leftBoostScale: 1.2
      }
    };
)HTML";
auto kWebHidPlayerHtmlPart2P1 = R"HTML(

    const defaultOutputDeviceProfiles = {
      switch: {
        leftScale: 1.0,
        rightScale: 1.0,
        leftFloor: 0,
        rightFloor: 0,
        textureSafetyThreshold: 0.56,
        textureSafetyClamp: 0.88,
        impactRightBoost: 18.0,
        textureRightBoost: 22.0,
        summaryRightBalanceLimit: 1.45,
        summaryRightClampScale: 0.90
      },
      xbox: {
        leftScale: 0.9,
        rightScale: 0.78,
        leftFloor: 0,
        rightFloor: 0,
        impactLeftBoost: 42.0,
        bodyLeftBoost: 24.0,
        textureRightBoost: 12.0,
        textureDamping: 0.82,
        summaryLeftMinAverage: 110.0,
        summaryLeftBoostScale: 1.08,
        summaryRightBalanceLimit: 0.95,
        summaryRightClampScale: 0.90
      }
    };
    const outputDeviceProfiles = JSON.parse(JSON.stringify(defaultOutputDeviceProfiles));
    const defaultAdaptiveTuning = {
      impactBase: 0.8,
      impactRange: 0.9,
      bodyBase: 0.85,
      bodyRange: 0.55,
      textureBase: 0.65,
      textureRange: 0.95,
      rhythmBase: 0.55,
      rhythmRange: 1.1,
      leftBoostBase: 0.75,
      leftBoostRange: 0.6,
      rightBoostBase: 0.7,
      rightBoostRange: 0.55
    };
    const adaptiveTuning = { ...defaultAdaptiveTuning };
   const defaultLayerTuning = {
	  transientOnsetWeight: 4.4,
	  transientHighRiseWeight: 2.2,
	  sustainLeftWeight: 0.62,
	  sustainRhythmWeight: 0.08,
	  sustainOnsetWeight: 0.06,
	  textureRightWeight: 1.05,
	  textureHighRiseWeight: 3.4,
	  textureBrightnessRiseWeight: 7.2,
	  textureBrightnessWeight: 0.34,
	  rhythmPeriodicityWeight: 0.24,
	  rhythmLeftWeight: 0.05,
	  rhythmOnsetWeight: 0.05,
	 impactCarry: 0.10,
	bodyCarry: 0.28,
	textureCarry: 0.14,
	rhythmCarry: 0.18
	};
    const layerTuning = { ...defaultLayerTuning };
    const defaultTuning = {
	  shapeExponent: 1.18,
	  activeThreshold: 10,
	  leftMinAmplitude: 0,
	  rightMinAmplitude: 0,
	  rightCadenceMin: 1,
	  rightCadenceMax: 4,
	  rightPulseWidthMax: 2,
	  testRightAmplitude: 96,
	  outputMinAmplitude: 0,
	  outputMaxAmplitude: 1003,
	  presetCountMin: 4,
	  presetCountMax: 45,
	  fixedPresetCount: 45,
	  fixedPresetEnabled: false
	};
    const runtimeTuning = { ...defaultTuning };

    const audioPlayer = document.getElementById("audioPlayer");
    const audioFileInput = document.getElementById("audioFileInput");
    const logBox = document.getElementById("logBox");
    const tuningFields = [
      ["shapeExponent", "shapeExponentInput", "shapeExponentValue"],
      ["activeThreshold", "activeThresholdInput", "activeThresholdValue"],
      ["leftMinAmplitude", "leftMinAmpInput", "leftMinAmpValue"],
      ["rightMinAmplitude", "rightMinAmpInput", "rightMinAmpValue"],
      ["rightCadenceMin", "rightCadenceMinInput", "rightCadenceMinValue"],
      ["rightCadenceMax", "rightCadenceMaxInput", "rightCadenceMaxValue"],
      ["rightPulseWidthMax", "rightPulseWidthMaxInput", "rightPulseWidthMaxValue"],
      ["testRightAmplitude", "testRightAmpInput", "testRightAmpValue"],
      ["outputMinAmplitude", "outputMinAmpInput", "outputMinAmpValue"],
      ["outputMaxAmplitude", "outputMaxAmpInput", "outputMaxAmpValue"],
      ["presetCountMin", "presetCountMinInput", "presetCountMinValue"],
      ["presetCountMax", "presetCountMaxInput", "presetCountMaxValue"],
      ["fixedPresetCount", "fixedPresetCountInput", "fixedPresetCountValue"]
    ];
    const layerTuningFields = [
      ["transientOnsetWeight", "layerTransientOnsetInput", "layerTransientOnsetValue"],
      ["bodyCarry", "layerBodyCarryInput", "layerBodyCarryValue"],
      ["textureBrightnessRiseWeight", "layerTextureRiseInput", "layerTextureRiseValue"],
      ["textureCarry", "layerTextureCarryInput", "layerTextureCarryValue"],
      ["rhythmPeriodicityWeight", "layerRhythmPeriodicityInput", "layerRhythmPeriodicityValue"]
    ];
    const outputDeviceProfileFields = [
      ["switch", "textureSafetyThreshold", "switchTextureSafetyThresholdInput", "switchTextureSafetyThresholdValue"],
      ["switch", "textureSafetyClamp", "switchTextureSafetyClampInput", "switchTextureSafetyClampValue"],
      ["switch", "textureRightBoost", "switchTextureRightBoostInput", "switchTextureRightBoostValue"],
      ["switch", "summaryRightBalanceLimit", "switchSummaryRightBalanceLimitInput", "switchSummaryRightBalanceLimitValue"],
      ["switch", "summaryRightClampScale", "switchSummaryRightClampScaleInput", "switchSummaryRightClampScaleValue"],
      ["xbox", "impactLeftBoost", "xboxImpactLeftBoostInput", "xboxImpactLeftBoostValue"],
      ["xbox", "bodyLeftBoost", "xboxBodyLeftBoostInput", "xboxBodyLeftBoostValue"],
      ["xbox", "textureDamping", "xboxTextureDampingInput", "xboxTextureDampingValue"],
      ["xbox", "summaryLeftMinAverage", "xboxSummaryLeftMinAverageInput", "xboxSummaryLeftMinAverageValue"],
      ["xbox", "summaryLeftBoostScale", "xboxSummaryLeftBoostScaleInput", "xboxSummaryLeftBoostScaleValue"],
      ["xbox", "summaryRightBalanceLimit", "xboxSummaryRightBalanceLimitInput", "xboxSummaryRightBalanceLimitValue"],
      ["xbox", "summaryRightClampScale", "xboxSummaryRightClampScaleInput", "xboxSummaryRightClampScaleValue"]
    ];

    const log = (message) => {
      logBox.textContent += message + "\n";
      logBox.scrollTop = logBox.scrollHeight;
    };

    const setStatus = (elementId, text) => {
  document.getElementById(elementId).textContent = text;
	};

	const formatPlaybackTime = (seconds) => {
	  const safeSeconds = Math.max(0, seconds);

	  const minutes =
	    Math.floor(safeSeconds / 60);

	  const remainSeconds =
	    Math.floor(safeSeconds % 60);

	  const milliseconds =
	    Math.floor((safeSeconds % 1) * 1000);

	  return (
	    String(minutes).padStart(2, "0") + ":" +
	    String(remainSeconds).padStart(2, "0") + "." +
	    String(milliseconds).padStart(3, "0")
	  );
	};
    const syncTuningPresetUi = () => {
      const presetSelectElement = document.getElementById("tuningPresetSelect");
      const presetStatusElement = document.getElementById("currentTuningPresetStatus");
      const presetNames = getAllTuningPresetNames();
      const previousValue = presetSelectElement.value;

      presetSelectElement.innerHTML = "";

      for (const presetName of presetNames) {
        const optionElement = document.createElement("option");
        optionElement.value = presetName;
        optionElement.textContent = presetName;
        presetSelectElement.appendChild(optionElement);
      }

      if (presetNames.includes(currentTuningPresetName)) {
        presetSelectElement.value = currentTuningPresetName;
      } else if (presetNames.includes(previousValue)) {
        presetSelectElement.value = previousValue;
      } else if (presetNames.length > 0) {
        presetSelectElement.value = presetNames[0];
      }

      presetStatusElement.textContent = "現在: " + currentTuningPresetName;
    };

    const isSwitchReady = () =>
      currentUsbDevice !== null &&
      currentUsbEndpoint !== null &&
      currentHidDevice !== null;

    const findXboxGamepad = () => {
      const gamepads = navigator.getGamepads ? navigator.getGamepads() : [];

      for (const gamepad of gamepads) {
        if (!gamepad) {
          continue;
        }

        const gamepadId = String(gamepad.id || "").toLowerCase();
        const hasVibrationActuator =
          gamepad.vibrationActuator !== undefined &&
          gamepad.vibrationActuator !== null &&
          typeof gamepad.vibrationActuator.playEffect === "function";

        if (hasVibrationActuator && gamepadId.includes("xbox")) {
          return gamepad;
        }
      }

      return null;
    };

    const isXboxReady = () => findXboxGamepad() !== null;

    const refreshOutputStatus = () => {
      if (isSwitchReady()) {
        currentOutputDeviceKind = "switch";
        setStatus("usbStatus", "接続");
        setStatus("hidStatus", "接続");
        return;
      }

      const xboxGamepad = findXboxGamepad();

      if (xboxGamepad !== null) {
        currentXboxGamepadIndex = xboxGamepad.index;
        currentOutputDeviceKind = "xbox";
        setStatus("usbStatus", "Xbox");
        setStatus("hidStatus", "通常振動");
        return;
      }

      currentXboxGamepadIndex = -1;
      currentOutputDeviceKind = "none";
      setStatus("usbStatus", "未接続");
      setStatus("hidStatus", "未接続");
    };

    const updateButtons = () => {
      refreshOutputStatus();
      const hasOutput = isSwitchReady() || isXboxReady();
      const isPlaying = playbackTimer !== null;

      document.getElementById("playButton").disabled = !hasOutput || isPlaying;
      document.getElementById("stopButton").disabled = !isPlaying;
    };

    const delay = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));
	const drawSpectrum = () => {
  const canvas = document.getElementById("spectrumCanvas");

  if (canvas === null || spectrumAnalyser === null || spectrumDataArray === null) {
    return;
  }

  const context = canvas.getContext("2d");

  spectrumAnalyser.getByteFrequencyData(spectrumDataArray);

  context.clearRect(0, 0, canvas.width, canvas.height);

  const sampleRate =
    playbackAudioContext !== null ? playbackAudioContext.sampleRate : 44100;

  const maxFrequencyHz = 500.0;
  const bandStepHz = 10.0;
  const bandCount = Math.floor(maxFrequencyHz / bandStepHz) + 1;
  const barWidth = canvas.width / bandCount;

  for (let bandIndex = 0; bandIndex < bandCount; bandIndex++) {
    const frequencyHz = bandIndex * bandStepHz;
    const binIndex = Math.round(
      frequencyHz / (sampleRate / 2.0) * (spectrumDataArray.length - 1)
    );

    const safeBinIndex =
      Math.max(0, Math.min(spectrumDataArray.length - 1, binIndex));

    let bandTotal = 0.0;
let bandSamples = 0;

const bandStartHz = Math.max(20.0, frequencyHz - bandStepHz * 0.5);
const bandEndHz = frequencyHz + bandStepHz * 0.5;

const startBin = Math.max(
  0,
  Math.floor(bandStartHz / (sampleRate / 2.0) * (spectrumDataArray.length - 1))
);

const endBin = Math.min(
  spectrumDataArray.length - 1,
  Math.ceil(bandEndHz / (sampleRate / 2.0) * (spectrumDataArray.length - 1))
);

for (let binIndex = startBin; binIndex <= endBin; binIndex++) {
  bandTotal += spectrumDataArray[binIndex];
  bandSamples++;
}

const rawValue =
  bandSamples > 0 ? bandTotal / bandSamples / 255.0 : 0.0;

const value = Math.pow(rawValue, 2.4);
const barHeight = value * canvas.height * 0.85;

    context.fillRect(
      bandIndex * barWidth,
      canvas.height - barHeight,
      Math.max(1, barWidth - 2),
      barHeight
    );
  }

  spectrumAnimationId = requestAnimationFrame(drawSpectrum);
};

const stopSpectrum = () => {
  if (spectrumAnimationId !== null) {
    cancelAnimationFrame(spectrumAnimationId);
    spectrumAnimationId = null;
  }
};
    const syncTuningUi = () => {
      for (const [key, inputId, valueId] of tuningFields) {
        const inputElement = document.getElementById(inputId);
        const valueElement = document.getElementById(valueId);
        inputElement.value = String(runtimeTuning[key]);
        valueElement.textContent = String(runtimeTuning[key]);
      }

      for (const [key, inputId, valueId] of layerTuningFields) {
        const inputElement = document.getElementById(inputId);
        const valueElement = document.getElementById(valueId);
        inputElement.value = String(layerTuning[key]);
        valueElement.textContent = String(layerTuning[key]);
      }

      for (const [deviceKind, key, inputId, valueId] of outputDeviceProfileFields) {
        const inputElement = document.getElementById(inputId);
        const valueElement = document.getElementById(valueId);
        inputElement.value = String(outputDeviceProfiles[deviceKind][key]);
        valueElement.textContent = String(outputDeviceProfiles[deviceKind][key]);
      }

      const fixedPresetEnabledInput = document.getElementById("fixedPresetEnabledInput");
      const fixedPresetEnabledValue = document.getElementById("fixedPresetEnabledValue");
      fixedPresetEnabledInput.checked = runtimeTuning.fixedPresetEnabled;
      fixedPresetEnabledValue.textContent = runtimeTuning.fixedPresetEnabled ? "ON" : "OFF";
    };
)HTML";
auto kWebHidPlayerHtmlPart2P2 = R"HTML(

    const updateTuningFromInputs = () => {
      for (const [key, inputId, valueId] of tuningFields) {
        const inputElement = document.getElementById(inputId);
        const numericValue = Number(inputElement.value);
        runtimeTuning[key] = numericValue;
        document.getElementById(valueId).textContent = String(numericValue);
      }

      runtimeTuning.rightCadenceMax =
        Math.max(runtimeTuning.rightCadenceMin, runtimeTuning.rightCadenceMax);
      document.getElementById("rightCadenceMaxInput").value = String(runtimeTuning.rightCadenceMax);
      document.getElementById("rightCadenceMaxValue").textContent = String(runtimeTuning.rightCadenceMax);

      runtimeTuning.outputMaxAmplitude =
        Math.max(runtimeTuning.outputMinAmplitude, runtimeTuning.outputMaxAmplitude);
      document.getElementById("outputMaxAmpInput").value = String(runtimeTuning.outputMaxAmplitude);
      document.getElementById("outputMaxAmpValue").textContent = String(runtimeTuning.outputMaxAmplitude);

      runtimeTuning.presetCountMax =
        Math.max(runtimeTuning.presetCountMin, runtimeTuning.presetCountMax);
      document.getElementById("presetCountMaxInput").value = String(runtimeTuning.presetCountMax);
      document.getElementById("presetCountMaxValue").textContent = String(runtimeTuning.presetCountMax);

      runtimeTuning.fixedPresetCount =
        Math.max(1, Math.min(runtimeTuning.fixedPresetCount, hapticPatternTable.length));
      document.getElementById("fixedPresetCountInput").value = String(runtimeTuning.fixedPresetCount);
      document.getElementById("fixedPresetCountValue").textContent = String(runtimeTuning.fixedPresetCount);

      runtimeTuning.fixedPresetEnabled =
        document.getElementById("fixedPresetEnabledInput").checked === true;
      document.getElementById("fixedPresetEnabledValue").textContent =
        runtimeTuning.fixedPresetEnabled ? "ON" : "OFF";
    };

    const updateLayerTuningFromInputs = () => {
      for (const [key, inputId, valueId] of layerTuningFields) {
        const inputElement = document.getElementById(inputId);
        const numericValue = Number(inputElement.value);
        layerTuning[key] = numericValue;
        document.getElementById(valueId).textContent = String(numericValue);
      }
    };

    const updateOutputDeviceProfilesFromInputs = () => {
      for (const [deviceKind, key, inputId, valueId] of outputDeviceProfileFields) {
        const inputElement = document.getElementById(inputId);
        const numericValue = Number(inputElement.value);
        outputDeviceProfiles[deviceKind][key] = numericValue;
        document.getElementById(valueId).textContent = String(numericValue);
      }
    };

    const rebuildFramesFromCurrentTrack = async () => {
      if (currentAudioArrayBuffer === null) {
        return;
      }

      await analyzeAudioArrayBuffer(currentAudioArrayBuffer);
      log("調整を反映しました。");
    };

    const getCurrentHapticsProfile = () => {
      const selectedProfile = hapticsProfiles[currentHapticsProfileName];

      if (selectedProfile) {
        return selectedProfile;
      }

      return hapticsProfiles.music;
    };

    const buildAdaptiveProfileFromFeatureFrame = (featureFrame, featureStatistics = null) => {
      const onsetReferenceValue =
        Math.max(0.0001, featureStatistics?.onsetP90 ?? 0.12);
      const highRiseReferenceValue =
        Math.max(0.0001, featureStatistics?.highRiseP90 ?? 0.06);
      const leftReferenceValue =
        Math.max(0.0001, featureStatistics?.leftRawP90 ?? 0.16);
      const rightReferenceValue =
        Math.max(0.0001, featureStatistics?.rightRawP90 ?? 0.16);
      const periodicityReferenceValue =
        Math.max(0.0001, featureStatistics?.periodicityP90 ?? 0.8);
      const brightnessRiseReferenceValue =
        Math.max(0.0001, featureStatistics?.brightnessRiseP90 ?? 0.08);
      const onsetNormalizedValue =
        Math.max(0.0, Math.min(1.0, featureFrame.onsetValue / onsetReferenceValue));
      const highRiseNormalizedValue =
        Math.max(0.0, Math.min(1.0, featureFrame.highRiseValue / highRiseReferenceValue));
      const leftNormalizedValue =
        Math.max(0.0, Math.min(1.0, featureFrame.leftRawValue / leftReferenceValue));
      const rightNormalizedValue =
        Math.max(0.0, Math.min(1.0, featureFrame.rightRawValue / rightReferenceValue));
      const periodicityNormalizedValue =
        Math.max(0.0, Math.min(1.0, featureFrame.periodicityValue / periodicityReferenceValue));
      const brightnessRiseNormalizedValue =
        Math.max(
          0.0,
          Math.min(1.0, featureFrame.brightnessRiseValue / brightnessRiseReferenceValue)
        );
      const impactAmount =
        Math.max(
          0.0,
          Math.min(
            1.0,
            onsetNormalizedValue * 0.72 + highRiseNormalizedValue * 0.38
          )
        );
      const bodyAmount =
        Math.max(0.0, Math.min(1.0, leftNormalizedValue * 0.95));
      const textureAmount =
        Math.max(
          0.0,
          Math.min(
            1.0,
            rightNormalizedValue * 0.78 +
            featureFrame.brightnessValue * 0.35 +
            brightnessRiseNormalizedValue * 0.52
          )
        );
      const rhythmAmount =
        Math.max(
          0.0,
          Math.min(1.0, periodicityNormalizedValue * 0.82 + onsetNormalizedValue * 0.18)
        );

      return {
        impactScale:
          adaptiveTuning.impactBase + impactAmount * adaptiveTuning.impactRange,
        bodyScale:
          adaptiveTuning.bodyBase + bodyAmount * adaptiveTuning.bodyRange,
        textureScale:
          adaptiveTuning.textureBase + textureAmount * adaptiveTuning.textureRange,
        rhythmScale:
          adaptiveTuning.rhythmBase + rhythmAmount * adaptiveTuning.rhythmRange,
        rightBoostScale:
          adaptiveTuning.rightBoostBase + textureAmount * adaptiveTuning.rightBoostRange,
        leftBoostScale:
          adaptiveTuning.leftBoostBase +
          Math.max(bodyAmount, rhythmAmount) * adaptiveTuning.leftBoostRange
      };
    };

    const shapeAmplitude = (value) => {
	  const clampedValue = Math.max(0.0, Math.min(1.0, value));
	  const curvedValue = Math.pow(clampedValue, runtimeTuning.shapeExponent);

	  return Math.max(
	    0,
	    Math.min(
	      runtimeTuning.outputMaxAmplitude,
	      Math.floor(curvedValue * runtimeTuning.outputMaxAmplitude)
	    )
	  );
	};
)HTML";
auto kWebHidPlayerHtmlPart2T = R"HTML(
   const clampAmplitude = (value) => {
	  return Math.max(0, Math.min(2000, Math.floor(value)));
	};

    const summarizeOutputFrames = (frames) => {
      if (!Array.isArray(frames) || frames.length <= 0) {
        return {
          frameCount: 0,
          activeRatio: 0.0,
          averageLeftAmplitude: 0.0,
          averageRightAmplitude: 0.0,
          maxLeftAmplitude: 0,
          maxRightAmplitude: 0,
          summaryText: "未解析"
        };
      }

      let activeFrameCount = 0;
      let totalLeftAmplitude = 0.0;
      let totalRightAmplitude = 0.0;
      let maxLeftAmplitude = 0;
      let maxRightAmplitude = 0;
)HTML";
auto kWebHidPlayerHtmlPart2S = R"HTML(
      for (const frame of frames) {
        const leftAmplitude = Number(frame.leftAmplitude ?? 0);
        const rightAmplitude = Number(frame.rightAmplitude ?? 0);

        totalLeftAmplitude += leftAmplitude;
        totalRightAmplitude += rightAmplitude;
        maxLeftAmplitude = Math.max(maxLeftAmplitude, leftAmplitude);
        maxRightAmplitude = Math.max(maxRightAmplitude, rightAmplitude);

        if (leftAmplitude > 0 || rightAmplitude > 0) {
          activeFrameCount++;
        }
      }

      const activeRatio = activeFrameCount / frames.length;
      const averageLeftAmplitude = totalLeftAmplitude / frames.length;
      const averageRightAmplitude = totalRightAmplitude / frames.length;

      return {
        frameCount: frames.length,
        activeRatio: activeRatio,
        averageLeftAmplitude: averageLeftAmplitude,
        averageRightAmplitude: averageRightAmplitude,
        maxLeftAmplitude: maxLeftAmplitude,
        maxRightAmplitude: maxRightAmplitude,
        summaryText:
          "L" + Math.round(averageLeftAmplitude) +
          " / R" + Math.round(averageRightAmplitude) +
          " / 最大 " + Math.max(maxLeftAmplitude, maxRightAmplitude) +
          " / 稼働 " + Math.round(activeRatio * 100.0) + "%"
      };
    };

)HTML";
auto kWebHidPlayerHtmlPart2Q = R"HTML(
    const buildPreviewFrameSummaries = () => {
      const switchFrames = adaptOutputFramesForDevice(vibrationFrames, "switch");
      const xboxFrames = adaptOutputFramesForDevice(vibrationFrames, "xbox");

      return {
        switch: summarizeOutputFrames(switchFrames),
        xbox: summarizeOutputFrames(xboxFrames)
      };
    };

    const updatePreviewFrameStatuses = () => {
      const previewSummaries = buildPreviewFrameSummaries();
      setStatus("switchPreviewStatus", previewSummaries.switch.summaryText);
      setStatus("xboxPreviewStatus", previewSummaries.xbox.summaryText);
    };
)HTML";
auto kWebHidPlayerHtmlPart2R = R"HTML(
    const applySummaryBasedOutputCorrection = (deviceFrames, deviceKind) => {
      const previewSummary = summarizeOutputFrames(deviceFrames);
      let leftScale = 1.0;
      let rightScale = 1.0;

      if (deviceKind === "switch") {
        const switchProfile = outputDeviceProfiles.switch;
        if (previewSummary.averageRightAmplitude >
              previewSummary.averageLeftAmplitude * switchProfile.summaryRightBalanceLimit &&
            previewSummary.maxRightAmplitude >= 420) {
          rightScale *= switchProfile.summaryRightClampScale;
        }

        if (previewSummary.activeRatio >= 0.88 &&
            previewSummary.averageRightAmplitude >= 180.0) {
          rightScale *= 0.92;
        }
      } else if (deviceKind === "xbox") {
        const xboxProfile = outputDeviceProfiles.xbox;
        if (previewSummary.averageLeftAmplitude <= xboxProfile.summaryLeftMinAverage &&
            previewSummary.activeRatio >= 0.28) {
          leftScale *= xboxProfile.summaryLeftBoostScale;
        }

        if (previewSummary.averageRightAmplitude >
              previewSummary.averageLeftAmplitude * xboxProfile.summaryRightBalanceLimit &&
            previewSummary.maxRightAmplitude >= 240) {
          rightScale *= xboxProfile.summaryRightClampScale;
        }
      }

      if (Math.abs(leftScale - 1.0) <= 0.0001 &&
          Math.abs(rightScale - 1.0) <= 0.0001) {
        return deviceFrames;
      }

      return deviceFrames.map((frame) => ({
        ...frame,
        leftAmplitude: clampAmplitude((frame.leftAmplitude ?? 0) * leftScale),
        rightAmplitude: clampAmplitude((frame.rightAmplitude ?? 0) * rightScale)
      }));
    };

    const adaptAmplitudePairForDevice = (
      leftAmplitude,
      rightAmplitude,
      deviceKind,
      optionalLayers = null
    ) => {
      if (deviceKind === "xbox") {
        const outputProfile = outputDeviceProfiles.xbox;
        const impactValue = optionalLayers?.impactLayer ?? 0.0;
        const bodyValue = optionalLayers?.bodyLayer ?? 0.0;
        const textureValue = optionalLayers?.textureLayer ?? 0.0;
        const rhythmValue = optionalLayers?.rhythmLayer ?? 0.0;

	    let  strongMotorValue =
          leftAmplitude * outputProfile.leftScale +
          rightAmplitude * 0.12 +
          impactValue * 80.0 +
			rhythmValue * 24.0;
        let  weakMotorValue =
          rightAmplitude * outputProfile.rightScale +
          leftAmplitude * 0.08 +
          textureValue * 48.0 +
	bodyValue * 18.0;
			if (strongMotorValue < 0.00001) {
			  strongMotorValue = 0.0;
			}

			if (weakMotorValue < 0.001) {
			  weakMotorValue = 0.0;
			}

        return {
          leftAmplitude: clampAmplitude(Math.max(outputProfile.leftFloor, strongMotorValue)),
          rightAmplitude: clampAmplitude(Math.max(outputProfile.rightFloor, weakMotorValue))
        };
      }

      const switchProfile = outputDeviceProfiles.switch;
      return {
        leftAmplitude: clampAmplitude(Math.max(switchProfile.leftFloor, leftAmplitude * switchProfile.leftScale)),
        rightAmplitude: clampAmplitude(Math.max(switchProfile.rightFloor, rightAmplitude * switchProfile.rightScale))
      };
    };
)HTML";
auto kWebHidPlayerHtmlPart2O = R"HTML(

    const computePercentile = (values, percentile) => {
      if (values.length <= 0) {
        return 0.0;
      }

      const sortedValues = values.slice().sort((leftValue, rightValue) => leftValue - rightValue);
      const clampedPercentile = Math.max(0.0, Math.min(1.0, percentile));
      const sampleIndex = Math.floor((sortedValues.length - 1) * clampedPercentile);
      return sortedValues[sampleIndex];
    };

    const computeFeatureStatistics = (featureFrames) => {
      if (!Array.isArray(featureFrames) || featureFrames.length <= 0) {
        return {
          frameCount: 0,
          leftRawP90: 0.0,
          rightRawP90: 0.0,
          onsetP90: 0.0,
          highRiseP90: 0.0,
          brightnessRiseP90: 0.0,
          periodicityP90: 0.0
        };
      }

      return {
        frameCount: featureFrames.length,
        leftRawP90: computePercentile(featureFrames.map((frame) => frame.leftRawValue), 0.9),
        rightRawP90: computePercentile(featureFrames.map((frame) => frame.rightRawValue), 0.9),
        onsetP90: computePercentile(featureFrames.map((frame) => frame.onsetValue), 0.9),
        highRiseP90: computePercentile(featureFrames.map((frame) => frame.highRiseValue), 0.9),
        brightnessRiseP90: computePercentile(
          featureFrames.map((frame) => frame.brightnessRiseValue),
          0.9
        ),
        periodicityP90: computePercentile(
          featureFrames.map((frame) => frame.periodicityValue),
          0.9
        )
      };
    };

    const buildSourceAdaptiveBias = (featureStatistics) => {
      const leftEnergyValue = Math.max(0.0, featureStatistics?.leftRawP90 ?? 0.0);
      const rightEnergyValue = Math.max(0.0, featureStatistics?.rightRawP90 ?? 0.0);
      const onsetEnergyValue = Math.max(0.0, featureStatistics?.onsetP90 ?? 0.0);
      const periodicityValue = Math.max(0.0, featureStatistics?.periodicityP90 ?? 0.0);
      const brightnessRiseValue =
        Math.max(0.0, featureStatistics?.brightnessRiseP90 ?? 0.0);
      const totalEnergyValue = Math.max(0.0001, leftEnergyValue + rightEnergyValue);
      const lowDominanceValue = leftEnergyValue / totalEnergyValue;
      const highDominanceValue = rightEnergyValue / totalEnergyValue;
      const transientDominanceValue =
        Math.max(
          0.0,
          Math.min(
            1.0,
            onsetEnergyValue / Math.max(0.0001, totalEnergyValue + onsetEnergyValue)
          )
        );
      const periodicDominanceValue =
        Math.max(0.0, Math.min(1.0, periodicityValue));
      const brightTransientValue =
        Math.max(
          0.0,
          Math.min(
            1.0,
            brightnessRiseValue / Math.max(0.0001, rightEnergyValue + brightnessRiseValue)
          )
        );

      return {
        impactBias: 0.9 + transientDominanceValue * 0.45,
        bodyBias: 0.85 + lowDominanceValue * 0.35,
        textureBias: 0.82 + highDominanceValue * 0.28 + brightTransientValue * 0.18,
        rhythmBias: 0.82 + periodicDominanceValue * 0.42,
        leftBoostBias: 0.88 + Math.max(lowDominanceValue, periodicDominanceValue) * 0.22,
        rightBoostBias: 0.84 + Math.max(highDominanceValue, brightTransientValue) * 0.26
      };
    };

    const cloneOutputDeviceProfiles = () => {
      return JSON.parse(JSON.stringify(outputDeviceProfiles));
    };

    const buildFullTuningSnapshot = () => {
      return {
        profileName: currentHapticsProfileName,
        adaptiveTuning: { ...adaptiveTuning },
        layerTuning: { ...layerTuning },
        runtimeTuning: { ...runtimeTuning },
        outputDeviceProfiles: cloneOutputDeviceProfiles()
      };
    };

    const applyFullTuningSnapshot = async (snapshot, shouldRebuild = true) => {
      currentTuningPresetName = "manual";

      if (snapshot && typeof snapshot === "object") {
        if (typeof snapshot.profileName === "string" &&
            Object.prototype.hasOwnProperty.call(hapticsProfiles, snapshot.profileName)) {
          currentHapticsProfileName = snapshot.profileName;
        }

        if (snapshot.adaptiveTuning && typeof snapshot.adaptiveTuning === "object") {
          Object.assign(adaptiveTuning, snapshot.adaptiveTuning);
        }

        if (snapshot.layerTuning && typeof snapshot.layerTuning === "object") {
          Object.assign(layerTuning, snapshot.layerTuning);
        }

        if (snapshot.runtimeTuning && typeof snapshot.runtimeTuning === "object") {
          Object.assign(runtimeTuning, snapshot.runtimeTuning);
        }

        if (snapshot.outputDeviceProfiles &&
            typeof snapshot.outputDeviceProfiles === "object") {
          if (snapshot.outputDeviceProfiles.switch &&
              typeof snapshot.outputDeviceProfiles.switch === "object") {
            Object.assign(outputDeviceProfiles.switch, snapshot.outputDeviceProfiles.switch);
          }

          if (snapshot.outputDeviceProfiles.xbox &&
              typeof snapshot.outputDeviceProfiles.xbox === "object") {
            Object.assign(outputDeviceProfiles.xbox, snapshot.outputDeviceProfiles.xbox);
          }
        }
      }

      syncTuningUi();
      syncTuningPresetUi();
      updateTuningFromInputs();
      updateLayerTuningFromInputs();
      updateOutputDeviceProfilesFromInputs();

      if (shouldRebuild) {
        await rebuildFramesFromCurrentTrack();
      } else {
        updatePreviewFrameStatuses();
      }

      return buildFullTuningSnapshot();
    };

    const resetAllTuningState = async () => {
      currentHapticsProfileName = "adaptive";
      currentTuningPresetName = "manual";
      Object.assign(adaptiveTuning, defaultAdaptiveTuning);
      Object.assign(layerTuning, defaultLayerTuning);
      Object.assign(runtimeTuning, defaultTuning);
      Object.assign(outputDeviceProfiles.switch, defaultOutputDeviceProfiles.switch);
      Object.assign(outputDeviceProfiles.xbox, defaultOutputDeviceProfiles.xbox);

      syncTuningUi();
      syncTuningPresetUi();
      updateTuningFromInputs();
      updateLayerTuningFromInputs();
      updateOutputDeviceProfilesFromInputs();
      await rebuildFramesFromCurrentTrack();
      return buildFullTuningSnapshot();
    };

    const saveCurrentTuningAsCustomPreset = (presetName = "custom") => {
      customTuningPresets[presetName] = buildFullTuningSnapshot();
      currentTuningPresetName = presetName;
      syncTuningPresetUi();
      return JSON.parse(JSON.stringify(customTuningPresets[presetName]));
    };

    const createTuningPresetSnapshot = (
      profileName,
      adaptiveOverrides = {},
      layerOverrides = {},
      runtimeOverrides = {},
      switchOverrides = {},
      xboxOverrides = {}
    ) => {
      return {
        profileName: profileName,
        adaptiveTuning: {
          ...defaultAdaptiveTuning,
          ...adaptiveOverrides
        },
        layerTuning: {
          ...defaultLayerTuning,
          ...layerOverrides
        },
        runtimeTuning: {
          ...defaultTuning,
          ...runtimeOverrides
        },
        outputDeviceProfiles: {
          switch: {
            ...defaultOutputDeviceProfiles.switch,
            ...switchOverrides
          },
          xbox: {
            ...defaultOutputDeviceProfiles.xbox,
            ...xboxOverrides
          }
        }
      };
    };

    const builtInTuningPresets = {
      music: createTuningPresetSnapshot(
        "adaptive",
        {
          textureBase: 0.7,
          textureRange: 1.02,
          rhythmBase: 0.62,
          rhythmRange: 1.18
        },
        {
          textureBrightnessRiseWeight: 5.6,
          textureCarry: 0.62,
          rhythmPeriodicityWeight: 0.98
        },
        {
          shapeExponent: 1.8,
          activeThreshold: 42,
          rightCadenceMin: 1,
          rightCadenceMax: 4,
          rightPulseWidthMax: 3
        },
        {
          textureRightBoost: 24.0
        },
        {
          textureDamping: 0.84
        }
      ),
      impact: createTuningPresetSnapshot(
        "impact",
        {
          impactBase: 0.92,
          impactRange: 1.04,
          bodyBase: 0.95,
          bodyRange: 0.68,
          textureBase: 0.55,
          textureRange: 0.72
        },
        {
          transientOnsetWeight: 10.2,
          bodyCarry: 0.9,
          textureCarry: 0.48
        },
        {
          shapeExponent: 1.95,
          activeThreshold: 50,
          leftMinAmplitude: 84,
          rightMinAmplitude: 52
        },
        {
          impactRightBoost: 24.0
        },
        {
          impactLeftBoost: 54.0,
          bodyLeftBoost: 30.0,
          textureDamping: 0.76
        }
      ),
      switchFine: createTuningPresetSnapshot(
        "adaptive",
        {
          textureBase: 0.76,
          textureRange: 1.08,
          rightBoostBase: 0.78,
          rightBoostRange: 0.62
        },
        {
          textureHighRiseWeight: 5.2,
          textureBrightnessRiseWeight: 5.8,
          textureCarry: 0.64
        },
        {
          shapeExponent: 1.78,
          activeThreshold: 40,
          rightCadenceMin: 1,
          rightCadenceMax: 4,
          rightPulseWidthMax: 2,
          presetCountMax: 32
        },
        {
          textureSafetyThreshold: 0.6,
          textureSafetyClamp: 0.9,
          textureRightBoost: 26.0,
          summaryRightBalanceLimit: 1.52,
          summaryRightClampScale: 0.93
        }
      ),
      xboxStable: createTuningPresetSnapshot(
        "adaptive",
        {
          impactBase: 0.86,
          bodyBase: 0.92,
          bodyRange: 0.62,
          textureBase: 0.52,
          textureRange: 0.7
        },
        {
          bodyCarry: 0.9,
          textureCarry: 0.44,
          rhythmPeriodicityWeight: 0.82
        },
        {
          shapeExponent: 1.92,
          activeThreshold: 48,
          leftMinAmplitude: 78,
          rightMinAmplitude: 42,
          rightCadenceMin: 2,
          rightCadenceMax: 5
        },
        {},
        {
          leftScale: 0.94,
          rightScale: 0.74,
          impactLeftBoost: 50.0,
          bodyLeftBoost: 28.0,
          textureDamping: 0.74,
          summaryLeftMinAverage: 120.0,
          summaryLeftBoostScale: 1.12,
          summaryRightBalanceLimit: 0.9,
          summaryRightClampScale: 0.88
        }
      )
    };
    const customTuningPresets = {};

    const getAllTuningPresetNames = () => {
      return [
        ...Object.keys(builtInTuningPresets),
        ...Object.keys(customTuningPresets)
      ];
    };

    const findTuningPresetSnapshot = (presetName) => {
      if (Object.prototype.hasOwnProperty.call(builtInTuningPresets, presetName)) {
        return builtInTuningPresets[presetName];
      }

      if (Object.prototype.hasOwnProperty.call(customTuningPresets, presetName)) {
        return customTuningPresets[presetName];
      }

      return null;
    };

    const getPreferredPresetNameForDevice = (deviceKind) => {
      if (deviceKind === "switch") {
        if (findTuningPresetSnapshot("customSwitch") !== null) {
          return "customSwitch";
        }

        return "switchFine";
      }

      if (deviceKind === "xbox") {
        if (findTuningPresetSnapshot("customXbox") !== null) {
          return "customXbox";
        }

        return "xboxStable";
      }

      return null;
    };

    const autoApplyPreferredPresetForDevice = async (deviceKind) => {
      const preferredPresetName = getPreferredPresetNameForDevice(deviceKind);

      if (preferredPresetName === null || currentTuningPresetName === preferredPresetName) {
        return null;
      }

      const presetSnapshot = findTuningPresetSnapshot(preferredPresetName);

      if (presetSnapshot === null) {
        return null;
      }

      const appliedSnapshot =
        await applyFullTuningSnapshot(
          JSON.parse(JSON.stringify(presetSnapshot)),
          true
        );
      currentTuningPresetName = preferredPresetName;
      syncTuningPresetUi();
      log("接続デバイスに合わせて既定セットを自動適用しました: " + preferredPresetName);
      return appliedSnapshot;
    };

    const extractFeatureFramesFromAudioBuffer = (audioBuffer) => {
      const sampleRate = audioBuffer.sampleRate;
      const leftChannel = audioBuffer.getChannelData(0);
      const rightChannel =
        audioBuffer.numberOfChannels >= 2 ? audioBuffer.getChannelData(1) : leftChannel;
      const samplesPerFrame =
        Math.max(1, Math.floor((sampleRate * frameIntervalMs) / 1000));
      const totalSamples = leftChannel.length;
      const totalFrames = Math.ceil(totalSamples / samplesPerFrame);
      const rawFrames = [];

      let lowState = 0.0;
      let previousFrameEnergy = 0.0;
      let previousHighPeak = 0.0;
      let previousBrightness = 0.0;
      let lastLowHitFrameIndex = -1;
      let smoothedPulseIntervalFrames = 0.0;
      const lowCutFrequencyHz = 80.0;

		const lowAlpha =
		  Math.min(
		    0.35,
		    (2.0 * Math.PI * lowCutFrequencyHz) / sampleRate
		  );
)HTML";
auto kWebHidPlayerHtmlPart2L = R"HTML(

      for (let frameIndex = 0; frameIndex < totalFrames; frameIndex++) {
        const startSampleIndex = frameIndex * samplesPerFrame;
        const endSampleIndex = Math.min(totalSamples, startSampleIndex + samplesPerFrame);
        let lowPeak = 0.0;
        let highPeak = 0.0;
        let frameEnergy = 0.0;

        for (let sampleIndex = startSampleIndex; sampleIndex < endSampleIndex; sampleIndex++) {
		  const monoValue = (leftChannel[sampleIndex] + rightChannel[sampleIndex]) * 0.5;

		  if (Math.abs(monoValue) < 0.01) {
		    continue;
		  }

		  lowState += (monoValue - lowState) * lowAlpha;
		  const highValue = monoValue - lowState;
		  const lowMagnitude = Math.abs(lowState);
		  const highMagnitude = Math.abs(highValue);

		  if (lowMagnitude > lowPeak) {
		    lowPeak = lowMagnitude;
		  }

		  if (highMagnitude > highPeak) {
		    highPeak = highMagnitude;
		  }

		  frameEnergy += Math.abs(monoValue);
		}   

        const sampleCount = Math.max(1, endSampleIndex - startSampleIndex);
        frameEnergy /= sampleCount;

        const onsetValue = Math.max(0.0, frameEnergy - previousFrameEnergy * 0.36);
        const highRiseValue = Math.max(0.0, highPeak - previousHighPeak * 0.9);
        const brightnessValue = highPeak / Math.max(0.0001, lowPeak + highPeak);
        const brightnessRiseValue = Math.max(0.0, brightnessValue - previousBrightness * 0.92);
)HTML";
auto kWebHidPlayerHtmlPart2K = R"HTML(

        const lowHitValue = lowPeak + onsetValue * 0.8;
        let periodicityValue = 0.0;

        if (lowHitValue >= 0.035) {
          if (lastLowHitFrameIndex >= 0) {
            const pulseIntervalFrames =
              Math.max(1, frameIndex - lastLowHitFrameIndex);

            if (smoothedPulseIntervalFrames <= 0.0) {
              smoothedPulseIntervalFrames = pulseIntervalFrames;
            } else {
              smoothedPulseIntervalFrames =
                smoothedPulseIntervalFrames * 0.7 + pulseIntervalFrames * 0.3;
            }

            const intervalDelta =
              Math.abs(pulseIntervalFrames - smoothedPulseIntervalFrames);
            periodicityValue =
              Math.max(0.0, 1.0 - intervalDelta / Math.max(2.0, smoothedPulseIntervalFrames));
)HTML";
auto kWebHidPlayerHtmlPart2J = R"HTML(

          }

          lastLowHitFrameIndex = frameIndex;
        } else {
          smoothedPulseIntervalFrames *= 0.5;
        }

        previousFrameEnergy = previousFrameEnergy * 0.7 + frameEnergy * 0.3;
        previousHighPeak = previousHighPeak * 0.55 + highPeak * 0.45;
        previousBrightness = previousBrightness * 0.6 + brightnessValue * 0.4;
)HTML";
auto kWebHidPlayerHtmlPart2I = R"HTML(

        const audibleLowPeak =
		  Math.max(
		    0.0,
		    (lowPeak - 0.015) * 1.8
		  );

		const leftRawValue =
		  audibleLowPeak * 1.0 +
		  onsetValue * 1.4 +
		  highRiseValue * 0.2;
        const rightRawValue =
          highPeak * 1.0 + onsetValue * 0.7 + highRiseValue * 1.0 + brightnessRiseValue * 0.4;
        let rightPhaseStep = 1;
        const rightToneOffset = 0.0;
        const leftToneOffset = 0.0;

        rawFrames.push({
          frameIndex: frameIndex,
          startMs: frameIndex * frameIntervalMs,
          endMs: (frameIndex + 1) * frameIntervalMs,
          leftRawValue: leftRawValue,
          rightRawValue: rightRawValue,
          onsetValue: onsetValue,
          highRiseValue: highRiseValue,
          brightnessValue: brightnessValue,
          brightnessRiseValue: brightnessRiseValue,
          periodicityValue: periodicityValue,
          leftPhaseStep: 1,
          rightPhaseStep: rightPhaseStep,
          leftToneOffset: leftToneOffset,
          rightToneOffset: rightToneOffset
        });
      }

      return rawFrames;
    };
)HTML";
auto kWebHidPlayerHtmlPart2G = R"HTML(

    const buildLayeredHapticFrames = (featureFrames) => {
      const featureStatistics = computeFeatureStatistics(featureFrames);
      const sourceAdaptiveBias = buildSourceAdaptiveBias(featureStatistics);
      const leftReferenceValue = Math.max(
        0.0001,
        computePercentile(featureFrames.map((frame) => frame.leftRawValue), 0.97)
      );
      const rightReferenceValue = Math.max(
        0.0001,
        computePercentile(featureFrames.map((frame) => frame.rightRawValue), 0.97)
      );
      const layeredFrames = [];
      let previousImpactLayer = 0.0;
      let previousBodyLayer = 0.0;
      let previousTextureLayer = 0.0;
      let previousRhythmLayer = 0.0;

      for (const featureFrame of featureFrames) {
        let currentProfile =
          currentHapticsProfileName === "adaptive"
            ? buildAdaptiveProfileFromFeatureFrame(featureFrame, featureStatistics)
            : getCurrentHapticsProfile();

        if (currentHapticsProfileName === "adaptive") {
          currentProfile = {
            impactScale: currentProfile.impactScale * sourceAdaptiveBias.impactBias,
            bodyScale: currentProfile.bodyScale * sourceAdaptiveBias.bodyBias,
            textureScale: currentProfile.textureScale * sourceAdaptiveBias.textureBias,
            rhythmScale: currentProfile.rhythmScale * sourceAdaptiveBias.rhythmBias,
            rightBoostScale:
              currentProfile.rightBoostScale * sourceAdaptiveBias.rightBoostBias,
            leftBoostScale:
              currentProfile.leftBoostScale * sourceAdaptiveBias.leftBoostBias
          };
        }

        const normalizedLeftValue =
          Math.max(0.0, Math.min(1.0, featureFrame.leftRawValue / leftReferenceValue));
        const normalizedRightValue =
          Math.max(0.0, Math.min(1.0, featureFrame.rightRawValue / rightReferenceValue));
        const transientValue =
          Math.max(
            0.0,
            Math.min(
              1.0,
              featureFrame.onsetValue * layerTuning.transientOnsetWeight +
              featureFrame.highRiseValue * layerTuning.transientHighRiseWeight
            )
          );
        const sustainValue =
          Math.max(
            0.0,
            Math.min(
              1.0,
              normalizedLeftValue * layerTuning.sustainLeftWeight +
              featureFrame.periodicityValue * layerTuning.sustainRhythmWeight +
              featureFrame.onsetValue * layerTuning.sustainOnsetWeight
            )
          );
        const tactileTextureValue =
          Math.max(
            0.0,
            Math.min(
              1.0,
              normalizedRightValue * layerTuning.textureRightWeight +
              featureFrame.highRiseValue * layerTuning.textureHighRiseWeight +
              featureFrame.brightnessRiseValue * layerTuning.textureBrightnessRiseWeight +
              featureFrame.brightnessValue * layerTuning.textureBrightnessWeight
            )
          );
        const rotatingValue =
          Math.max(
            0.0,
            Math.min(
              1.0,
              featureFrame.periodicityValue * layerTuning.rhythmPeriodicityWeight +
              normalizedLeftValue * layerTuning.rhythmLeftWeight +
              featureFrame.onsetValue * layerTuning.rhythmOnsetWeight
            )
          );

        const rawImpactLayer =
          Math.max(normalizedLeftValue * 0.12, transientValue) * currentProfile.impactScale;
        const rawBodyLayer =
          Math.max(sustainValue, transientValue * 0.35) * currentProfile.bodyScale;
        const rawTextureLayer =
          Math.max(tactileTextureValue, transientValue * 0.2) * currentProfile.textureScale;
        const rawRhythmLayer =
          Math.max(rotatingValue, sustainValue * 0.18) * currentProfile.rhythmScale;

        const impactLayer =
          Math.max(
            0.0,
            Math.min(1.0, Math.max(rawImpactLayer, previousImpactLayer * layerTuning.impactCarry))
          );
        const bodyLayer =
          Math.max(
            0.0,
            Math.min(1.0, Math.max(rawBodyLayer, previousBodyLayer * layerTuning.bodyCarry))
          );
        const textureLayer =
          Math.max(
            0.0,
            Math.min(1.0, Math.max(rawTextureLayer, previousTextureLayer * layerTuning.textureCarry))
          );
        const rhythmLayer =
          Math.max(
            0.0,
            Math.min(1.0, Math.max(rawRhythmLayer, previousRhythmLayer * layerTuning.rhythmCarry))
          );

        previousImpactLayer = impactLayer;
        previousBodyLayer = bodyLayer;
        previousTextureLayer = textureLayer;
        previousRhythmLayer = rhythmLayer;

        layeredFrames.push({
          frameIndex: featureFrame.frameIndex,
          startMs: featureFrame.startMs,
          endMs: featureFrame.endMs,
          normalizedLeftValue: normalizedLeftValue,
          normalizedRightValue: normalizedRightValue,
          impactLayer: impactLayer,
          bodyLayer: bodyLayer,
          textureLayer: textureLayer,
          rhythmLayer: rhythmLayer,
          onsetValue: featureFrame.onsetValue,
          highRiseValue: featureFrame.highRiseValue,
          brightnessValue: featureFrame.brightnessValue,
          brightnessRiseValue: featureFrame.brightnessRiseValue,
          periodicityValue: featureFrame.periodicityValue,
          profileImpactScale: currentProfile.impactScale,
          profileBodyScale: currentProfile.bodyScale,
          profileTextureScale: currentProfile.textureScale,
          profileRhythmScale: currentProfile.rhythmScale,
          profileRightBoostScale: currentProfile.rightBoostScale,
          profileLeftBoostScale: currentProfile.leftBoostScale,
          leftPhaseStep: featureFrame.leftPhaseStep,
          rightPhaseStep: featureFrame.rightPhaseStep,
          leftToneOffset: featureFrame.leftToneOffset,
          rightToneOffset: featureFrame.rightToneOffset
        });
      }

      lastFeatureStatistics = featureStatistics;
      return layeredFrames;
    };
)HTML";
auto kWebHidPlayerHtmlPart2H = R"HTML(

    const renderOutputFramesFromLayers = (layeredFrames) => {
      const generatedFrames = [];
      const outputFeatureStatistics = lastFeatureStatistics;
      const outputSourceBias =
        outputFeatureStatistics !== null
          ? buildSourceAdaptiveBias(outputFeatureStatistics)
          : {
              impactBias: 1.0,
              bodyBias: 1.0,
              textureBias: 1.0,
              rhythmBias: 1.0,
              leftBoostBias: 1.0,
              rightBoostBias: 1.0
            };

      for (const layeredFrame of layeredFrames) {
        const leftAmplitude = Math.max(
          runtimeTuning.outputMinAmplitude,
          Math.min(
            runtimeTuning.outputMaxAmplitude,
            shapeAmplitude(Math.max(layeredFrame.bodyLayer, layeredFrame.rhythmLayer * 0.82))
          )
        );
        const rightAmplitude = Math.max(
          runtimeTuning.outputMinAmplitude,
          Math.min(runtimeTuning.outputMaxAmplitude, shapeAmplitude(layeredFrame.textureLayer))
        );
        const boostedLeftAmplitude = Math.min(
          runtimeTuning.outputMaxAmplitude,
          Math.floor(
            leftAmplitude +
            layeredFrame.impactLayer * 360.0 * layeredFrame.profileLeftBoostScale +
            layeredFrame.rhythmLayer * 120.0 * layeredFrame.profileLeftBoostScale
          )
        );
        const boostedRightAmplitude = Math.min(
          runtimeTuning.outputMaxAmplitude,
          Math.floor(
            rightAmplitude +
            layeredFrame.impactLayer * 160.0 * layeredFrame.profileRightBoostScale +
            layeredFrame.textureLayer * 180.0 * layeredFrame.profileRightBoostScale +
            layeredFrame.brightnessRiseValue * 220.0 * layeredFrame.profileRightBoostScale
          )
        );
        const hasLeftPulse =
          layeredFrame.bodyLayer >= 0.28 || layeredFrame.impactLayer >= 0.18;
        const hasRightPulse =
          layeredFrame.textureLayer >= 0.24 || layeredFrame.impactLayer >= 0.14;
        let leftCadenceFrames = 4;
        let rightCadenceFrames =
          Math.max(runtimeTuning.rightCadenceMin, runtimeTuning.rightCadenceMax - 2);
        let rightPhaseStep = 1;

        if (layeredFrame.bodyLayer >= 0.55) {
          leftCadenceFrames = 3;
        }

        if (layeredFrame.rhythmLayer >= 0.46 && layeredFrame.periodicityValue >= 0.45) {
          leftCadenceFrames = Math.min(leftCadenceFrames, 3);
        }

        if (layeredFrame.bodyLayer >= 0.78 || layeredFrame.impactLayer >= 0.32) {
          leftCadenceFrames = 2;
        }

        if (outputSourceBias.bodyBias >= 1.08 || outputSourceBias.rhythmBias >= 1.1) {
          leftCadenceFrames = Math.max(2, leftCadenceFrames - 1);
        }

          const highAmount =
          Math.max(
            layeredFrame.textureLayer,
            layeredFrame.brightnessValue * 0.72,
            layeredFrame.highRiseValue * 10.0,
            layeredFrame.brightnessRiseValue * 18.0
          );

        rightCadenceFrames =
          Math.max(
            runtimeTuning.rightCadenceMin,
            Math.min(
              runtimeTuning.rightCadenceMax,
              Math.round(
                runtimeTuning.rightCadenceMax -
                highAmount * (runtimeTuning.rightCadenceMax - runtimeTuning.rightCadenceMin)
              )
            )
          );

        rightPhaseStep =
          highAmount >= 0.72 ? 3 :
          highAmount >= 0.48 ? 2 : 1;

        const leftPulseWidthFrames =
          (layeredFrame.bodyLayer >= 0.72 ||
           layeredFrame.impactLayer >= 0.4 ||
           layeredFrame.rhythmLayer >= 0.62) ? 2 : 1;
        const rightPulseWidthFrames =
          Math.max(
            1,
            Math.min(
              runtimeTuning.rightPulseWidthMax,
              Math.round(1.0 + layeredFrame.textureLayer * 1.4 + layeredFrame.impactLayer * 0.5)
            )
          );
        const adjustedLeftPulseWidthFrames =
          outputSourceBias.bodyBias >= 1.08 && leftPulseWidthFrames < 2
            ? 2
            : leftPulseWidthFrames;
        const adjustedRightPulseWidthFrames =
          outputSourceBias.textureBias >= 1.08 && rightPulseWidthFrames > 1
            ? rightPulseWidthFrames - 1
            : rightPulseWidthFrames;
        const isLeftCadenceActive =
          (layeredFrame.frameIndex % leftCadenceFrames) < adjustedLeftPulseWidthFrames;
        const isRightCadenceActive =
          (layeredFrame.frameIndex % rightCadenceFrames) < adjustedRightPulseWidthFrames;
        let gatedLeftAmplitude = 0;
        let gatedRightAmplitude = 0;

        if (hasLeftPulse) {
          if (isLeftCadenceActive) {
            gatedLeftAmplitude = boostedLeftAmplitude;

            if (layeredFrame.bodyLayer >= 0.58 ||
                layeredFrame.impactLayer >= 0.34 ||
                layeredFrame.rhythmLayer >= 0.52) {
              gatedLeftAmplitude =
                Math.max(Math.floor(runtimeTuning.leftMinAmplitude * 0.6), gatedLeftAmplitude);
            }
          } else {
            const hasRealInput =
              layeredFrame.normalizedLeftValue >= 0.006 ||
              layeredFrame.normalizedRightValue >= 0.006 ||
              layeredFrame.onsetValue >= 0.0015 ||
              layeredFrame.highRiseValue >= 0.0015;

            gatedLeftAmplitude =
              hasRealInput ? Math.floor(leftAmplitude * 0.10) : 0;
          }
        }

        if (hasRightPulse) {
          if (isRightCadenceActive) {
            gatedRightAmplitude = boostedRightAmplitude;

            if (layeredFrame.textureLayer >= 0.54 ||
                layeredFrame.highRiseValue >= 0.038 ||
                layeredFrame.brightnessRiseValue >= 0.022) {
              gatedRightAmplitude =
                Math.max(Math.floor(runtimeTuning.rightMinAmplitude * 0.6), gatedRightAmplitude);
            }
          } else {
            const hasRealInput =
              layeredFrame.normalizedLeftValue >= 0.006 ||
              layeredFrame.normalizedRightValue >= 0.006 ||
              layeredFrame.onsetValue >= 0.0015 ||
              layeredFrame.highRiseValue >= 0.0015;

            gatedRightAmplitude =
              hasRealInput ? Math.floor(rightAmplitude * 0.08) : 0;
          }
        }

        generatedFrames.push({
          startMs: layeredFrame.startMs,
          endMs: layeredFrame.endMs,
          leftAmplitude: gatedLeftAmplitude,
          rightAmplitude: gatedRightAmplitude,
          impactLayer: layeredFrame.impactLayer,
          bodyLayer: layeredFrame.bodyLayer,
          textureLayer: layeredFrame.textureLayer,
          rhythmLayer: layeredFrame.rhythmLayer,
          leftPhaseStep: layeredFrame.leftPhaseStep,
          rightPhaseStep: rightPhaseStep,
          leftToneOffset: layeredFrame.leftToneOffset,
          rightToneOffset: layeredFrame.rightToneOffset
        });
      }

      return generatedFrames;
    };

const refineOutputFramesForTactilePlayback = (outputFrames) => {
  const refinedFrames = [];
  let previousLeftAmplitude = 0;
  let previousRightAmplitude = 0;

  for (const frame of outputFrames) {
    const impactValue = frame.impactLayer ?? 0.0;
    const bodyValue = frame.bodyLayer ?? 0.0;
    const textureValue = frame.textureLayer ?? 0.0;
    const rhythmValue = frame.rhythmLayer ?? 0.0;
    const baseLeftAmplitude = frame.leftAmplitude ?? 0;
    const baseRightAmplitude = frame.rightAmplitude ?? 0;

    const hasRealInput =
      baseLeftAmplitude > 0 ||
      baseRightAmplitude > 0 ||
      impactValue >= 0.08 ||
      bodyValue >= 0.10 ||
      textureValue >= 0.10 ||
      rhythmValue >= 0.10;

    let refinedLeftAmplitude = baseLeftAmplitude;
    let refinedRightAmplitude = baseRightAmplitude;

    if (!hasRealInput) {
      refinedLeftAmplitude = 0;
      refinedRightAmplitude = 0;
      previousLeftAmplitude = 0;
      previousRightAmplitude = 0;
    } else {
      const leftAttackBoost =
        clampAmplitude(impactValue * 110.0 + rhythmValue * 35.0);
      const rightAttackBoost =
        clampAmplitude(impactValue * 55.0 + textureValue * 45.0);

      const leftTailAmplitude =
        clampAmplitude(previousLeftAmplitude * (0.06 + bodyValue * 0.06));
      const rightTailAmplitude =
        clampAmplitude(previousRightAmplitude * (0.04 + textureValue * 0.06));

      if (impactValue >= 0.14 || bodyValue >= 0.22 || rhythmValue >= 0.2) {
        refinedLeftAmplitude =
          clampAmplitude(Math.max(refinedLeftAmplitude + leftAttackBoost, leftTailAmplitude));
      }

      if (impactValue >= 0.12 || textureValue >= 0.18) {
        refinedRightAmplitude =
          clampAmplitude(Math.max(refinedRightAmplitude + rightAttackBoost, rightTailAmplitude));
      }

      previousLeftAmplitude = refinedLeftAmplitude;
      previousRightAmplitude = refinedRightAmplitude;
    }

    refinedFrames.push({
      ...frame,
      leftAmplitude: refinedLeftAmplitude,
      rightAmplitude: refinedRightAmplitude
    });
  }

  return refinedFrames;
};

    const refineOutputFramesForDevice = (
      outputFrames,
      deviceKind,
      featureStatistics = null
    ) => {
      const deviceFrames = [];
      const sourceAdaptiveBias =
        featureStatistics !== null
          ? buildSourceAdaptiveBias(featureStatistics)
          : {
              impactBias: 1.0,
              bodyBias: 1.0,
              textureBias: 1.0,
              rhythmBias: 1.0,
              leftBoostBias: 1.0,
              rightBoostBias: 1.0
            };

      for (const frame of outputFrames) {
        let leftAmplitude = frame.leftAmplitude ?? 0;
        let rightAmplitude = frame.rightAmplitude ?? 0;
        let rightPhaseStep = frame.rightPhaseStep ?? 1;

        if (deviceKind === "xbox") {
          const xboxProfile = outputDeviceProfiles.xbox;
          leftAmplitude =
            clampAmplitude(
              leftAmplitude * (0.96 + Math.max(0.0, sourceAdaptiveBias.bodyBias - 1.0) * 0.32) +
              (frame.impactLayer ?? 0.0) * xboxProfile.impactLeftBoost +
              (frame.bodyLayer ?? 0.0) * xboxProfile.bodyLeftBoost
            );
          rightAmplitude =
            clampAmplitude(
              rightAmplitude *
                (xboxProfile.textureDamping -
                 Math.max(0.0, sourceAdaptiveBias.textureBias - 1.0) * 0.12) +
              (frame.textureLayer ?? 0.0) * xboxProfile.textureRightBoost
            );
          rightPhaseStep = 1;
        } else if (deviceKind === "switch") {
          const switchProfile = outputDeviceProfiles.switch;
          leftAmplitude =
            clampAmplitude(
              leftAmplitude * (0.98 + Math.max(0.0, sourceAdaptiveBias.leftBoostBias - 1.0) * 0.18)
            );
          rightAmplitude =
            clampAmplitude(
              rightAmplitude *
                (1.0 + Math.max(0.0, sourceAdaptiveBias.rightBoostBias - 1.0) * 0.22) +
              (frame.impactLayer ?? 0.0) * switchProfile.impactRightBoost +
              (frame.textureLayer ?? 0.0) * switchProfile.textureRightBoost
            );

          if (sourceAdaptiveBias.textureBias >= 1.08 && (frame.textureLayer ?? 0.0) >= 0.38) {
            rightPhaseStep = 1;
          }

          if ((frame.textureLayer ?? 0.0) >= switchProfile.textureSafetyThreshold &&
              sourceAdaptiveBias.textureBias >= 1.08) {
            rightAmplitude =
              clampAmplitude(rightAmplitude * switchProfile.textureSafetyClamp);
          }
        }

        deviceFrames.push({
          ...frame,
          leftAmplitude: leftAmplitude,
          rightAmplitude: rightAmplitude,
          rightPhaseStep: rightPhaseStep
        });
      }

      return applySummaryBasedOutputCorrection(deviceFrames, deviceKind);
    };
)HTML";
auto kWebHidPlayerHtmlPart2F = R"HTML(

    const buildFramesFromAudioBuffer = (audioBuffer) => {
      lastExtractedFeatureFrames = extractFeatureFramesFromAudioBuffer(audioBuffer);
      lastLayeredHapticFrames = buildLayeredHapticFrames(lastExtractedFeatureFrames);
      const rawOutputFrames = renderOutputFramesFromLayers(lastLayeredHapticFrames);
      return refineOutputFramesForTactilePlayback(rawOutputFrames);
    };

    const adaptOutputFramesForDevice = (outputFrames, deviceKind) => {
      const refinedFrames =
        refineOutputFramesForDevice(outputFrames, deviceKind, lastFeatureStatistics);

      return refinedFrames.map((frame) => {
        const adaptedPair =
          adaptAmplitudePairForDevice(
            frame.leftAmplitude ?? 0,
            frame.rightAmplitude ?? 0,
            deviceKind,
            frame
          );

        return {
          ...frame,
          leftAmplitude: adaptedPair.leftAmplitude,
          rightAmplitude: adaptedPair.rightAmplitude,
          deviceKind: deviceKind
        };
      });
    };

    const cloneFrameArray = (frames) => {
      return frames.map((frame) => ({ ...frame }));
    };

    const buildSilentOutputFrames = (durationMs) => {
      const totalFrames = Math.max(1, Math.ceil(durationMs / frameIntervalMs));
      const silentFrames = [];

      for (let frameIndex = 0; frameIndex < totalFrames; frameIndex++) {
        silentFrames.push({
          startMs: frameIndex * frameIntervalMs,
          endMs: (frameIndex + 1) * frameIntervalMs,
          leftAmplitude: 0,
          rightAmplitude: 0,
          impactLayer: 0.0,
          bodyLayer: 0.0,
          textureLayer: 0.0,
          rhythmLayer: 0.0,
          leftPhaseStep: 1,
          rightPhaseStep: 1,
          leftToneOffset: 0.0,
          rightToneOffset: 0.0
        });
      }

      return silentFrames;
    };

    const clampFrameAmplitude = (value) => {
      return Math.max(0, Math.min(1003, Math.floor(value)));
    };

    const mixAmplitudeValue = (baseValue, sourceValue, mixMode) => {
      if (mixMode === "add") {
        return clampFrameAmplitude(baseValue + sourceValue);
      }

      if (mixMode === "hybrid") {
        const boostedValue =
          Math.max(baseValue, sourceValue) + Math.min(baseValue, sourceValue) * 0.45;
        return clampFrameAmplitude(boostedValue);
      }

      return Math.max(baseValue, sourceValue);
    };

    const mixLayerValue = (baseValue, sourceValue, mixMode) => {
      if (mixMode === "add") {
        return Math.max(0.0, Math.min(1.0, baseValue + sourceValue));
      }

      if (mixMode === "hybrid") {
        return Math.max(0.0, Math.min(1.0, Math.max(baseValue, sourceValue) + Math.min(baseValue, sourceValue) * 0.35));
      }

      return Math.max(baseValue, sourceValue);
    };

    const mixPolicies = {
      balanced: {
        impactWeight: 1.0,
        bodyWeight: 1.0,
        textureWeight: 1.0,
        rhythmWeight: 1.0
      },
      impactFirst: {
        impactWeight: 1.35,
        bodyWeight: 1.05,
        textureWeight: 0.7,
        rhythmWeight: 0.8
      },
      textureFirst: {
        impactWeight: 0.9,
        bodyWeight: 0.85,
        textureWeight: 1.35,
        rhythmWeight: 0.9
      },
      rhythmFirst: {
        impactWeight: 0.85,
        bodyWeight: 1.0,
        textureWeight: 0.85,
        rhythmWeight: 1.4
      },
      bodyFirst: {
        impactWeight: 0.95,
        bodyWeight: 1.3,
        textureWeight: 0.75,
        rhythmWeight: 1.0
      }
    };

    const getMixPolicy = (mixPolicyName) => {
      if (typeof mixPolicyName === "string" &&
          Object.prototype.hasOwnProperty.call(mixPolicies, mixPolicyName)) {
        return mixPolicies[mixPolicyName];
      }

      return mixPolicies.balanced;
    };

    const buildResolvedMixPolicy = (options = {}) => {
      const basePolicy = {
        ...getMixPolicy(typeof options.mixPolicy === "string" ? options.mixPolicy : "balanced")
      };

      if (options.mixWeights && typeof options.mixWeights === "object") {
        if (typeof options.mixWeights.impactWeight === "number") {
          basePolicy.impactWeight = Math.max(0.0, options.mixWeights.impactWeight);
        }

        if (typeof options.mixWeights.bodyWeight === "number") {
          basePolicy.bodyWeight = Math.max(0.0, options.mixWeights.bodyWeight);
        }

        if (typeof options.mixWeights.textureWeight === "number") {
          basePolicy.textureWeight = Math.max(0.0, options.mixWeights.textureWeight);
        }

        if (typeof options.mixWeights.rhythmWeight === "number") {
          basePolicy.rhythmWeight = Math.max(0.0, options.mixWeights.rhythmWeight);
        }
      }

      return basePolicy;
    };

    const normalizeMixedFrames = (frames, ceiling = 1003) => {
      const normalizedFrames = cloneFrameArray(frames);
      let maximumAmplitude = 0;

      for (const frame of normalizedFrames) {
        maximumAmplitude = Math.max(maximumAmplitude, frame.leftAmplitude ?? 0, frame.rightAmplitude ?? 0);
      }

      if (maximumAmplitude <= 0 || maximumAmplitude <= ceiling) {
        return normalizedFrames;
      }

      const scale = ceiling / maximumAmplitude;

      for (const frame of normalizedFrames) {
        frame.leftAmplitude = clampFrameAmplitude((frame.leftAmplitude ?? 0) * scale);
        frame.rightAmplitude = clampFrameAmplitude((frame.rightAmplitude ?? 0) * scale);
      }

      return normalizedFrames;
    };

    const mixHapticsClips = (clipEntries, options = {}) => {
      const entries = Array.isArray(clipEntries) ? clipEntries : [];
      const validEntries = entries.filter((entry) =>
        entry &&
        entry.clip &&
        Array.isArray(entry.clip.outputFrames) &&
        entry.clip.outputFrames.length > 0
      );

      if (validEntries.length === 0) {
        return {
          version: 1,
          frameIntervalMs: frameIntervalMs,
          durationMs: 0,
          profileName: "mixed",
          mixMode: "max",
          outputFrames: []
        };
      }

      const mixMode =
        options.mixMode === "add" || options.mixMode === "hybrid"
          ? options.mixMode
          : "max";
      const mixPolicyName =
        typeof options.mixPolicy === "string" ? options.mixPolicy : "balanced";
      const mixPolicy = buildResolvedMixPolicy(options);
      const shouldNormalize = options.normalize === true;
      const normalizeCeiling = clampFrameAmplitude(options.normalizeCeiling ?? 1003);

      let mixedDurationMs = 0;

      for (const entry of validEntries) {
        const clipOffsetMs = Math.max(0, Math.floor(entry.offsetMs ?? 0));
        const clipDurationMs =
          entry.clip.durationMs ??
          entry.clip.outputFrames[entry.clip.outputFrames.length - 1].endMs ??
          0;
        mixedDurationMs = Math.max(mixedDurationMs, clipOffsetMs + clipDurationMs);
      }

      const mixedFrames = buildSilentOutputFrames(mixedDurationMs);

      for (const entry of validEntries) {
        const clipOffsetMs = Math.max(0, Math.floor(entry.offsetMs ?? 0));
        const clipLeftGain = Math.max(0.0, Number(entry.leftGain ?? 1.0));
        const clipRightGain = Math.max(0.0, Number(entry.rightGain ?? 1.0));

        for (const sourceFrame of entry.clip.outputFrames) {
          const targetFrameIndex =
            Math.floor((clipOffsetMs + (sourceFrame.startMs ?? 0)) / frameIntervalMs);

          if (targetFrameIndex < 0 || targetFrameIndex >= mixedFrames.length) {
            continue;
          }

          const targetFrame = mixedFrames[targetFrameIndex];
          const sourceLeftAmplitude =
            clampFrameAmplitude((sourceFrame.leftAmplitude ?? 0) * clipLeftGain);
          const sourceRightAmplitude =
            clampFrameAmplitude((sourceFrame.rightAmplitude ?? 0) * clipRightGain);
          const sourceImpactLayer =
            Math.max(0.0, Math.min(1.0, (sourceFrame.impactLayer ?? 0.0) * mixPolicy.impactWeight));
          const sourceBodyLayer =
            Math.max(0.0, Math.min(1.0, (sourceFrame.bodyLayer ?? 0.0) * mixPolicy.bodyWeight));
          const sourceTextureLayer =
            Math.max(0.0, Math.min(1.0, (sourceFrame.textureLayer ?? 0.0) * mixPolicy.textureWeight));
          const sourceRhythmLayer =
            Math.max(0.0, Math.min(1.0, (sourceFrame.rhythmLayer ?? 0.0) * mixPolicy.rhythmWeight));

          targetFrame.leftAmplitude = mixAmplitudeValue(
            targetFrame.leftAmplitude,
            sourceLeftAmplitude,
            mixMode
          );
          targetFrame.rightAmplitude = mixAmplitudeValue(
            targetFrame.rightAmplitude,
            sourceRightAmplitude,
            mixMode
          );
          targetFrame.impactLayer =
            mixLayerValue(targetFrame.impactLayer, sourceImpactLayer, mixMode);
          targetFrame.bodyLayer =
            mixLayerValue(targetFrame.bodyLayer, sourceBodyLayer, mixMode);
          targetFrame.textureLayer =
            mixLayerValue(targetFrame.textureLayer, sourceTextureLayer, mixMode);
          targetFrame.rhythmLayer =
            mixLayerValue(targetFrame.rhythmLayer, sourceRhythmLayer, mixMode);
          targetFrame.leftPhaseStep =
            Math.max(targetFrame.leftPhaseStep, sourceFrame.leftPhaseStep ?? 1);
          targetFrame.rightPhaseStep =
            Math.max(targetFrame.rightPhaseStep, sourceFrame.rightPhaseStep ?? 1);
        }
      }

      const mixedOutputFrames =
        shouldNormalize
          ? normalizeMixedFrames(mixedFrames, normalizeCeiling)
          : mixedFrames;
      const outputDeviceKind =
        typeof options.deviceKind === "string" ? options.deviceKind : null;
      const finalOutputFrames =
        outputDeviceKind !== null
          ? adaptOutputFramesForDevice(mixedOutputFrames, outputDeviceKind)
          : mixedOutputFrames;

      return {
        version: 1,
        frameIntervalMs: frameIntervalMs,
        durationMs: mixedDurationMs,
        profileName: "mixed",
        mixMode: mixMode,
        mixPolicy: mixPolicyName,
        mixWeights: { ...mixPolicy },
        outputFrames: finalOutputFrames
      };
    };

)HTML";
auto kWebHidPlayerHtmlPart2N = R"HTML(

    const createMixerRuntime = (defaultOptions = {}) => {
      let nextEntryId = 1;
      const queuedEntries = [];
      let runtimeClockStartMs = performance.now();
      let lastUpdateFrameSlot = -1;
      let hasPrimedSingleFramePlayback = false;
      let hasSentIdleStopFrame = false;
      const runtimeDefaults = {
        deviceKind: typeof defaultOptions.deviceKind === "string"
          ? defaultOptions.deviceKind
          : null,
        mixMode:
          defaultOptions.mixMode === "add" || defaultOptions.mixMode === "hybrid"
            ? defaultOptions.mixMode
            : "max",
        mixPolicy:
          typeof defaultOptions.mixPolicy === "string"
            ? defaultOptions.mixPolicy
            : "balanced",
        mixWeights:
          defaultOptions.mixWeights && typeof defaultOptions.mixWeights === "object"
            ? { ...defaultOptions.mixWeights }
            : null,
        normalize: defaultOptions.normalize === true,
        normalizeCeiling: clampFrameAmplitude(defaultOptions.normalizeCeiling ?? 1003),
        stopWhenIdle:
          typeof defaultOptions.stopWhenIdle === "boolean"
            ? defaultOptions.stopWhenIdle
            : true
      };

      const buildResolvedOptions = (overrideOptions = {}) => {
        const resolvedMixWeights =
          overrideOptions.mixWeights && typeof overrideOptions.mixWeights === "object"
            ? { ...overrideOptions.mixWeights }
            : runtimeDefaults.mixWeights !== null
              ? { ...runtimeDefaults.mixWeights }
              : undefined;

        return {
          deviceKind:
            typeof overrideOptions.deviceKind === "string"
              ? overrideOptions.deviceKind
              : runtimeDefaults.deviceKind,
          mixMode:
            overrideOptions.mixMode === "add" || overrideOptions.mixMode === "hybrid"
              ? overrideOptions.mixMode
              : runtimeDefaults.mixMode,
          mixPolicy:
            typeof overrideOptions.mixPolicy === "string"
              ? overrideOptions.mixPolicy
              : runtimeDefaults.mixPolicy,
          mixWeights: resolvedMixWeights,
          normalize:
            typeof overrideOptions.normalize === "boolean"
              ? overrideOptions.normalize
              : runtimeDefaults.normalize,
          stopWhenIdle:
            typeof overrideOptions.stopWhenIdle === "boolean"
              ? overrideOptions.stopWhenIdle
              : runtimeDefaults.stopWhenIdle,
          normalizeCeiling: clampFrameAmplitude(
            overrideOptions.normalizeCeiling ?? runtimeDefaults.normalizeCeiling
          )
        };
      };

      const getElapsedMs = () => {
        return Math.max(0, Math.floor(performance.now() - runtimeClockStartMs));
      };

      const enqueueClipEntry = (clip, entryOptions = {}) => {
        if (!clip || !Array.isArray(clip.outputFrames)) {
          throw new Error("outputFrames を持つクリップが必要です。");
        }

        const queuedEntry = {
          id: nextEntryId++,
          clip: clip,
          tag:
            typeof entryOptions.tag === "string" && entryOptions.tag.length > 0
              ? entryOptions.tag
              : "",
          offsetMs: Math.max(0, Math.floor(entryOptions.offsetMs ?? 0)),
          leftGain: Math.max(0.0, Number(entryOptions.leftGain ?? 1.0)),
          rightGain: Math.max(0.0, Number(entryOptions.rightGain ?? 1.0)),
          durationMs: Math.max(
            0,
            Math.floor(
              clip.durationMs ??
              clip.outputFrames[clip.outputFrames.length - 1]?.endMs ??
              0
            )
          ),
          profileName: String(clip.profileName ?? "clip")
        };

        queuedEntries.push(queuedEntry);
        return {
          ...queuedEntry
        };
      };

      const buildSnapshot = () => {
        return queuedEntries.map((entry) => ({
          id: entry.id,
          tag: entry.tag,
          offsetMs: entry.offsetMs,
          leftGain: entry.leftGain,
          rightGain: entry.rightGain,
          durationMs: entry.durationMs,
          profileName: entry.profileName
        }));
      };

      const buildScheduledEntriesAt = (timeMs) => {
        const currentTimeMs = Math.max(0, Math.floor(timeMs));
        const scheduledEntries = [];

        for (const entry of queuedEntries) {
          const entryEndMs = entry.offsetMs + entry.durationMs;

          if (entryEndMs <= currentTimeMs) {
            continue;
          }

          scheduledEntries.push({
            clip: entry.clip,
            offsetMs: Math.max(0, entry.offsetMs - currentTimeMs),
            leftGain: entry.leftGain,
            rightGain: entry.rightGain
          });
        }

        return scheduledEntries;
      };

      const buildMixedClip = (overrideOptions = {}) => {
        const resolvedOptions = buildResolvedOptions(overrideOptions);
        const clipEntries = queuedEntries.map((entry) => ({
          clip: entry.clip,
          offsetMs: entry.offsetMs,
          leftGain: entry.leftGain,
          rightGain: entry.rightGain
        }));

        const mixedClip = mixHapticsClips(clipEntries, resolvedOptions);
        mixedClip.queueEntries = buildSnapshot();
        return mixedClip;
      };

      const pruneEndedEntries = (timeMs = getElapsedMs()) => {
        const currentTimeMs = Math.max(0, Math.floor(timeMs));
        const remainingEntries = queuedEntries.filter((entry) =>
          entry.offsetMs + entry.durationMs > currentTimeMs
        );
        const removedCount = queuedEntries.length - remainingEntries.length;

        queuedEntries.length = 0;

        for (const entry of remainingEntries) {
          queuedEntries.push(entry);
        }

        return removedCount;
      };

      const updateMixedFrameAt = async (timeMs, overrideOptions = {}) => {
        const currentTimeMs = Math.max(0, Math.floor(timeMs));
        const currentFrameSlot = Math.floor(currentTimeMs / frameIntervalMs);
        const resolvedOptions = buildResolvedOptions(overrideOptions);

        if (currentFrameSlot === lastUpdateFrameSlot) {
          return null;
        }

        lastUpdateFrameSlot = currentFrameSlot;
        pruneEndedEntries(currentTimeMs);
        const scheduledEntries = buildScheduledEntriesAt(currentTimeMs);

        if (scheduledEntries.length <= 0) {
          if (resolvedOptions.stopWhenIdle === true && hasSentIdleStopFrame === false) {
            await sendSingleOutputFrame(null, false);
            hasSentIdleStopFrame = true;
          }

          return {
            version: 1,
            frameIntervalMs: frameIntervalMs,
            durationMs: 0,
            profileName: "queued-idle",
            queueEntries: buildSnapshot(),
            timeMs: currentTimeMs,
            outputFrames: []
          };
        }

        hasSentIdleStopFrame = false;
        const mixedClip = mixHapticsClips(
          scheduledEntries,
          resolvedOptions
        );
        const firstFrame =
          Array.isArray(mixedClip.outputFrames) && mixedClip.outputFrames.length > 0
            ? mixedClip.outputFrames[0]
            : null;

        await sendSingleOutputFrame(firstFrame, hasPrimedSingleFramePlayback === false);
        hasPrimedSingleFramePlayback = true;
        mixedClip.queueEntries = buildSnapshot();
        mixedClip.timeMs = currentTimeMs;
        return mixedClip;
      };

      return {
        enqueueClip: (clip, entryOptions = {}) => {
          return enqueueClipEntry(clip, entryOptions);
        },
        enqueueNowClip: (clip, entryOptions = {}) => {
          const delayMs = Math.max(0, Math.floor(entryOptions.delayMs ?? 0));
          return enqueueClipEntry(clip, {
            ...entryOptions,
            offsetMs: getElapsedMs() + delayMs
          });
        },
        removeClip: (entryId) => {
          const removeIndex = queuedEntries.findIndex((entry) => entry.id === entryId);

          if (removeIndex < 0) {
            return false;
          }

          queuedEntries.splice(removeIndex, 1);
          return true;
        },
        clear: () => {
          queuedEntries.length = 0;
        },
        resetClock: (startOffsetMs = 0) => {
          runtimeClockStartMs = performance.now() - Math.max(0, Number(startOffsetMs ?? 0));
          lastUpdateFrameSlot = -1;
          hasPrimedSingleFramePlayback = false;
          hasSentIdleStopFrame = false;
          return getElapsedMs();
        },
        getElapsedMs: () => {
          return getElapsedMs();
        },
        pruneEnded: (timeMs = getElapsedMs()) => {
          return pruneEndedEntries(timeMs);
        },
        resetUpdateState: () => {
          lastUpdateFrameSlot = -1;
          hasPrimedSingleFramePlayback = false;
          hasSentIdleStopFrame = false;
        },
        getEntries: () => {
          return buildSnapshot();
        },
        getDefaults: () => {
          return {
            ...runtimeDefaults,
            mixWeights:
              runtimeDefaults.mixWeights !== null ? { ...runtimeDefaults.mixWeights } : null
          };
        },
        setDefaults: (partialDefaults = {}) => {
          if (typeof partialDefaults.deviceKind === "string" || partialDefaults.deviceKind === null) {
            runtimeDefaults.deviceKind = partialDefaults.deviceKind;
          }

          if (partialDefaults.mixMode === "max" ||
              partialDefaults.mixMode === "add" ||
              partialDefaults.mixMode === "hybrid") {
            runtimeDefaults.mixMode = partialDefaults.mixMode;
          }

          if (typeof partialDefaults.mixPolicy === "string") {
            runtimeDefaults.mixPolicy = partialDefaults.mixPolicy;
          }

          if (typeof partialDefaults.normalize === "boolean") {
            runtimeDefaults.normalize = partialDefaults.normalize;
          }

          if (typeof partialDefaults.normalizeCeiling === "number") {
            runtimeDefaults.normalizeCeiling =
              clampFrameAmplitude(partialDefaults.normalizeCeiling);
          }

          if (typeof partialDefaults.stopWhenIdle === "boolean") {
            runtimeDefaults.stopWhenIdle = partialDefaults.stopWhenIdle;
          }

          if (partialDefaults.mixWeights && typeof partialDefaults.mixWeights === "object") {
            runtimeDefaults.mixWeights = { ...partialDefaults.mixWeights };
          }

          if (partialDefaults.mixWeights === null) {
            runtimeDefaults.mixWeights = null;
          }

          return {
            ...runtimeDefaults,
            mixWeights:
              runtimeDefaults.mixWeights !== null ? { ...runtimeDefaults.mixWeights } : null
          };
        },
        buildClip: (overrideOptions = {}) => {
          return buildMixedClip(overrideOptions);
        },
        buildScheduledClipAt: (timeMs, overrideOptions = {}) => {
          const resolvedOptions = buildResolvedOptions(overrideOptions);
          const mixedClip = mixHapticsClips(
            buildScheduledEntriesAt(timeMs),
            resolvedOptions
          );
          mixedClip.queueEntries = buildSnapshot();
          mixedClip.timeMs = Math.max(0, Math.floor(timeMs));
          return mixedClip;
        },
        buildScheduledClipNow: (overrideOptions = {}) => {
          const currentTimeMs = getElapsedMs();
          const mixedClip = mixHapticsClips(
            buildScheduledEntriesAt(currentTimeMs),
            buildResolvedOptions(overrideOptions)
          );
          mixedClip.queueEntries = buildSnapshot();
          mixedClip.timeMs = currentTimeMs;
          return mixedClip;
        },
        play: async (overrideOptions = {}) => {
          const mixedClip = buildMixedClip(overrideOptions);
          await playHapticsClip(mixedClip);
          return mixedClip;
        },
        playScheduledAt: async (timeMs, overrideOptions = {}) => {
          const mixedClip = mixHapticsClips(
            buildScheduledEntriesAt(timeMs),
            buildResolvedOptions(overrideOptions)
          );
          mixedClip.queueEntries = buildSnapshot();
          mixedClip.timeMs = Math.max(0, Math.floor(timeMs));
          await playHapticsClip(mixedClip);
          return mixedClip;
        },
        playScheduledNow: async (overrideOptions = {}) => {
          const currentTimeMs = getElapsedMs();
          const mixedClip = mixHapticsClips(
            buildScheduledEntriesAt(currentTimeMs),
            buildResolvedOptions(overrideOptions)
          );
          mixedClip.queueEntries = buildSnapshot();
          mixedClip.timeMs = currentTimeMs;
          await playHapticsClip(mixedClip);
          return mixedClip;
        },
        updateAt: async (timeMs, overrideOptions = {}) => {
          return await updateMixedFrameAt(timeMs, overrideOptions);
        },
        updateNow: async (overrideOptions = {}) => {
          return await updateMixedFrameAt(getElapsedMs(), overrideOptions);
        }
      };
    };

    const defaultMixerRuntime = createMixerRuntime();
)HTML";
auto kWebHidPlayerHtmlPart2M = R"HTML(

    const createHapticsClipFromAudioBuffer = (audioBuffer, options = {}) => {
      const previousProfileName = currentHapticsProfileName;
      const previousAdaptiveTuning = { ...adaptiveTuning };
      const previousLayerTuning = { ...layerTuning };
      const previousRuntimeTuning = { ...runtimeTuning };
      const previousOutputDeviceProfiles = cloneOutputDeviceProfiles();

      try {
        if (typeof options.profileName === "string" &&
            Object.prototype.hasOwnProperty.call(hapticsProfiles, options.profileName)) {
          currentHapticsProfileName = options.profileName;
        }

        if (options.adaptiveTuning) {
          Object.assign(adaptiveTuning, options.adaptiveTuning);
        }

        if (options.layerTuning) {
          Object.assign(layerTuning, options.layerTuning);
        }

        if (options.runtimeTuning) {
          Object.assign(runtimeTuning, options.runtimeTuning);
        }

        if (options.outputDeviceProfiles && typeof options.outputDeviceProfiles === "object") {
          if (options.outputDeviceProfiles.switch &&
              typeof options.outputDeviceProfiles.switch === "object") {
            Object.assign(outputDeviceProfiles.switch, options.outputDeviceProfiles.switch);
          }

          if (options.outputDeviceProfiles.xbox &&
              typeof options.outputDeviceProfiles.xbox === "object") {
            Object.assign(outputDeviceProfiles.xbox, options.outputDeviceProfiles.xbox);
          }
        }

        const featureFrames = extractFeatureFramesFromAudioBuffer(audioBuffer);
        const layeredFrames = buildLayeredHapticFrames(featureFrames);
        const rawOutputFrames = renderOutputFramesFromLayers(layeredFrames);
        const outputFrames = refineOutputFramesForTactilePlayback(rawOutputFrames);
        const clipDurationMs = Math.floor(audioBuffer.duration * 1000.0);
        const previewSwitchFrames = adaptOutputFramesForDevice(outputFrames, "switch");
        const previewXboxFrames = adaptOutputFramesForDevice(outputFrames, "xbox");
        const previewSummaries = {
          switch: summarizeOutputFrames(previewSwitchFrames),
          xbox: summarizeOutputFrames(previewXboxFrames)
        };

        return {
          version: 1,
          frameIntervalMs: frameIntervalMs,
          durationMs: clipDurationMs,
          profileName: currentHapticsProfileName,
          featureStatistics: computeFeatureStatistics(featureFrames),
          adaptiveTuning: { ...adaptiveTuning },
          layerTuning: { ...layerTuning },
          runtimeTuning: { ...runtimeTuning },
          outputDeviceProfiles: cloneOutputDeviceProfiles(),
          featureFrames: cloneFrameArray(featureFrames),
          layeredFrames: cloneFrameArray(layeredFrames),
          outputFrames: cloneFrameArray(outputFrames),
          previewFrames: {
            switch: previewSwitchFrames,
            xbox: previewXboxFrames
          },
          previewSummaries: previewSummaries
        };
      } finally {
        currentHapticsProfileName = previousProfileName;
        Object.assign(adaptiveTuning, previousAdaptiveTuning);
        Object.assign(layerTuning, previousLayerTuning);
        Object.assign(runtimeTuning, previousRuntimeTuning);
        Object.assign(outputDeviceProfiles.switch, previousOutputDeviceProfiles.switch);
        Object.assign(outputDeviceProfiles.xbox, previousOutputDeviceProfiles.xbox);
      }
    };

    const createCurrentHapticsClip = () => {
      const clipDurationMs =
        currentDecodedAudioBuffer !== null
          ? Math.floor(currentDecodedAudioBuffer.duration * 1000.0)
          : (vibrationFrames.length > 0 ? vibrationFrames[vibrationFrames.length - 1].endMs : 0);
      const previewSwitchFrames = adaptOutputFramesForDevice(vibrationFrames, "switch");
      const previewXboxFrames = adaptOutputFramesForDevice(vibrationFrames, "xbox");
      const previewSummaries = {
        switch: summarizeOutputFrames(previewSwitchFrames),
        xbox: summarizeOutputFrames(previewXboxFrames)
      };

      return {
        version: 1,
        frameIntervalMs: frameIntervalMs,
        durationMs: clipDurationMs,
        profileName: currentHapticsProfileName,
        featureStatistics:
          lastFeatureStatistics !== null ? { ...lastFeatureStatistics } : null,
        adaptiveTuning: { ...adaptiveTuning },
        layerTuning: { ...layerTuning },
        runtimeTuning: { ...runtimeTuning },
        outputDeviceProfiles: cloneOutputDeviceProfiles(),
        featureFrames: cloneFrameArray(lastExtractedFeatureFrames),
        layeredFrames: cloneFrameArray(lastLayeredHapticFrames),
        outputFrames: cloneFrameArray(vibrationFrames),
        previewFrames: {
          switch: previewSwitchFrames,
          xbox: previewXboxFrames
        },
        previewSummaries: previewSummaries
      };
    };
)HTML";
auto kWebHidPlayerHtmlPart2E = R"HTML(

    const analyzeAudioArrayBuffer = async (arrayBuffer) => {
      const audioContext = new AudioContext();

      try {
        const audioBuffer = await audioContext.decodeAudioData(arrayBuffer.slice(0));
        currentDecodedAudioBuffer = audioBuffer;
        vibrationFrames = buildFramesFromAudioBuffer(audioBuffer);
        updatePreviewFrameStatuses();
      } finally {
        await audioContext.close();
      }
    };

    const loadAudioFile = async (file) => {
      const arrayBuffer = await file.arrayBuffer();
      currentAudioArrayBuffer = arrayBuffer.slice(0);
      await analyzeAudioArrayBuffer(arrayBuffer);

      currentFrameIndex = 0;
      setStatus("trackStatus", file.name);
      log("曲を読み込みました: " + file.name);
    };

    const loadDefaultTrackAnalysis = async () => {
      const response = await fetch("/BGM.wav", { cache: "no-store" });

      if (!response.ok) {
        throw new Error("既定の曲を取得できませんでした。");
      }

      const arrayBuffer = await response.arrayBuffer();
      currentAudioArrayBuffer = arrayBuffer.slice(0);
      await analyzeAudioArrayBuffer(arrayBuffer);
      setStatus("trackStatus", "BGM.wav");
      log("既定の曲を解析しました。");
    };

    const sendUsbData = async (bytes) => {
      const writeResult =
        await currentUsbDevice.transferOut(currentUsbEndpoint.endpointNumber, new Uint8Array(bytes));

      await delay(10);

      try {
        await currentUsbDevice.transferIn(currentUsbEndpoint.endpointNumber, 32);
      } catch (error) {
      }

      return writeResult;
    };

    const createSilentPattern = () => [0x00, 0x00, 0x00, 0x00, 0x00];

    const selectPattern = (amplitude, phaseStateName, phaseStep, toneOffset) => {
      const clampedAmplitude = Math.max(0, Math.min(1003, amplitude));

      if (clampedAmplitude <= 0) {
        if (phaseStateName === "left") {
          leftPatternPhase = 0;
        } else {
          rightPatternPhase = 0;
        }

        return createSilentPattern();
      }

     if (clampedAmplitude < runtimeTuning.activeThreshold) {
		  if (phaseStateName === "left") {
		    leftPatternPhase = 0;
		  } else {
		    rightPatternPhase = 0;
		  }

		  return createSilentPattern();
		}

		const normalizedAmplitude =
		  clampedAmplitude / 1003.0;

		const widenedAmplitude =
		  Math.pow(
		    Math.max(0.0, Math.min(1.0, normalizedAmplitude)),
		    runtimeTuning.shapeExponent
		  ) * 1003.0;

		const effectiveAmplitude =
		  clampAmplitude(widenedAmplitude);

		let activePatternCount = runtimeTuning.presetCountMin;
      if (runtimeTuning.fixedPresetEnabled) {
        activePatternCount = runtimeTuning.fixedPresetCount;
      } else {
        if (effectiveAmplitude >= 192) {
          activePatternCount = Math.max(activePatternCount, 12);
        }

        if (effectiveAmplitude >= 256) {
          activePatternCount = Math.max(activePatternCount, 16);
        }

        if (effectiveAmplitude >= 320) {
          activePatternCount = Math.max(activePatternCount, 20);
        }

        if (effectiveAmplitude >= 384) {
          activePatternCount = Math.max(activePatternCount, 24);
        }

        if (effectiveAmplitude >= 640) {
          activePatternCount = runtimeTuning.presetCountMax;
        }
      }

      activePatternCount = Math.max(1, Math.min(activePatternCount, Math.min(runtimeTuning.presetCountMax, hapticPatternTable.length)));

      let currentPhase = phaseStateName === "left" ? leftPatternPhase : rightPatternPhase;
      const patternIndex =
        Math.floor(currentPhase + toneOffset * (activePatternCount - 1)) % activePatternCount;
      const pattern = hapticPatternTable[patternIndex];
      currentPhase = (currentPhase + phaseStep) % activePatternCount;

      if (phaseStateName === "left") {
        leftPatternPhase = currentPhase;
      } else {
        rightPatternPhase = currentPhase;
      }

      return pattern;
    };

    const buildReport = (leftAmplitude, rightAmplitude) => {
      const leftPattern =
        selectPattern(leftAmplitude, "left", currentLeftPhaseStep, currentLeftToneOffset);
      const rightPattern =
        selectPattern(rightAmplitude, "right", currentRightPhaseStep, currentRightToneOffset);
      const report = new Uint8Array(64);
      const counterByte = 0x50 | (vibrationCounter & 0x0F);

      report[0] = 0x02;
      report[1] = counterByte;
      report[17] = counterByte;

      for (let byteIndex = 0; byteIndex < 5; byteIndex++) {
        report[2 + byteIndex] = leftPattern[byteIndex];
        report[18 + byteIndex] = rightPattern[byteIndex];
      }

      vibrationCounter = (vibrationCounter + 1) & 0x0F;
      return report;
    };

    const sendXboxRumble = async (leftAmplitude, rightAmplitude) => {
      const xboxGamepad = findXboxGamepad();

      if (xboxGamepad === null) {
        throw new Error("Xbox controller is not connected.");
      }

      currentXboxGamepadIndex = xboxGamepad.index;
      currentOutputDeviceKind = "xbox";

      const adaptedPair =
        adaptAmplitudePairForDevice(leftAmplitude, rightAmplitude, "xbox");

      const strongMagnitude =
        Math.max(0.0, Math.min(1.0, adaptedPair.leftAmplitude / 1003.0));
      const weakMagnitude =
        Math.max(0.0, Math.min(1.0, adaptedPair.rightAmplitude / 1003.0));
      const effectDurationMs = Math.max(16, frameIntervalMs);

      await xboxGamepad.vibrationActuator.playEffect("dual-rumble", {
        startDelay: 0,
        duration: effectDurationMs,
        strongMagnitude: strongMagnitude,
        weakMagnitude: weakMagnitude
      });
    };

    const sendReport = async (leftAmplitude, rightAmplitude) => {
      if (currentOutputDeviceKind === "xbox" || (!isSwitchReady() && isXboxReady())) {
        await sendXboxRumble(leftAmplitude, rightAmplitude);
        return;
      }

      if (currentHidDevice === null) {
        throw new Error("HID is not connected.");
      }

      const adaptedPair =
        adaptAmplitudePairForDevice(leftAmplitude, rightAmplitude, "switch");
      const report = buildReport(adaptedPair.leftAmplitude, adaptedPair.rightAmplitude);
      await currentHidDevice.sendReport(report[0], report.slice(1));
    };

    const normalizeAmplitude = (strength) => {
      const clampedStrength = Math.max(0.0, Math.min(1.0, strength));
      return Math.floor(clampedStrength * 1003.0);
    };

    const primeHaptics = async () => {
      if (currentOutputDeviceKind === "xbox") {
        await sendReport(1003, 1003);
        await delay(16);
        await sendReport(0, 0);
        return;
      }

      for (let packetIndex = 0; packetIndex < 12; packetIndex++) {
        await sendReport(1003, 1003);
        await delay(4);
      }
    };

    const reinitializeUsbOutput = async () => {
      if (!currentUsbDevice || !currentUsbEndpoint) {
        throw new Error("USBが未接続です。");
      }

      for (const command of initCommands) {
        await sendUsbData(command);
      }
    };

    const getFrameAt = (currentTimeMs) => {
      while (
        currentFrameIndex + 1 < vibrationFrames.length &&
        currentTimeMs >= vibrationFrames[currentFrameIndex].endMs
      ) {
        currentFrameIndex++;
      }

      const currentFrame = vibrationFrames[currentFrameIndex];

      if (!currentFrame) {
        return null;
      }

      if (currentTimeMs >= currentFrame.startMs && currentTimeMs < currentFrame.endMs) {
        return currentFrame;
      }

      return null;
    };

    const ensureManualRumbleReady = async (shouldPrime) => {
      if (isSwitchReady()) {
        currentOutputDeviceKind = "switch";
        await reinitializeUsbOutput();

        if (shouldPrime) {
          await primeHaptics();
        }

        return;
      }

      if (isXboxReady()) {
        currentOutputDeviceKind = "xbox";

        if (shouldPrime) {
          await primeHaptics();
        }

        return;
      }

      throw new Error("No supported controller is connected.");
    };
)HTML";
auto kWebHidPlayerHtmlPart2D = R"HTML(

    const stopPlayback = async () => {
      playbackSessionId++;
      playbackTimer = null;
	stopSpectrum();
      playbackStartPerformanceMs = 0.0;

      if (playbackAudioSource !== null) {
        try {
          playbackAudioSource.stop();
        } catch (error) {
        }

        playbackAudioSource = null;
      }

      if (playbackAudioContext !== null) {
        try {
          await playbackAudioContext.close();
        } catch (error) {
        }

        playbackAudioContext = null;
      }

      vibrationCounter = 0;
      leftPatternPhase = 0;
      rightPatternPhase = 0;
      currentFrameIndex = 0;
      currentLeftPhaseStep = 1;
      currentRightPhaseStep = 1;
      currentLeftToneOffset = 0.0;
      currentRightToneOffset = 0.0;

      if (currentOutputDeviceKind !== "none") {
        try {
          for (let stopIndex = 0; stopIndex < 4; stopIndex++) {
            await sendReport(0, 0);
            await delay(10);
          }
        } catch (error) {
          log("Stop report failed: " + error.message);
        }
      }

      setStatus("playbackStatus", "待機");
      updateButtons();
    };

    const stopManualRumble = async () => {
      if (currentOutputDeviceKind === "none") {
        return;
      }

      for (let stopIndex = 0; stopIndex < 4; stopIndex++) {
        await sendReport(0, 0);
        await delay(10);
      }
    };

    const testRightNoise = async () => {
      playbackSessionId++;
      playbackTimer = null;
      await ensureManualRumbleReady(true);
      currentLeftPhaseStep = 1;
      currentRightPhaseStep = 1;
      currentLeftToneOffset = 0.0;
      currentRightToneOffset = 0.0;

      log("右だけ診断を開始します。異音が出た値を見てください。");

      for (let cadenceValue = runtimeTuning.rightCadenceMin;
           cadenceValue <= runtimeTuning.rightCadenceMax;
           cadenceValue++) {
        for (let pulseWidthValue = 1;
             pulseWidthValue <= runtimeTuning.rightPulseWidthMax;
             pulseWidthValue++) {
          log(
            "診断 cadence=" + cadenceValue +
            " pulse=" + pulseWidthValue +
            " amp=" + runtimeTuning.testRightAmplitude +
              " presetMax=" + runtimeTuning.presetCountMax +
              " fixed=" + (runtimeTuning.fixedPresetEnabled ? runtimeTuning.fixedPresetCount : "auto")
            );

          for (let frameIndex = 0; frameIndex < 64; frameIndex++) {
            const isActive =
              (frameIndex % cadenceValue) < pulseWidthValue;
            const rightAmplitude = isActive
              ? Math.max(runtimeTuning.rightMinAmplitude, runtimeTuning.testRightAmplitude)
              : 0;
            await sendReport(0, rightAmplitude);
            await delay(frameIntervalMs);
          }

          for (let stopIndex = 0; stopIndex < 8; stopIndex++) {
            await sendReport(0, 0);
            await delay(frameIntervalMs);
          }
        }
      }

      await stopManualRumble();
    };
)HTML";
auto kWebHidPlayerHtmlPart2C = R"HTML(

    const rumbleOnce = async (leftStrength = 1.0, rightStrength = 1.0, durationMs = 120) => {
      playbackSessionId++;
      playbackTimer = null;
      await ensureManualRumbleReady(true);

      const leftAmplitude = normalizeAmplitude(leftStrength);
      const rightAmplitude = normalizeAmplitude(rightStrength);
      const repeatCount = Math.max(1, Math.floor(durationMs / frameIntervalMs));

      for (let repeatIndex = 0; repeatIndex < repeatCount; repeatIndex++) {
        await sendReport(leftAmplitude, rightAmplitude);
        await delay(frameIntervalMs);
      }

      await stopManualRumble();
    };

    const rumblePulse = async (
      leftStrength = 1.0,
      rightStrength = 1.0,
      onDurationMs = 80,
      offDurationMs = 40,
      pulseCount = 1
    ) => {
      playbackSessionId++;
      playbackTimer = null;
      await ensureManualRumbleReady(true);

      const leftAmplitude = normalizeAmplitude(leftStrength);
      const rightAmplitude = normalizeAmplitude(rightStrength);

      for (let pulseIndex = 0; pulseIndex < pulseCount; pulseIndex++) {
        const onRepeatCount = Math.max(1, Math.floor(onDurationMs / frameIntervalMs));

        for (let repeatIndex = 0; repeatIndex < onRepeatCount; repeatIndex++) {
          await sendReport(leftAmplitude, rightAmplitude);
          await delay(frameIntervalMs);
        }

        const offRepeatCount = Math.max(1, Math.floor(offDurationMs / frameIntervalMs));

        for (let repeatIndex = 0; repeatIndex < offRepeatCount; repeatIndex++) {
          await sendReport(0, 0);
          await delay(frameIntervalMs);
        }
      }

      await stopManualRumble();
    };

    const rumbleSequence = async (sequence) => {
      playbackSessionId++;
      playbackTimer = null;
      await ensureManualRumbleReady(true);

      for (const step of sequence) {
        const leftAmplitude = normalizeAmplitude(step.leftStrength ?? 0.0);
        const rightAmplitude = normalizeAmplitude(step.rightStrength ?? 0.0);
        const durationMs = Math.max(frameIntervalMs, step.durationMs ?? frameIntervalMs);
        const repeatCount = Math.max(1, Math.floor(durationMs / frameIntervalMs));

        for (let repeatIndex = 0; repeatIndex < repeatCount; repeatIndex++) {
          await sendReport(leftAmplitude, rightAmplitude);
          await delay(frameIntervalMs);
        }
      }

      await stopManualRumble();
    };

    const playOutputFrames = async (outputFrames) => {
      playbackSessionId++;
      const localSessionId = playbackSessionId;
      playbackTimer = localSessionId;
      vibrationCounter = 0;
      leftPatternPhase = 0;
      rightPatternPhase = 0;
      currentFrameIndex = 0;
      currentLeftPhaseStep = 1;
      currentRightPhaseStep = 1;
      currentLeftToneOffset = 0.0;
      currentRightToneOffset = 0.0;
      setStatus("playbackStatus", "再生中");
      updateButtons();

      await ensureManualRumbleReady(true);

      for (const outputFrame of outputFrames) {
        if (playbackTimer !== localSessionId) {
          break;
        }

        currentLeftPhaseStep = outputFrame.leftPhaseStep ?? 1;
        currentRightPhaseStep = outputFrame.rightPhaseStep ?? 1;
        currentLeftToneOffset = outputFrame.leftToneOffset ?? 0.0;
        currentRightToneOffset = outputFrame.rightToneOffset ?? 0.0;
        await sendReport(outputFrame.leftAmplitude ?? 0, outputFrame.rightAmplitude ?? 0);
        await delay(frameIntervalMs);
      }

      if (playbackTimer === localSessionId) {
        await stopManualRumble();
      }
    };

    const playHapticsClip = async (clip) => {
      if (!clip || !Array.isArray(clip.outputFrames)) {
        throw new Error("outputFrames を持つクリップが必要です。");
      }

      await playOutputFrames(clip.outputFrames);
    };

    const sendSingleOutputFrame = async (outputFrame, shouldPrime = false) => {
      const targetFrame = outputFrame || {
        leftAmplitude: 0,
        rightAmplitude: 0,
        leftPhaseStep: 1,
        rightPhaseStep: 1,
        leftToneOffset: 0.0,
        rightToneOffset: 0.0
      };

      await ensureManualRumbleReady(shouldPrime);

      currentLeftPhaseStep = targetFrame.leftPhaseStep ?? 1;
      currentRightPhaseStep = targetFrame.rightPhaseStep ?? 1;
      currentLeftToneOffset = targetFrame.leftToneOffset ?? 0.0;
      currentRightToneOffset = targetFrame.rightToneOffset ?? 0.0;
      await sendReport(targetFrame.leftAmplitude ?? 0, targetFrame.rightAmplitude ?? 0);
    };
)HTML";
auto kWebHidPlayerHtmlPart2A = R"HTML(
    const playSynced = async () => {
      if (currentDecodedAudioBuffer === null) {
        throw new Error("Audio buffer is not loaded.");
      }

      playbackSessionId++;
      const localSessionId = playbackSessionId;
      playbackTimer = localSessionId;
      vibrationCounter = 0;
      leftPatternPhase = 0;
      rightPatternPhase = 0;
      currentFrameIndex = 0;
      currentLeftPhaseStep = 1;
      currentRightPhaseStep = 1;
      currentLeftToneOffset = 0.0;
      currentRightToneOffset = 0.0;
      setStatus("playbackStatus", "準備中");
      updateButtons();

      await ensureManualRumbleReady(true);

      playbackAudioContext = new AudioContext();
      playbackAudioSource = playbackAudioContext.createBufferSource();
      playbackAudioSource.buffer = currentDecodedAudioBuffer;
      spectrumAnalyser = playbackAudioContext.createAnalyser();
		spectrumAnalyser.fftSize = 4096;
		spectrumDataArray = new Uint8Array(spectrumAnalyser.frequencyBinCount);

		playbackAudioSource.connect(spectrumAnalyser);
		spectrumAnalyser.connect(playbackAudioContext.destination);

		drawSpectrum();

  const startDelaySeconds = 0.05;
	const hapticDelayMs = 25;

	const scheduledStartTime =
	  playbackAudioContext.currentTime + startDelaySeconds;

	playbackStartPerformanceMs =
	  performance.now() + startDelaySeconds * 1000.0;

	setStatus("playbackTimeStatus", "00:00.000");
	playbackAudioSource.start(scheduledStartTime);
	setStatus("playbackStatus", "再生中");
)HTML";
auto kWebHidPlayerHtmlPart2B = R"HTML(

     while (playbackTimer === localSessionId) {
        try {
         const audioTimeMs = Math.max(
		  0,
		  Math.floor(performance.now() - playbackStartPerformanceMs)
		);

		const currentTimeMs = Math.max(
		  0,
		  audioTimeMs - hapticDelayMs
		);

          setStatus(
            "playbackTimeStatus",
           formatPlaybackTime(audioTimeMs / 1000.0)
          );

          if (currentTimeMs >= Math.floor(currentDecodedAudioBuffer.duration * 1000.0)) {
            break;
          }

          const currentFrame = getFrameAt(currentTimeMs);

          if (currentFrame === null) {
            currentLeftPhaseStep = 1;
            currentRightPhaseStep = 1;
            currentLeftToneOffset = 0.0;
            currentRightToneOffset = 0.0;
            await sendReport(0, 0);
          } else {
            currentLeftPhaseStep = currentFrame.leftPhaseStep ?? 1;
            currentRightPhaseStep = currentFrame.rightPhaseStep ?? 1;
            currentLeftToneOffset = currentFrame.leftToneOffset ?? 0.0;
            currentRightToneOffset = currentFrame.rightToneOffset ?? 0.0;
            await sendReport(currentFrame.leftAmplitude, currentFrame.rightAmplitude);
          }
        } catch (error) {
          log("再生に失敗しました: " + error.message);
          break;
        }

        await delay(frameIntervalMs);
      }

      if (playbackTimer === localSessionId) {
        await stopPlayback();
      }
    };
)HTML";
auto kWebHidPlayerHtmlPart3 = R"HTML(
    const connectUsb = async () => {
      const device = await navigator.usb.requestDevice({
        filters: [{ vendorId: vendorId, productId: productId }]
      });

      await device.open();

      if (!device.configuration) {
        await device.selectConfiguration(1);
      }

      await device.claimInterface(usbInterfaceNumber);

      const usbInterface = device.configuration.interfaces[usbInterfaceNumber];
      const endpointOut =
        usbInterface.alternate.endpoints.find((endpoint) =>
          endpoint.direction === "out" && endpoint.type === "bulk");

      if (!endpointOut) {
        throw new Error("Bulk OUT endpoint not found.");
      }

      currentUsbDevice = device;
      currentUsbEndpoint = endpointOut;
      currentOutputDeviceKind = "switch";
      setStatus("usbStatus", "接続");
      log("USBを接続しました。");

      for (const command of initCommands) {
        await sendUsbData(command);
      }

      log("USB初期化が完了しました。");
      //await autoApplyPreferredPresetForDevice("switch");
      updateButtons();
    };

    const connectHid = async () => {
      const devices = await navigator.hid.requestDevice({
        filters: [{ vendorId: vendorId, productId: productId }]
      });

      if (devices.length === 0) {
        throw new Error("No HID device selected.");
      }

      const device = devices[0];

      if (!device.opened) {
        await device.open();
      }

      currentHidDevice = device;
      currentOutputDeviceKind = "switch";
      setStatus("hidStatus", "接続");
      log("HIDを接続しました: " + device.productName);
      //await autoApplyPreferredPresetForDevice("switch");
      updateButtons();
    };

    const detectDevices = () => {
      const xboxGamepad = findXboxGamepad();

      return {
        switchUsbConnected: currentUsbDevice !== null && currentUsbEndpoint !== null,
        switchHidConnected: currentHidDevice !== null,
        xboxConnected: xboxGamepad !== null,
        activeDeviceKind: currentOutputDeviceKind
      };
    };

    const connectXbox = async () => {
      const xboxGamepad = findXboxGamepad();

      if (xboxGamepad === null) {
        throw new Error("Xbox controller with rumble was not found.");
      }

      currentXboxGamepadIndex = xboxGamepad.index;
      currentOutputDeviceKind = "xbox";
      refreshOutputStatus();
      log("Xboxコントローラーを検出しました: " + xboxGamepad.id);
      await autoApplyPreferredPresetForDevice("xbox");
      updateButtons();
    };

    const getInputState = () => {
      const xboxGamepad = findXboxGamepad();

      if (xboxGamepad !== null) {
        return {
          deviceKind: "xbox",
          buttons: xboxGamepad.buttons.map((button, index) => ({
            index: index,
            pressed: button.pressed === true,
            value: button.value
          })),
          axes: Array.from(xboxGamepad.axes)
        };
      }

      return {
        deviceKind: currentOutputDeviceKind,
        buttons: [],
        axes: []
      };
    };

    const disconnectDevices = async () => {
      await stopPlayback();
      defaultMixerRuntime.resetUpdateState();

      if (currentHidDevice !== null && currentHidDevice.opened) {
        await currentHidDevice.close();
      }

      if (currentUsbDevice !== null && currentUsbDevice.opened) {
        try {
          await currentUsbDevice.releaseInterface(usbInterfaceNumber);
        } catch (error) {
        }

        await currentUsbDevice.close();
      }

      currentUsbDevice = null;
      currentUsbEndpoint = null;
      currentHidDevice = null;
      currentXboxGamepadIndex = -1;
      currentOutputDeviceKind = "none";
      refreshOutputStatus();
      updateButtons();
      log("デバイスを切断しました。");
    };
)HTML";
auto kWebHidPlayerHtmlPart4 = R"HTML(

    document.getElementById("connectUsbButton").addEventListener("click", async () => {
      try {
        await connectUsb();
      } catch (error) {
        log("USB接続に失敗しました: " + error.message);
      }
    });

    document.getElementById("connectHidButton").addEventListener("click", async () => {
      try {
        await connectHid();
      } catch (error) {
        log("HID接続に失敗しました: " + error.message);
      }
    });

    document.getElementById("playButton").addEventListener("click", async () => {
      try {
        await playSynced();
      } catch (error) {
        log("同期再生に失敗しました: " + error.message);
      }
    });

    document.getElementById("stopButton").addEventListener("click", async () => {
      await stopPlayback();
    });

    document.getElementById("testRightNoiseButton").addEventListener("click", async () => {
      try {
        await testRightNoise();
      } catch (error) {
        log("右だけ診断に失敗しました: " + error.message);
      }
    });

    document.getElementById("applyTuningPresetButton").addEventListener("click", async () => {
      const presetSelectElement = document.getElementById("tuningPresetSelect");
      const presetName = String(presetSelectElement.value || "");

      try {
        await window.feelKitAudioToHaptics.applyTuningPreset(presetName, { rebuild: true });
        log("既定セットを適用しました: " + presetName);
      } catch (error) {
        log("既定セットの適用に失敗しました: " + error.message);
      }
    });

    document.getElementById("saveCustomPresetButton").addEventListener("click", () => {
      try {
        window.feelKitAudioToHaptics.saveCurrentTuningAsCustomPreset("custom");
        log("現在値を custom として保存しました。");
      } catch (error) {
        log("custom 保存に失敗しました: " + error.message);
      }
    });

    document.getElementById("saveCustomSwitchPresetButton").addEventListener("click", () => {
      try {
        window.feelKitAudioToHaptics.saveCurrentTuningAsCustomPreset("customSwitch");
        log("現在値を customSwitch として保存しました。");
      } catch (error) {
        log("customSwitch 保存に失敗しました: " + error.message);
      }
    });

    document.getElementById("saveCustomXboxPresetButton").addEventListener("click", () => {
      try {
        window.feelKitAudioToHaptics.saveCurrentTuningAsCustomPreset("customXbox");
        log("現在値を customXbox として保存しました。");
      } catch (error) {
        log("customXbox 保存に失敗しました: " + error.message);
      }
    });

    document.getElementById("resetTuningButton").addEventListener("click", async () => {
      try {
        await resetAllTuningState();
      } catch (error) {
        log("調整の初期化に失敗しました: " + error.message);
      }
    });

    for (const [, inputId] of tuningFields) {
      document.getElementById(inputId).addEventListener("input", () => {
        updateTuningFromInputs();
      });

      document.getElementById(inputId).addEventListener("change", async () => {
        updateTuningFromInputs();

        try {
          await rebuildFramesFromCurrentTrack();
        } catch (error) {
          log("調整更新に失敗しました: " + error.message);
        }
      });
    }

    for (const [, inputId] of layerTuningFields) {
      document.getElementById(inputId).addEventListener("input", () => {
        updateLayerTuningFromInputs();
      });

      document.getElementById(inputId).addEventListener("change", async () => {
        updateLayerTuningFromInputs();

        try {
          await rebuildFramesFromCurrentTrack();
        } catch (error) {
          log("レイヤー調整更新に失敗しました: " + error.message);
        }
      });
    }

    for (const [, , inputId] of outputDeviceProfileFields) {
      document.getElementById(inputId).addEventListener("input", () => {
        updateOutputDeviceProfilesFromInputs();
      });

      document.getElementById(inputId).addEventListener("change", async () => {
        updateOutputDeviceProfilesFromInputs();

        try {
          await rebuildFramesFromCurrentTrack();
        } catch (error) {
          log("デバイス補正更新に失敗しました: " + error.message);
        }
      });
    }

    document.getElementById("fixedPresetEnabledInput").addEventListener("change", async () => {
      updateTuningFromInputs();

      try {
        await rebuildFramesFromCurrentTrack();
      } catch (error) {
        log("調整更新に失敗しました: " + error.message);
      }
    });

    window.addEventListener("gamepadconnected", async (event) => {
      const gamepadId = String(event.gamepad?.id || "");
      log("ゲームパッド接続: " + gamepadId);
      refreshOutputStatus();
      if (findXboxGamepad() !== null) {
        try {
          await autoApplyPreferredPresetForDevice("xbox");
        } catch (error) {
          log("Xbox既定セットの自動適用に失敗しました: " + error.message);
        }
      }
      updateButtons();
    });

    window.addEventListener("gamepaddisconnected", (event) => {
      const gamepadId = String(event.gamepad?.id || "");
      log("ゲームパッド切断: " + gamepadId);
      refreshOutputStatus();
      updateButtons();
    });
)HTML";
auto kWebHidPlayerHtmlPart5 = R"HTML(

    window.feelKitGamepadHaptics = {
      detectDevices: () => {
        return detectDevices();
      },
      connectSwitchUsb: async () => {
        await connectUsb();
        return detectDevices();
      },
      connectSwitchHid: async () => {
        await connectHid();
        return detectDevices();
      },
      connectSwitch: async (options = {}) => {
        const shouldConnectUsb = options.usb !== false;
        const shouldConnectHid = options.hid !== false;

        if (shouldConnectUsb) {
          await connectUsb();
        }

        if (shouldConnectHid) {
          await connectHid();
        }

        return detectDevices();
      },
      connectXbox: async () => {
        await connectXbox();
        return detectDevices();
      },
      setActiveDevice: async (deviceKind) => {
        if (deviceKind === "switch") {
          if (!isSwitchReady()) {
            throw new Error("Switch controller is not ready.");
          }

          currentOutputDeviceKind = "switch";
          updateButtons();
          return detectDevices();
        }

        if (deviceKind === "xbox") {
          if (!isXboxReady()) {
            throw new Error("Xbox controller is not ready.");
          }

          await connectXbox();
          return detectDevices();
        }

        if (deviceKind === "auto") {
          refreshOutputStatus();
          updateButtons();
          return detectDevices();
        }

        throw new Error("Unknown device kind: " + deviceKind);
      },
      disconnect: async () => {
        await disconnectDevices();
        return detectDevices();
      },
      getInputState: () => {
        return getInputState();
      },
      getActiveDevice: () => {
        return currentOutputDeviceKind;
      },
      prepare: async () => {
        await ensureManualRumbleReady(true);
        return detectDevices();
      },
      loadAudioFile: async (file) => {
        await loadAudioFile(file);
      },
      reloadCurrentAudio: async () => {
        await rebuildFramesFromCurrentTrack();
      },
      playLoadedTrack: async () => {
        await playSynced();
      },
      stop: async () => {
        await stopPlayback();
      },
      rumbleOnce: async (leftStrength = 1.0, rightStrength = 1.0, durationMs = 120) => {
        await rumbleOnce(leftStrength, rightStrength, durationMs);
      },
      rumblePulse: async (
        leftStrength = 1.0,
        rightStrength = 1.0,
        onDurationMs = 80,
        offDurationMs = 40,
        pulseCount = 1
      ) => {
        await rumblePulse(leftStrength, rightStrength, onDurationMs, offDurationMs, pulseCount);
      },
      rumbleSequence: async (sequence) => {
        await rumbleSequence(sequence);
      },
      playOutputFrames: async (outputFrames) => {
        await playOutputFrames(outputFrames);
      },
      playClip: async (clip) => {
        await playHapticsClip(clip);
      },
      mixAndPlayClips: async (clipEntries, options = {}) => {
        const mixedClip = mixHapticsClips(clipEntries, options);
        await playHapticsClip(mixedClip);
        return mixedClip;
      },
      queueClip: (clip, entryOptions = {}) => {
        return defaultMixerRuntime.enqueueClip(clip, entryOptions);
      },
      queueClipNow: (clip, entryOptions = {}) => {
        return defaultMixerRuntime.enqueueNowClip(clip, entryOptions);
      },
      removeQueuedClip: (entryId) => {
        return defaultMixerRuntime.removeClip(entryId);
      },
      clearQueuedClips: () => {
        defaultMixerRuntime.clear();
      },
      getQueuedClips: () => {
        return defaultMixerRuntime.getEntries();
      },
      resetQueueClock: (startOffsetMs = 0) => {
        return defaultMixerRuntime.resetClock(startOffsetMs);
      },
      getQueueElapsedMs: () => {
        return defaultMixerRuntime.getElapsedMs();
      },
      pruneQueuedClips: (timeMs) => {
        return defaultMixerRuntime.pruneEnded(timeMs);
      },
      updateQueuedAt: async (timeMs, options = {}) => {
        return await defaultMixerRuntime.updateAt(timeMs, options);
      },
      updateQueuedNow: async (options = {}) => {
        return await defaultMixerRuntime.updateNow(options);
      },
      update: async (options = {}) => {
        return await defaultMixerRuntime.updateNow(options);
      },
      resetQueuedUpdateState: () => {
        defaultMixerRuntime.resetUpdateState();
      },
      buildQueuedClipNow: (options = {}) => {
        return defaultMixerRuntime.buildScheduledClipNow(options);
      },
      playQueuedNow: async (options = {}) => {
        return await defaultMixerRuntime.playScheduledNow(options);
      },
      playOneShot: async (clip, entryOptions = {}, updateOptions = {}) => {
        const entryId = defaultMixerRuntime.enqueueNowClip(clip, entryOptions);
        const outputFrame = await defaultMixerRuntime.updateNow(updateOptions);

        return {
          entryId: entryId,
          outputFrame: outputFrame
        };
      },
      createMixerRuntime: (defaultOptions = {}) => {
        return createMixerRuntime(defaultOptions);
      },
      getDefaultMixerRuntime: () => {
        return defaultMixerRuntime;
      }
    };
)HTML";
auto kWebHidPlayerHtmlPart5H = R"HTML(

    window.feelKitAudioToHaptics = {
      setProfile: async (profileName) => {
        if (!Object.prototype.hasOwnProperty.call(hapticsProfiles, profileName)) {
          throw new Error("Unknown haptics profile: " + profileName);
        }

        currentHapticsProfileName = profileName;
        await rebuildFramesFromCurrentTrack();
        return currentHapticsProfileName;
      },
      getProfile: () => {
        return currentHapticsProfileName;
      },
      getProfiles: () => {
        return Object.keys(hapticsProfiles);
      },
      getTuningPresets: () => {
        return getAllTuningPresetNames();
      },
      getTuningPresetSnapshot: (presetName) => {
        const presetSnapshot = findTuningPresetSnapshot(presetName);

        if (presetSnapshot === null) {
          throw new Error("Unknown tuning preset: " + presetName);
        }

        return JSON.parse(JSON.stringify(presetSnapshot));
      },
      applyTuningPreset: async (presetName, options = {}) => {
        const presetSnapshot = findTuningPresetSnapshot(presetName);

        if (presetSnapshot === null) {
          throw new Error("Unknown tuning preset: " + presetName);
        }

        const shouldRebuild = options.rebuild !== false;
        const appliedSnapshot =
          await applyFullTuningSnapshot(
            JSON.parse(JSON.stringify(presetSnapshot)),
            shouldRebuild
          );
        currentTuningPresetName = presetName;
        syncTuningPresetUi();
        return appliedSnapshot;
      },
      saveCurrentTuningAsCustomPreset: (presetName = "custom") => {
        return saveCurrentTuningAsCustomPreset(presetName);
      },
      getCurrentTuningPresetName: () => {
        return currentTuningPresetName;
      },
      getTuningSnapshot: () => {
        return buildFullTuningSnapshot();
      },
      applyTuningSnapshot: async (snapshot, options = {}) => {
        const shouldRebuild = options.rebuild !== false;
        return await applyFullTuningSnapshot(snapshot, shouldRebuild);
      },
      resetAllTuning: async () => {
        return await resetAllTuningState();
      },
      getAdaptiveTuning: () => {
        return { ...adaptiveTuning };
      },
      setAdaptiveTuning: async (partialTuning) => {
        Object.assign(adaptiveTuning, partialTuning || {});
        await rebuildFramesFromCurrentTrack();
        return { ...adaptiveTuning };
      },
      resetAdaptiveTuning: async () => {
        Object.assign(adaptiveTuning, defaultAdaptiveTuning);
        await rebuildFramesFromCurrentTrack();
        return { ...adaptiveTuning };
      },
      getLayerTuning: () => {
        return { ...layerTuning };
      },
      setLayerTuning: async (partialTuning) => {
        Object.assign(layerTuning, partialTuning || {});
        await rebuildFramesFromCurrentTrack();
        return { ...layerTuning };
      },
      resetLayerTuning: async () => {
        Object.assign(layerTuning, defaultLayerTuning);
        await rebuildFramesFromCurrentTrack();
        return { ...layerTuning };
      },
      getOutputDeviceProfiles: () => {
        return cloneOutputDeviceProfiles();
      },
      setOutputDeviceProfiles: async (partialProfiles) => {
        if (partialProfiles && typeof partialProfiles === "object") {
          if (partialProfiles.switch && typeof partialProfiles.switch === "object") {
            Object.assign(outputDeviceProfiles.switch, partialProfiles.switch);
          }

          if (partialProfiles.xbox && typeof partialProfiles.xbox === "object") {
            Object.assign(outputDeviceProfiles.xbox, partialProfiles.xbox);
          }
        }

        await rebuildFramesFromCurrentTrack();
        return cloneOutputDeviceProfiles();
      },
      resetOutputDeviceProfiles: async () => {
        Object.assign(outputDeviceProfiles.switch, defaultOutputDeviceProfiles.switch);
        Object.assign(outputDeviceProfiles.xbox, defaultOutputDeviceProfiles.xbox);
        await rebuildFramesFromCurrentTrack();
        return cloneOutputDeviceProfiles();
      },
      getMixPolicies: () => {
        return Object.keys(mixPolicies);
      },
      getMixPolicyDefaults: () => {
        return JSON.parse(JSON.stringify(mixPolicies));
      },
      adaptOutputFrame: (frame, deviceKind) => {
        return adaptAmplitudePairForDevice(
          frame.leftAmplitude ?? 0,
          frame.rightAmplitude ?? 0,
          deviceKind,
          frame
        );
      },
      extractFeatureFramesFromCurrentAudio: () => {
        return lastExtractedFeatureFrames.slice();
      },
      getFeatureStatistics: () => {
        return lastFeatureStatistics !== null ? { ...lastFeatureStatistics } : null;
      },
      extractLayerFramesFromCurrentAudio: () => {
        return lastLayeredHapticFrames.slice();
      },
      getOutputFrames: () => {
        return vibrationFrames.slice();
      },
      getPreviewFrameSummaries: () => {
        return buildPreviewFrameSummaries();
      },
      createClipFromCurrentAudio: () => {
        return createCurrentHapticsClip();
      },
      buildClipFromCurrentAudio: (options = {}) => {
        if (currentDecodedAudioBuffer === null) {
          throw new Error("Audio buffer is not loaded.");
        }

        return createHapticsClipFromAudioBuffer(currentDecodedAudioBuffer, options);
      },
      buildClipFromAudioArrayBuffer: async (arrayBuffer, options = {}) => {
        const audioContext = new AudioContext();

        try {
          const decodedAudioBuffer =
            await audioContext.decodeAudioData(arrayBuffer.slice(0));
          return createHapticsClipFromAudioBuffer(decodedAudioBuffer, options);
        } finally {
          await audioContext.close();
        }
      },
      mixClips: (clipEntries, options = {}) => {
        return mixHapticsClips(clipEntries, options);
      },
      queueClip: (clip, entryOptions = {}) => {
        return defaultMixerRuntime.enqueueClip(clip, entryOptions);
      },
      queueClipNow: (clip, entryOptions = {}) => {
        return defaultMixerRuntime.enqueueNowClip(clip, entryOptions);
      },
      removeQueuedClip: (entryId) => {
        return defaultMixerRuntime.removeClip(entryId);
      },
      clearQueuedClips: () => {
        defaultMixerRuntime.clear();
      },
      getQueuedClips: () => {
        return defaultMixerRuntime.getEntries();
      },
      resetQueueClock: (startOffsetMs = 0) => {
        return defaultMixerRuntime.resetClock(startOffsetMs);
      },
      getQueueElapsedMs: () => {
        return defaultMixerRuntime.getElapsedMs();
      },
      pruneQueuedClips: (timeMs) => {
        return defaultMixerRuntime.pruneEnded(timeMs);
      },
      updateQueuedAt: async (timeMs, options = {}) => {
        return await defaultMixerRuntime.updateAt(timeMs, options);
      },
      updateQueuedNow: async (options = {}) => {
        return await defaultMixerRuntime.updateNow(options);
      },
      resetQueuedUpdateState: () => {
        defaultMixerRuntime.resetUpdateState();
      },
      buildQueuedClipNow: (options = {}) => {
        return defaultMixerRuntime.buildScheduledClipNow(options);
      },
      createMixerRuntime: (defaultOptions = {}) => {
        return createMixerRuntime(defaultOptions);
      },
      getDefaultMixerRuntime: () => {
        return defaultMixerRuntime;
      },
      rebuildFromCurrentAudio: async () => {
        await rebuildFramesFromCurrentTrack();
        return vibrationFrames.slice();
      }
    };

    log("Game API: window.feelKitGamepadHaptics.rumbleOnce(left, right, ms)");
    log("Game API: window.feelKitGamepadHaptics.rumblePulse(left, right, onMs, offMs, count)");
    log("Game API: window.feelKitGamepadHaptics.rumbleSequence([{ leftStrength, rightStrength, durationMs }])");
    log("Game API: window.feelKitGamepadHaptics.detectDevices()");
    log("Game API: window.feelKitGamepadHaptics.connectXbox()");
    log("Game API: window.feelKitGamepadHaptics.disconnect()");
    log("Game API: window.feelKitGamepadHaptics.getInputState()");
    log("Game API: window.feelKitGamepadHaptics.loadAudioFile(file)");
    log("Game API: window.feelKitGamepadHaptics.reloadCurrentAudio()");
    log("Game API: window.feelKitGamepadHaptics.playLoadedTrack()");
    log("Game API: window.feelKitGamepadHaptics.playOutputFrames(outputFrames)");
    log("Game API: window.feelKitGamepadHaptics.playClip(clip)");
    log("Game API: window.feelKitGamepadHaptics.mixAndPlayClips([{ clip, offsetMs, leftGain, rightGain }], { deviceKind, mixMode, mixPolicy, mixWeights, normalize, normalizeCeiling })");
    log("Game API: window.feelKitGamepadHaptics.queueClip(clip, { offsetMs, leftGain, rightGain, tag })");
    log("Game API: window.feelKitGamepadHaptics.queueClipNow(clip, { delayMs, leftGain, rightGain, tag })");
    log("Game API: window.feelKitGamepadHaptics.getQueuedClips()");
    log("Game API: window.feelKitGamepadHaptics.updateQueuedAt(timeMs, options)");
    log("Game API: window.feelKitGamepadHaptics.updateQueuedNow(options)");
    log("Game API: window.feelKitGamepadHaptics.buildQueuedClipNow(options)");
    log("Game API: window.feelKitGamepadHaptics.playQueuedNow(options)");
    log("Game API: window.feelKitGamepadHaptics.createMixerRuntime(defaultOptions)");
    log("Game API: window.feelKitGamepadHaptics.getDefaultMixerRuntime()");
    log("Game API: window.feelKitAudioToHaptics.extractFeatureFramesFromCurrentAudio()");
    log("Game API: window.feelKitAudioToHaptics.getFeatureStatistics()");
    log("Game API: window.feelKitAudioToHaptics.extractLayerFramesFromCurrentAudio()");
    log("Game API: window.feelKitAudioToHaptics.getOutputFrames()");
    log("Game API: window.feelKitAudioToHaptics.getPreviewFrameSummaries()");
    log("Game API: window.feelKitAudioToHaptics.createClipFromCurrentAudio()");
    log("Game API: window.feelKitAudioToHaptics.buildClipFromCurrentAudio(options)");
    log("Game API: window.feelKitAudioToHaptics.buildClipFromAudioArrayBuffer(arrayBuffer, options)");
    log("Game API: window.feelKitAudioToHaptics.mixClips([{ clip, offsetMs, leftGain, rightGain }], { deviceKind, mixMode, mixPolicy, mixWeights, normalize, normalizeCeiling })");
    log("Game API: window.feelKitAudioToHaptics.queueClip(clip, { offsetMs, leftGain, rightGain, tag })");
    log("Game API: window.feelKitAudioToHaptics.queueClipNow(clip, { delayMs, leftGain, rightGain, tag })");
    log("Game API: window.feelKitAudioToHaptics.getQueuedClips()");
    log("Game API: window.feelKitAudioToHaptics.updateQueuedAt(timeMs, options)");
    log("Game API: window.feelKitAudioToHaptics.updateQueuedNow(options)");
    log("Game API: window.feelKitAudioToHaptics.buildQueuedClipNow(options)");
    log("Game API: window.feelKitAudioToHaptics.createMixerRuntime(defaultOptions)");
    log("Game API: window.feelKitAudioToHaptics.getDefaultMixerRuntime()");
    log("Game API: window.feelKitAudioToHaptics.rebuildFromCurrentAudio()");
    log("Game API: window.feelKitAudioToHaptics.setProfile('adaptive|music|impact|rotor|engine')");
    log("Game API: window.feelKitAudioToHaptics.getProfile()");
    log("Game API: window.feelKitAudioToHaptics.getProfiles()");
    log("Game API: window.feelKitAudioToHaptics.getTuningPresets()");
    log("Game API: window.feelKitAudioToHaptics.getTuningPresetSnapshot(presetName)");
    log("Game API: window.feelKitAudioToHaptics.applyTuningPreset(presetName, { rebuild })");
    log("Game API: window.feelKitAudioToHaptics.saveCurrentTuningAsCustomPreset(presetName)");
    log("Game API: window.feelKitAudioToHaptics.getCurrentTuningPresetName()");
    log("Game API: window.feelKitAudioToHaptics.getTuningSnapshot()");
    log("Game API: window.feelKitAudioToHaptics.applyTuningSnapshot(snapshot, { rebuild })");
    log("Game API: window.feelKitAudioToHaptics.resetAllTuning()");
    log("Game API: window.feelKitAudioToHaptics.getAdaptiveTuning()");
    log("Game API: window.feelKitAudioToHaptics.setAdaptiveTuning({ impactBase, impactRange, bodyBase, bodyRange, textureBase, textureRange, rhythmBase, rhythmRange, leftBoostBase, leftBoostRange, rightBoostBase, rightBoostRange })");
    log("Game API: window.feelKitAudioToHaptics.resetAdaptiveTuning()");
    log("Game API: window.feelKitAudioToHaptics.getLayerTuning()");
    log("Game API: window.feelKitAudioToHaptics.setLayerTuning({ transientOnsetWeight, transientHighRiseWeight, sustainLeftWeight, sustainRhythmWeight, sustainOnsetWeight, textureRightWeight, textureHighRiseWeight, textureBrightnessRiseWeight, textureBrightnessWeight, rhythmPeriodicityWeight, rhythmLeftWeight, rhythmOnsetWeight, impactCarry, bodyCarry, textureCarry, rhythmCarry })");
    log("Game API: window.feelKitAudioToHaptics.resetLayerTuning()");
)HTML";
auto kWebHidPlayerHtmlPart5G = R"HTML(

    //================================================================
    // ゲーム組み込み用の公開API
    //================================================================
    const createFeelKitHapticsGameLibrary = () => {
      let isVibrationEnabled = true;
      let autoUpdateLoopId = 0;
      let autoUpdatePromise = null;
      const soundClipCache = new Map();

      const getGamepadApi = () => {
        if (!window.feelKitGamepadHaptics) {
          throw new Error("feelKitGamepadHaptics is not ready.");
        }

        return window.feelKitGamepadHaptics;
      };

      const getAudioApi = () => {
        if (!window.feelKitAudioToHaptics) {
          throw new Error("feelKitAudioToHaptics is not ready.");
        }

        return window.feelKitAudioToHaptics;
      };

      const buildClipFromUrl = async (url, options = {}) => {
        const response = await fetch(url);

        if (!response.ok) {
          throw new Error("Audio file could not be loaded: " + response.status);
        }

        const arrayBuffer = await response.arrayBuffer();
        return await getAudioApi().buildClipFromAudioArrayBuffer(arrayBuffer, options);
      };

      const normalizeSoundOptions = (enabledOrOptions = true, options = {}) => {
        if (typeof enabledOrOptions === "boolean") {
          return {
            ...options,
            enabled: enabledOrOptions
          };
        }

        if (enabledOrOptions && typeof enabledOrOptions === "object") {
          return {
            ...enabledOrOptions,
            ...options
          };
        }

        return {
          ...options,
          enabled: true
        };
      };

      const buildSoundCacheKey = (url, options = {}) => {
        if (typeof options.cacheKey === "string" && options.cacheKey.length > 0) {
          return options.cacheKey;
        }

        return String(url);
      };

      const getSoundClip = async (url, options = {}) => {
        const cacheKey = buildSoundCacheKey(url, options);

        if (options.cache !== false && soundClipCache.has(cacheKey)) {
          return soundClipCache.get(cacheKey);
        }

        const clip = await buildClipFromUrl(url, options.clip || {});

        if (options.cache !== false) {
          soundClipCache.set(cacheKey, clip);
        }

        return clip;
      };

      const startAutoUpdate = (options = {}) => {
        if (autoUpdatePromise !== null) {
          return autoUpdatePromise;
        }

        const loopId = ++autoUpdateLoopId;
        const intervalMs = Math.max(4, Math.floor(options.intervalMs ?? 4));
        const updateOptions = options.update || {};
        const shouldStopWhenIdle = options.stopWhenIdle !== false;

        autoUpdatePromise = (async () => {
          while (loopId === autoUpdateLoopId) {
            const gamepadApi = getGamepadApi();

            if (isVibrationEnabled) {
              try {
                const updateResult = await gamepadApi.update(updateOptions);
                const queuedClips = gamepadApi.getQueuedClips();
                const isIdle =
                  updateResult !== null &&
                  Array.isArray(updateResult.outputFrames) &&
                  updateResult.outputFrames.length === 0 &&
                  queuedClips.length === 0;

                if (shouldStopWhenIdle && isIdle) {
                  break;
                }
              } catch (error) {
                console.warn("FeelKitHaptics auto update failed:", error);
                break;
              }
            }

            await delay(intervalMs);
          }

          if (loopId === autoUpdateLoopId) {
            autoUpdatePromise = null;
          }
        })();

        return autoUpdatePromise;
      };

      const stopAutoUpdate = () => {
        autoUpdateLoopId++;
        autoUpdatePromise = null;
      };

      const setEnabled = (shouldEnable) => {
        isVibrationEnabled = shouldEnable === true;

        if (!isVibrationEnabled) {
          getGamepadApi().clearQueuedClips();
          getGamepadApi()
            .stop()
            .catch((error) => {
              console.warn("FeelKitHaptics stop failed:", error);
            });
        }

        return isVibrationEnabled;
      };

      const vibrateSound = async (url, enabledOrOptions = true, options = {}) => {
        const soundOptions = normalizeSoundOptions(enabledOrOptions, options);
        const shouldVibrate = soundOptions.enabled !== false && isVibrationEnabled;

        if (!shouldVibrate) {
          return {
            enabled: false,
            entry: null
          };
        }

        const clip = await getSoundClip(url, soundOptions);
        const entry = getGamepadApi().queueClipNow(clip, {
          delayMs: Math.max(0, Math.floor(soundOptions.delayMs ?? 0)),
          leftGain: Math.max(0.0, Number(soundOptions.leftGain ?? 1.0)),
          rightGain: Math.max(0.0, Number(soundOptions.rightGain ?? 1.0)),
          tag: String(soundOptions.tag ?? url)
        });

        if (soundOptions.autoUpdate !== false) {
          startAutoUpdate(soundOptions.autoUpdateOptions || {});
        }

        return {
          enabled: true,
          entry: entry,
          clip: clip
        };
      };

      const playSound = (url, enabledOrOptions = true, options = {}) => {
        const soundOptions = normalizeSoundOptions(enabledOrOptions, options);
        const audioElement = new Audio(url);

        if (typeof soundOptions.volume === "number") {
          audioElement.volume = Math.max(0.0, Math.min(1.0, soundOptions.volume));
        }

        if (typeof soundOptions.loop === "boolean") {
          audioElement.loop = soundOptions.loop;
        }

        const playPromise = audioElement.play();
        const hapticsPromise = vibrateSound(url, soundOptions);

        return {
          audioElement: audioElement,
          playPromise: playPromise,
          hapticsPromise: hapticsPromise
        };
      };

      const vibrateAudioElement = async (audioElement, enabledOrOptions = true, options = {}) => {
        if (!audioElement) {
          throw new Error("Audio element is required.");
        }

        const sourceUrl = audioElement.currentSrc || audioElement.src;

        if (!sourceUrl) {
          throw new Error("Audio element source is empty.");
        }

        return await vibrateSound(sourceUrl, enabledOrOptions, options);
      };

      const initialize = async (options = {}) => {
        const gamepadApi = getGamepadApi();
        const audioApi = getAudioApi();

        if (typeof options.presetName === "string") {
          await audioApi.applyTuningPreset(options.presetName, {
            rebuild: options.rebuild !== false
          });
        }

        if (options.deviceKind === "switch") {
          await gamepadApi.connectSwitch(options.switch || {});
        }
        else if (options.deviceKind === "xbox") {
          await gamepadApi.connectXbox();
        }
        else if (options.deviceKind === "auto") {
          const devices = gamepadApi.detectDevices();

          if (devices.xboxConnected && !devices.switchUsbConnected) {
            await gamepadApi.connectXbox();
          }
        }

        if (options.prepare === true) {
          await gamepadApi.prepare();
        }

        return gamepadApi.detectDevices();
      };

      return Object.freeze({
        version: "0.1.0",
        initialize: initialize,
        setEnabled: setEnabled,
        setVibrationEnabled: setEnabled,
        isEnabled: () => isVibrationEnabled,
        isVibrationEnabled: () => isVibrationEnabled,
        detectDevices: () => getGamepadApi().detectDevices(),
        connectSwitch: (options = {}) => getGamepadApi().connectSwitch(options),
        connectSwitchUsb: () => getGamepadApi().connectSwitchUsb(),
        connectSwitchHid: () => getGamepadApi().connectSwitchHid(),
        connectXbox: () => getGamepadApi().connectXbox(),
        setActiveDevice: (deviceKind) => getGamepadApi().setActiveDevice(deviceKind),
        getActiveDevice: () => getGamepadApi().getActiveDevice(),
        disconnect: () => getGamepadApi().disconnect(),
        getInputState: () => getGamepadApi().getInputState(),
        prepare: () => getGamepadApi().prepare(),
        stop: () => getGamepadApi().stop(),
        rumbleOnce: (leftStrength = 1.0, rightStrength = 1.0, durationMs = 120) =>
          getGamepadApi().rumbleOnce(leftStrength, rightStrength, durationMs),
        rumblePulse: (
          leftStrength = 1.0,
          rightStrength = 1.0,
          onDurationMs = 80,
          offDurationMs = 40,
          pulseCount = 1
        ) =>
          getGamepadApi()
            .rumblePulse(
              leftStrength,
              rightStrength,
              onDurationMs,
              offDurationMs,
              pulseCount
            ),
        rumbleSequence: (sequence) => getGamepadApi().rumbleSequence(sequence),
        playOutputFrames: (outputFrames) => getGamepadApi().playOutputFrames(outputFrames),
        playClip: (clip) => getGamepadApi().playClip(clip),
        playOneShot: (clip, entryOptions = {}, updateOptions = {}) =>
          getGamepadApi().playOneShot(clip, entryOptions, updateOptions),
        queueClip: (clip, entryOptions = {}) => getGamepadApi().queueClip(clip, entryOptions),
        queueClipNow: (clip, entryOptions = {}) =>
          getGamepadApi().queueClipNow(clip, entryOptions),
        removeQueuedClip: (entryId) => getGamepadApi().removeQueuedClip(entryId),
        clearQueuedClips: () => getGamepadApi().clearQueuedClips(),
        getQueuedClips: () => getGamepadApi().getQueuedClips(),
        resetQueueClock: (startOffsetMs = 0) => getGamepadApi().resetQueueClock(startOffsetMs),
        update: (options = {}) => getGamepadApi().update(options),
        updateQueuedAt: (timeMs, options = {}) => getGamepadApi().updateQueuedAt(timeMs, options),
        updateQueuedNow: (options = {}) => getGamepadApi().updateQueuedNow(options),
        startAutoUpdate: startAutoUpdate,
        stopAutoUpdate: stopAutoUpdate,
        preloadSound: (url, options = {}) => getSoundClip(url, options),
        clearSoundCache: (url = null) => {
          if (url === null) {
            soundClipCache.clear();
            return 0;
          }

          return soundClipCache.delete(String(url)) ? 1 : 0;
        },
        playSound: playSound,
        playSoundEffect: playSound,
        vibrateSound: vibrateSound,
        vibrateForSound: vibrateSound,
        vibrateAudioElement: vibrateAudioElement,
        buildClipFromUrl: buildClipFromUrl,
        buildClipFromArrayBuffer: (arrayBuffer, options = {}) =>
          getAudioApi().buildClipFromAudioArrayBuffer(arrayBuffer, options),
        buildClipFromCurrentAudio: (options = {}) =>
          getAudioApi().buildClipFromCurrentAudio(options),
        createClipFromCurrentAudio: () => getAudioApi().createClipFromCurrentAudio(),
        mixClips: (clipEntries, options = {}) => getAudioApi().mixClips(clipEntries, options),
        applyTuningPreset: (presetName, options = {}) =>
          getAudioApi().applyTuningPreset(presetName, options),
        getTuningSnapshot: () => getAudioApi().getTuningSnapshot(),
        applyTuningSnapshot: (snapshot, options = {}) =>
          getAudioApi().applyTuningSnapshot(snapshot, options),
        createMixerRuntime: (defaultOptions = {}) =>
          getGamepadApi().createMixerRuntime(defaultOptions),
        getDefaultMixerRuntime: () => getGamepadApi().getDefaultMixerRuntime(),
        get gamepad() {
          return getGamepadApi();
        },
        get audio() {
          return getAudioApi();
        }
      });
    };

    window.FeelKitHaptics = createFeelKitHapticsGameLibrary();
    log("Game Library: window.FeelKitHaptics is ready.");
)HTML";
auto kWebHidPlayerHtmlPart5D = R"HTML(
    log("Game API: window.feelKitAudioToHaptics.getMixPolicies()");
    log("Game API: window.feelKitAudioToHaptics.getMixPolicyDefaults()");
    log("Game API: window.feelKitAudioToHaptics.getOutputDeviceProfiles()");
    log("Game API: window.feelKitAudioToHaptics.setOutputDeviceProfiles({ switch: {...}, xbox: {...} })");
    log("Game API: window.feelKitAudioToHaptics.resetOutputDeviceProfiles()");
    log("Game API: window.feelKitAudioToHaptics.adaptOutputFrame(frame, 'switch|xbox')");
)HTML";
auto kWebHidPlayerHtmlPart5C = R"HTML(
    audioFileInput.addEventListener("change", async (event) => {
      const files = event.target.files;

      if (!files || files.length === 0) {
        return;
      }

      try {
        await stopPlayback();
        await loadAudioFile(files[0]);
      } catch (error) {
        log("曲の読み込みに失敗しました: " + error.message);
      }
    });

    window.addEventListener("load", async () => {
      syncTuningUi();
      syncTuningPresetUi();
      updateTuningFromInputs();
      updateLayerTuningFromInputs();
      updateOutputDeviceProfilesFromInputs();

      try {
        await loadDefaultTrackAnalysis();
      } catch (error) {
        log("既定の曲の解析に失敗しました: " + error.message);
      }
    });

    updateButtons();
  </script>
</body>
</html>)HTML";

constexpr unsigned char kFeatureFlagHaptics = 0x09;
constexpr unsigned char kAllPlayerLedMask = 0x0F;

//================================================================
// 初期化コマンド
//================================================================
const unsigned char kInitCommand03[] = {
	0x03, 0x91, 0x00, 0x0D, 0x00, 0x08, 0x00, 0x00,
	0x01, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

const unsigned char kUnknownCommand07[] = {
	0x07, 0x91, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
};

const unsigned char kUnknownCommand16[] = {
	0x16, 0x91, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
};

const unsigned char kRequestControllerMac[] = {
	0x15, 0x91, 0x00, 0x01, 0x00, 0x0E, 0x00, 0x00,
	0x00, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

const unsigned char kRequestLtk[] = {
	0x15, 0x91, 0x00, 0x02, 0x00, 0x11, 0x00, 0x00,
	0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF,
};

const unsigned char kUnknownCommand15Arg03[] = {
	0x15, 0x91, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00,
	0x00,
};

const unsigned char kUnknownCommand09[] = {
	0x09, 0x91, 0x00, 0x07, 0x00, 0x08, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

const unsigned char kImuCommand02[] = {
	0x0C, 0x91, 0x00, 0x02, 0x00, 0x04, 0x00, 0x00,
	0x27, 0x00, 0x00, 0x00,
};

const unsigned char kUnknownCommand11[] = {
	0x11, 0x91, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00,
};

const unsigned char kUnknownCommand0A[] = {
	0x0A, 0x91, 0x00, 0x08, 0x00, 0x14, 0x00, 0x00,
	0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0x35, 0x00, 0x46, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
};

const unsigned char kImuCommand04[] = {
	0x0C, 0x91, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00,
	0x27, 0x00, 0x00, 0x00,
};

constexpr unsigned char kEnableHapticsCommand[] = {
	0x03, 0x91, 0x00, 0x0A, 0x00, 0x04, 0x00, 0x00,
	kFeatureFlagHaptics, 0x00, 0x00, 0x00,
};

const unsigned char kUnknownCommand10[] = {
	0x10, 0x91, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
};

const unsigned char kUnknownCommand01[] = {
	0x01, 0x91, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x00,
};

const unsigned char kUnknownCommand03Alt[] = {
	0x03, 0x91, 0x00, 0x01, 0x00, 0x00, 0x00,
};

const unsigned char kUnknownCommand0AAlt[] = {
	0x0A, 0x91, 0x00, 0x02, 0x00, 0x04, 0x00, 0x00,
	0x03, 0x00, 0x00, 0x00,
};

constexpr unsigned char kSetPlayerLedCommand[] = {
	0x09, 0x91, 0x00, 0x07, 0x00, 0x08, 0x00, 0x00,
	kAllPlayerLedMask, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00,
};

//================================================================
// データ構造体
//================================================================
struct VibFrame {
	int startMs;
	int endMs;
	int leftAmplitude;
	int rightAmplitude;
};

struct WavBuffer {
	HWAVEOUT handle;
	WAVEHDR header;
	BYTE* data;
};

struct SwitchUsbController {
	libusb_context* context;
	libusb_device_handle* deviceHandle;
	hid_device* hidHandle;
	HANDLE hidRawHandle;
	unsigned char bulkOutEndpoint;
	unsigned char bulkInEndpoint;
	bool hasBulkInEndpoint;
	int claimedInterfaceNumber;
	unsigned char vibrationCounter;
	size_t leftPatternPhase;
	size_t rightPatternPhase;
	int rumbleSendCount;
	std::vector<std::string> hidCandidatePaths;
	int activeHidCandidateIndex;
};

struct HapticPattern {
	unsigned char data[5];
};

const HapticPattern kSilentHapticPattern = {{0x00, 0x00, 0x00, 0x00, 0x00}};
const HapticPattern kHapticPatternTable[] = {
	{{0x93, 0x35, 0x36, 0x1C, 0x0D}},
	{{0xA8, 0x29, 0xC5, 0xDC, 0x0C}},
	{{0x75, 0x21, 0xB5, 0x5D, 0x13}},
	{{0x75, 0xF5, 0x70, 0x1E, 0x11}},
	{{0xBA, 0x55, 0x40, 0x1E, 0x08}},
	{{0x90, 0x31, 0x10, 0x9E, 0x00}},
	{{0x90, 0x15, 0x10, 0x9E, 0x00}},
	{{0x90, 0x01, 0x10, 0x1E, 0x00}},
	{{0x75, 0x15, 0x73, 0x1E, 0x11}},
	{{0x7B, 0x95, 0x92, 0x5C, 0x13}},
	{{0x8D, 0xC5, 0xA1, 0x1B, 0x10}},
	{{0x7E, 0x31, 0xC1, 0xDC, 0x0B}},
	{{0x6F, 0x2D, 0x31, 0xDC, 0x03}},
	{{0x75, 0x19, 0x41, 0x9B, 0x03}},
	{{0x6F, 0x15, 0xE1, 0xDA, 0x02}},
	{{0x66, 0xF1, 0xE0, 0xDA, 0x02}},
	{{0x63, 0xDD, 0x10, 0x5B, 0x02}},
	{{0x5A, 0xB9, 0x10, 0x5B, 0x02}},
	{{0x4E, 0x99, 0x50, 0x5A, 0x02}},
	{{0x45, 0x81, 0x20, 0x5A, 0x02}},
	{{0x48, 0x85, 0x50, 0x5A, 0x02}},
	{{0x4B, 0x85, 0x50, 0x5A, 0x02}},
	{{0x4B, 0x7D, 0x80, 0x5A, 0x02}},
	{{0x48, 0x71, 0x20, 0x5A, 0x02}},
	{{0x48, 0x71, 0xC0, 0x99, 0x02}},
	{{0x45, 0x65, 0x90, 0x99, 0x02}},
	{{0x42, 0x61, 0x90, 0x99, 0x02}},
	{{0x3C, 0x59, 0xD0, 0x98, 0x02}},
	{{0x36, 0x59, 0xA0, 0x98, 0x02}},
	{{0x30, 0x55, 0x70, 0x18, 0x02}},
	{{0x2A, 0x55, 0x70, 0x18, 0x02}},
	{{0x27, 0x4D, 0x70, 0x18, 0x02}},
	{{0x21, 0x4D, 0x70, 0x18, 0x02}},
	{{0x24, 0x45, 0x70, 0x18, 0x02}},
	{{0x2A, 0x45, 0xA0, 0x18, 0x02}},
	{{0x2D, 0x41, 0xA0, 0x58, 0x01}},
	{{0x36, 0x41, 0xF0, 0x59, 0x01}},
	{{0x39, 0x41, 0xF0, 0x59, 0x01}},
	{{0x3F, 0x39, 0xF0, 0x99, 0x00}},
	{{0x3F, 0x31, 0xF0, 0x99, 0x00}},
	{{0x3F, 0x2D, 0xF0, 0x99, 0x00}},
	{{0x3F, 0x25, 0xF0, 0x99, 0x00}},
	{{0x3F, 0x1D, 0xF0, 0x99, 0x00}},
	{{0x3F, 0x19, 0xF0, 0x99, 0x00}},
	{{0x3F, 0x01, 0xF0, 0x19, 0x00}},
};

//================================================================
// 小ヘルパー
//================================================================
FILE* gDebugLogFile = nullptr;

bool resolveAssetPath(const wchar_t* fileName,
                      wchar_t* resolvedPath,
                      size_t resolvedCount);

void closeDebugLog() {
	if (gDebugLogFile != nullptr) {
		fclose(gDebugLogFile);
		gDebugLogFile = nullptr;
	}
}

void debugLog(const char* formatText, ...) {
	if (gDebugLogFile == nullptr) {
		return;
	}

	va_list args;
	va_start(args, formatText);
	vfprintf(gDebugLogFile, formatText, args);
	va_end(args);
	fflush(gDebugLogFile);
}

int getMinValue(int leftValue, int rightValue) {
	if (leftValue <= rightValue) {
		return leftValue;
	}

	return rightValue;
}

size_t getMinSize(size_t leftValue, size_t rightValue) {
	if (leftValue <= rightValue) {
		return leftValue;
	}

	return rightValue;
}

int clampValue(int value, int minValue, int maxValue) {
	if (value <= minValue) {
		return minValue;
	}

	if (value >= maxValue) {
		return maxValue;
	}

	return value;
}

void printHexBuffer(const unsigned char* data, int size) {
	for (int byteIndex = 0; byteIndex < size; byteIndex++) {
		std::printf("%02X ", data[byteIndex]);
	}

	std::printf("\n");
}

void printLibusbError(const char* label, int errorCode) {
	std::printf("%s failed\n", label);
	std::printf("  libusb_error=%d (%s)\n", errorCode, libusb_error_name(errorCode));
	debugLog("%s failed\n", label);
	debugLog("  libusb_error=%d (%s)\n", errorCode, libusb_error_name(errorCode));
}

void printHidError(const char* label, hid_device* hidHandle) {
	std::printf("%s failed\n", label);
	debugLog("%s failed\n", label);

	if (hidHandle != nullptr) {
		const wchar_t* errorText = hid_error(hidHandle);

		if (errorText != nullptr) {
			std::wprintf(L"  hid_error=%ls\n", errorText);
			if (gDebugLogFile != nullptr) {
				fwprintf(gDebugLogFile, L"  hid_error=%ls\n", errorText);
				fflush(gDebugLogFile);
			}
			return;
		}
	}

	std::printf("  hid_error=(null)\n");
	debugLog("  hid_error=(null)\n");
}

bool fileExists(const wchar_t* filePath) {
	DWORD attributes = GetFileAttributesW(filePath);
	return attributes != INVALID_FILE_ATTRIBUTES &&
		(attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool loadBinaryFile(const wchar_t* filePath, std::vector<BYTE>& fileData) {
	wchar_t resolvedFilePath[MAX_PATH] = {};

	if (!resolveAssetPath(filePath, resolvedFilePath, _countof(resolvedFilePath))) {
		return false;
	}

	HANDLE fileHandle = CreateFileW(resolvedFilePath,
	                                GENERIC_READ,
	                                FILE_SHARE_READ,
	                                nullptr,
	                                OPEN_EXISTING,
	                                FILE_ATTRIBUTE_NORMAL,
	                                nullptr);

	if (fileHandle == INVALID_HANDLE_VALUE) {
		return false;
	}

	LARGE_INTEGER fileSize = {};

	if (!GetFileSizeEx(fileHandle, &fileSize) || fileSize.QuadPart <= 0) {
		CloseHandle(fileHandle);
		return false;
	}

	fileData.resize(static_cast<size_t>(fileSize.QuadPart));
	DWORD totalReadSize = 0;

	while (totalReadSize < fileData.size()) {
		DWORD chunkReadSize = 0;
		DWORD requestSize =
			static_cast<DWORD>(getMinSize(fileData.size() - totalReadSize, 1 << 20));

		if (!ReadFile(fileHandle,
		              fileData.data() + totalReadSize,
		              requestSize,
		              &chunkReadSize,
		              nullptr)) {
			CloseHandle(fileHandle);
			fileData.clear();
			return false;
		}

		if (chunkReadSize == 0) {
			break;
		}

		totalReadSize += chunkReadSize;
	}

	CloseHandle(fileHandle);

	if (totalReadSize != fileData.size()) {
		fileData.resize(totalReadSize);
	}

	return !fileData.empty();
}

bool convertMultiByteToWideText(const char* sourceText,
                                wchar_t* destinationText,
                                int destinationCount) {
	if (sourceText == nullptr || destinationText == nullptr || destinationCount <= 0) {
		return false;
	}

	int convertedCount = MultiByteToWideChar(CP_UTF8,
	                                         0,
	                                         sourceText,
	                                         -1,
	                                         destinationText,
	                                         destinationCount);

	return convertedCount > 0;
}

bool tryBuildAssetPath(const wchar_t* baseDirectory,
                       const wchar_t* fileName,
                       wchar_t* destinationPath,
                       size_t destinationCount) {
	if (baseDirectory == nullptr || fileName == nullptr) {
		return false;
	}

	swprintf_s(destinationPath, destinationCount, L"%ls\\%ls", baseDirectory, fileName);
	return fileExists(destinationPath);
}

bool resolveAssetPath(const wchar_t* fileName,
                      wchar_t* destinationPath,
                      size_t destinationCount) {
	if (fileExists(fileName)) {
		wcscpy_s(destinationPath, destinationCount, fileName);
		return true;
	}

	wchar_t currentDirectory[MAX_PATH] = {};

	if (GetCurrentDirectoryW(_countof(currentDirectory), currentDirectory) > 0) {
		if (tryBuildAssetPath(currentDirectory, fileName, destinationPath, destinationCount)) {
			return true;
		}
	}

	wchar_t modulePath[MAX_PATH] = {};

	if (GetModuleFileNameW(nullptr, modulePath, _countof(modulePath)) == 0) {
		return false;
	}

	wchar_t* lastSlash = wcsrchr(modulePath, L'\\');

	if (lastSlash == nullptr) {
		return false;
	}

	*lastSlash = L'\0';

	if (tryBuildAssetPath(modulePath, fileName, destinationPath, destinationCount)) {
		return true;
	}

	wchar_t projectAssetDirectory[MAX_PATH] = {};
	swprintf_s(projectAssetDirectory, _countof(projectAssetDirectory), L"%ls\\..\\..\\konnto", modulePath);

	if (tryBuildAssetPath(projectAssetDirectory, fileName, destinationPath, destinationCount)) {
		return true;
	}

	wchar_t solutionDirectoryAssetPath[MAX_PATH] = {};
	swprintf_s(solutionDirectoryAssetPath, _countof(solutionDirectoryAssetPath), L"%ls\\..\\..", modulePath);

	if (tryBuildAssetPath(solutionDirectoryAssetPath, fileName, destinationPath, destinationCount)) {
		return true;
	}

	return false;
}

//================================================================
// 音量変換
//================================================================
int convertDbToAmplitude(double dbValue) {
	if (dbValue <= static_cast<double>(kSilentThresholdDb)) {
		return 0;
	}

	double normalizedValue = (dbValue - static_cast<double>(kSilentThresholdDb)) /
		-static_cast<double>(kSilentThresholdDb);

	if (normalizedValue <= 0.0) {
		normalizedValue = 0.0;
	}

	if (normalizedValue >= 1.0) {
		normalizedValue = 1.0;
	}

	double emphasizedValue = normalizedValue * normalizedValue;
	int amplitude =
		static_cast<int>(emphasizedValue * static_cast<double>(kMaxRumbleAmplitude));

	return clampValue(amplitude, 0, kMaxRumbleAmplitude);
}

int convertNormalizedAmplitudeToRumble(double normalizedAmplitude) {
	if (normalizedAmplitude <= 0.0) {
		return 0;
	}

	if (normalizedAmplitude >= 1.0) {
		normalizedAmplitude = 1.0;
	}

	double emphasizedValue = normalizedAmplitude * normalizedAmplitude;
	int amplitude =
		static_cast<int>(emphasizedValue * static_cast<double>(kMaxRumbleAmplitude));

	return clampValue(amplitude, 0, kMaxRumbleAmplitude);
}

double readWaveSample(const BYTE* frameData,
                      int channelIndex,
                      int channelCount,
                      int bitsPerSample,
                      bool isFloatFormat) {
	int bytesPerSample = bitsPerSample / 8;
	const BYTE* samplePointer = frameData + (channelIndex * bytesPerSample);

	if (channelIndex >= channelCount) {
		return 0.0;
	}

	if (isFloatFormat && bitsPerSample == 32) {
		float sampleValue = *reinterpret_cast<const float*>(samplePointer);

		if (sampleValue <= -1.0f) {
			return 1.0;
		}

		if (sampleValue >= 1.0f) {
			return 1.0;
		}

		return fabs(static_cast<double>(sampleValue));
	}

	if (bitsPerSample == 8) {
		int sampleValue = static_cast<int>(samplePointer[0]) - 128;
		return fabs(static_cast<double>(sampleValue) / 128.0);
	}

	if (bitsPerSample == 16) {
		short sampleValue = *reinterpret_cast<const short*>(samplePointer);
		return fabs(static_cast<double>(sampleValue) / 32768.0);
	}

	if (bitsPerSample == 24) {
		int sampleValue =
			static_cast<int>(samplePointer[0]) |
			(static_cast<int>(samplePointer[1]) << 8) |
			(static_cast<int>(samplePointer[2]) << 16);

		if ((sampleValue & 0x00800000) != 0) {
			sampleValue |= ~0x00FFFFFF;
		}

		return fabs(static_cast<double>(sampleValue) / 8388608.0);
	}

	if (bitsPerSample == 32) {
		int sampleValue = *reinterpret_cast<const int*>(samplePointer);
		return fabs(static_cast<double>(sampleValue) / 2147483648.0);
	}

	return 0.0;
}

std::vector<VibFrame> buildVibrationFramesFromWav(const WAVEFORMATEX& waveFormat,
                                                  const WavBuffer& wavBuffer) {
	std::vector<VibFrame> frames;

	bool isPcmFormat = waveFormat.wFormatTag == kWaveFormatPcm;
	bool isFloatFormat = waveFormat.wFormatTag == kWaveFormatIeeeFloat;

	if (!isPcmFormat && !isFloatFormat) {
		std::printf("Unsupported WAV format: %u\n", waveFormat.wFormatTag);
		return frames;
	}

	if (waveFormat.nChannels <= 0 || waveFormat.nBlockAlign <= 0 ||
		waveFormat.nSamplesPerSec <= 0) {
		std::printf("Invalid WAV format\n");
		return frames;
	}

	int bitsPerSample = waveFormat.wBitsPerSample;

	if (bitsPerSample != 8 && bitsPerSample != 16 &&
		bitsPerSample != 24 && bitsPerSample != 32) {
		std::printf("Unsupported bits per sample: %d\n", bitsPerSample);
		return frames;
	}

	int samplesPerFrame =
		static_cast<int>((waveFormat.nSamplesPerSec * kMinRefreshIntervalMs) / 1000);

	if (samplesPerFrame <= 0) {
		samplesPerFrame = 1;
	}

	int totalSampleFrames =
		static_cast<int>(wavBuffer.header.dwBufferLength / waveFormat.nBlockAlign);

	if (totalSampleFrames <= 0) {
		return frames;
	}

	int previousLeftAmplitude = 0;
	int previousRightAmplitude = 0;

	for (int sampleFrameIndex = 0;
	     sampleFrameIndex < totalSampleFrames;
	     sampleFrameIndex += samplesPerFrame) {
		VibFrame frame = {};
		int nextSampleFrameIndex = sampleFrameIndex + samplesPerFrame;

		if (nextSampleFrameIndex > totalSampleFrames) {
			nextSampleFrameIndex = totalSampleFrames;
		}

		double leftPeak = 0.0;
		double rightPeak = 0.0;

		for (int windowSampleIndex = sampleFrameIndex;
		     windowSampleIndex < nextSampleFrameIndex;
		     windowSampleIndex++) {
			const BYTE* frameData =
				wavBuffer.data + (windowSampleIndex * waveFormat.nBlockAlign);

			double leftSample =
				readWaveSample(frameData,
				               0,
				               waveFormat.nChannels,
				               bitsPerSample,
				               isFloatFormat);

			double rightSample = leftSample;

			if (waveFormat.nChannels >= 2) {
				rightSample =
					readWaveSample(frameData,
					               1,
					               waveFormat.nChannels,
					               bitsPerSample,
					               isFloatFormat);
			}

			if (leftSample >= leftPeak) {
				leftPeak = leftSample;
			}

			if (rightSample >= rightPeak) {
				rightPeak = rightSample;
			}
		}

		frame.startMs =
			static_cast<int>((static_cast<long long>(sampleFrameIndex) * 1000) /
				waveFormat.nSamplesPerSec);
		frame.endMs =
			static_cast<int>((static_cast<long long>(nextSampleFrameIndex) * 1000) /
				waveFormat.nSamplesPerSec);
		frame.leftAmplitude = convertNormalizedAmplitudeToRumble(leftPeak);
		frame.rightAmplitude = convertNormalizedAmplitudeToRumble(rightPeak);

		int leftDifference = abs(frame.leftAmplitude - previousLeftAmplitude);
		int rightDifference = abs(frame.rightAmplitude - previousRightAmplitude);

		if (leftDifference >= kPulseThreshold) {
			frame.leftAmplitude += kPulseAmplitudeBoost;
		}

		if (rightDifference >= kPulseThreshold) {
			frame.rightAmplitude += kPulseAmplitudeBoost;
		}

		frame.leftAmplitude = clampValue(frame.leftAmplitude, 0, kMaxRumbleAmplitude);
		frame.rightAmplitude = clampValue(frame.rightAmplitude, 0, kMaxRumbleAmplitude);
		frames.push_back(frame);

		previousLeftAmplitude = frame.leftAmplitude;
		previousRightAmplitude = frame.rightAmplitude;
	}

	return frames;
}

//================================================================
// WAV 再生
//================================================================
bool loadWavFile(const wchar_t* filePath, WAVEFORMATEX& waveFormat, WavBuffer& wavBuffer) {
	wchar_t resolvedFilePath[MAX_PATH] = {};

	if (!resolveAssetPath(filePath, resolvedFilePath, _countof(resolvedFilePath))) {
		return false;
	}

	HMMIO mediaFileHandle =
		mmioOpenW(resolvedFilePath, nullptr, MMIO_READ | MMIO_ALLOCBUF);

	if (mediaFileHandle == nullptr) {
		return false;
	}

	MMCKINFO riffChunk = {};
	riffChunk.fccType = mmioFOURCC('W', 'A', 'V', 'E');

	if (mmioDescend(mediaFileHandle, &riffChunk, nullptr, MMIO_FINDRIFF) != MMSYSERR_NOERROR) {
		mmioClose(mediaFileHandle, 0);
		return false;
	}

	MMCKINFO formatChunk = {};
	formatChunk.ckid = mmioFOURCC('f', 'm', 't', ' ');

	if (mmioDescend(mediaFileHandle, &formatChunk, &riffChunk, MMIO_FINDCHUNK) !=
		MMSYSERR_NOERROR) {
		mmioClose(mediaFileHandle, 0);
		return false;
	}

	LONG formatReadSize =
		mmioRead(mediaFileHandle, reinterpret_cast<HPSTR>(&waveFormat), sizeof(WAVEFORMATEX));

	if (formatReadSize <= 0) {
		mmioClose(mediaFileHandle, 0);
		return false;
	}

	mmioAscend(mediaFileHandle, &formatChunk, 0);

	MMCKINFO dataChunk = {};
	dataChunk.ckid = mmioFOURCC('d', 'a', 't', 'a');

	if (mmioDescend(mediaFileHandle, &dataChunk, &riffChunk, MMIO_FINDCHUNK) !=
		MMSYSERR_NOERROR) {
		mmioClose(mediaFileHandle, 0);
		return false;
	}

	wavBuffer.data = new BYTE[dataChunk.cksize];

	LONG dataReadSize =
		mmioRead(mediaFileHandle, reinterpret_cast<HPSTR>(wavBuffer.data), dataChunk.cksize);

	mmioClose(mediaFileHandle, 0);

	if (dataReadSize != static_cast<LONG>(dataChunk.cksize)) {
		delete[] wavBuffer.data;
		wavBuffer.data = nullptr;
		return false;
	}

	wavBuffer.header = {};
	wavBuffer.header.lpData = reinterpret_cast<LPSTR>(wavBuffer.data);
	wavBuffer.header.dwBufferLength = dataChunk.cksize;
	wavBuffer.header.dwFlags = 0;

	return true;
}

bool startWavePlayback(WAVEFORMATEX& waveFormat, WavBuffer& wavBuffer) {
	MMRESULT openResult =
		waveOutOpen(&wavBuffer.handle, WAVE_MAPPER, &waveFormat, 0, 0, CALLBACK_NULL);

	if (openResult != MMSYSERR_NOERROR) {
		return false;
	}

	MMRESULT prepareResult =
		waveOutPrepareHeader(wavBuffer.handle, &wavBuffer.header, sizeof(WAVEHDR));

	if (prepareResult != MMSYSERR_NOERROR) {
		waveOutClose(wavBuffer.handle);
		wavBuffer.handle = nullptr;
		return false;
	}

	MMRESULT writeResult = waveOutWrite(wavBuffer.handle, &wavBuffer.header, sizeof(WAVEHDR));

	if (writeResult != MMSYSERR_NOERROR) {
		waveOutUnprepareHeader(wavBuffer.handle, &wavBuffer.header, sizeof(WAVEHDR));
		waveOutClose(wavBuffer.handle);
		wavBuffer.handle = nullptr;
		return false;
	}

	return true;
}

void finalizeWavePlayback(WavBuffer& wavBuffer) {
	if (wavBuffer.handle != nullptr) {
		waveOutReset(wavBuffer.handle);
		waveOutUnprepareHeader(wavBuffer.handle, &wavBuffer.header, sizeof(WAVEHDR));
		waveOutClose(wavBuffer.handle);
		wavBuffer.handle = nullptr;
	}

	if (wavBuffer.data != nullptr) {
		delete[] wavBuffer.data;
		wavBuffer.data = nullptr;
	}
}

//================================================================
// libusb 制御
//================================================================
void initializeControllerState(SwitchUsbController& controller) {
	controller.context = nullptr;
	controller.deviceHandle = nullptr;
	controller.hidHandle = nullptr;
	controller.hidRawHandle = INVALID_HANDLE_VALUE;
	controller.bulkOutEndpoint = 0;
	controller.bulkInEndpoint = 0;
	controller.hasBulkInEndpoint = true;
	controller.claimedInterfaceNumber = -1;
	controller.vibrationCounter = 0;
	controller.leftPatternPhase = 0;
	controller.rightPatternPhase = 0;
	controller.rumbleSendCount = 0;
	controller.hidCandidatePaths.clear();
	controller.activeHidCandidateIndex = -1;
}

bool findEndpointsFromConfig(const libusb_config_descriptor* configDescriptor,
                             int& interfaceNumber,
                             unsigned char& bulkOutEndpoint,
                             unsigned char& bulkInEndpoint,
                             bool& hasBulkInEndpoint) {
	if (configDescriptor == nullptr) {
		return false;
	}

	for (uint8_t interfaceIndex = 0;
	     interfaceIndex < configDescriptor->bNumInterfaces;
	     interfaceIndex++) {
		const libusb_interface& usbInterface = configDescriptor->interface[interfaceIndex];

		for (int altSettingIndex = 0;
		     altSettingIndex < usbInterface.num_altsetting;
		     altSettingIndex++) {
			const libusb_interface_descriptor& interfaceDescriptor =
				usbInterface.altsetting[altSettingIndex];

			unsigned char localBulkOutEndpoint = 0;
			unsigned char localBulkInEndpoint = 0;
			bool localHasBulkInEndpoint = false;

			for (uint8_t endpointIndex = 0;
			     endpointIndex < interfaceDescriptor.bNumEndpoints;
			     endpointIndex++) {
				const libusb_endpoint_descriptor& endpointDescriptor =
					interfaceDescriptor.endpoint[endpointIndex];

				bool isBulkEndpoint =
					(endpointDescriptor.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) ==
					LIBUSB_TRANSFER_TYPE_BULK;

				if (!isBulkEndpoint) {
					continue;
				}

				bool isInEndpoint =
					(endpointDescriptor.bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) ==
					LIBUSB_ENDPOINT_IN;

				if (isInEndpoint) {
					localBulkInEndpoint = endpointDescriptor.bEndpointAddress;
					localHasBulkInEndpoint = true;
				}
				else {
					localBulkOutEndpoint = endpointDescriptor.bEndpointAddress;
				}
			}

			if (localBulkOutEndpoint != 0) {
				interfaceNumber = interfaceDescriptor.bInterfaceNumber;
				bulkOutEndpoint = localBulkOutEndpoint;
				bulkInEndpoint = localBulkInEndpoint;
				hasBulkInEndpoint = localHasBulkInEndpoint;
				return true;
			}
		}
	}

	return false;
}

bool openSwitchUsbController(SwitchUsbController& controller) {
	initializeControllerState(controller);

	int initResult = libusb_init(&controller.context);

	if (initResult != LIBUSB_SUCCESS) {
		printLibusbError("libusb_init", initResult);
		return false;
	}

	libusb_device** deviceList = nullptr;
	ssize_t deviceCount = libusb_get_device_list(controller.context, &deviceList);

	if (deviceCount < 0) {
		printLibusbError("libusb_get_device_list", static_cast<int>(deviceCount));
		libusb_exit(controller.context);
		controller.context = nullptr;
		return false;
	}

	bool isOpened = false;

	for (ssize_t deviceIndex = 0; deviceIndex < deviceCount; deviceIndex++) {
		libusb_device* device = deviceList[deviceIndex];
		libusb_device_descriptor deviceDescriptor = {};
		int descriptorResult = libusb_get_device_descriptor(device, &deviceDescriptor);

		if (descriptorResult != LIBUSB_SUCCESS) {
			continue;
		}

		if (deviceDescriptor.idVendor != kNintendoVendorId ||
			deviceDescriptor.idProduct != kSwitch2ProControllerProductId) {
			continue;
		}

		std::printf("Found controller: vid=%04X pid=%04X\n",
		            deviceDescriptor.idVendor,
		            deviceDescriptor.idProduct);

		libusb_config_descriptor* configDescriptor = nullptr;
		int configResult = libusb_get_active_config_descriptor(device, &configDescriptor);

		if (configResult != LIBUSB_SUCCESS) {
			printLibusbError("libusb_get_active_config_descriptor", configResult);
			continue;
		}

		bool hasValidEndpoints = findEndpointsFromConfig(configDescriptor,
		                                                 controller.claimedInterfaceNumber,
		                                                 controller.bulkOutEndpoint,
		                                                 controller.bulkInEndpoint,
		                                                 controller.hasBulkInEndpoint);

		if (!hasValidEndpoints) {
			libusb_free_config_descriptor(configDescriptor);
			continue;
		}

		std::printf("Using interface %d, bulkOut=0x%02X, bulkIn=0x%02X, hasBulkIn=%d\n",
		            controller.claimedInterfaceNumber,
		            controller.bulkOutEndpoint,
		            controller.bulkInEndpoint,
		            controller.hasBulkInEndpoint ? 1 : 0);

		int openResult = libusb_open(device, &controller.deviceHandle);
		libusb_free_config_descriptor(configDescriptor);

		if (openResult != LIBUSB_SUCCESS) {
			printLibusbError("libusb_open", openResult);
			continue;
		}

		int claimResult =
			libusb_claim_interface(controller.deviceHandle, controller.claimedInterfaceNumber);

		if (claimResult != LIBUSB_SUCCESS) {
			printLibusbError("libusb_claim_interface", claimResult);
			libusb_close(controller.deviceHandle);
			controller.deviceHandle = nullptr;
			continue;
		}

		isOpened = true;
		break;
	}

	libusb_free_device_list(deviceList, 1);

	if (!isOpened) {
		std::printf("Switch 2 Pro Controller could not be opened with libusb\n");
		libusb_exit(controller.context);
		controller.context = nullptr;
		return false;
	}

	return true;
}

bool openSwitchHidController(SwitchUsbController& controller) {
	if (hid_init() != 0) {
		std::printf("hid_init failed\n");
		return false;
	}

	auto closeCurrentHidHandles = [&controller]() {
		if (controller.hidHandle != nullptr) {
			hid_close(controller.hidHandle);
			controller.hidHandle = nullptr;
		}

		if (controller.hidRawHandle != INVALID_HANDLE_VALUE) {
			CloseHandle(controller.hidRawHandle);
			controller.hidRawHandle = INVALID_HANDLE_VALUE;
		}
	};

	auto openHidPath = [&controller, &closeCurrentHidHandles](const char* devicePath) -> bool {
		closeCurrentHidHandles();

		controller.hidHandle = hid_open_path(devicePath);

		if (controller.hidHandle == nullptr) {
			debugLog("hid_open_path failed: %s\n", devicePath);
			return false;
		}

		controller.hidRawHandle = CreateFileA(devicePath,
		                                      GENERIC_WRITE,
		                                      FILE_SHARE_READ | FILE_SHARE_WRITE,
		                                      nullptr,
		                                      OPEN_EXISTING,
		                                      FILE_FLAG_OVERLAPPED,
		                                      nullptr);

		if (controller.hidRawHandle == INVALID_HANDLE_VALUE) {
			DWORD errorCode = GetLastError();
			debugLog("CreateFileA failed: %s\n", devicePath);
			debugLog("  win32_error=0x%08lX\n", errorCode);
			hid_close(controller.hidHandle);
			controller.hidHandle = nullptr;
			return false;
		}

		PHIDP_PREPARSED_DATA preparsedData = nullptr;
		HIDP_CAPS caps = {};

		if (HidD_GetPreparsedData(controller.hidRawHandle, &preparsedData)) {
			if (HidP_GetCaps(preparsedData, &caps) == HIDP_STATUS_SUCCESS) {
				std::printf("InputReportByteLength=%d\n", caps.InputReportByteLength);
				std::printf("OutputReportByteLength=%d\n", caps.OutputReportByteLength);
				std::printf("FeatureReportByteLength=%d\n", caps.FeatureReportByteLength);
				debugLog("InputReportByteLength=%d\n", caps.InputReportByteLength);
				debugLog("OutputReportByteLength=%d\n", caps.OutputReportByteLength);
				debugLog("FeatureReportByteLength=%d\n", caps.FeatureReportByteLength);
			}

			HidD_FreePreparsedData(preparsedData);
		}
		else {
			std::printf("HidD_GetPreparsedData failed\n");
			debugLog("HidD_GetPreparsedData failed: %s\n", devicePath);
		}

		std::printf("Opened HID path: %s\n", devicePath);
		debugLog("Opened HID path: %s\n", devicePath);
		return true;
	};

	hid_device_info* deviceList =
		hid_enumerate(kNintendoVendorId, kSwitch2ProControllerProductId);
	hid_device_info* currentDevice = deviceList;

	while (currentDevice != nullptr) {
		std::printf("HID enum: interface=%d usage_page=0x%04X usage=0x%04X path=%s\n",
		            currentDevice->interface_number,
		            currentDevice->usage_page,
		            currentDevice->usage,
		            currentDevice->path);
		debugLog("HID enum: interface=%d usage_page=0x%04X usage=0x%04X path=%s\n",
		         currentDevice->interface_number,
		         currentDevice->usage_page,
		         currentDevice->usage,
		         currentDevice->path);

		bool isInterfaceZero = currentDevice->interface_number == 0;

		if (isInterfaceZero) {
			controller.hidCandidatePaths.push_back(currentDevice->path);
		}

		currentDevice = currentDevice->next;
	}

	hid_free_enumeration(deviceList);

	for (size_t candidateIndex = 0;
	     candidateIndex < controller.hidCandidatePaths.size();
	     candidateIndex++) {
		const std::string& candidatePath = controller.hidCandidatePaths[candidateIndex];
		debugLog("Trying HID candidate %zu: %s\n", candidateIndex, candidatePath.c_str());

		if (openHidPath(candidatePath.c_str())) {
			controller.activeHidCandidateIndex = static_cast<int>(candidateIndex);
			break;
		}
	}

	if (controller.hidHandle == nullptr || controller.hidRawHandle == INVALID_HANDLE_VALUE) {
		std::printf("Switch 2 HID output device could not be opened\n");
		hid_exit();
		return false;
	}

	return true;
}

bool reopenNextHidCandidate(SwitchUsbController& controller) {
	if (controller.hidCandidatePaths.empty()) {
		return false;
	}

	int nextCandidateIndex = controller.activeHidCandidateIndex + 1;

	while (nextCandidateIndex < static_cast<int>(controller.hidCandidatePaths.size())) {
		if (controller.hidHandle != nullptr) {
			hid_close(controller.hidHandle);
			controller.hidHandle = nullptr;
		}

		if (controller.hidRawHandle != INVALID_HANDLE_VALUE) {
			CloseHandle(controller.hidRawHandle);
			controller.hidRawHandle = INVALID_HANDLE_VALUE;
		}

		const std::string& candidatePath =
			controller.hidCandidatePaths[static_cast<size_t>(nextCandidateIndex)];

		debugLog("Reopen HID candidate %d: %s\n",
		         nextCandidateIndex,
		         candidatePath.c_str());

		controller.hidHandle = hid_open_path(candidatePath.c_str());

		if (controller.hidHandle == nullptr) {
			nextCandidateIndex++;
			continue;
		}

		controller.hidRawHandle = CreateFileA(candidatePath.c_str(),
		                                      GENERIC_WRITE,
		                                      FILE_SHARE_READ | FILE_SHARE_WRITE,
		                                      nullptr,
		                                      OPEN_EXISTING,
		                                      FILE_FLAG_OVERLAPPED,
		                                      nullptr);

		if (controller.hidRawHandle == INVALID_HANDLE_VALUE) {
			DWORD errorCode = GetLastError();
			debugLog("CreateFileA failed on reopen\n");
			debugLog("  win32_error=0x%08lX\n", errorCode);
			hid_close(controller.hidHandle);
			controller.hidHandle = nullptr;
			nextCandidateIndex++;
			continue;
		}

		controller.activeHidCandidateIndex = nextCandidateIndex;
		debugLog("Reopened HID path: %s\n", candidatePath.c_str());
		return true;
	}

	return false;
}

bool writeBulkPacket(SwitchUsbController& controller,
                     const unsigned char* packetData,
                     int packetSize,
                     const char* label) {
	int transferredSize = 0;
	int writeResult = libusb_bulk_transfer(controller.deviceHandle,
	                                       controller.bulkOutEndpoint,
	                                       const_cast<unsigned char*>(packetData),
	                                       packetSize,
	                                       &transferredSize,
	                                       kBulkTransferTimeoutMs);

	if (writeResult != LIBUSB_SUCCESS) {
		printLibusbError(label, writeResult);
		return false;
	}

	if (transferredSize != packetSize) {
		std::printf("%s failed\n", label);
		std::printf("  transferred=%d expected=%d\n", transferredSize, packetSize);
		return false;
	}

	return true;
}

void tryReadCommandResponse(SwitchUsbController& controller, const char* label) {
	if (!controller.hasBulkInEndpoint) {
		return;
	}

	unsigned char responseBuffer[64] = {};
	int transferredSize = 0;
	int readResult = libusb_bulk_transfer(controller.deviceHandle,
	                                      controller.bulkInEndpoint,
	                                      responseBuffer,
	                                      sizeof(responseBuffer),
	                                      &transferredSize,
	                                      kBulkTransferTimeoutMs);

	if (readResult == LIBUSB_ERROR_TIMEOUT) {
		std::printf("[%s] No response\n", label);
		return;
	}

	if (readResult != LIBUSB_SUCCESS) {
		std::printf("[%s] Read failed\n", label);
		std::printf("  libusb_error=%d (%s)\n", readResult, libusb_error_name(readResult));
		return;
	}

	std::printf("[%s] Response (%d bytes): ", label, transferredSize);
	printHexBuffer(responseBuffer, transferredSize);
	debugLog("[%s] Response (%d bytes): ", label, transferredSize);
	for (int byteIndex = 0; byteIndex < transferredSize; byteIndex++) {
		debugLog("%02X ", responseBuffer[byteIndex]);
	}
	debugLog("\n");
}

bool sendUsbCommand(SwitchUsbController& controller,
                    const unsigned char* commandData,
                    int commandSize,
                    const char* label,
                    bool shouldReadResponse) {
	std::printf("[%s] Send (%d bytes): ", label, commandSize);
	printHexBuffer(commandData, commandSize);
	debugLog("[%s] Send (%d bytes): ", label, commandSize);
	for (int byteIndex = 0; byteIndex < commandSize; byteIndex++) {
		debugLog("%02X ", commandData[byteIndex]);
	}
	debugLog("\n");

	if (!writeBulkPacket(controller, commandData, commandSize, label)) {
		return false;
	}

	Sleep(10);

	if (shouldReadResponse) {
		tryReadCommandResponse(controller, label);
	}

	return true;
}

bool initializeControllerFeatures(SwitchUsbController& controller) {
	if (!sendUsbCommand(controller, kInitCommand03, sizeof(kInitCommand03), "Init 0x03", true)) {
		return false;
	}

	if (!sendUsbCommand(controller, kUnknownCommand07, sizeof(kUnknownCommand07), "Unknown 0x07", true)) {
		return false;
	}

	if (!sendUsbCommand(controller, kUnknownCommand16, sizeof(kUnknownCommand16), "Unknown 0x16", true)) {
		return false;
	}

	if (!sendUsbCommand(controller, kRequestControllerMac, sizeof(kRequestControllerMac), "Request MAC", true)) {
		return false;
	}

	if (!sendUsbCommand(controller, kRequestLtk, sizeof(kRequestLtk), "Request LTK", true)) {
		return false;
	}

	if (!sendUsbCommand(controller, kUnknownCommand15Arg03, sizeof(kUnknownCommand15Arg03), "Unknown 0x15", true)) {
		return false;
	}

	if (!sendUsbCommand(controller, kUnknownCommand09, sizeof(kUnknownCommand09), "Unknown 0x09", true)) {
		return false;
	}

	if (!sendUsbCommand(controller, kImuCommand02, sizeof(kImuCommand02), "IMU 0x02", true)) {
		return false;
	}

	if (!sendUsbCommand(controller, kUnknownCommand11, sizeof(kUnknownCommand11), "Unknown 0x11", true)) {
		return false;
	}

	if (!sendUsbCommand(controller, kUnknownCommand0A, sizeof(kUnknownCommand0A), "Unknown 0x0A", true)) {
		return false;
	}

	if (!sendUsbCommand(controller, kImuCommand04, sizeof(kImuCommand04), "IMU 0x04", true)) {
		return false;
	}

	if (!sendUsbCommand(controller,
	                    kEnableHapticsCommand,
	                    sizeof(kEnableHapticsCommand),
	                    "Enable Haptics",
	                    true)) {
		return false;
	}

	if (!sendUsbCommand(controller, kUnknownCommand10, sizeof(kUnknownCommand10), "Unknown 0x10", true)) {
		return false;
	}

	if (!sendUsbCommand(controller, kUnknownCommand01, sizeof(kUnknownCommand01), "Unknown 0x01", true)) {
		return false;
	}

	if (!sendUsbCommand(controller, kUnknownCommand03Alt, sizeof(kUnknownCommand03Alt), "Unknown 0x03 Alt", true)) {
		return false;
	}

	if (!sendUsbCommand(controller, kUnknownCommand0AAlt, sizeof(kUnknownCommand0AAlt), "Unknown 0x0A Alt", true)) {
		return false;
	}

	if (!sendUsbCommand(controller,
	                    kSetPlayerLedCommand,
	                    sizeof(kSetPlayerLedCommand),
	                    "Set Player LED",
	                    true)) {
		return false;
	}

	return true;
}

void finalizeSwitchUsbController(SwitchUsbController& controller) {
	if (controller.hidHandle != nullptr) {
		hid_close(controller.hidHandle);
		controller.hidHandle = nullptr;
	}

	if (controller.hidRawHandle != INVALID_HANDLE_VALUE) {
		CloseHandle(controller.hidRawHandle);
		controller.hidRawHandle = INVALID_HANDLE_VALUE;
	}

	hid_exit();

	if (controller.deviceHandle != nullptr) {
		if (controller.claimedInterfaceNumber >= 0) {
			libusb_release_interface(controller.deviceHandle, controller.claimedInterfaceNumber);
		}

		libusb_close(controller.deviceHandle);
		controller.deviceHandle = nullptr;
	}

	if (controller.context != nullptr) {
		libusb_exit(controller.context);
		controller.context = nullptr;
	}
}

//================================================================
// HD 振動パケット生成
//================================================================
const HapticPattern& getAnimatedHapticPattern(int amplitude, size_t& patternPhase) {
	int clampedAmplitude = clampValue(amplitude, 0, kMaxRumbleAmplitude);

	if (clampedAmplitude < kActiveHapticThreshold) {
		patternPhase = 0;
		return kSilentHapticPattern;
	}

	size_t patternCount = sizeof(kHapticPatternTable) / sizeof(kHapticPatternTable[0]);
	size_t activePatternCount = 8;

	if (clampedAmplitude >= 256) {
		activePatternCount = 16;
	}

	if (clampedAmplitude >= 512) {
		activePatternCount = 24;
	}

	if (clampedAmplitude >= 768) {
		activePatternCount = patternCount;
	}

	size_t patternIndex = patternPhase % activePatternCount;
	patternPhase = (patternPhase + 1) % activePatternCount;

	return kHapticPatternTable[patternIndex];
}

void buildHidHapticReport(unsigned char* reportBuffer,
                          const HapticPattern& leftPattern,
                          const HapticPattern& rightPattern,
                          unsigned char vibrationCounter) {
	memset(reportBuffer, 0, kHidReportSize);

	reportBuffer[0] = 0x02;
	reportBuffer[1] = static_cast<unsigned char>(0x50 | (vibrationCounter & 0x0F));
	reportBuffer[17] = reportBuffer[1];

	for (int byteIndex = 0; byteIndex < 5; byteIndex++) {
		reportBuffer[2 + byteIndex] = leftPattern.data[byteIndex];
		reportBuffer[18 + byteIndex] = rightPattern.data[byteIndex];
	}
}

bool sendRumblePacket(SwitchUsbController& controller,
                      int leftAmplitude,
                      int rightAmplitude) {
	if (controller.hidHandle == nullptr) {
		std::printf("HID handle is not ready\n");
		return false;
	}

	const HapticPattern& leftPattern =
		getAnimatedHapticPattern(leftAmplitude, controller.leftPatternPhase);
	const HapticPattern& rightPattern =
		getAnimatedHapticPattern(rightAmplitude, controller.rightPatternPhase);
	unsigned char reportBuffer[kHidReportSize] = {};
	buildHidHapticReport(reportBuffer,
	                     leftPattern,
	                     rightPattern,
	                     controller.vibrationCounter);
	controller.rumbleSendCount++;

	if (controller.rumbleSendCount <= 8 || (controller.rumbleSendCount % 120) == 0) {
		debugLog("rumble[%d] counter=%u left=%d right=%d\n",
		         controller.rumbleSendCount,
		         controller.vibrationCounter,
		         leftAmplitude,
		         rightAmplitude);
		debugLog("rumble report: ");
		for (size_t byteIndex = 0; byteIndex < kHidReportSize; byteIndex++) {
			debugLog("%02X ", reportBuffer[byteIndex]);
		}
		debugLog("\n");
	}

	auto tryWriteFileOverlapped =
		[&controller](const char* attemptName,
		              const unsigned char* sendBuffer,
		              DWORD sendSize,
		              DWORD expectedSize) -> bool {
		debugLog("%s start packet=%d size=%lu\n",
		         attemptName,
		         controller.rumbleSendCount,
		         sendSize);

		OVERLAPPED overlapped = {};
		overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

		if (overlapped.hEvent == nullptr) {
			DWORD errorCode = GetLastError();
			debugLog("%s CreateEventW failed\n", attemptName);
			debugLog("  win32_error=0x%08lX\n", errorCode);
			return false;
		}

		DWORD bytesWritten = 0;
		BOOL writeResult = WriteFile(controller.hidRawHandle,
		                             sendBuffer,
		                             sendSize,
		                             &bytesWritten,
		                             &overlapped);

		if (!writeResult) {
			DWORD errorCode = GetLastError();

			if (errorCode != ERROR_IO_PENDING) {
				debugLog("%s WriteFile failed\n", attemptName);
				debugLog("  win32_error=0x%08lX\n", errorCode);
				CloseHandle(overlapped.hEvent);
				return false;
			}

			DWORD waitResult =
				WaitForSingleObject(overlapped.hEvent, kHidWriteTimeoutMs);

			if (waitResult == WAIT_TIMEOUT) {
				CancelIo(controller.hidRawHandle);
				debugLog("%s timed out\n", attemptName);
				CloseHandle(overlapped.hEvent);
				return false;
			}

			if (waitResult != WAIT_OBJECT_0) {
				DWORD waitErrorCode = GetLastError();
				debugLog("%s WaitForSingleObject failed\n", attemptName);
				debugLog("  win32_error=0x%08lX\n", waitErrorCode);
				CloseHandle(overlapped.hEvent);
				return false;
			}

			if (!GetOverlappedResult(controller.hidRawHandle,
			                         &overlapped,
			                         &bytesWritten,
			                         FALSE)) {
				DWORD overlappedErrorCode = GetLastError();
				debugLog("%s GetOverlappedResult failed\n", attemptName);
				debugLog("  win32_error=0x%08lX\n", overlappedErrorCode);
				CloseHandle(overlapped.hEvent);
				return false;
			}
		}

		CloseHandle(overlapped.hEvent);

		if (bytesWritten != expectedSize) {
			debugLog("%s short write\n", attemptName);
			debugLog("  bytes_written=%lu expected=%lu\n",
			         bytesWritten,
			         expectedSize);
			return false;
		}

		debugLog("%s ok bytes=%lu packet=%d\n",
		         attemptName,
		         bytesWritten,
		         controller.rumbleSendCount);
		return true;
	};

	auto tryOutputReportApi =
		[&controller](const unsigned char* sendBuffer, DWORD sendSize) -> bool {
		debugLog("HidD_SetOutputReport start packet=%d size=%lu\n",
		         controller.rumbleSendCount,
		         sendSize);

		BOOL result = HidD_SetOutputReport(controller.hidRawHandle,
		                                   const_cast<unsigned char*>(sendBuffer),
		                                   sendSize);

		if (!result) {
			DWORD errorCode = GetLastError();
			debugLog("HidD_SetOutputReport failed\n");
			debugLog("  win32_error=0x%08lX\n", errorCode);
			return false;
		}

		debugLog("HidD_SetOutputReport ok packet=%d\n",
		         controller.rumbleSendCount);
		return true;
	};

	auto tryHidApiWrite =
		[&controller](const char* attemptName,
		              const unsigned char* sendBuffer,
		              size_t sendSize,
		              int expectedSize,
		              bool useOutputReport) -> bool {
		debugLog("%s start packet=%d size=%zu\n",
		         attemptName,
		         controller.rumbleSendCount,
		         sendSize);

		int result = -1;

		if (useOutputReport) {
			result = hid_send_output_report(controller.hidHandle, sendBuffer, sendSize);
		}
		else {
			result = hid_write(controller.hidHandle, sendBuffer, sendSize);
		}

		if (result < 0) {
			const wchar_t* hidErrorText = hid_error(controller.hidHandle);
			debugLog("%s failed\n", attemptName);

			if (hidErrorText != nullptr) {
				debugLog("%s hid_error=%ls\n", attemptName, hidErrorText);
			}

			return false;
		}

		if (result != expectedSize) {
			debugLog("%s short write\n", attemptName);
			debugLog("  bytes_written=%d expected=%d\n", result, expectedSize);
			return false;
		}

		debugLog("%s ok bytes=%d packet=%d\n",
		         attemptName,
		         result,
		         controller.rumbleSendCount);
		return true;
	};

	bool sendSucceeded = false;

	if (kUseHidSendExperiment) {
		unsigned char prefixedReportBuffer[kHidReportSize + 1] = {};
		prefixedReportBuffer[0] = 0x00;
		memcpy(prefixedReportBuffer + 1, reportBuffer, kHidReportSize);

		sendSucceeded = tryHidApiWrite("hid_send_output_report_64",
		                               reportBuffer,
		                               kHidReportSize,
		                               kHidReportSize,
		                               true);

		if (!sendSucceeded) {
			sendSucceeded = tryHidApiWrite("hid_write_64",
			                               reportBuffer,
			                               kHidReportSize,
			                               kHidReportSize,
			                               false);
		}

		if (!sendSucceeded) {
			sendSucceeded = tryHidApiWrite("hid_write_65_WithPrefix0",
			                               prefixedReportBuffer,
			                               kHidReportSize + 1,
			                               kHidReportSize + 1,
			                               false);
		}

		if (!sendSucceeded) {
			sendSucceeded = tryOutputReportApi(reportBuffer, kHidReportSize);
		}

		if (!sendSucceeded) {
			sendSucceeded = tryWriteFileOverlapped("WriteFile_63_NoReportId",
			                                       reportBuffer + 1,
			                                       kHidReportSize - 1,
			                                       kHidReportSize - 1);
		}

		if (!sendSucceeded) {
			sendSucceeded = tryWriteFileOverlapped("WriteFile_65_WithPrefix0",
			                                       prefixedReportBuffer,
			                                       kHidReportSize + 1,
			                                       kHidReportSize + 1);
		}
	}
	else {
		sendSucceeded = tryWriteFileOverlapped("WriteFile_64",
		                                       reportBuffer,
		                                       kHidReportSize,
		                                       kHidReportSize);
	}

	if (!sendSucceeded) {
		std::printf("Rumble packet send failed\n");
		debugLog("Rumble packet send failed packet=%d\n", controller.rumbleSendCount);
		return false;
	}

	controller.vibrationCounter =
		static_cast<unsigned char>((controller.vibrationCounter + 1) & 0x0F);
	debugLog("counter advanced=%u packet=%d\n",
	         controller.vibrationCounter,
	         controller.rumbleSendCount);

	return true;
}

bool primeHapticOutput(SwitchUsbController& controller) {
	for (;;) {
		debugLog("prime start candidate=%d\n", controller.activeHidCandidateIndex);
		controller.vibrationCounter = 0;
		controller.leftPatternPhase = 0;
		controller.rightPatternPhase = 0;
		controller.rumbleSendCount = 0;

		for (int packetIndex = 0; packetIndex < kPrimePacketCount; packetIndex++) {
			if (!sendRumblePacket(controller, kMaxRumbleAmplitude, kMaxRumbleAmplitude)) {
				debugLog("prime failed packet=%d candidate=%d\n",
				         packetIndex,
				         controller.activeHidCandidateIndex);

				if (!reopenNextHidCandidate(controller)) {
					return false;
				}

				goto retryPrime;
			}

			Sleep(kPrimePacketIntervalMs);
		}

		debugLog("prime end candidate=%d\n", controller.activeHidCandidateIndex);
		return true;

	retryPrime:
		continue;
	}
}

void sendStopRumble(SwitchUsbController& controller) {
	for (int stopCount = 0; stopCount < 4; stopCount++) {
		sendRumblePacket(controller, 0, 0);
		Sleep(10);
	}
}

//================================================================
// WebHID ブラウザ再生
//================================================================
std::string buildFramesScript(const std::vector<VibFrame>& vibrationFrames) {
	std::string scriptText;
	scriptText += "window.konntoConfig = {\n";
	scriptText += "  frameIntervalMs: 4,\n";
	scriptText += "  activeHapticThreshold: ";
	scriptText += std::to_string(kActiveHapticThreshold);
	scriptText += ",\n";
	scriptText += "  initCommands: [\n";

	auto appendByteArray = [&scriptText](const unsigned char* data, size_t dataSize) {
		scriptText += "    [";

		for (size_t byteIndex = 0; byteIndex < dataSize; byteIndex++) {
			scriptText += std::to_string(data[byteIndex]);

			if (byteIndex + 1 < dataSize) {
				scriptText += ", ";
			}
		}

		scriptText += "]";
	};

	appendByteArray(kInitCommand03, sizeof(kInitCommand03));
	scriptText += ",\n";
	appendByteArray(kUnknownCommand07, sizeof(kUnknownCommand07));
	scriptText += ",\n";
	appendByteArray(kUnknownCommand16, sizeof(kUnknownCommand16));
	scriptText += ",\n";
	appendByteArray(kRequestControllerMac, sizeof(kRequestControllerMac));
	scriptText += ",\n";
	appendByteArray(kRequestLtk, sizeof(kRequestLtk));
	scriptText += ",\n";
	appendByteArray(kUnknownCommand15Arg03, sizeof(kUnknownCommand15Arg03));
	scriptText += ",\n";
	appendByteArray(kUnknownCommand09, sizeof(kUnknownCommand09));
	scriptText += ",\n";
	appendByteArray(kImuCommand02, sizeof(kImuCommand02));
	scriptText += ",\n";
	appendByteArray(kUnknownCommand11, sizeof(kUnknownCommand11));
	scriptText += ",\n";
	appendByteArray(kUnknownCommand0A, sizeof(kUnknownCommand0A));
	scriptText += ",\n";
	appendByteArray(kImuCommand04, sizeof(kImuCommand04));
	scriptText += ",\n";
	appendByteArray(kEnableHapticsCommand, sizeof(kEnableHapticsCommand));
	scriptText += ",\n";
	appendByteArray(kUnknownCommand10, sizeof(kUnknownCommand10));
	scriptText += ",\n";
	appendByteArray(kUnknownCommand01, sizeof(kUnknownCommand01));
	scriptText += ",\n";
	appendByteArray(kUnknownCommand03Alt, sizeof(kUnknownCommand03Alt));
	scriptText += ",\n";
	appendByteArray(kUnknownCommand0AAlt, sizeof(kUnknownCommand0AAlt));
	scriptText += ",\n";
	appendByteArray(kSetPlayerLedCommand, sizeof(kSetPlayerLedCommand));
	scriptText += "\n";
	scriptText += "  ],\n";
	scriptText += "  hapticPatternTable: [\n";

	for (size_t patternIndex = 0;
	     patternIndex < sizeof(kHapticPatternTable) / sizeof(kHapticPatternTable[0]);
	     patternIndex++) {
		scriptText += "    [";

		for (int byteIndex = 0; byteIndex < 5; byteIndex++) {
			scriptText +=
				std::to_string(kHapticPatternTable[patternIndex].data[byteIndex]);

			if (byteIndex < 4) {
				scriptText += ", ";
			}
		}

		scriptText += "]";

		if (patternIndex + 1 <
			sizeof(kHapticPatternTable) / sizeof(kHapticPatternTable[0])) {
			scriptText += ",";
		}

		scriptText += "\n";
	}

	scriptText += "  ],\n";
	scriptText += "  frames: [\n";

	for (size_t frameIndex = 0; frameIndex < vibrationFrames.size(); frameIndex++) {
		const VibFrame& vibrationFrame = vibrationFrames[frameIndex];
		scriptText += "    { startMs: ";
		scriptText += std::to_string(vibrationFrame.startMs);
		scriptText += ", endMs: ";
		scriptText += std::to_string(vibrationFrame.endMs);
		scriptText += ", leftAmplitude: ";
		scriptText += std::to_string(vibrationFrame.leftAmplitude);
		scriptText += ", rightAmplitude: ";
		scriptText += std::to_string(vibrationFrame.rightAmplitude);
		scriptText += " }";

		if (frameIndex + 1 < vibrationFrames.size()) {
			scriptText += ",";
		}

		scriptText += "\n";
	}

	scriptText += "  ]\n";
	scriptText += "};\n";

	return scriptText;
}

bool sendSocketBuffer(SOCKET clientSocket, const char* buffer, int bufferSize) {
	int totalSentSize = 0;

	while (totalSentSize < bufferSize) {
		int sentSize =
			send(clientSocket, buffer + totalSentSize, bufferSize - totalSentSize, 0);

		if (sentSize == SOCKET_ERROR) {
			return false;
		}

		totalSentSize += sentSize;
	}

	return true;
}

std::string buildHttpHeader(const char* contentType, size_t contentLength) {
	std::string headerText = "HTTP/1.1 200 OK\r\nContent-Type: ";
	headerText += contentType;
	headerText += "\r\nContent-Length: ";
	headerText += std::to_string(contentLength);
	headerText += "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
	return headerText;
}

std::string buildNotFoundHeader() {
	return "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
}

std::string parseHttpPath(const std::string& requestText) {
	size_t firstSpaceIndex = requestText.find(' ');

	if (firstSpaceIndex == std::string::npos) {
		return "/";
	}

	size_t secondSpaceIndex = requestText.find(' ', firstSpaceIndex + 1);

	if (secondSpaceIndex == std::string::npos) {
		return "/";
	}

	return requestText.substr(firstSpaceIndex + 1, secondSpaceIndex - firstSpaceIndex - 1);
}

bool launchBrowser(unsigned short serverPort) {
	wchar_t urlText[128] = {};
	swprintf_s(urlText, L"http://127.0.0.1:%u/", serverPort);
	HINSTANCE result = ShellExecuteW(nullptr, L"open", urlText, nullptr, nullptr, SW_SHOWNORMAL);
	return reinterpret_cast<INT_PTR>(result) > 32;
}

bool startWebHidServer(const std::string& frameScriptText,
                       const std::vector<BYTE>& wavFileData,
                       unsigned short& serverPort,
                       std::atomic<bool>& shouldKeepServerRunning,
                       std::thread& serverThread) {
	WSADATA wsaData = {};

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		return false;
	}

	SOCKET listenSocket = INVALID_SOCKET;

	for (unsigned short candidatePort = kWebServerStartPort;
	     candidatePort <= kWebServerEndPort;
	     candidatePort++) {
		listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

		if (listenSocket == INVALID_SOCKET) {
			WSACleanup();
			return false;
		}

		sockaddr_in address = {};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = htons(candidatePort);

		if (bind(listenSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
			serverPort = candidatePort;
			break;
		}

		closesocket(listenSocket);
		listenSocket = INVALID_SOCKET;
	}

	if (listenSocket == INVALID_SOCKET) {
		WSACleanup();
		return false;
	}

	if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
		closesocket(listenSocket);
		WSACleanup();
		return false;
	}

	serverThread = std::thread([frameScriptText, wavFileData, listenSocket, &shouldKeepServerRunning]() {
		const std::string htmlText =
			std::string(kWebHidPlayerHtmlPart1) +
			std::string(kWebHidPlayerHtmlPart1B) +
			std::string(kWebHidPlayerHtmlPart2) +
			std::string(kWebHidPlayerHtmlPart2T) +
			std::string(kWebHidPlayerHtmlPart2S) +
			std::string(kWebHidPlayerHtmlPart2Q) +
			std::string(kWebHidPlayerHtmlPart2R) +
			std::string(kWebHidPlayerHtmlPart2P1) +
			std::string(kWebHidPlayerHtmlPart2P2) +
			std::string(kWebHidPlayerHtmlPart2O) +
			std::string(kWebHidPlayerHtmlPart2L) +
			std::string(kWebHidPlayerHtmlPart2K) +
			std::string(kWebHidPlayerHtmlPart2J) +
			std::string(kWebHidPlayerHtmlPart2I) +
			std::string(kWebHidPlayerHtmlPart2G) +
			std::string(kWebHidPlayerHtmlPart2H) +
			std::string(kWebHidPlayerHtmlPart2F) +
			std::string(kWebHidPlayerHtmlPart2N) +
			std::string(kWebHidPlayerHtmlPart2M) +
			std::string(kWebHidPlayerHtmlPart2E) +
			std::string(kWebHidPlayerHtmlPart2D) +
			std::string(kWebHidPlayerHtmlPart2C) +
			std::string(kWebHidPlayerHtmlPart2A) +
			std::string(kWebHidPlayerHtmlPart2B) +
			std::string(kWebHidPlayerHtmlPart3) +
			std::string(kWebHidPlayerHtmlPart4) +
			std::string(kWebHidPlayerHtmlPart5) +
			std::string(kWebHidPlayerHtmlPart5H) +
			std::string(kWebHidPlayerHtmlPart5G) +
			std::string(kWebHidPlayerHtmlPart5D) +
			std::string(kWebHidPlayerHtmlPart5C);

		while (shouldKeepServerRunning.load()) {
			fd_set readSet = {};
			FD_ZERO(&readSet);
			FD_SET(listenSocket, &readSet);

			timeval timeout = {};
			timeout.tv_sec = 0;
			timeout.tv_usec = 200000;

			int selectResult = select(0, &readSet, nullptr, nullptr, &timeout);

			if (selectResult <= 0) {
				continue;
			}

			SOCKET clientSocket = accept(listenSocket, nullptr, nullptr);

			if (clientSocket == INVALID_SOCKET) {
				continue;
			}

			char requestBuffer[4096] = {};
			int receivedSize = recv(clientSocket, requestBuffer, sizeof(requestBuffer) - 1, 0);

			if (receivedSize > 0) {
				std::string requestText(requestBuffer, requestBuffer + receivedSize);
				std::string requestPath = parseHttpPath(requestText);

				if (requestPath == "/" || requestPath == "/index.html") {
					std::string headerText =
						buildHttpHeader("text/html; charset=utf-8",
						                htmlText.size());
					sendSocketBuffer(clientSocket,
					                 headerText.c_str(),
					                 static_cast<int>(headerText.size()));
					sendSocketBuffer(clientSocket,
					                 htmlText.c_str(),
					                 static_cast<int>(htmlText.size()));
				}
				else if (requestPath == kFrameScriptPath) {
					std::string headerText =
						buildHttpHeader("application/javascript; charset=utf-8",
						                frameScriptText.size());
					sendSocketBuffer(clientSocket,
					                 headerText.c_str(),
					                 static_cast<int>(headerText.size()));
					sendSocketBuffer(clientSocket,
					                 frameScriptText.c_str(),
					                 static_cast<int>(frameScriptText.size()));
				}
				else if (requestPath == kAudioHttpPath) {
					std::string headerText =
						buildHttpHeader("audio/wav", wavFileData.size());
					sendSocketBuffer(clientSocket,
					                 headerText.c_str(),
					                 static_cast<int>(headerText.size()));
					sendSocketBuffer(clientSocket,
					                 reinterpret_cast<const char*>(wavFileData.data()),
					                 static_cast<int>(wavFileData.size()));
				}
				else if (requestPath == "/quit") {
					std::string bodyText = "bye";
					std::string headerText =
						buildHttpHeader("text/plain; charset=utf-8", bodyText.size());
					sendSocketBuffer(clientSocket,
					                 headerText.c_str(),
					                 static_cast<int>(headerText.size()));
					sendSocketBuffer(clientSocket,
					                 bodyText.c_str(),
					                 static_cast<int>(bodyText.size()));
					shouldKeepServerRunning.store(false);
				}
				else {
					std::string headerText = buildNotFoundHeader();
					sendSocketBuffer(clientSocket,
					                 headerText.c_str(),
					                 static_cast<int>(headerText.size()));
				}
			}

			closesocket(clientSocket);
		}

		closesocket(listenSocket);
		WSACleanup();
	});

	return true;
}

//================================================================
// FeelKit 統合フィードバックデモ
//================================================================

static void runFeedbackDemo() {
	FeelKit::Init();

	FeelKit::SetSoundEffectCallback(
		[](const char* path, float volume) {
			std::printf("[FeelKit] SE: %s (vol=%.2f)\n", path, volume);
		});

	FeelKit::SetVibrationCallback(
		[](float power, float seconds) {
			std::printf("[FeelKit] Vibration: power=%.2f seconds=%.3f\n",
			            power, seconds);
		});

	FeelKit::SetEffectDrawCallback(
		[](const FeelKit::EffectDrawInfo& info) {
			std::printf("[FeelKit] EffectDraw: tex=%d pos=(%.0f,%.0f,%.0f) "
			            "frame=%d src=(%d,%d %dx%d)\n",
			            info.textureHandle, info.x, info.y, info.z,
			            info.frameIndex,
			            info.sourceX, info.sourceY,
			            info.sourceWidth, info.sourceHeight);
		});

	std::printf("--- FeelKit Demo ---\n\n");

	FeelKit::Vibrate(0.8f, 0.12f);
	FeelKit::PlaySE("hit.wav", 1.0f);
	FeelKit::Shake(5.0f, 0.3f);
	FeelKit::Flash(0xFFFFFFFFu, 0.15f, 0.8f);
	FeelKit::HitStop(0.1f);
	FeelKit::SlowMotion(0.3f, 0.2f);

	FeelKit::Update(0.016f);
	FeelKit::ScreenState ss1 = FeelKit::GetScreenState();
	std::printf("[FeelKit] Screen: shake=(%.2f,%.2f) rot=%.4f zoom=%.2f "
	            "flashAlpha=%.2f hitStop=%d slowMo=%.2f\n\n",
	            ss1.shakeOffset.x, ss1.shakeOffset.y, ss1.shakeRotation,
	            ss1.zoomScale, ss1.flashAlpha,
	            ss1.isHitStop ? 1 : 0, ss1.slowMotionScale);

	FeelKit::EffectDesc explosion;
	explosion.textureHandle = 100;
	explosion.layout = FeelKit::EffectLayout::grid;
	explosion.frameWidth = 64;
	explosion.frameHeight = 64;
	explosion.frameCount = 12;
	explosion.columns = 4;
	explosion.rows = 3;
	explosion.seconds = 0.5f;
	explosion.loop = false;

	std::printf("--- LoadEffect + PlayEffect ---\n");
	FeelKit::LoadEffect("explosion", explosion);
	int eid = FeelKit::PlayEffect("explosion", FeelKit::Vec2{320.0f, 240.0f});
	std::printf("[FeelKit] PlayEffect(\"explosion\", 320, 240) -> id=%d\n", eid);
	FeelKit::Update(0.25f);
	FeelKit::Draw();
	FeelKit::Update(0.25f);
	FeelKit::Draw();

	std::printf("\n--- Audio Analysis ---\n");
	FeelKit::AudioAnalysisResult analysis = FeelKit::AnalyzeAudioFile("BGM.wav");
	if (analysis.isValid) {
		std::printf("[FeelKit] BGM.wav: duration=%.2fs avg=%.4f peak=%.4f "
		            "low=%.4f high=%.4f\n",
		            analysis.durationSeconds, analysis.averageAmplitude,
		            analysis.peakAmplitude, analysis.lowBandEnergy,
		            analysis.highBandEnergy);

		std::printf("\n--- PlayHapticFromAudio ---\n");
		FeelKit::PlayHapticFromAudio("BGM.wav");
		std::printf("\n--- PlayShakeFromAudio ---\n");
		FeelKit::PlayShakeFromAudio("BGM.wav");
		std::printf("\n--- PlayFlashFromAudio ---\n");
		FeelKit::PlayFlashFromAudio("BGM.wav", 0xFFFF4400u);
	} else {
		std::printf("[FeelKit] BGM.wav analysis skipped (file not found)\n");
	}

	std::printf("\n--- Impact Analysis ---\n");
	FeelKit::ImpactAnalysisResult impact = FeelKit::AnalyzeImpact(15.0f, 2.0f);
	std::printf("[FeelKit] AnalyzeImpact(15.0, 2.0): power=%.2f seconds=%.3f\n",
	            impact.power, impact.seconds);
	FeelKit::GenerateImpactHaptics(FeelKit::Vec3{5.0f, -3.0f, 2.0f}, 1.5f);
	FeelKit::GenerateImpactShake(FeelKit::Vec3{12.0f, 0.0f, 0.0f}, 3.0f);

	FeelKit::Shutdown();
	std::printf("\n--- FeelKit Demo Done ---\n\n");
}

//================================================================
// main
//================================================================
int main() {
	runFeedbackDemo();

	timeBeginPeriod(1);
	fopen_s(&gDebugLogFile, kDebugLogFilePath, "w");
	debugLog("konnto log start\n");

	WAVEFORMATEX waveFormat = {};
	WavBuffer wavBuffer = {};

	if (!loadWavFile(kAudioFilePath, waveFormat, wavBuffer)) {
		std::printf("WAV load failed: BGM.wav\n");
		timeEndPeriod(1);
		closeDebugLog();
		return 1;
	}

	std::vector<VibFrame> vibrationFrames =
		buildVibrationFramesFromWav(waveFormat, wavBuffer);

	if (vibrationFrames.empty()) {
		std::printf("No vibration frames were generated from BGM.wav\n");
		finalizeWavePlayback(wavBuffer);
		timeEndPeriod(1);
		closeDebugLog();
		return 1;
	}

	std::string frameScriptText = buildFramesScript(vibrationFrames);
	std::vector<BYTE> wavFileData;

	if (!loadBinaryFile(kAudioFilePath, wavFileData)) {
		std::printf("WAV binary load failed: BGM.wav\n");
		finalizeWavePlayback(wavBuffer);
		timeEndPeriod(1);
		closeDebugLog();
		return 1;
	}

	unsigned short serverPort = 0;
	std::atomic<bool> shouldKeepServerRunning = true;
	std::thread serverThread;

	if (!startWebHidServer(frameScriptText,
	                       wavFileData,
	                       serverPort,
	                       shouldKeepServerRunning,
	                       serverThread)) {
		std::printf("Web server start failed\n");
		finalizeWavePlayback(wavBuffer);
		timeEndPeriod(1);
		closeDebugLog();
		return 1;
	}

	std::printf("WebHID player started: http://127.0.0.1:%u/\n", serverPort);
	debugLog("WebHID player started: http://127.0.0.1:%u/\n", serverPort);

	if (!launchBrowser(serverPort)) {
		std::printf("Browser launch failed\n");
		debugLog("Browser launch failed\n");
	}

	// Clear stdin
	while (getchar() != '\n') {}

	std::printf("\n===== FeelKit Interactive Test Menu =====\n");
	std::printf("Select functions to test them individually.\n");
	std::printf("Choose 'Back' (0) at the top menu to exit and stop the server.\n\n");
	runInteractiveMenu();

	shouldKeepServerRunning.store(false);

	if (serverThread.joinable()) {
		serverThread.join();
	}

	finalizeWavePlayback(wavBuffer);
	timeEndPeriod(1);
	std::printf("WebHID player finished\n");
	debugLog("WebHID player finished\n");
	closeDebugLog();

	return 0;
}
