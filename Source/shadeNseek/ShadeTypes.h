#pragma once

#include "CoreMinimal.h"
#include "ShadeTypes.generated.h"

/** Run flow owned by AShadeGameMode. */
UENUM()
enum class EShadePhase : uint8
{
	Title,
	Intro,
	Playing,
	Finale,
	Reunion,
	Ended
};

/** Custom trace channel declared in DefaultEngine.ini ("Sunlight"): anything that blocks it casts gameplay shade. */
#define ECC_Sunlight ECC_GameTraceChannel1

namespace ShadeEase
{
	inline float Clamp01(float X) { return FMath::Clamp(X, 0.f, 1.f); }
	inline float Smooth(float X) { X = Clamp01(X); return X * X * (3.f - 2.f * X); }
	inline float OutCubic(float X) { X = Clamp01(X); const float I = 1.f - X; return 1.f - I * I * I; }
	inline float InCubic(float X) { X = Clamp01(X); return X * X * X; }
	inline float InOutCubic(float X) { X = Clamp01(X); return X < 0.5f ? 4.f * X * X * X : 1.f - FMath::Pow(-2.f * X + 2.f, 3.f) * 0.5f; }
	inline float OutBack(float X, float S = 1.70158f) { X = Clamp01(X) - 1.f; return 1.f + (S + 1.f) * X * X * X + S * X * X; }
	/** Window(t, a, b): 0 before a, ramps to 1 at b. */
	inline float Window(float T, float A, float B) { return Clamp01((T - A) / FMath::Max(B - A, 1e-4f)); }
	/** Frame-rate independent exponential approach. */
	inline float Approach(float Current, float Target, float Speed, float Dt) { return FMath::Lerp(Target, Current, FMath::Exp(-Speed * Dt)); }
	inline FVector Approach(const FVector& Current, const FVector& Target, float Speed, float Dt) { return FMath::Lerp(Target, Current, FMath::Exp(-Speed * Dt)); }
	/** 0..1 pulse that peaks at the middle of [A,B]. */
	inline float Bump(float T, float A, float B) { const float X = Window(T, A, B); return FMath::Sin(X * PI); }
}

/** Critically-damped-ish spring for procedural secondary motion. */
struct FShadeSpring
{
	float Value = 0.f;
	float Velocity = 0.f;

	float Update(float Target, float Stiffness, float Damping, float Dt)
	{
		Dt = FMath::Min(Dt, 0.05f);
		const float Accel = (Target - Value) * Stiffness - Velocity * Damping;
		Velocity += Accel * Dt;
		Value += Velocity * Dt;
		return Value;
	}
};
