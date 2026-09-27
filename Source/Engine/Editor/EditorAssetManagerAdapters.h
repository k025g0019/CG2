#pragma once

// AssetManager(Engine層)へ、既存の各種Manager(Model/Texture/Audio/VFX等)をAdapterとして
// 登録する。AssetManager自体はEditor層を知らないため、この接続はEditor層側から
// 起動時に一度だけ行う。
void RegisterEditorAssetManagerAdapters();
