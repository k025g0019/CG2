#pragma once

struct Vector3 {
	float x;  // x は横方向またはローカル X 軸方向の成分。
	float y;  // y は縦方向またはローカル Y 軸方向の成分。
	float z;  // z は奥行き方向またはローカル Z 軸方向の成分。
};


void VectorScreenPrintf(int x, int y, const Vector3& vector, const char* label);  // 旧デバッグ表示用の関数。現在の Editor UI では直接使っていない。
Vector3 Add(const Vector3& v1, const Vector3& v2);  // 2 つの Vector3 を成分ごとに加算する。
Vector3 Subtract(const Vector3& v1, const Vector3& v2);  // v1 から v2 を成分ごとに減算する。
Vector3 Multiply(float scalar, const Vector3& v);  // Vector3 の各成分へ scalar を掛ける。
float Dot(const Vector3& v1, const Vector3& v2);  // 2 つの Vector3 の内積を返す。
Vector3 Cross(const Vector3& v1, const Vector3& v2);  // 2 つの Vector3 の外積を返す。
float Length(const Vector3& v);  // Vector3 の長さを返す。
Vector3 Normalize(const Vector3& v);  // Vector3 を長さ 1.0f の方向ベクトルにする。
