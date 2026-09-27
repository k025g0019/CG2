# Whisper Backend の配置

CG2Engine の `SpeechRecognizer` で Backend に `Whisper` を選ぶ場合、
公式 [whisper.cpp](https://github.com/ggml-org/whisper.cpp) の Windows 向け `whisper-cli.exe` と、
その実行に必要な DLL をこの Folder へ配置します。

現在の Windows 配布版では、少なくとも次のファイルを `whisper-cli.exe` と同じ場所へ置きます。

- `whisper.dll`
- `ggml.dll`
- `ggml-base.dll`
- `ggml-cpu.dll`

`whisper-cli.exe` だけをコピーすると、モデルが存在していても推論開始時に DLL 不足で終了します。

認識モデルは Scene に固定せず、Inspector の `モデル` で whisper.cpp 用モデル（例: `ggml-small.bin`）を指定します。
モデルパスは絶対パス、Project Folder 相対、Engine 実行ファイル相対のいずれかを使用できます。

日本語では `tiny` / `base` は短い命令語を誤認識しやすいため、通常は多言語版の `ggml-small.bin` 以上を推奨します。
`.en` が付くモデルは英語専用なので、日本語認識には使用しません。

Inspectorでは「無音で発話終了」を有効にすると、発話後の無音を検出した時点で推論を開始します。
「最大録音秒」は無音を検出できない場合の上限で、「発話終了の無音秒」と「音声判定音量」も調整できます。

実行時の検索順は次のとおりです。

1. Engine 実行ファイルの隣
2. Engine 実行ファイル側の `Tools/Whisper`
3. Project の `Tools/Whisper`
4. `ThirdParty/whisper.cpp/build/bin/Release`
5. `ThirdParty/whisper.cpp/build/bin`
6. Project 直下
7. Windows の `PATH`

CLI またはモデルが無い場合、Windows Speech APIへ勝手に切り替えず、実行状態を`利用不可`にして理由を表示します。
